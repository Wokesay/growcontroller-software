// SPDX-License-Identifier: AGPL-3.0-or-later
// Schicht 8: Aktor-Gateway und Dosierausführung.
//
// Actuators ist der EINZIGE Weg zu Pumpen und Schaltausgängen (Regel R1).
// Hier steht das fest codierte Sicherheitsminimum, das der Katalog nur
// verschärfen kann (R7): Einmesswert vorhanden, Laufzeitgrenzen, ein Lauf
// zugleich, Not-Halt, Trockenlaufschutz, Notgrenze des Zulaufs.
// Der Watchdog bindet diesen Header nie ein (Architekturtest, R2).
#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "gc/bus.hpp"
#include "gc/config.hpp"
#include "gc/events.hpp"
#include "gc/mix.hpp"
#include "gc/truth.hpp"

namespace gc {

// Gemeinsamer Takt-Kontext für Gateway, Aufträge und Regler.
struct Ctx {
  const Catalog& cat;
  const Config& cfg;
  RuntimeState& rt;
  const SensorTruth& truth;
  EventLog& log;
  const PumpMap& pumps;
  Ms now = 0;
  Epoch epoch = 0;
  bool stopped = false;          // Not-Halt aktiv
  Epoch maintenanceUntil = 0;    // Pflegemodus: Automatik ruht
};

// Minimum pause of a compressor device after switching off (rationale:
// RAT-034). A lock against the opposite device belongs to the climate
// function; here the rule is "never both at once".
constexpr Ms kCompressorPause = 5 * kMinute;
// Obergrenze der Luftfeuchte für den Befeuchter: Kondensat an Lampen und
// Steckdosen vermeiden (Annahme, Vorschlag hardware 06.10.2026).
constexpr double kHumidifierMaxRh = 85.0;

// Protection setting in the network device per profile: off after a power
// loss (rationale: RAT-019); for puls and heizen an auto-off slightly above
// the maximum run time, ratio 1.11 (rationale: RAT-060; applied to puls as
// an assumption).
SwitchSafety safetyForRole(const RoleDef& rd);

class Actuators {
 public:
  explicit Actuators(IBus& bus) : bus_(bus) {}
  // Netzgeräte (schaltbare Steckdosen); ohne Netz-Bus nur die Hub-Ausgänge.
  void setNet(INetBus* net) { net_ = net; }

  bool startRun(const Ctx& c, const std::string& pump, Ms ms, const std::string& purpose, const std::string& jobId,
                Msg& err);
  RunStatus runStatus(const std::string& pump) const { return bus_.runStatus(pump); }
  bool pumpRunning() const { return !runningPump_.empty(); }
  void clearRun(const std::string& pump);
  void stopPumps();

  // Schaltausgänge nur über Rollen (tank.circulation, zone.light …), je nach
  // Gerät am Hub-Ausgang oder an einer Netzsteckdose.
  // `who` names the cause; logged for the inlet (e.g. say("who.refill")).
  bool setRole(const Ctx& c, const std::string& role, bool on, const Msg& who, Msg& err);
  std::optional<bool> roleState(const Config& cfg, const std::string& role) const;
  // Warum eine Rolle gerade nicht eingeschaltet werden darf (Einschaltsperre).
  std::optional<Msg> inhibit(const Ctx& c, const std::string& role) const;

  // Not-Halt: alle Pumpen und Ausgänge aus, idempotent (Quelle: RAT-036).
  // Zählt als Ausschalten: Mindestpausen gelten auch danach.
  // keepPowerLossOn: after a restart, roles that come back on after a power
  // loss (fans, PD-050) keep their state.
  void stopAll(const Catalog& cat, const Config& cfg, Ms now, bool keepPowerLossOn = false);
  // Je Takt: Trockenlauf, Notgrenze, Zeitlimits; schaltet nur AUS, nie EIN.
  void enforce(const Ctx& c);

 private:
  bool sw(const std::string& dev, int channel, bool on, std::string& err);
  std::optional<bool> swState(const std::string& dev, int channel) const;
  // Schutzabschaltung einer Rolle, je Grund (`key`) einmal gemeldet, bis die
  // Rolle aus ist oder umgehängt wird (zwei Gründe wechseln sich nicht je
  // Takt ab).
  // Scheitert das Ausschalten, lautet die Meldung „Aus nicht bestätigt“;
  // enforce versucht es weiter, solange Grund oder Rastung bestehen, und
  // meldet „Aus bestätigt“, sobald der Ausgang aus ist.
  bool cut(const Ctx& c, const std::string& role, const std::string& key, const std::string& type,
           const std::string& severity, const Msg& title, const Msg& text);
  // Grund wieder scharf (neue Rastung) bzw. als schon gemeldet vermerken.
  void rearm(const std::string& role, const std::string& key);
  void noteReported(const std::string& role, const std::string& key);

