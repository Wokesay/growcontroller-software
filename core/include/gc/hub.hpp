// SPDX-License-Identifier: AGPL-3.0-or-later
// Kompositionswurzel des Hub-Kerns: verbindet Bus, Sensorwahrheit, Gateway,
// Aufträge, Regler, Resolver, Watchdog, Verlauf und Speicher. Läuft als eine
// Ereignisschleife mit injizierter Uhr (Regel R8); die API ruft die
// öffentlichen Methoden unter dem Hub-Mutex auf.
#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "gc/auth.hpp"
#include "gc/clock.hpp"
#include "gc/control.hpp"
#include "gc/history.hpp"
#include "gc/resolver.hpp"
#include "gc/watchdog.hpp"

namespace gc {

// Updates sind plattformabhängig (ESP32: OTA mit A/B; Simulator: Attrappe).
class IUpdater {
 public:
  virtual ~IUpdater() = default;
  virtual json status() = 0;
  virtual json check() = 0;
  virtual json install(const std::string& version) = 0;
  virtual json rollback() = 0;
};

struct Result {
  int status = 200;
  json body = json::object();
  static Result ok(json b = json::object()) { return {200, std::move(b)}; }
  // An error is a message with key and values, shown in the page's language (SD-032, SD-083).
  static Result fail(int st, const Msg& m, json extra = json::object());
};

class Hub {
 public:
  Hub(const Catalog& cat, IBus& bus, IStorage& storage, const IClock& clock, RandomFn rng);

  void setUpdater(IUpdater* u) { updater_ = u; }
  // Netzgeräte (schaltbare Steckdosen); optional.
  void setNetBus(INetBus* n) {
    net_ = n;
    act_.setNet(n);
  }
  void setPlatform(json p) { platform_ = std::move(p); }
  // The files the hub reads from its storage, and the copies of broken ones
  // it writes: one list for all who handle its folder (#212).
  static const std::vector<std::string>& files();
  static const std::vector<std::string>& brokenCopies();
  void boot();
  void tick();
  void flush();     // saves what is not on disk yet: password, state, new events
  bool saveAuth();  // the password file, on disk when it returns true
  // A write the storage could not finish (the simulator's fast-forward): said and written again.
  void storageFailed(const std::string& name);

  std::recursive_mutex& mutex() { return mtx_; }
  const Catalog& catalog() const { return cat_; }
  const Config& config() const { return cfg_; }
  // Passwort war gesetzt, auth.json fehlt aber: kein Setup über das Netz.
  bool credentialsLost() const { return cfg_.system.passwordSet && !auth_.hasPassword(); }
  // auth.json is there but cannot be read: no first password over it (SD-081)
  bool passwordUnreadable() const;
  // The files that were there but could not be read at the start (SD-081)
  const std::vector<std::string>& unreadableFiles() const { return unreadable_; }
  void markPasswordSet();
  // The platform shows the code to its owner alone (#79); "" when a password
  // is set or its credentials are lost (setup is refused then anyway).
  std::string armSetupCode(Ms validFor = 0);
  Auth& auth() { return auth_; }
  IUpdater* updater() { return updater_; }

  // ---- Lesen
  json info() const;
  json state();
  // The hub's wall time (PD-069); the API reads it here, not from the platform.
  Epoch now() const;
  json configJson() const;
  json history(const std::string& series, Epoch from, Epoch to, size_t points) const;
  json events(Epoch from, Epoch to, const std::string& type, size_t limit) const;
  json diagnostics();
  std::string language() const;  // the hub's own, "de" or "en" (SD-014)
  // lang "de": semicolon and decimal comma; otherwise English, comma and decimal point
  std::string exportCsv(const std::vector<std::string>& series, Epoch from, Epoch to, const std::string& lang) const;

