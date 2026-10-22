// SPDX-License-Identifier: AGPL-3.0-or-later
// Layer 8: actuator gateway and dose execution.
//
// Actuators is the ONLY way to pumps and switched outputs (rule R1).
// It holds the hard-coded safety minimum that the catalog can only make
// stricter (R7): a calibration value present, run time limits, one run at
// a time, the Stop, dry-run protection, the inlet's emergency limit, no
// watering on water not fit for it (SD-093).
// The watchdog never includes this header (architecture test, R2).
#pragma once

#include <algorithm>
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
  bool stopped = false;          // Stop active
  Epoch maintenanceUntil = 0;    // Pflegemodus: Automatik ruht
  bool probeCalibrating = false;  // a pH or EC probe of the tank is in a calibration: its readings are not the tank's
};

// Minimum pause of a compressor device after switching off (rationale:
// RAT-034). A lock against the opposite device belongs to the climate
// function; here the rule is "never both at once".
constexpr Ms kCompressorPause = 5 * kMinute;
// Obergrenze der Luftfeuchte für den Befeuchter: Kondensat an Lampen und
// Steckdosen vermeiden (Annahme, Vorschlag hardware 06.10.2026).
constexpr double kHumidifierMaxRh = 85.0;
// Water not fit for watering (SD-093, RAT-086), fixed in code (R7): a pH outside 4.0–8.5, or an EC
// more than 1.0 mS/cm above the EC target. The band catches a pump that ran too long or dosed too
// much, not a recipe missed: uncorrected tap water stays fit (RAT-084). The values are assumptions.
constexpr double kWaterPhMin = 4.0;
constexpr double kWaterPhMax = 8.5;
constexpr double kWaterEcAboveTarget = 1.0;
// The EC limit whatever the target, and the limit when no EC target is set and EC control is off
constexpr double kWaterEcMax = 5.0;
// After a dose and after a start, watering waits until it has mixed in: at least 240 s, 100 s per
// 20 L in the tank (RAT-052; the scaling is an assumption), never more than 60 min (R7)
constexpr Ms kWaterMixMinMs = 240 * kSecond;
constexpr Ms kWaterMixPer20LMs = 100 * kSecond;
constexpr Ms kWaterMixMaxMs = 60 * kMinute;

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
  // After a start: a dose the restart cut is not known, so watering waits as after one (SD-093).
  void holdWatering(Ms now) {
    doseUntil_ = std::max(doseUntil_.value_or(now), now);
    holdFromStart_ = true;
  }

  // Stop: all pumps and outputs off, idempotent (RAT-036).
  // Counts as switching off: minimum pauses hold afterwards too.
  // keepPowerLossOn: after a restart, roles that come back on after a power
  // loss (fans, PD-050) keep their state.
  void stopAll(const Catalog& cat, const Config& cfg, Ms now, bool keepPowerLossOn = false);
  // Je Takt: Trockenlauf, Notgrenze, Zeitlimits; schaltet nur AUS, nie EIN.
  void enforce(const Ctx& c);

  // The circulation pump's dry-run latch stands and will release itself (SD-067): the
  // controller shows it as blocked, not as needing a person.
  bool dryLatchReleasesItself(const Ctx& c) const;

 private:
  bool sw(const std::string& dev, int channel, bool on, std::string& err);
  std::optional<bool> swState(const std::string& dev, int channel) const;
  // Schutzabschaltung einer Rolle, je Grund (`key`) einmal gemeldet, bis die
  // Rolle aus ist oder umgehängt wird (zwei Gründe wechseln sich nicht je
  // Takt ab).
  // Scheitert das Ausschalten, lautet die Meldung „Aus nicht bestätigt“;
  // enforce versucht es weiter, solange Grund oder Rastung bestehen, und
  // meldet „Aus bestätigt“, sobald der Ausgang aus ist.
  // failedText, if given, is the reason in "Off not confirmed" (a text that promises no release).
  bool cut(const Ctx& c, const std::string& role, const std::string& key, const std::string& type,
           const std::string& severity, const Msg& title, const Msg& text, const Msg* failedText = nullptr);
  // Grund wieder scharf (neue Rastung) bzw. als schon gemeldet vermerken.
  void rearm(const std::string& role, const std::string& key);
  void noteReported(const std::string& role, const std::string& key);
  // A dry-run latch caused only by a low level releases itself once the tank is refilled (SD-022).
  void releaseDryLatch(const Ctx& c);
  // Why the tank's water is not fit for watering now (SD-093, RAT-086): a pH or EC probe in a
  // calibration or without a usable reading, a reading outside the band, or a dose running or mixing
  // in. Nothing when fit. A probe not assigned, or of a class calibrated outside the hub, is not
  // checked (RAT-025); with neither probe checked, nothing is.
  std::optional<Msg> waterUnfit(const Ctx& c) const;

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
  // A dosing pump ran until then at the latest: its planned end, or the last tick it still ran;
  // after a start the start itself (SD-093). None before the first.
  std::optional<Ms> doseUntil_;
  bool holdFromStart_ = false;  // the hold is the start's, no dose ran since
  std::map<std::string, Ms> onSince_;  // Rolle → eingeschaltet seit
  std::optional<Ms> dryBackSince_;     // level back above the release level since (SD-022)
  bool dryOffSeen_ = false;            // the latched pump has been read as off (SD-067)
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
  // Drops the running order without booking or logging it, for when booking it failed during a
  // Stop: nothing of the order runs or is booked later, and the pumps go off once more.
  void drop(Actuators& act);
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
  std::string state = "running";      // running | waiting_user | mixing | settling | done | failed | aborted
  std::vector<JobStep> steps;
  size_t index = 0;
  Msg message;
  Epoch startedAt = 0, finishedAt = 0;
  Ms waitUntil = 0;                   // end of mixing between parts or at the end, or of the wait for stirring
  bool guided = true;
  bool circulation = false;           // Umwälzpumpe vom Auftrag eingeschaltet
  bool unmixed = false;               // the circulation pump stood still while the mix settled at its end
  bool pumpStands = false;            // … and the card says so now
  Ms pumpOffSince = 0;                // since when the pump reads off or unknown while it mixes in; 0: it runs
  bool cutMarked = false;             // EC and pH were told to settle what went in (Hub::mixCut)
  json info = json::object();         // Rezept, Wasser, Einmessdaten
};
void to_json(json& j, const Job& job);

}  // namespace gc