  IBus& bus_;
  INetBus* net_ = nullptr;
  std::map<std::string, Ms> offSince_;  // Rolle → aus seit (Mindestpause Kompressor)
  // Gemeldete Schutzabschaltung je Rolle (kein Protokoll je Takt). Gerät und
  // Kanal vom Zeitpunkt des Schnitts: Wird umgehängt, endet die Verfolgung
  // ohne Entwarnung (der alte Ausgang ist nicht mehr im Blick).
  struct Cut {
    std::set<std::string> keys;  // schon gemeldete Gründe
    std::string device;
    int channel = 0;
    bool failed = false;  // Ausschalten gescheitert, noch nicht als aus gelesen
  };
  std::map<std::string, Cut> cuts_;
  std::string runningPump_;
  std::string runningPurpose_;
  std::map<std::string, Ms> onSince_;  // Rolle → eingeschaltet seit
};

// Ein Dosierauftrag = ein Kanister, in Teilläufe zerlegt. Gebucht wird je
// Lauf aus der Ist-Laufzeit (Quelle: RAT-070), nicht erst am Ende.
struct DoseOrder {
  std::string id;
  std::string purpose;  // mix | manual | ec | ph | calibration | prime
  DoseStep step;
};

struct DoseProgress {
  enum class State { Idle, Running, Done, Failed, Aborted } state = State::Idle;
  std::string orderId;
  size_t run = 0;
  double mlDone = 0;
  Ms msDone = 0;
  Msg error;
};

class Doser {
 public:
  // Kennung dieses Starts. Sie hängt an jeder Bus-Job-ID, damit eine ID nach
  // einem Neustart des Hubs nie als Wiederholung eines alten Laufs gilt.
  void setBootTag(std::string tag) { bootTag_ = std::move(tag); }
  bool busy() const { return active_.has_value(); }
  bool start(const Ctx& c, Actuators& act, DoseOrder order, Msg& err);
  // Liefert den Fortschritt; bucht abgeschlossene Läufe. Meldet der Block einen
  // Lauf nicht in der Frist zurück, schaltet der Doser ab und bucht ihn als
  // gelaufen (sichere Richtung: nicht nachdosieren).
  void tick(const Ctx& c, Actuators& act);
  // Stoppt und bucht, was schon gelaufen ist (RAT-070).
  void abort(const Ctx& c, Actuators& act, const Msg& reason);
  const DoseProgress& progress() const { return progress_; }
  const std::optional<DoseOrder>& active() const { return active_; }
  // Fertig/fehlgeschlagen abholen und zurücksetzen.
  std::optional<DoseProgress> takeFinished();

 private:
  void book(const Ctx& c, Ms ms);
  void logOrder(const Ctx& c);
  bool launch(const Ctx& c, Actuators& act);
  void fail(const Ctx& c, Msg error);
  std::string busJob() const;
  std::optional<DoseOrder> active_;
  DoseProgress progress_;
  std::optional<DoseProgress> finished_;
  Ms pauseUntil_ = 0;
  Ms runStartedAt_ = 0;
  Ms runRequestedMs_ = 0;
  bool running_ = false;
  int seq_ = 0;
  std::string bootTag_;
};

// Sichtbare Aufträge des Nutzers: Mischen, Handgabe, Einmessen, Schlauch füllen.
struct JobStep {
  DoseStep dose;
  std::string state = "pending";  // pending | running | done | failed | skipped
  double mlDone = 0;
};

struct Job {
  std::string id, type;               // mix | manual | calibration | prime
  std::string state = "running";      // running | waiting_user | mixing | done | failed | aborted
  std::vector<JobStep> steps;
  size_t index = 0;
  Msg message;
  Epoch startedAt = 0, finishedAt = 0;
  Ms waitUntil = 0;                   // Durchmischen mit Umwälzpumpe (Countdown)
  bool guided = true;
  bool circulation = false;           // Umwälzpumpe vom Auftrag eingeschaltet
  json info = json::object();         // Rezept, Wasser, Einmessdaten
};
void to_json(json& j, const Job& job);

}  // namespace gc