  // ---- Einrichtung
  Result completeSetup();
  Result setSystem(const json& j);
  Result acceptDevice(const std::string& id, const std::string& name);
  Result renameDevice(const std::string& id, const std::string& name);
  // A sensor's sign of life (SD-042): source "" removes it.
  Result setDeviceLife(const std::string& id, const std::string& source, const std::string& channel);
  Result removeDevice(const std::string& id);
  Result bindRole(const std::string& role, const std::string& device, int channel);
  Result unbindRole(const std::string& role);
  // Ausgang 3 s einschalten, um zu sehen, welches Gerät dran hängt.
  Result testRole(const std::string& role);
  // Handbetrieb: Ausgang an oder aus, mit allen Sperren und Höchstlaufzeiten.
  Result switchRole(const std::string& role, bool on);
  Result putTank(const json& j);
  Result putZone(const json& j);
  Result putCanister(const json& j);
  Result deleteCanister(const std::string& id);
  Result setStock(const std::string& id, double ml);
  Result putRecipe(const json& j);
  Result deleteRecipe(const std::string& id);
  // lang: "de" or "en" for name, note and part names; empty = system language (#32)
  Result applyRecipeTemplate(const std::string& templateId, const json& map = json::object(), const std::string& lang = "");
  Result putFunction(const std::string& id, const json& j);
  Result importConfig(const json& j);

  // ---- Bedienen
  Result mixPlan(const json& req);
  Result mixStart(const json& req);
  Result manualDose(const std::string& canister, double ml);
  Result startCalibration(const std::string& pump, double seconds);
  Result calibrationResult(const std::string& jobId, double ml);
  Result prime(const std::string& pump, double seconds);
  Result jobContinue(const std::string& id);
  Result jobAbort(const std::string& id);
  Result jobResume(const std::string& id);
  Result probeCalibration(const json& j);
  Result ackLatch(const std::string& id);
  Result stop(const Msg& who);
  Result resume();
  Result maintenance(double minutes);
  Result manualMeasure(const json& j);
  Result growStart(const json& j);
  Result growNextPhase();
  Result growHarvest();
  Result growComplete();

  // Ereignis aus der API (z. B. Anmeldung), sicherheitsrelevant protokolliert
  void logEvent(const std::string& type, const std::string& sev, Msg title, Msg text = {});

 private:
  Ctx ctx();
  // Saves the configuration; with a message it also logs what changed.
  void saveConfig(const std::optional<Msg>& what);
  void autoBindMeasures();
  void releaseSocket(const RoleDef& rd, const Binding& b);
  void setFanSockets(bool comeBackOn);
  void watchClock(Ms now);
  void shiftDeadlines(Epoch jump, Epoch epoch);
  void saveState();
  bool stopKeepingState();  // a state.json kept unread is written anew from here (SD-081)
  void saveJob();
  void saveEvents();    // appends the new events to the journal
  void saveSnapshot();  // history and events whole; empties their journals
  void saveEventsSnapshot();
  void saveHistorySnapshot();
  // Writes through the storage; a failed write is said once as an alarm and
  // written again from what the hub holds (#68).
  bool writeFile(const std::string& name, const std::string& data);
  bool appendFile(const std::string& name, const std::string& data);
  bool stored(bool ok, const std::string& name);
  void retryUnsaved(Epoch epoch);
  bool writeJob(const std::string& data);
  void sampleHistory(Epoch epoch);
  void tickImpl();
  void tickJob(Ctx& c);
  void onJobDose(Ctx& c, const DoseProgress& p);
  bool startJobStep(Ctx& c);
  void finishJob(const std::string& state, Msg m);
  void finishMixDone(Ctx& c, bool unmixed);  // all parts in: the mix's report and "done"
  Ms mixSettleMs() const;      // how long a mix mixes in at its end
  bool controllerFree(const char* fn, const char* latch) const;  // switched on and not locked
  bool controlOn() const;      // EC or pH control free: something waits for the mixed tank
  const RecipeCfg* ecRecipe() const;  // the recipe EC control doses, as it picks it (activeRecipe in control.cpp)
  MixRequest mixRequest(const json& req) const;  // a mix as asked, with the EC target's range (SD-098)
  void recipeEdited(const RecipeCfg& before, const RecipeCfg& after);  // its learned EC effect, its round (SD-092)
  void forgetEcEffect(const std::string& id);  // a recipe deleted, left out or new (SD-092)
  // A mix ending early with nutrients in (or a part cut unbooked): EC and pH settle first (SD-057)
  void mixCut(bool byStop, bool doseCut = false);
  void settleControllersFirst(bool byStop);
  json jobJson() const;  // {job, lastJob}: job ist leer, wenn der Auftrag gerade fertig wurde
  double tankVolume() const;
  void endFailedMix();
  // A job of the user's that holds control: dosing, waiting for the user, mixing between parts, or the
  // mix settling at its end (SD-057). The boot report lists the same states.
  static bool activeJobState(const std::string& s) { return s == "running" || s == "waiting_user" || s == "mixing" || s == "settling"; }
  bool userJobActive() const { return job_ && activeJobState(job_->state); }
  std::string newId(const std::string& prefix);
  void detectDevices();
  // Probe sessions each tick: a point of a sensor on its own schedule takes
  // the first report after it was asked for; a session left for 30 min no
  // longer explains jumps, but holds pH and EC control until it is saved or
  // cancelled, as the probe may still be in a buffer (SD-041)
  void tendProbeSessions(Ms now);
  static void addProbePoint(json& sess, double raw, double ref);
  static constexpr Ms kProbeIdle = 30 * kMinute;
  static constexpr Ms kSameReportMs = 2 * kSecond;  // as SD-035: reports under 2 s apart are one
  static constexpr size_t kMaxProbePoints = 20;

