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

class Actuators {
 public:
  explicit Actuators(IBus& bus) : bus_(bus) {}

  bool startRun(const Ctx& c, const std::string& pump, Ms ms, const std::string& purpose, const std::string& jobId,
                Msg& err);
  RunStatus runStatus(const std::string& pump) const { return bus_.runStatus(pump); }
  bool pumpRunning() const { return !runningPump_.empty(); }
  void clearRun(const std::string& pump);
  void stopPumps();

  // Schaltausgänge nur über Rollen (tank.circulation, tank.inlet).
  bool setRole(const Ctx& c, const std::string& role, bool on, const std::string& who, Msg& err);
  std::optional<bool> roleState(const Config& cfg, const std::string& role) const;
  // Warum eine Rolle gerade nicht eingeschaltet werden darf (Einschaltsperre).
  std::optional<Msg> inhibit(const Ctx& c, const std::string& role) const;

  // Not-Halt: alle Pumpen und Ausgänge aus, idempotent (Quelle: RAT-036).
  void stopAll(const Config& cfg);
  // Je Takt: Trockenlauf, Notgrenze, Zeitlimits; schaltet nur AUS, nie EIN.
  void enforce(const Ctx& c);

 private:
  IBus& bus_;
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
  bool busy() const { return active_.has_value(); }
  bool start(const Ctx& c, Actuators& act, DoseOrder order, Msg& err);
  // Liefert den Fortschritt; bucht abgeschlossene Läufe.
  void tick(const Ctx& c, Actuators& act);
  void abort(Actuators& act, const Config& cfg);
  const DoseProgress& progress() const { return progress_; }
  const std::optional<DoseOrder>& active() const { return active_; }
  // Fertig/fehlgeschlagen abholen und zurücksetzen.
  std::optional<DoseProgress> takeFinished();

 private:
  void book(const Ctx& c, Ms ms);
  void logOrder(const Ctx& c);
  bool launch(const Ctx& c, Actuators& act);
  std::optional<DoseOrder> active_;
  DoseProgress progress_;
  std::optional<DoseProgress> finished_;
  Ms pauseUntil_ = 0;
  bool running_ = false;
  int seq_ = 0;
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
