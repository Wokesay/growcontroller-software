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
  static Result fail(int st, const std::string& key, const std::string& text, json extra = json::object());
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
  void boot();
  void tick();
  void flush();  // alles Ungespeicherte sichern

  std::recursive_mutex& mutex() { return mtx_; }
  const Catalog& catalog() const { return cat_; }
  const Config& config() const { return cfg_; }
  // Passwort war gesetzt, auth.json fehlt aber: kein Setup über das Netz.
  bool credentialsLost() const { return cfg_.system.passwordSet && !auth_.hasPassword(); }
  void markPasswordSet();
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
  std::string exportCsv(const std::vector<std::string>& series, Epoch from, Epoch to) const;

  // ---- Einrichtung
  Result completeSetup();
  Result setSystem(const json& j);
  Result acceptDevice(const std::string& id, const std::string& name);
  Result renameDevice(const std::string& id, const std::string& name);
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
  Result stop(const std::string& who);
  Result resume();
  Result maintenance(double minutes);
  Result manualMeasure(const json& j);
  Result growStart(const json& j);
  Result growNextPhase();
  Result growHarvest();
  Result growComplete();

  // Ereignis aus der API (z. B. Anmeldung), sicherheitsrelevant protokolliert
  void logEvent(const std::string& type, const std::string& sev, const std::string& title, const std::string& text);

 private:
  Ctx ctx();
  void saveConfig(const std::string& what);
  void autoBindMeasures();
  void releaseSocket(const RoleDef& rd, const Binding& b);
  void setFanSockets(bool comeBackOn);
  void watchClock(Ms now);
  void shiftDeadlines(Epoch jump, Epoch epoch);
  void saveState();
  void saveJob();
  void sampleHistory(Epoch epoch);
  void tickImpl();
  void tickJob(Ctx& c);
  void onJobDose(Ctx& c, const DoseProgress& p);
  bool startJobStep(Ctx& c);
  void finishJob(const std::string& state, Msg m);
  json jobJson() const;  // {job, lastJob}: job ist leer, wenn der Auftrag gerade fertig wurde
  double tankVolume() const;
  bool userJobActive() const { return job_ && (job_->state == "running" || job_->state == "waiting_user" || job_->state == "mixing"); }
  std::string newId(const std::string& prefix);
  void detectDevices();

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

  std::optional<Job> job_;
  std::optional<Job> lastJob_;
  std::map<std::string, json> probeSessions_;
  bool stopped_ = false;
  Epoch maintenanceUntil_ = 0;
  Epoch bootEpoch_ = 0;
  Ms bootMs_ = 0;
  Epoch lastSample_ = 0, lastWatch_ = 0, lastStateSave_ = 0, lastHistorySave_ = 0;
  Epoch lastTickEpoch_ = 0;
  Ms lastTickMs_ = 0;
  bool unsecuredReported_ = false;
  bool stateDirty_ = false;
  std::uint64_t savedEventId_ = 0;
  int idSeq_ = 0;
};

}  // namespace gc