  bool tickFault_ = false;  // letzter Takt mit Ausnahme: Aktoren aus, einmal melden
  mutable std::recursive_mutex mtx_;
  const Catalog& cat_;
  IBus& bus_;
  IStorage& store_;
  HubClock clock_;  // wall time, secured flag, continued clock, operating time (PD-069)
  RandomFn rng_;
  IUpdater* updater_ = nullptr;
  INetBus* net_ = nullptr;
  std::map<std::string, Ms> testOff_;  // Rolle → aus um (Testen)
  json platform_ = json::object();

  Config cfg_;
  RuntimeState rt_;
  Auth auth_;
  SensorTruth truth_;
  Actuators act_;
  Doser doser_;
  EcController ec_;
  PhController ph_;
  RefillController refill_;
  CirculationController circ_;
  History history_;
  EventLog log_;
  WatchResult watch_;
  std::vector<FunctionState> functions_;
  std::vector<DeviceReport> devices_;
  std::vector<PortReport> ports_;
  PumpMap pumps_;
  std::set<std::string> seenOnline_;
  // Devices logged as not responding since the start. Only they are "back"
  // when they answer again, so a start logs no "back", however the devices
  // come up one after another (#115).
  std::set<std::string> reportedLost_;

  std::optional<Job> job_;
  std::optional<Job> lastJob_;
  std::map<std::string, json> probeSessions_;
  bool stopped_ = false;
  Epoch maintenanceUntil_ = 0;
  Epoch bootEpoch_ = 0;
  Ms bootMs_ = 0;
  Epoch lastSample_ = 0, lastWatch_ = 0, lastStateSave_ = 0, lastSnapshot_ = 0;
  Epoch lastTickEpoch_ = 0;
  Ms lastTickMs_ = 0;
  bool unsecuredReported_ = false;
  bool stateDirty_ = false;
  std::uint64_t savedEventId_ = 0;
  std::uint64_t journaledEvents_ = 0;  // in the events journal since its snapshot
  std::set<std::string> unsaved_;      // files whose last write failed
  std::set<std::string> kept_;         // not written: unreadable at the start, or its partner, until written anew (SD-081)
  std::vector<std::string> unreadable_;  // there but unreadable at the start
  Epoch lastRetry_ = 0, snapshotRetryAt_ = 0, snapshotBackoffS_ = 10;
  std::string jobOnDisk_;  // what job.json should hold
  int idSeq_ = 0;
};

}  // namespace gc
