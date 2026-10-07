// SPDX-License-Identifier: AGPL-3.0-or-later
#include "gc/hub.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <sstream>

#include "gc/embedded.hpp"
#include "gc/sha256.hpp"

namespace gc {

Result Result::fail(int st, const std::string& key, const std::string& text, json extra) {
  json b = {{"error", {{"key", key}, {"text", text}}}};
  for (auto& [k, v] : extra.items()) b[k] = v;
  return {st, b};
}

namespace {

constexpr const char* kConfigFile = "config.json";
constexpr const char* kStateFile = "state.json";
constexpr const char* kAuthFile = "auth.json";
constexpr const char* kEventsFile = "events.json";
constexpr const char* kHistoryFile = "history.bin";
constexpr const char* kJobFile = "job.json";
constexpr Epoch kMaxMaintenanceS = 240 * 60;  // longest maintenance window

// Kurzname für IDs: Kleinbuchstaben, Ziffern, Bindestrich; deutsche Umlaute
// umschrieben („Blüte“ → „bluete“), damit IDs lesbar bleiben.
std::string slug(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    const auto c = static_cast<unsigned char>(s[i]);
    if (c == 0xC3 && i + 1 < s.size()) {
      const auto n = static_cast<unsigned char>(s[i + 1]);
      const char* rep = n == 0xA4 || n == 0x84 ? "ae" : n == 0xB6 || n == 0x96 ? "oe" : n == 0xBC || n == 0x9C ? "ue" : n == 0x9F ? "ss" : nullptr;
      if (rep) {
        out += rep;
        ++i;
        continue;
      }
    }
    if (std::isalnum(c)) out += static_cast<char>(std::tolower(c));
    else if (!out.empty() && out.back() != '-') out += '-';
  }
  while (!out.empty() && out.back() == '-') out.pop_back();
  return out.empty() ? "x" : out.substr(0, 24);
}

Result errors(const std::vector<Msg>& errs) {
  json arr = json::array();
  for (const auto& e : errs) arr.push_back(e);
  return {422, {{"error", {{"key", "validation"}, {"text", errs.empty() ? "" : errs.front().text}}}, {"errors", arr}}};
}

}  // namespace

Hub::Hub(const Catalog& cat, IBus& bus, IStorage& storage, const IClock& clock, RandomFn rng)
    : cat_(cat), bus_(bus), store_(storage), clock_(clock), rng_(rng), auth_(rng), truth_(cat), act_(bus) {}

std::string Hub::newId(const std::string& prefix) {
  std::uint8_t b[3];
  rng_(b, sizeof b);
  return prefix + "-" + toHex(b, sizeof b);
}

Ctx Hub::ctx() {
  return Ctx{cat_, cfg_, rt_, truth_, log_, pumps_, clock_.nowMs(), clock_.epoch(), stopped_, maintenanceUntil_};
}

void Hub::logEvent(const std::string& type, const std::string& sev, const std::string& title, const std::string& text) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  log_.add(clock_.epoch(), type, sev, title, text);
}

// ------------------------------------------------------------------ Start

void Hub::boot() {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  doser_.setBootTag(newId("b"));  // Bus-Job-IDs über Neustarts eindeutig
  if (auto s = store_.read(kEventsFile)) {
    auto j = json::parse(*s, nullptr, false);
    if (!j.is_discarded()) log_.load(j);
  }
  savedEventId_ = log_.lastId();
  // Time base first (PD-069): without a secured time the clock continues
  // from the moment saved last, never before the newest event.
  const std::optional<std::string> stateRaw = store_.read(kStateFile);
  const json stateJson = stateRaw ? json::parse(*stateRaw, nullptr, false) : json();
  const bool stateObj = stateJson.is_object();
  const std::uint32_t boot = stateObj && stateJson.contains("bootCount") && stateJson["bootCount"].is_number_unsigned()
                                 ? stateJson["bootCount"].get<std::uint32_t>() + 1
                                 : 1;
  clock_.start(stateObj && stateJson.contains("clock") ? stampFromJson(stateJson["clock"]) : std::nullopt, log_.newestTs(),
               boot);
  bootEpoch_ = clock_.epoch();
  bootMs_ = clock_.nowMs();
  lastTickEpoch_ = bootEpoch_;
  lastTickMs_ = bootMs_;
  if (auto s = store_.read(kConfigFile)) {
    auto j = json::parse(*s, nullptr, false);
    try {
      if (j.is_discarded()) throw std::runtime_error("kein gültiges JSON");
      cfg_ = configFromJson(j);
    } catch (const std::exception& e) {
      // Mit der letzten guten Konfiguration ist hier nichts mehr zu retten:
      // Werkseinstellung, alle Aktoren aus, laut melden (Vorschlag architekt).
      store_.write("config.broken.json", *s);
      cfg_ = Config{};
      log_.add(bootEpoch_, "system", "alarm", "Konfiguration unlesbar",
               std::string("Start mit Werkseinstellung. Kopie in config.broken.json. Grund: ") + e.what());
    }
  }
  if (cfg_.tanks.empty()) cfg_.tanks.emplace_back();
  if (stateRaw) {
    try {
      rt_ = runtimeFromJson(stateJson);
    } catch (const std::exception& e) {
      rt_ = RuntimeState{};
      log_.add(bootEpoch_, "system", "alarm", "Laufzeitzustand unlesbar",
               std::string("Rastungen, Sprungsperren und Vorrat sind verloren – bitte Tank und Kanister prüfen. Grund: ") +
                   e.what());
    }
  }
  if (auto s = store_.read(kAuthFile)) auth_.load(json::parse(*s, nullptr, false));
  if (auth_.hasPassword() && !cfg_.system.passwordSet) {
    cfg_.system.passwordSet = true;  // Geräte von vor dieser Kennzeichnung nachziehen
    saveConfig("");
  }
  if (credentialsLost())
    log_.add(bootEpoch_, "system", "alarm", "Zugangsdaten fehlen",
             "Das Passwort ist nicht mehr lesbar. Einrichtung über das Netz ist gesperrt – Werksreset am Gerät nötig.");
  if (auto s = store_.read(kHistoryFile)) history_.load(*s);
  rt_.bootCount = boot;
  // Nach dem Start ist alles aus; Abläufe werden nicht fortgesetzt (R6).
  // Fans keep their state: their sockets come back on after a power loss
  // (PD-050), and after a restart of the hub alone they keep running.
  // An emergency stop survives the restart and stops the fans too (PD-076).
  stopped_ = rt_.stopped;
  act_.stopAll(cat_, cfg_, clock_.nowMs(), !stopped_);
  if (stopped_)
    log_.add(bootEpoch_, "system", "alarm", "Not-Halt besteht weiter",
             "Nach dem Neustart bleibt alles aus, auch die Lüfter, bis jemand fortsetzt.");
  if (auto s = store_.read(kJobFile)) {
    auto j = json::parse(*s, nullptr, false);
    const std::string state = jstr(j, "state");
    if (state == "running" || state == "waiting_user" || state == "mixing") {
      std::string what = jstr(j, "type") == "mix" ? "Mischlauf" : "Auftrag";
      double idxNum = jnum(j, "index", 0);
      size_t idx = isNum(idxNum) && idxNum >= 0 ? static_cast<size_t>(idxNum) : 0;
      const json steps = j.contains("steps") && j["steps"].is_array() ? j["steps"] : json::array();
      size_t total = steps.size();
      std::string done;
      for (const auto& st : steps) {
        double ml = jnum(st, "mlDone", 0);
        if (isNum(ml) && ml > 0) done += (done.empty() ? "" : ", ") + jstr(st, "name") + " " + fmt(ml, 1) + " ml";
      }
      log_.add(bootEpoch_, "mix", "warn", what + " durch Neustart unterbrochen",
               "Bei Schritt " + std::to_string(idx + 1) + "/" + std::to_string(total) + ". Drin: " +
                   (done.empty() ? "nichts" : done) + ". Nicht automatisch fortgesetzt.");
      j["state"] = "aborted";
      j["message"] = {{"key", "job.reboot"}, {"text", "Durch Neustart unterbrochen – nicht fortgesetzt"}};
      store_.write(kJobFile, j.dump());
    }
  }
  log_.add(bootEpoch_, "system", "info", "Hub gestartet", std::string("Version ") + embedded::kVersion);
}

void Hub::flush() {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  if (auth_.hasPassword()) store_.write(kAuthFile, auth_.toJson().dump());
  saveState();
  store_.write(kEventsFile, log_.toJson().dump());
  store_.write(kHistoryFile, history_.dump());
}

void Hub::saveConfig(const std::string& what) {
  cfg_.revision++;
  store_.write(kConfigFile, json(cfg_).dump(1));
  if (!what.empty())
    log_.add(clock_.epoch(), "config", "info", what, "Einstellung geändert (Revision " + std::to_string(cfg_.revision) + ")",
             {{"revision", cfg_.revision}});
}

void Hub::saveState() {
  rt_.clock = clock_.stamp();
  store_.write(kStateFile, json(rt_).dump());
  stateDirty_ = false;
}

void Hub::saveJob() {
  if (job_) store_.write(kJobFile, json(*job_).dump());
}

// ------------------------------------------------------------------ Takt

void Hub::detectDevices() {
  std::set<std::string> online;
  for (const auto& d : devices_)
    if (d.online) online.insert(d.id);
  Epoch e = clock_.epoch();
  for (const auto& id : online)
    if (!seenOnline_.count(id)) {
      const DeviceReport* d = nullptr;
      for (const auto& x : devices_)
        if (x.id == id) d = &x;
      const DeviceClassDef* dc = d ? cat_.deviceClass(d->cls) : nullptr;
      const DeviceCfg* known = cfg_.device(id);
      std::string where = d && d->slot >= 0 ? "Pumpe " + std::to_string(d->slot + 1) + " am Dosierblock"
                          : d && d->port > 0 ? "Anschluss " + std::to_string(d->port)
                          : dc && dc->attach == "net" ? "im Netzwerk" : "im Hub";
      std::string title = (dc ? dc->label : "Unbekanntes Gerät") + (known ? " wieder da" : " erkannt");
      std::string text = where;
      // Pumpe nach Umstecken: sitzt sie noch auf demselben Kanister? (Vorschlag anwender)
      if (known && d && d->cls == "pump_cap")
        if (const CanisterCfg* k = cfg_.canisterByPump(id)) text += ". Sitzt sie noch auf " + k->name + "?";
      if (!seenOnline_.empty() || !known) log_.add(e, "device", "info", title, text, {{"device", id}});
    }
  for (const auto& id : seenOnline_)
    if (!online.count(id)) {
      const DeviceCfg* known = cfg_.device(id);
      log_.add(e, "device", known ? "warn" : "info", (known && !known->name.empty() ? known->name : id) + " getrennt",
               "Gerät antwortet nicht mehr", {{"device", id}});
    }
  seenOnline_ = online;
}

double Hub::tankVolume() const {
  const auto& lvl = truth_.get("tank.level");
  if (lvl.usable()) return *lvl.value;
  return rt_.tankVolumeL;
}

void Hub::tick() {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  try {
    tickImpl();
    if (tickFault_) {
      tickFault_ = false;
      log_.add(clock_.epoch(), "system", "info", "Steuerung läuft wieder", "Der Takt läuft wieder ohne Fehler.");
    }
  } catch (const std::exception& e) {
    // Sicherer Zustand statt Absturz oder Boot-Schleife: alles aus, laufende
    // Dosierung abbrechen, einmal laut melden. Fans keep running (PD-077).
    act_.stopAll(cat_, cfg_, clock_.nowMs(), true);
    try {
      Ctx c = ctx();
      doser_.abort(c, act_, "Interner Fehler");
      if (auto fin = doser_.takeFinished()) {
        const bool controllerDose = fin->orderId.rfind("ec-", 0) == 0 || fin->orderId.rfind("ph-", 0) == 0;
        if (!controllerDose) onJobDose(c, *fin);
      }
      ec_.reset();
      ph_.reset();
      // Nutzerauftrag nicht hängen lassen: beenden und sagen, was drin ist
      if (userJobActive()) {
        std::string done;
        for (const auto& s : job_->steps)
          if (s.mlDone > 0) done += (done.empty() ? "" : ", ") + s.dose.name + " " + fmt(s.mlDone, 1) + " ml";
        finishJob("aborted", {"job.internal", "Durch einen internen Fehler abgebrochen. Drin: " + (done.empty() ? "nichts" : done),
                              json::object()});
      }
    } catch (...) {
      // Aufräumen darf den sicheren Zustand nicht verhindern: Aktoren sind schon aus.
    }
    if (!tickFault_) {
      tickFault_ = true;
      log_.add(clock_.epoch(), "system", "alarm", "Interner Fehler – alles aus außer den Lüftern",
               std::string("Die Steuerung hat einen Fehler abgefangen und alle Pumpen und Ausgänge abgeschaltet; "
                           "die Lüfter laufen weiter: ") +
                   e.what());
    }
  }
}

namespace {
std::string spanText(Epoch s) {
  s = s < 0 ? -s : s;
  if (s < 120) return std::to_string(s) + " s";
  if (s < 2 * 3600) return std::to_string(s / 60) + " min";
  return fmt(static_cast<double>(s) / 3600.0, 1) + " h";
}
}  // namespace

// Secured time and clock steps (PD-069, PD-073).
void Hub::watchClock(Ms now) {
  const bool was = clock_.secured();
  const std::string before = clock_.source();
  clock_.poll();
  const bool secured = clock_.secured();
  const Epoch expected = lastTickEpoch_ + (now - lastTickMs_) / 1000;
  if (was && !secured) {
    // Lost while running: continue from the last secured time, no jump.
    clock_.lose(expected);
    unsecuredReported_ = true;
    log_.add(expected, "system", "warn", "Uhrzeit nicht mehr gesichert",
             "Der Hub zählt ab der letzten gesicherten Uhrzeit weiter.", {{"source", clock_.source()}});
  }
  const Epoch epoch = clock_.epoch();
  const Epoch jump = epoch - expected;
  if (!was && secured) {
    shiftDeadlines(jump, epoch);
    if (unsecuredReported_ || jump > 120 || jump < -120)
      log_.add(epoch, "system", "info", "Uhrzeit gesichert",
               before == "unset" ? "Netzwerkzeit empfangen. Uhrzeiten davor im Verlauf stimmen nicht."
               : jump > 0        ? "Die Uhr springt um " + spanText(jump) + " vor; so lange fehlte die Uhrzeit."
               : jump < 0        ? "Die Uhr springt um " + spanText(jump) + " zurück."
                                 : "Netzwerkzeit empfangen.",
               {{"jumpS", jump}, {"before", before}});
    unsecuredReported_ = false;
    stateDirty_ = true;
  } else if (was && secured && (jump > 5 || jump < -5)) {
    // The network time stepped the clock while it was secured.
    shiftDeadlines(jump, epoch);
    if (jump > 120 || jump < -120)
      log_.add(epoch, "system", "info", "Uhr gestellt",
               std::string("Die Uhr springt um ") + spanText(jump) + (jump > 0 ? " vor." : " zurück."), {{"jumpS", jump}});
    stateDirty_ = true;
  } else if (!secured && !unsecuredReported_ && now - bootMs_ >= 2 * kMinute) {
    unsecuredReported_ = true;
    log_.add(epoch, "system", "warn", "Uhrzeit nicht gesichert",
             std::string(clock_.source()) == "continued"
                 ? "Keine Netzwerkzeit. Der Hub zählt ab dem zuletzt gespeicherten Stand weiter; die Dauer des Ausfalls fehlt in der Uhrzeit."
                 : "Keine Netzwerkzeit und keine gespeicherte Uhrzeit. Uhrzeiten im Verlauf stimmen erst, wenn die Uhrzeit gesichert ist.",
             {{"source", clock_.source()}});
  }
  lastTickEpoch_ = epoch;
  lastTickMs_ = now;
}

// The wall clock jumped: deadlines in wall time keep their distance; jump
// locks and maintenance never last longer than they can (RAT-044).
void Hub::shiftDeadlines(Epoch jump, Epoch epoch) {
  for (Epoch* t : {&lastSample_, &lastWatch_, &lastStateSave_, &lastHistorySave_, &bootEpoch_}) *t += jump;
  if (watch_.evaluatedAt > 0) watch_.evaluatedAt += jump;
  if (rt_.lastMixAt > 0) rt_.lastMixAt += jump;
  if (maintenanceUntil_ > 0) maintenanceUntil_ = std::min(maintenanceUntil_ + jump, epoch + kMaxMaintenanceS);
  for (auto& [role, until] : rt_.jumpLocks) until = std::min(until + jump, epoch + kJumpHoldS);
  ec_.shiftClock(jump);
  ph_.shiftClock(jump);
  refill_.shiftClock(jump);
}

void Hub::tickImpl() {
  Ms now = clock_.nowMs();
  watchClock(now);
  Epoch epoch = clock_.epoch();
  bus_.poll(now);
  devices_ = bus_.devices();
  if (net_) {
    net_->poll(now);
    for (auto& d : net_->devices()) devices_.push_back(std::move(d));
  }
  ports_ = bus_.ports();
  pumps_ = pumpsFrom(devices_);
  detectDevices();
  // Angekündigte Änderungen erklären Sprünge (Quelle: RAT-042): eigene
  // Mischläufe und Handgaben, offener Zulauf, Kalibrierung, Pflegemodus.
  if (userJobActive() || (doser_.active() && doser_.active()->purpose != "ph" && doser_.active()->purpose != "ec")) {
    truth_.expectChange("tank.ec", now + 10 * kMinute);
    truth_.expectChange("tank.ph", now + 10 * kMinute);
  }
  // Eigene EC-Runde: Deckel 1,0 mS/cm liegt über der Sprungschwelle 0,5 – ankündigen.
  // pH-Gaben bleiben unangekündigt (Deckel 0,3 pH unter der Schwelle 1,0).
  if (ec_.busy()) truth_.expectChange("tank.ec", now + 10 * kMinute);
  if (act_.roleState(cfg_, "tank.inlet").value_or(false))
    for (const char* r : {"tank.ec", "tank.ph", "tank.level"}) truth_.expectChange(r, now + 5 * kMinute);
  if (epoch < maintenanceUntil_)
    for (const auto& [role, def] : cat_.roles) truth_.expectChange(role, now + (maintenanceUntil_ - epoch) * kSecond);
  for (const auto& [key, sess] : probeSessions_) {
    std::string dev = jstr(sess, "device");
    cfg_.forEachBinding([&](const std::string& role, const Binding& b) {
      if (b.device == dev) truth_.expectChange(role, now + 10 * kMinute);
    });
  }
  auto locksBefore = rt_.jumpLocks;
  truth_.update(cfg_, bus_, rt_, now, epoch);
  for (const auto& [role, until] : rt_.jumpLocks)
    if (!locksBefore.count(role)) {
      const auto& r = truth_.get(role);
      log_.add(epoch, "block", "alarm", "Sprungsperre: " + (cat_.role(role) ? cat_.role(role)->label : role), r.reason.text,
               {{"role", role}, {"until", until}});
      stateDirty_ = true;
    }
  for (const auto& [role, until] : locksBefore)
    if (!rt_.jumpLocks.count(role)) {
      log_.add(epoch, "unblock", "info", "Sprungsperre aufgehoben: " + (cat_.role(role) ? cat_.role(role)->label : role),
               "15 min Ruhe, Wert wieder gültig", {{"role", role}});
      stateDirty_ = true;
    }

  Ctx c = ctx();
  act_.enforce(c);
  for (auto it = testOff_.begin(); it != testOff_.end();) {
    if (now < it->second) {
      ++it;
      continue;
    }
    Msg e;
    act_.setRole(c, it->first, false, "Testen", e);
    it = testOff_.erase(it);
  }
  doser_.tick(c, act_);
  ControlEnv env{act_, doser_, truth_};
  env.userJob = userJobActive();
  env.volumeL = tankVolume();
  env.lastEcDoseAt = ec_.lastDoseAt();
  env.circulationOn = act_.roleState(cfg_, "tank.circulation").value_or(false);
  env.refilling = act_.roleState(cfg_, "tank.inlet").value_or(false);
  env.calibrating = !probeSessions_.empty();
  if (auto fin = doser_.takeFinished()) {
    const std::string& id = fin->orderId;
    if (id.rfind("ec-", 0) == 0) ec_.onDoseFinished(c, env, *fin);
    else if (id.rfind("ph-", 0) == 0) ph_.onDoseFinished(c, env, *fin);
    else onJobDose(c, *fin);
    stateDirty_ = true;
  }
  tickJob(c);
  env.userJob = userJobActive();
  // Nachfüllen zuerst: Es startet nur, wenn keine Gabe läuft oder einschwingt,
  // und hält dann neue EC- und pH-Runden an (RAT-055).
  env.ecBusy = ec_.busy();
  env.phBusy = ph_.busy();
  refill_.tick(c, env);
  env.refilling = act_.roleState(cfg_, "tank.inlet").value_or(false);
  ec_.tick(c, env);
  env.ecBusy = ec_.busy();
  env.lastEcDoseAt = ec_.lastDoseAt();
  ph_.tick(c, env);
  bool demand = ec_.wantsCirculation() || ph_.wantsCirculation() || (job_ && job_->circulation && userJobActive());
  circ_.tick(c, env, demand);

  if (epoch - lastSample_ >= 10) {
    lastSample_ = epoch;
    sampleHistory(epoch);
  }
  functions_ = resolveFunctions(cat_, cfg_, devices_, pumps_, truth_);
  if (epoch - lastWatch_ >= 5) {
    lastWatch_ = epoch;
    WatchInput in{cat_, cfg_, rt_, truth_.all(), {}, {}, {}, epoch, bootEpoch_, maintenanceUntil_, stopped_};
    for (const auto& d : devices_) {
      const DeviceCfg* dc = cfg_.device(d.id);
      in.devices.push_back({d.id, dc ? dc->name : d.id, d.online});
    }
    in.controllers = {{"ec", "EC nachdosieren", ec_.status().state, ec_.status().line.text},
                      {"ph", "pH regeln", ph_.status().state, ph_.status().line.text},
                      {"refill", "Nachfüllen", refill_.status().state, refill_.status().line.text},
                      {"circulation", "Umwälzen", circ_.status().state, circ_.status().line.text}};
    for (const auto& [id, p] : pumps_) in.pumpFlow[id] = p.flowMlPerMin;
    watch_ = evaluate(in);  // Watchdog liest nur, er bekommt keine Aktoren
  }
  if (stateDirty_ || epoch - lastStateSave_ >= 60) {
    lastStateSave_ = epoch;
    saveState();
  }
  if (log_.lastId() != savedEventId_ && epoch % 15 == 0) {
    savedEventId_ = log_.lastId();
    store_.write(kEventsFile, log_.toJson().dump());
  }
  if (epoch - lastHistorySave_ >= 600) {
    lastHistorySave_ = epoch;
    store_.write(kHistoryFile, history_.dump());
  }
}

void Hub::sampleHistory(Epoch epoch) {
  for (const auto& [roleId, role] : cat_.roles) {
    if (!role.series) continue;
    const auto& r = truth_.get(roleId);
    const CapabilityDef* cap = cat_.capability(role.capability);
    const bool derived = cap && cap->kind == "derived";
    if (derived ? r.quality == Quality::NotBound : !cfg_.binding(roleId)) continue;
    history_.add(roleId, epoch, r.usable() ? *r.value : kNaN);  // Lücke statt 0
  }
  double v = tankVolume();
  if (isNum(v)) history_.add("tank.volume", epoch, v);
}

// ------------------------------------------------------------------ Lesen

Epoch Hub::now() const {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  return clock_.epoch();
}

json Hub::info() const {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  return {{"product", "growcontroller"},
          {"version", embedded::kVersion},
          {"api", 1},
          {"catalogVersion", cat_.version},
          {"schemaVersion", kSchemaVersion},
          {"platform", platform_},
          {"setupDone", cfg_.system.setupDone},
          {"hasPassword", auth_.hasPassword()},
          {"name", cfg_.system.name}};
}

json Hub::state() {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  Epoch epoch = clock_.epoch();
  json j;
  j["now"] = epoch;
  j["uptimeS"] = (clock_.nowMs() - bootMs_) / 1000;
  j["time"] = {{"secured", clock_.secured()}, {"source", clock_.source()}, {"operatingS", clock_.operatingS()}};
  j["setupDone"] = cfg_.system.setupDone;
  j["stopped"] = stopped_;
  j["maintenanceUntil"] = maintenanceUntil_;
  json ports = json::array();
  for (const auto& p : ports_)
    ports.push_back({{"port", p.port}, {"state", portStateName(p.state)}, {"device", p.deviceId}, {"class", p.cls}, {"message", p.message}});
  j["ports"] = ports;
  json devs = json::array();
  for (const auto& d : devices_) {
    const DeviceCfg* dc = cfg_.device(d.id);
    const DeviceClassDef* cls = cat_.deviceClass(d.cls);
    json cal = json::object();
    if (auto it = cfg_.calibrations.find(d.id); it != cfg_.calibrations.end())
      for (const auto& [k, v] : it->second) cal[k] = static_cast<Epoch>(jnum(v, "at", 0));
    devs.push_back({{"id", d.id},
                    {"class", d.cls},
                    {"classLabel", cls ? cls->label : d.cls},
                    {"name", dc ? dc->name : ""},
                    {"configured", dc != nullptr},
                    {"online", d.online},
                    {"port", d.port},
                    {"slot", d.slot},
                    {"parent", d.parent},
                    {"fw", d.fw},
                    {"fault", d.fault},
                    {"info", d.info},
                    {"calibrations", cal}});
  }
  // eingerichtete, aber nicht erkannte Geräte ebenfalls zeigen
  for (const auto& d : cfg_.devices) {
    bool seen = false;
    for (const auto& x : devices_) seen = seen || x.id == d.id;
    if (!seen) {
      const DeviceClassDef* cls = cat_.deviceClass(d.cls);
      devs.push_back({{"id", d.id}, {"class", d.cls}, {"classLabel", cls ? cls->label : d.cls}, {"name", d.name},
                      {"configured", true}, {"online", false}, {"port", 0}, {"slot", -1}, {"parent", ""},
                      {"fw", ""}, {"fault", ""}, {"info", json::object()}, {"calibrations", json::object()}});
    }
  }
  j["devices"] = devs;
  json readings = json::object();
  for (const auto& [role, r] : truth_.all())
    readings[role] = {{"value", numOrNull(r.value)}, {"quality", qualityName(r.quality)}, {"reason", r.reason},
                      {"ageS", r.ageMs >= 0 ? json(r.ageMs / 1000) : json(nullptr)}, {"unit", r.unit},
                      {"decimals", r.decimals}, {"usable", r.usable()}};
  j["readings"] = readings;
  const auto& lvl = truth_.get("tank.level");
  j["tank"] = {{"volumeL", numOrNull(tankVolume())},
               {"source", lvl.usable() ? "level" : "mix"},
               {"capacityL", numOrNull(cfg_.tank().capacityL)},
               {"minL", numOrNull(cfg_.tank().minL)}};
  j["controllers"] = {{"ec", ec_.status()}, {"ph", ph_.status()}, {"refill", refill_.status()}, {"circulation", circ_.status()}};
  json outputs = json::object();
  for (const auto& [role, rd] : cat_.roles)
    if (!rd.profile.empty())
      if (auto st = act_.roleState(cfg_, role)) outputs[role] = *st;
  j["outputs"] = outputs;
  j["job"] = job_ ? json(*job_) : json(nullptr);
  j["lastJob"] = lastJob_ ? json(*lastJob_) : json(nullptr);
  if (doser_.active()) {
    const auto& o = *doser_.active();
    const auto& p = doser_.progress();
    j["dosing"] = {{"order", o.id}, {"purpose", o.purpose}, {"canister", o.step.canister}, {"name", o.step.name},
                   {"ml", numOrNull(o.step.ml)}, {"mlDone", p.mlDone}, {"run", p.run + 1}, {"runs", o.step.runs.size()}};
  } else {
    j["dosing"] = nullptr;
  }
  json wd = watch_;
  wd["stale"] = epoch - watch_.evaluatedAt > kWatchStaleS;
  j["watchdog"] = wd;
  j["functions"] = functions_;
  j["latches"] = rt_.latches;
  json stock = json::object();
  for (const auto& k : cfg_.canisters) {
    auto it = rt_.stockMl.find(k.id);
    stock[k.id] = it == rt_.stockMl.end() ? json(nullptr) : numOrNull(it->second);
  }
  j["stock"] = stock;
  j["effects"] = {{"ph", numOrNull(rt_.phEffect)}, {"ec", numOrNull(rt_.ecEffect)}};
  j["lastMixAt"] = rt_.lastMixAt;
  j["manual"] = {{"values", rt_.manual}, {"at", rt_.manualAt}};
  j["grow"] = json(cfg_)["grow"];
  j["eventId"] = log_.lastId();
  json probes = json::object();
  for (const auto& [k, v] : probeSessions_) probes[k] = v;
  j["probeCalibration"] = probes;
  return j;
}

json Hub::configJson() const {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  return cfg_;
}

json Hub::history(const std::string& series, Epoch from, Epoch to, size_t points) const {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  auto p = history_.query(series, from, to, points);
  json t = p.t, avg = json::array(), mn = json::array(), mx = json::array();
  for (size_t i = 0; i < p.t.size(); ++i) {
    avg.push_back(numOrNull(p.avg[i]));
    mn.push_back(numOrNull(p.min[i]));
    mx.push_back(numOrNull(p.max[i]));
  }
  return {{"series", series}, {"stepS", p.stepS}, {"t", t}, {"avg", avg}, {"min", mn}, {"max", mx}};
}

json Hub::events(Epoch from, Epoch to, const std::string& type, size_t limit) const {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  json arr = json::array();
  for (const auto& e : log_.query(from, to, type, limit)) arr.push_back(e);
  return {{"events", arr}};
}

std::string Hub::exportCsv(const std::vector<std::string>& series, Epoch from, Epoch to) const {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  std::ostringstream os;
  os << "zeit_utc";
  std::vector<SeriesPoints> pts;
  for (const auto& s : series) {
    os << ';' << s;
    pts.push_back(history_.query(s, from, to, 0));
  }
  os << '\n';
  std::map<Epoch, std::vector<double>> rows;
  for (size_t i = 0; i < pts.size(); ++i)
    for (size_t k = 0; k < pts[i].t.size(); ++k) {
      auto& row = rows[pts[i].t[k]];
      row.resize(series.size(), kNaN);
      row[i] = pts[i].avg[k];
    }
  for (const auto& [t, row] : rows) {
    os << t;
    for (double v : row) {
      os << ';';
      if (isNum(v)) os << fmt(v, 3);  // leer statt 0 (RAT-016, RAT-006)
    }
    os << '\n';
  }
  return os.str();
}

json Hub::diagnostics() {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  // Diagnosepaket für "Problem melden": ohne Geheimnisse (kein Passwort-Hash,
  // keine Sitzungen, kein WLAN), Inhalt wird vor dem Herunterladen gezeigt.
  std::uint8_t b[4];
  rng_(b, sizeof b);
  json cfg = cfg_;
  json st = state();
  json evs = json::array();
  for (const auto& e : log_.query(clock_.epoch() - 72 * 3600, clock_.epoch(), "", 300)) evs.push_back(e);
  return {{"reportId", "GC-" + toHex(b, sizeof b)},
          {"createdAt", clock_.epoch()},
          {"info", info()},
          {"config", cfg},
          {"ports", st["ports"]},
          {"devices", st["devices"]},
          {"readings", st["readings"]},
          {"controllers", st["controllers"]},
          {"watchdog", st["watchdog"]},
          {"latches", st["latches"]},
          {"events", evs},
          {"redacted", {"Passwort-Hash", "Sitzungen", "WLAN-Zugang", "IP-Adressen"}}};
}

// ------------------------------------------------------------------ Einrichtung

void Hub::markPasswordSet() {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  if (cfg_.system.passwordSet) return;
  cfg_.system.passwordSet = true;
  saveConfig("");
}

Result Hub::completeSetup() {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  cfg_.system.setupDone = true;
  saveConfig("Einrichtung abgeschlossen");
  return Result::ok();
}

Result Hub::setSystem(const json& j) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  // Erst alles prüfen, dann übernehmen: eine Ablehnung ändert nichts.
  SystemCfg sys = cfg_.system;
  Limits lim = cfg_.limits;
  if (j.contains("name") && j["name"].is_string()) sys.name = utf8Prefix(j["name"].get<std::string>(), 40);
  if (j.contains("timezone") && j["timezone"].is_string()) sys.timezone = j["timezone"];
  if (j.contains("language") && j["language"].is_string()) {
    std::string lang = j["language"];
    if (lang != "de" && lang != "en") return Result::fail(422, "system.language", "Sprache muss de oder en sein");
    sys.language = lang;
  }
  if (j.contains("updateCheck") && j["updateCheck"].is_boolean()) sys.updateCheck = j["updateCheck"];
  if (j.contains("updateChannel") && j["updateChannel"].is_string()) {
    std::string ch = j["updateChannel"];
    if (ch != "stable" && ch != "beta") return Result::fail(422, "system.channel", "Kanal muss stable oder beta sein");
    sys.updateChannel = ch;
  }
  if (j.contains("handDoseMaxMl") && j["handDoseMaxMl"].is_number()) {
    double v = j["handDoseMaxMl"];
    if (v < 0.5 || v > 50) return Result::fail(422, "system.hand", "Grenze je Handgabe 0,5–50 ml");
    lim.handDoseMaxMl = v;
  }
  cfg_.system = sys;
  cfg_.limits = lim;
  saveConfig("System");
  return Result::ok();
}

Result Hub::acceptDevice(const std::string& id, const std::string& name) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  const DeviceReport* d = nullptr;
  for (const auto& x : devices_)
    if (x.id == id) d = &x;
  if (!d) return Result::fail(404, "device.unknown", "Gerät nicht erkannt");
  if (!cat_.deviceClass(d->cls)) return Result::fail(422, "device.class", "Unbekannte Geräteklasse – Update nötig");
  if (cfg_.device(id)) return Result::ok();
  const DeviceClassDef* dc = cat_.deviceClass(d->cls);
  cfg_.devices.push_back({id, d->cls, name.empty() ? dc->label : utf8Prefix(name, 40)});
  // Netzgerät: jeden Kanal „nach Stromausfall aus“ setzen (R6 gilt auch dort).
  if (net_ && net_->owns(id))
    for (int ch = 0; ch < std::max(1, dc->channels); ++ch) {
      std::string e;
      if (!net_->configure(id, ch, SwitchSafety{}, e))
        log_.add(clock_.epoch(), "device", "warn", cfg_.devices.back().name + ": Schutz nicht gesetzt", e, {{"device", id}});
    }
  // Messrollen automatisch zuordnen, wenn genau ein Gerät passt (nie bei
  // Dosier- oder Schaltrollen). Sonst wählt der Nutzer unter Zuordnung.
  autoBindMeasures();
  saveConfig("Gerät übernommen: " + cfg_.devices.back().name);
  return Result::ok();
}

void Hub::autoBindMeasures() {
  if (cfg_.devices.empty()) return;
  auto provides = [&](const DeviceCfg& d, const std::string& cap) {
    const DeviceClassDef* c = cat_.deviceClass(d.cls);
    return c && std::find(c->provides.begin(), c->provides.end(), cap) != c->provides.end();
  };
  const DeviceCfg& added = cfg_.devices.back();
  for (const auto& [roleId, role] : cat_.roles) {
    if (cfg_.binding(roleId)) continue;
    const CapabilityDef* cap = cat_.capability(role.capability);
    if (!cap || cap->kind != "measure") continue;
    // Nur Rollen, die das neue Gerät liefert: Eine bewusst gelöste Rolle
    // bleibt gelöst, wenn ein anderes Gerät dazukommt.
    if (!provides(added, role.capability)) continue;
    std::vector<const DeviceCfg*> cand;
    for (const auto& d : cfg_.devices)
      if (provides(d, role.capability)) cand.push_back(&d);
    if (cand.size() == 1) cfg_.rolesFor(roleId)[roleId] = {cand.front()->id, 0};
  }
}

Result Hub::renameDevice(const std::string& id, const std::string& name) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  for (auto& d : cfg_.devices)
    if (d.id == id) {
      d.name = utf8Prefix(name, 40);
      saveConfig("Gerät umbenannt: " + d.name);
      return Result::ok();
    }
  return Result::fail(404, "device.unknown", "Gerät nicht eingerichtet");
}

Result Hub::removeDevice(const std::string& id) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  auto it = std::find_if(cfg_.devices.begin(), cfg_.devices.end(), [&](const DeviceCfg& d) { return d.id == id; });
  if (it == cfg_.devices.end()) return Result::fail(404, "device.unknown", "Gerät nicht eingerichtet");
  std::string name = it->name;
  // Ausgänge dieses Geräts erst aus, dann vergessen.
  std::vector<std::string> on;
  cfg_.forEachBinding([&](const std::string& role, const Binding& b) {
    if (b.device == id) on.push_back(role);
  });
  for (const auto& role : on)
    if (const RoleDef* rd = cat_.role(role); rd && !rd->profile.empty()) {
      Msg e;
      Ctx c = ctx();
      if (!act_.setRole(c, role, false, "Gerät entfernt", e))
        log_.add(clock_.epoch(), "block", "warn", rd->label + ": Aus nicht bestätigt", "Gerät entfernt: " + e.text, {{"role", role}, {"device", id}});
      if (const Binding* b = cfg_.binding(role)) releaseSocket(*rd, *b);
    }
  it = std::find_if(cfg_.devices.begin(), cfg_.devices.end(), [&](const DeviceCfg& d) { return d.id == id; });
  cfg_.devices.erase(it);
  auto unbind = [&](RoleMap& roles) {
    for (auto r = roles.begin(); r != roles.end();) r = r->second.device == id ? roles.erase(r) : std::next(r);
  };
  for (auto& t : cfg_.tanks) unbind(t.roles);
  for (auto& z : cfg_.zones) unbind(z.roles);
  for (auto& k : cfg_.canisters)
    if (k.pump == id) k.pump.clear();
  cfg_.calibrations.erase(id);
  saveConfig("Gerät entfernt: " + name);
  return Result::ok();
}

Result Hub::bindRole(const std::string& role, const std::string& device, int channel) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  Config next = cfg_;
  next.rolesFor(role)[role] = {device, channel};
  auto errs = validateConfig(next, cat_);
  if (!errs.empty()) return errors(errs);
  const RoleDef* rd = cat_.role(role);
  // Netzgerät: Schutzeinstellung schreiben und zurücklesen, erst dann binden.
  if (net_ && net_->owns(device) && rd && !rd->profile.empty()) {
    SwitchSafety want = safetyForRole(*rd);
    std::string e;
    if (!net_->configure(device, channel, want, e))
      return Result::fail(502, "role.net.write", "Schutzeinstellung nicht geschrieben: " + e);
    auto got = net_->readConfig(device, channel);
    if (!got || !sameSafety(*got, want))
      return Result::fail(502, "role.net.verify", "Schutzeinstellung im Gerät nicht bestätigt – nicht zugeordnet");
    const std::string afterLoss = want.powerOn == PowerOn::On ? "Nach Stromausfall an" : "Nach Stromausfall aus";
    log_.add(clock_.epoch(), "device", "info", rd->label + ": Schutz im Gerät gesetzt",
             isNum(want.autoOffS) ? afterLoss + ", Abschaltung im Gerät nach " + fmt(want.autoOffS / 60, 0) + " min" : afterLoss,
             {{"device", device}, {"channel", channel}});
  }
  // War die Rolle schon anderswo zugeordnet: dort erst ausschalten, sonst
  // verliert der Hub einen laufenden Ausgang aus dem Blick.
  if (const Binding* old = cfg_.binding(role); old && rd && !rd->profile.empty() && (old->device != device || old->channel != channel)) {
    Msg e;
    Ctx c = ctx();
    if (!act_.setRole(c, role, false, "Zuordnung geändert", e))
      log_.add(clock_.epoch(), "block", "warn", rd->label + ": Aus nicht bestätigt", "Alter Ausgang: " + e.text, {{"role", role}, {"device", old->device}});
    releaseSocket(*rd, *old);
  }
  cfg_ = next;
  saveConfig("Zuordnung: " + (rd ? rd->label : role));
  return Result::ok();
}

Result Hub::switchRole(const std::string& role, bool on) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  const RoleDef* rd = cat_.role(role);
  if (!rd || rd->profile.empty()) return Result::fail(404, "role.unknown", "Kein Schaltausgang");
  Ctx c = ctx();
  Msg e;
  if (!act_.setRole(c, role, on, "Hand", e)) return Result::fail(409, e.key, e.text);
  testOff_.erase(role);
  log_.add(clock_.epoch(), "manual", "info", rd->label + (on ? " an" : " aus"), "von Hand", {{"role", role}, {"on", on}});
  return Result::ok();
}

Result Hub::testRole(const std::string& role) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  const RoleDef* rd = cat_.role(role);
  if (!rd || rd->profile.empty()) return Result::fail(404, "role.unknown", "Kein Schaltausgang");
  if (act_.roleState(cfg_, role).value_or(false)) return Result::ok();  // läuft schon: nicht nach 3 s abschalten
  Ctx c = ctx();
  Msg e;
  if (!act_.setRole(c, role, true, "Testen", e)) return Result::fail(409, e.key, e.text);
  testOff_[role] = c.now + 3 * kSecond;
  return Result::ok();
}

// A socket that loses a role which comes back on after a power loss goes
// back to "off after power loss" (PD-050).
void Hub::releaseSocket(const RoleDef& rd, const Binding& b) {
  if (!rd.onAfterPowerLoss || !net_ || !net_->owns(b.device)) return;
  std::string e;
  bool ok = net_->configure(b.device, b.channel, SwitchSafety{}, e);
  if (ok) {
    auto got = net_->readConfig(b.device, b.channel);
    ok = got && sameSafety(*got, SwitchSafety{});
    if (!ok) e = "im Gerät nicht bestätigt";
  }
  if (!ok)
    log_.add(clock_.epoch(), "device", "warn", rd.label + ": Dose bleibt „nach Stromausfall an“",
             "Zurücksetzen auf „nach Stromausfall aus“ gescheitert: " + e + ". Bitte in der Dose selbst einstellen.",
             {{"device", b.device}, {"channel", b.channel}});
}

// Fan sockets come back on after a power loss, except during an emergency
// stop (PD-050, PD-076). Written and read back; a failure is reported.
void Hub::setFanSockets(bool comeBackOn) {
  if (!net_) return;
  cfg_.forEachBinding([&](const std::string& role, const Binding& b) {
    const RoleDef* rd = cat_.role(role);
    if (!rd || !rd->onAfterPowerLoss || !net_->owns(b.device)) return;
    const SwitchSafety want = comeBackOn ? safetyForRole(*rd) : SwitchSafety{};
    std::string e;
    bool ok = net_->configure(b.device, b.channel, want, e);
    if (ok) {
      auto got = net_->readConfig(b.device, b.channel);
      ok = got && sameSafety(*got, want);
      if (!ok) e = "im Gerät nicht bestätigt";
    }
    if (!ok)
      log_.add(clock_.epoch(), "device", "warn",
               rd->label + (comeBackOn ? ": „nach Stromausfall an“ nicht gesetzt" : ": „nach Stromausfall aus“ nicht gesetzt"),
               e + (comeBackOn ? ". Dose neu zuordnen." : ". Läuft nach einem Stromausfall womöglich wieder an."),
               {{"role", role}, {"device", b.device}});
  });
}

Result Hub::unbindRole(const std::string& role) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  if (const RoleDef* rd = cat_.role(role); rd && !rd->profile.empty()) {
    Msg e;
    Ctx c = ctx();
    const Binding* old = cfg_.binding(role);
    if (old && !act_.setRole(c, role, false, "Zuordnung entfernt", e))
      log_.add(clock_.epoch(), "block", "warn", rd->label + ": Aus nicht bestätigt", "Zuordnung entfernt: " + e.text, {{"role", role}, {"device", old->device}});
    if (old) releaseSocket(*rd, *old);
  }
  cfg_.rolesFor(role).erase(role);
  saveConfig("Zuordnung entfernt: " + role);
  return Result::ok();
}

Result Hub::putTank(const json& j) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  Config next = cfg_;
  auto& t = next.tank();
  if (j.contains("name") && j["name"].is_string()) t.name = utf8Prefix(j["name"].get<std::string>(), 40);
  if (j.contains("capacityL")) t.capacityL = j["capacityL"].is_number() ? j["capacityL"].get<double>() : kNaN;
  if (j.contains("minL")) t.minL = j["minL"].is_number() ? j["minL"].get<double>() : kNaN;
  if (j.contains("water") && j["water"].is_string()) t.water = j["water"];
  if (j.contains("volumeL") && j["volumeL"].is_number()) rt_.tankVolumeL = j["volumeL"];
  auto errs = validateConfig(next, cat_);
  if (!errs.empty()) return errors(errs);
  cfg_ = next;
  saveConfig("Tank: " + t.name);
  return Result::ok();
}

Result Hub::putZone(const json& j) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  Config next = cfg_;
  auto& z = next.zone();
  if (j.contains("name") && j["name"].is_string()) z.name = utf8Prefix(j["name"].get<std::string>(), 40);
  if (j.contains("kind") && j["kind"].is_string()) z.kind = j["kind"];
  auto errs = validateConfig(next, cat_);
  if (!errs.empty()) return errors(errs);
  cfg_ = next;
  saveConfig("Bereich: " + z.name);
  return Result::ok();
}

Result Hub::putCanister(const json& j) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  Config next = cfg_;
  std::string id = jstr(j, "id");
  CanisterCfg* k = nullptr;
  for (auto& x : next.canisters)
    if (x.id == id) k = &x;
  if (!k) {
    std::string base = slug(jstr(j, "name", "kanister"));
    id = base;
    for (int n = 2; next.canister(id); ++n) id = base + "-" + std::to_string(n);
    next.canisters.push_back({});
    k = &next.canisters.back();
    k->id = id;
  }
  if (j.contains("name")) k->name = utf8Prefix(jstr(j, "name"), 40);
  if (j.contains("kind")) k->kind = jstr(j, "kind");
  if (j.contains("pump")) k->pump = jstr(j, "pump");
  if (j.contains("pair")) k->pair = jstr(j, "pair");
  if (j.contains("color")) k->color = jstr(j, "color");
  if (j.contains("capacityMl")) k->capacityMl = j["capacityMl"].is_number() ? j["capacityMl"].get<double>() : kNaN;
  if (k->name.empty()) return Result::fail(422, "canister.name", "Name fehlt");
  auto errs = validateConfig(next, cat_);
  // Ein unvollständiges Paar ist beim Anlegen des ersten Partners erlaubt.
  errs.erase(std::remove_if(errs.begin(), errs.end(), [](const Msg& m) { return m.key == "cfg.pair.single" || m.key == "cfg.recipe.pair"; }),
             errs.end());
  if (!errs.empty()) return errors(errs);
  cfg_ = next;
  if (j.contains("stockMl") && j["stockMl"].is_number()) rt_.stockMl[id] = j["stockMl"];
  else if (!rt_.stockMl.count(id) && isNum(k->capacityMl)) rt_.stockMl[id] = k->capacityMl;
  stateDirty_ = true;
  saveConfig("Kanister: " + k->name);
  return Result::ok({{"id", id}});
}

Result Hub::deleteCanister(const std::string& id) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  for (const auto& r : cfg_.recipes)
    for (const auto& s : r.steps)
      if (s.canister == id) return Result::fail(409, "canister.used", "Wird im Rezept „" + r.name + "“ verwendet");
  auto it = std::find_if(cfg_.canisters.begin(), cfg_.canisters.end(), [&](const CanisterCfg& k) { return k.id == id; });
  if (it == cfg_.canisters.end()) return Result::fail(404, "canister.unknown", "Kanister nicht gefunden");
  std::string name = it->name;
  cfg_.canisters.erase(it);
  rt_.stockMl.erase(id);
  saveConfig("Kanister entfernt: " + name);
  return Result::ok();
}

Result Hub::setStock(const std::string& id, double ml) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  const CanisterCfg* k = cfg_.canister(id);
  if (!k) return Result::fail(404, "canister.unknown", "Kanister nicht gefunden");
  if (!isNum(ml) || ml < 0) return Result::fail(422, "canister.stock", "Vorrat ungültig");
  rt_.stockMl[id] = ml;
  stateDirty_ = true;
  log_.add(clock_.epoch(), "config", "info", "Kanister gewechselt", k->name + ": " + fmt(ml, 0) + " ml");
  return Result::ok();
}

Result Hub::putRecipe(const json& j) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  Config next = cfg_;
  std::string id = jstr(j, "id");
  RecipeCfg* r = nullptr;
  for (auto& x : next.recipes)
    if (x.id == id) r = &x;
  if (!r) {
    std::string base = slug(jstr(j, "name", "rezept"));
    id = base;
    for (int n = 2; next.recipe(id); ++n) id = base + "-" + std::to_string(n);
    next.recipes.push_back({});
    r = &next.recipes.back();
    r->id = id;
  }
  if (j.contains("name")) r->name = utf8Prefix(jstr(j, "name"), 40);
  if (j.contains("note")) r->note = jstr(j, "note");
  if (j.contains("steps")) {
    r->steps.clear();
    for (const auto& s : j["steps"]) r->steps.push_back({jstr(s, "canister"), jnum(s, "mlPerL")});
  }
  if (r->name.empty()) return Result::fail(422, "recipe.name", "Name fehlt");
  auto errs = validateConfig(next, cat_);
  if (!errs.empty()) return errors(errs);
  cfg_ = next;
  saveConfig("Rezept: " + r->name);
  return Result::ok({{"id", id}});
}

Result Hub::deleteRecipe(const std::string& id) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  auto it = std::find_if(cfg_.recipes.begin(), cfg_.recipes.end(), [&](const RecipeCfg& r) { return r.id == id; });
  if (it == cfg_.recipes.end()) return Result::fail(404, "recipe.unknown", "Rezept nicht gefunden");
  std::string name = it->name;
  cfg_.recipes.erase(it);
  saveConfig("Rezept entfernt: " + name);
  return Result::ok();
}

Result Hub::applyRecipeTemplate(const std::string& templateId, const json& map) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  // Zuordnung je Teil der Vorlage: ausdrücklich (map: Rolle → Kanister),
  // sonst über den Namen (ohne Groß/Klein). Die Reihenfolge der Vorlage ist die
  // Mischreihenfolge der Herstellertabelle (Athena: B vor A, CalMag danach).
  // Ein Kanister darf nur einen Teil tragen; ein Paar der Vorlage (A/B) wird
  // auf Kanister ohne eigenes Paar übernommen, damit sie gemeinsam skalieren.
  auto lower = [](std::string x) {
    for (auto& ch : x) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return x;
  };
  for (const auto& t : cat_.templates.value("recipes", json::array())) {
    if (jstr(t, "id") != templateId) continue;
    json steps = json::array();
    std::vector<std::string> missing;
    std::map<std::string, std::string> pairs;  // Kanister → Paar aus der Vorlage
    std::set<std::string> used;
    for (const auto& s : t.value("steps", json::array())) {
      const std::string role = jstr(s, "role"), name = jstr(s, "name");
      const CanisterCfg* k = nullptr;
      if (map.is_object() && map.contains(role) && map[role].is_string()) k = cfg_.canister(map[role].get<std::string>());
      for (const auto& x : cfg_.canisters)
        if (!k && lower(x.name) == lower(name)) k = &x;
      if (!k) {
        missing.push_back(name);
        continue;
      }
      if (!used.insert(k->id).second)
        return Result::fail(422, "recipe.template.twice", "Kanister „" + k->name + "“ ist zwei Teilen der Vorlage zugeordnet");
      if (!jstr(s, "pair").empty()) pairs[k->id] = jstr(s, "pair");
      steps.push_back({{"canister", k->id}, {"mlPerL", jnum(s, "mlPerL")}});
    }
    if (!missing.empty()) {
      std::string m;
      for (const auto& x : missing) m += (m.empty() ? "" : ", ") + x;
      return Result::fail(422, "recipe.template", "Vorlage braucht Kanister: " + m, {{"missing", missing}});
    }
    Result r = putRecipe({{"name", jstr(t, "name")}, {"note", jstr(t, "note")}, {"steps", steps}});
    if (r.status != 200) return r;
    // Ist der Paarname schon an anderen Kanistern vergeben, einen freien
    // wählen (AB2, AB3, …), sonst zählte das Paar vier Kanister.
    Config next = cfg_;
    std::map<std::string, std::string> rename;  // Paar der Vorlage → freier Name
    for (const auto& [id, p] : pairs) {
      if (rename.count(p)) continue;
      auto takenByOthers = [&](const std::string& name) {
        for (const auto& k : next.canisters)
          if (k.pair == name && !pairs.count(k.id)) return true;
        return false;
      };
      std::string name = p;
      for (int n = 2; takenByOthers(name); ++n) name = p + std::to_string(n);
      rename[p] = name;
    }
    bool changed = false;
    for (auto& k : next.canisters)
      if (pairs.count(k.id) && k.pair.empty() && k.kind == "nutrient") {
        k.pair = rename[pairs[k.id]];
        changed = true;
      }
    if (changed && validateConfig(next, cat_).empty()) {
      cfg_ = next;
      saveConfig("Paar aus der Vorlage übernommen");
    }
    return r;
  }
  return Result::fail(404, "recipe.template", "Vorlage nicht gefunden");
}

Result Hub::putFunction(const std::string& id, const json& j) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  const FunctionDef* fd = cat_.function(id);
  if (!fd) return Result::fail(404, "function.unknown", "Funktion nicht gefunden");
  Config next = cfg_;
  auto& f = next.functions[id];
  if (j.contains("params") && j["params"].is_object())
    for (const auto& [k, v] : j["params"].items()) f.params[k] = v;
  if (j.contains("enabled") && j["enabled"].is_boolean()) {
    bool en = j["enabled"];
    if (en && !fd->alwaysOn) {
      // Einschalten nur, wenn die Einrichtung vollständig ist ("eingeschränkt" genügt).
      for (const auto& fs : functions_)
        if (fs.id == id && (fs.setup == "unavailable" || fs.setup == "needs_setup"))
          return Result::fail(409, "function.not_ready", fs.summary.text);
    }
    f.enabled = en;
  }
  auto errs = validateConfig(next, cat_);
  if (!errs.empty()) return errors(errs);
  cfg_ = next;
  functions_ = resolveFunctions(cat_, cfg_, devices_, pumps_, truth_);
  saveConfig(fd->label + (j.contains("enabled") ? (f.enabled ? " eingeschaltet" : " ausgeschaltet") : ": Parameter"));
  return Result::ok();
}

Result Hub::importConfig(const json& j) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  Config next;
  try {
    next = configFromJson(j);
  } catch (const std::exception& e) {
    return Result::fail(422, "config.import", std::string("Datei unlesbar: ") + e.what());
  }
  auto errs = validateConfig(next, cat_);
  for (const auto& [dev, kinds] : next.calibrations)
    for (const auto& [kind, data] : kinds)
      if (auto e = SensorTruth::checkCalibration(kind, data))
        errs.push_back({"cfg.calibration", "Kalibrierung " + dev + " (" + kind + "): " + *e, json::object()});
  if (!errs.empty()) return errors(errs);
  if (userJobActive() || doser_.busy())
    return Result::fail(409, "job.busy", "Erst den laufenden Auftrag beenden, dann importieren");
  act_.stopAll(cat_, cfg_, clock_.nowMs());
  // A socket that loses its fan role goes back to "off after power loss";
  // every switched socket in the import gets the setting of its role.
  cfg_.forEachBinding([&](const std::string& role, const Binding& b) {
    const RoleDef* rd = cat_.role(role);
    const Binding* nb = next.binding(role);
    if (rd && (!nb || nb->device != b.device || nb->channel != b.channel)) releaseSocket(*rd, b);
  });
  next.revision = cfg_.revision;
  // Die Sperre gegen eine zweite Einrichtung kommt nie aus einer Datei.
  next.system.passwordSet = cfg_.system.passwordSet || auth_.hasPassword();
  cfg_ = next;
  if (net_)
    cfg_.forEachBinding([&](const std::string& role, const Binding& b) {
      const RoleDef* rd = cat_.role(role);
      if (!rd || rd->profile.empty() || !net_->owns(b.device)) return;
      const SwitchSafety want = stopped_ && rd->onAfterPowerLoss ? SwitchSafety{} : safetyForRole(*rd);
      std::string e;
      bool ok = net_->configure(b.device, b.channel, want, e);
      if (ok) {
        auto got = net_->readConfig(b.device, b.channel);
        ok = got && sameSafety(*got, want);
      }
      if (!ok)
        log_.add(clock_.epoch(), "device", "warn", rd->label + ": Schutz im Gerät nicht gesetzt",
                 "Nach dem Import nicht bestätigt – bis zur neuen Zuordnung schaltet der Hub diese Dose nicht ein.",
                 {{"role", role}, {"device", b.device}});
    });
  saveConfig("Konfiguration importiert");
  return Result::ok();
}

// ------------------------------------------------------------------ Aufträge

Result Hub::mixPlan(const json& req) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  MixRequest r{jstr(req, "recipe"), jnum(req, "waterL"), jstr(req, "mode", "new"), jbool(req, "confirmRepeat", false)};
  auto it = cfg_.functions.find("ph_control");
  bool phAuto = it != cfg_.functions.end() && it->second.enabled;
  auto plan = planMix(cfg_, rt_, pumps_, r, clock_.epoch(), phAuto);
  return Result::ok(plan);
}

Result Hub::mixStart(const json& req) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  if (userJobActive()) return Result::fail(409, "job.busy", "Es läuft bereits ein Auftrag");  // kein Doppelstart (RAT-003)
  if (doser_.busy()) return Result::fail(409, "job.dosing", "Die Regelung dosiert gerade – gleich noch einmal versuchen");
  if (stopped_) return Result::fail(409, "job.stopped", "Not-Halt aktiv – erst fortsetzen");
  MixRequest r{jstr(req, "recipe"), jnum(req, "waterL"), jstr(req, "mode", "new"), jbool(req, "confirmRepeat", false)};
  auto it = cfg_.functions.find("ph_control");
  bool phAuto = it != cfg_.functions.end() && it->second.enabled;
  auto plan = planMix(cfg_, rt_, pumps_, r, clock_.epoch(), phAuto);
  if (!plan.ok) {
    json b = plan;
    return Result::fail(422, plan.errors.empty() ? "mix.invalid" : plan.errors.front().key,
                        plan.errors.empty() ? "Plan ungültig" : plan.errors.front().text, {{"plan", b}});
  }
  // Abgleich des Rezepts mit den wirklich vorhandenen Kanistern vor der ersten Gabe.
  Job j;
  j.id = newId("mix");
  j.type = "mix";
  j.guided = jbool(req, "guided", true);
  j.startedAt = clock_.epoch();
  j.circulation = cfg_.binding("tank.circulation") != nullptr;
  j.info = {{"recipe", plan.recipe}, {"recipeName", plan.recipeName}, {"waterL", plan.waterL}, {"mode", plan.mode},
            {"after", plan.after}};
  for (const auto& s : plan.steps) j.steps.push_back({s, "pending", 0});
  rt_.tankVolumeL = plan.mode == "topup" && isNum(rt_.tankVolumeL) ? rt_.tankVolumeL + plan.waterL : plan.waterL;
  rt_.lastMixAt = clock_.epoch();
  stateDirty_ = true;
  log_.add(clock_.epoch(), "mix", "info", "Mischlauf gestartet",
           plan.recipeName + " · " + fmt(plan.waterL, 1) + " L " + (plan.mode == "topup" ? "nachgefüllt" : "neu"),
           {{"job", j.id}, {"recipe", plan.recipe}, {"waterL", plan.waterL}});
  job_ = j;
  Ctx c = ctx();
  if (!startJobStep(c)) return Result::fail(422, "mix.start", job_->message.text, {{"job", *job_}});
  saveJob();
  return Result::ok({{"job", *job_}});
}

bool Hub::startJobStep(Ctx& c) {
  Job& j = *job_;
  auto& st = j.steps[j.index];
  DoseOrder o;
  // Jeder Versuch bekommt eine eigene ID: dieselbe ID gilt beim Dosierblock als
  // Wiederholung desselben Auftrags und läuft nicht noch einmal (Schutz gegen Doppeldosierung).
  o.id = j.id + "-" + std::to_string(j.index + 1) + "." + std::to_string(++idSeq_);
  o.purpose = j.type == "mix" ? "mix" : j.type;
  o.step = st.dose;
  // Restmenge nach einer Unterbrechung neu in Läufe teilen
  if (st.mlDone > 0 && isNum(st.dose.ml)) {
    Msg err;
    o.step.ml = st.dose.ml - st.mlDone;
    o.step.runs = splitRuns(o.step.ml, o.step.flowMlPerMin, cfg_.limits, 0, err);
    if (o.step.ml <= 0 || err.key == "dose.too_small") {
      // Kein Rest oder Rest unter einem genauen Lauf: als erledigt werten und
      // weiterschalten. Achtung: Das kann den Auftrag beenden (job_ ist danach leer).
      DoseProgress rest;
      rest.state = DoseProgress::State::Done;
      rest.orderId = o.id;
      onJobDose(c, rest);
      return true;
    }
    if (!err.key.empty()) {
      st.state = "failed";
      j.state = "failed";
      j.message = {"job.start_failed", st.dose.name + ": " + err.text, json::object()};
      return false;
    }
  }
  Msg err;
  if (!doser_.start(c, act_, o, err)) {
    st.state = "failed";
    j.state = "failed";
    j.message = {"job.start_failed", st.dose.name + ": " + err.text, json::object()};
    return false;
  }
  st.state = "running";
  j.state = "running";
  j.message = {"job.dosing", "Schritt " + std::to_string(j.index + 1) + " von " + std::to_string(j.steps.size()) + ": " +
                                 st.dose.name + " · " + fmt(o.step.ml, 1) + " ml",
               json::object()};
  return true;
}

json Hub::jobJson() const {
  return {{"job", job_ ? json(*job_) : json(nullptr)}, {"lastJob", lastJob_ ? json(*lastJob_) : json(nullptr)}};
}

void Hub::finishJob(const std::string& state, Msg m) {
  Job& j = *job_;
  j.state = state;
  j.message = std::move(m);
  j.finishedAt = clock_.epoch();
  saveJob();
  lastJob_ = j;
  if (state == "done" || state == "aborted") job_.reset();
}

void Hub::onJobDose(Ctx& c, const DoseProgress& p) {
  if (!job_) return;
  Job& j = *job_;
  auto& st = j.steps[j.index];
  st.mlDone += p.mlDone;
  if (p.state == DoseProgress::State::Aborted) return;
  if (p.state == DoseProgress::State::Failed) {
    st.state = "failed";
    j.state = "failed";
    // Paar-Fehler sichtbar machen: Partner schon drin? (Vorschlag anwender, RAT-054)
    std::string partner;
    if (!st.dose.pair.empty())
      for (const auto& o : j.steps)
        if (&o != &st && o.dose.pair == st.dose.pair && o.mlDone > 0) partner = o.dose.name;
    std::string text = st.dose.name + " nicht vollständig dosiert (" + p.error.text + ").";
    if (!partner.empty())
      text += " " + partner + " ist schon drin. Ursache beheben, dann „" + st.dose.name +
              " nachholen“ – sonst stimmt das Verhältnis nicht.";
    j.message = {"job.pair_failed", text, {{"step", j.index}, {"partner", partner}}};
    log_.add(c.epoch, "mix", "alarm", j.type == "mix" ? "Mischlauf unterbrochen" : "Auftrag fehlgeschlagen", text,
             {{"job", j.id}});
    if (j.type != "mix") finishJob("failed", j.message);
    saveJob();
    return;
  }
  st.state = "done";
  if (j.type == "calibration") {
    j.state = "waiting_user";
    j.info["actualMs"] = p.msDone;
    j.message = {"cal.measure", "Wie viel ist im Messbecher? Menge in ml eintragen.", json::object()};
    saveJob();
    return;
  }
  if (j.type == "prime" || j.type == "manual") {
    finishJob("done", {"job.done", st.dose.name + ": " + fmt(st.mlDone, 1) + " ml", json::object()});
    return;
  }
  j.index++;
  if (j.index >= j.steps.size()) {
    std::string summary;
    double total = 0;
    for (const auto& s : j.steps) {
      summary += (summary.empty() ? "" : ", ") + s.dose.name + " " + fmt(s.mlDone, 1) + " ml";
      total += s.mlDone;
    }
    Msg after = j.info.contains("after") ? Msg{jstr(j.info["after"], "key"), jstr(j.info["after"], "text"), json::object()} : Msg{};
    log_.add(c.epoch, "mix", "info", "Mischlauf fertig",
             fmt(jnum(j.info, "waterL"), 1) + " L „" + jstr(j.info, "recipeName") + "“: " + summary + ". " + after.text,
             {{"job", j.id}, {"totalMl", total}});
    finishJob("done", {"mix.done", "Fertig: " + fmt(jnum(j.info, "waterL"), 1) + " L „" + jstr(j.info, "recipeName") + "“. " + after.text,
                       json::object()});
    return;
  }
  // Zwischen den Gaben durchmischen: mit Umwälzpumpe Countdown, sonst von Hand rühren.
  if (j.circulation) {
    j.state = "mixing";
    j.waitUntil = c.now + 60 * kSecond;
    j.message = {"mix.circulate", "Durchmischen mit der Umwälzpumpe (1 min)", json::object()};
  } else if (j.guided) {
    j.state = "waiting_user";
    j.message = {"mix.stir", "Fertig: " + st.dose.name + " " + fmt(st.mlDone, 1) + " ml. Jetzt 1 Minute umrühren, dann „Weiter“.",
                 json::object()};
  } else {
    startJobStep(c);
  }
  saveJob();
}

void Hub::tickJob(Ctx& c) {
  if (!job_) return;
  Job& j = *job_;
  if (j.state == "mixing" && c.now >= j.waitUntil) {
    if (startJobStep(c)) saveJob();
  }
}

Result Hub::jobContinue(const std::string& id) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  if (!job_ || job_->id != id) return Result::fail(404, "job.unknown", "Auftrag nicht aktiv");
  if (job_->state != "waiting_user" && job_->state != "mixing")
    return Result::fail(409, "job.state", "Auftrag wartet nicht");
  if (job_->type == "calibration") return Result::fail(409, "job.state", "Bitte die gemessene Menge eintragen");
  Ctx c = ctx();
  if (!startJobStep(c)) return Result::fail(422, "job.step", job_->message.text, {{"job", *job_}});
  saveJob();
  return Result::ok(jobJson());
}

Result Hub::jobResume(const std::string& id) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  if (!job_ || job_->id != id) return Result::fail(404, "job.unknown", "Auftrag nicht aktiv");
  if (job_->state != "failed") return Result::fail(409, "job.state", "Nur ein unterbrochener Auftrag kann nachgeholt werden");
  // Einmesswert und Pumpe neu lesen (z. B. nach dem Tausch der Kappe)
  auto& st = job_->steps[job_->index];
  if (auto it = pumps_.find(st.dose.pump); it != pumps_.end()) st.dose.flowMlPerMin = it->second.flowMlPerMin;
  const std::string name = st.dose.name;  // st gilt nach startJobStep nicht mehr sicher
  Ctx c = ctx();
  if (!startJobStep(c)) return Result::fail(422, "job.step", job_->message.text, {{"job", *job_}});
  log_.add(clock_.epoch(), "mix", "info", "Mischlauf fortgesetzt", name + " wird nachgeholt");
  saveJob();
  return Result::ok(jobJson());
}

Result Hub::jobAbort(const std::string& id) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  if (!job_ || job_->id != id) return Result::fail(404, "job.unknown", "Auftrag nicht aktiv");
  Ctx c = ctx();
  if (doser_.busy() && doser_.active()->id.rfind(id, 0) == 0) doser_.abort(c, act_, "Auftrag abgebrochen");
  if (auto fin = doser_.takeFinished()) onJobDose(c, *fin);
  std::string done;
  for (const auto& s : job_->steps)
    if (s.mlDone > 0) done += (done.empty() ? "" : ", ") + s.dose.name + " " + fmt(s.mlDone, 1) + " ml";
  log_.add(clock_.epoch(), "mix", "warn", "Auftrag abgebrochen", "Drin: " + (done.empty() ? "nichts" : done), {{"job", id}});
  finishJob("aborted", {"job.aborted", "Abgebrochen. Drin: " + (done.empty() ? "nichts" : done), json::object()});
  return Result::ok();
}

Result Hub::manualDose(const std::string& canister, double ml) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  if (userJobActive() || doser_.busy()) return Result::fail(409, "job.busy", "Es läuft bereits eine Dosierung");
  const CanisterCfg* k = cfg_.canister(canister);
  if (!k) return Result::fail(404, "canister.unknown", "Kanister nicht gefunden");
  if (!isNum(ml) || ml <= 0) return Result::fail(422, "dose.amount", "Menge fehlt");
  const Limits lim = cfg_.limits.bounded();
  if (ml > lim.handDoseMaxMl)
    return Result::fail(422, "dose.hand_limit", "Höchstens " + fmt(lim.handDoseMaxMl, 1) + " ml je Handgabe");
  auto pit = pumps_.find(k->pump);
  if (k->pump.empty() || pit == pumps_.end() || !pit->second.online)
    return Result::fail(422, "dose.pump", k->name + ": Pumpe nicht erkannt");
  DoseStep s;
  s.canister = k->id;
  s.name = k->name;
  s.pump = k->pump;
  s.color = k->color;
  s.pair = k->pair;
  s.ml = ml;
  s.flowMlPerMin = pit->second.flowMlPerMin;
  Msg err;
  s.runs = splitRuns(ml, s.flowMlPerMin, cfg_.limits, lim.maxPartialRuns, err);
  if (!err.key.empty()) return Result::fail(422, err.key, k->name + ": " + err.text);
  Job j;
  j.id = newId("dose");
  j.type = "manual";
  j.startedAt = clock_.epoch();
  j.steps.push_back({s, "pending", 0});
  job_ = j;
  Ctx c = ctx();
  if (!startJobStep(c)) {
    Msg m = job_->message;
    job_.reset();
    return Result::fail(422, "dose.start", m.text);
  }
  if (!k->pair.empty())
    log_.add(clock_.epoch(), "dose", "notice", "Handgabe aus einem Paar",
             k->name + " gehört zum Paar " + k->pair + ". Den Partner im gleichen Verhältnis geben.");
  return Result::ok({{"job", *job_}});
}

Result Hub::startCalibration(const std::string& pump, double seconds) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  if (userJobActive() || doser_.busy()) return Result::fail(409, "job.busy", "Es läuft bereits eine Dosierung");
  auto pit = pumps_.find(pump);
  if (pit == pumps_.end() || !pit->second.online) return Result::fail(404, "pump.unknown", "Pumpe nicht erkannt");
  if (!isNum(seconds) || seconds < 10 || seconds > 60)
    return Result::fail(422, "cal.seconds", "Einmesslauf 10–60 s (kurze Läufe sind ungenau)");
  const CanisterCfg* k = cfg_.canisterByPump(pump);
  DoseStep s;
  s.canister = k ? k->id : "";
  s.name = k ? k->name : pump;
  s.pump = pump;
  s.runs = {static_cast<Ms>(seconds * 1000)};
  Job j;
  j.id = newId("cal");
  j.type = "calibration";
  j.startedAt = clock_.epoch();
  j.steps.push_back({s, "pending", 0});
  j.info = {{"pump", pump}, {"seconds", seconds}, {"previous", numOrNull(pit->second.flowMlPerMin)}};
  job_ = j;
  Ctx c = ctx();
  if (!startJobStep(c)) {
    Msg m = job_->message;
    job_.reset();
    return Result::fail(422, "cal.start", m.text);
  }
  job_->message = {"cal.running", "Pumpe läuft " + fmt(seconds, 0) + " s in den Messbecher …", json::object()};
  return Result::ok({{"job", *job_}});
}

Result Hub::calibrationResult(const std::string& jobId, double ml) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  if (!job_ || job_->id != jobId || job_->type != "calibration" || job_->state != "waiting_user")
    return Result::fail(409, "cal.state", "Kein Einmesslauf wartet auf ein Ergebnis");
  if (!isNum(ml) || ml <= 0 || ml > 1000) return Result::fail(422, "cal.amount", "Menge ungültig – alter Wert bleibt");
  Ms actual = static_cast<Ms>(jnum(job_->info, "actualMs", 0));
  if (actual <= 0) return Result::fail(422, "cal.runtime", "Ist-Laufzeit fehlt – bitte erneut einmessen");
  // Rate aus der gemeldeten Ist-Laufzeit, nicht aus der angeforderten (RAT-070)
  double flow = ml / (static_cast<double>(actual) / 60000.0);
  std::string pump = jstr(job_->info, "pump");
  std::string e;
  if (!bus_.writePumpCalibration(pump, flow, e)) return Result::fail(502, "cal.write", "Schreiben in die Pumpe fehlgeschlagen: " + e);
  if (auto it = pumps_.find(pump); it != pumps_.end()) it->second.flowMlPerMin = flow;  // sofort gültig, nicht erst im nächsten Takt
  double prev = jnum(job_->info, "previous");
  std::string note = isNum(prev) && std::fabs(flow - prev) / prev > 0.3 ? " Deutlich anders als vorher (" + fmt(prev, 1) + ") – Schlauch prüfen." : "";
  log_.add(clock_.epoch(), "calibration", "info", "Pumpe eingemessen",
           job_->steps[0].dose.name + ": " + fmt(flow, 1) + " ml/min, gespeichert in der Pumpe." + note,
           {{"pump", pump}, {"flowMlPerMin", flow}, {"ml", ml}, {"ms", actual}});
  job_->info["flowMlPerMin"] = flow;
  finishJob("done", {"cal.done", "Gespeichert in der Pumpe: " + fmt(flow, 1) + " ml/min. Bleibt beim Umstecken erhalten." + note,
                     json::object()});
  return Result::ok({{"flowMlPerMin", flow}});
}

Result Hub::prime(const std::string& pump, double seconds) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  if (userJobActive() || doser_.busy()) return Result::fail(409, "job.busy", "Es läuft bereits eine Dosierung");
  auto pit = pumps_.find(pump);
  if (pit == pumps_.end() || !pit->second.online) return Result::fail(404, "pump.unknown", "Pumpe nicht erkannt");
  if (!isNum(seconds) || seconds <= 0 || seconds > 20) return Result::fail(422, "prime.seconds", "Schlauch füllen: höchstens 20 s je Lauf");
  const CanisterCfg* k = cfg_.canisterByPump(pump);
  DoseStep s;
  s.canister = k ? k->id : "";
  s.name = k ? k->name : pump;
  s.pump = pump;
  s.runs = {static_cast<Ms>(seconds * 1000)};
  Job j;
  j.id = newId("prime");
  j.type = "prime";
  j.startedAt = clock_.epoch();
  j.steps.push_back({s, "pending", 0});
  job_ = j;
  Ctx c = ctx();
  if (!startJobStep(c)) {
    Msg m = job_->message;
    job_.reset();
    return Result::fail(422, "prime.start", m.text);
  }
  return Result::ok({{"job", *job_}});
}

Result Hub::probeCalibration(const json& j) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  std::string dev = jstr(j, "device"), kind = jstr(j, "kind"), action = jstr(j, "action");
  if (!cfg_.device(dev)) return Result::fail(404, "device.unknown", "Gerät nicht eingerichtet");
  std::string cap = kind == "ph" ? "measure.ph" : kind == "ec" ? "measure.ec" : kind == "tank_curve" ? "measure.level" : "";
  if (cap.empty()) return Result::fail(422, "probe.kind", "Unbekannte Kalibrierung");
  std::string key = dev + ":" + kind;
  if (action == "start") {
    probeSessions_[key] = {{"device", dev}, {"kind", kind}, {"points", json::array()}};
    return Result::ok(probeSessions_[key]);
  }
  if (action == "cancel") {
    probeSessions_.erase(key);
    return Result::ok();
  }
  auto it = probeSessions_.find(key);
  if (it == probeSessions_.end()) return Result::fail(409, "probe.session", "Erst die Kalibrierung starten");
  if (action == "point") {
    double ref = jnum(j, "reference");
    if (!isNum(ref)) return Result::fail(422, "probe.reference", "Sollwert fehlt");
    auto s = bus_.sample(dev, cap);
    // Ein fehlender Rohwert wird nie Kalibrierpunkt (RAT-006)
    if (!s || !isNum(s->raw) || clock_.nowMs() - s->ts > 30 * kSecond)
      return Result::fail(422, "probe.no_raw", "Kein aktueller Rohwert – Sonde prüfen");
    it->second["points"].push_back({s->raw, ref});
    return Result::ok(it->second);
  }
  if (action == "commit") {
    json data;
    const json& pts = it->second["points"];
    if (kind == "ph") {
      if (pts.size() < 2) return Result::fail(422, "probe.points", "Zwei Pufferpunkte nötig (z. B. pH 7 und pH 4)");
      data = {{"points", json::array({pts[0], pts[1]})}};
    } else if (kind == "ec") {
      if (pts.empty()) return Result::fail(422, "probe.points", "Ein Referenzpunkt nötig (z. B. 1,413 mS/cm)");
      double raw = pts.back()[0].get<double>(), ref = pts.back()[1].get<double>();
      if (raw <= 0) return Result::fail(422, "probe.raw", "Rohwert ungültig");
      data = {{"factor", ref / raw}};
    } else {
      json sorted = pts;
      std::sort(sorted.begin(), sorted.end(), [](const json& a, const json& b) { return a[0].get<double>() < b[0].get<double>(); });
      data = {{"points", sorted}};
      std::string err;
      if (!Curve::fromJson(data, err)) return Result::fail(422, "probe.curve", err);
    }
    data["at"] = clock_.epoch();
    double probe = kind == "tank_curve" ? pts[0][0].get<double>() : 7.0;
    if (!SensorTruth::calibrate(cap, probe, &data))
      return Result::fail(422, "probe.invalid", "Kalibrierung unplausibel (Steigung oder Faktor außerhalb) – Sonde oder Puffer prüfen");
    cfg_.calibrations[dev][kind] = data;
    probeSessions_.erase(it);
    saveConfig("Kalibrierung " + kind + ": " + cfg_.device(dev)->name);
    log_.add(clock_.epoch(), "calibration", "info", "Sonde kalibriert", cfg_.device(dev)->name + " (" + kind + ")");
    return Result::ok({{"calibration", data}});
  }
  return Result::fail(422, "probe.action", "Unbekannter Schritt");
}

Result Hub::ackLatch(const std::string& id) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  auto it = rt_.latches.find(id);
  if (it == rt_.latches.end()) return Result::fail(404, "latch.unknown", "Keine solche Rastung");
  if (id.rfind("jump.", 0) == 0) return Result::fail(409, "latch.jump", "Die Sprungsperre hebt sich nach 15 min Ruhe selbst auf");
  rt_.latches.erase(it);
  stateDirty_ = true;
  log_.add(clock_.epoch(), "block", "info", "Rastung quittiert", id);
  return Result::ok();
}

Result Hub::stop(const std::string& who) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  // Not-Halt: aktive Abschaltkaskade, idempotent (RAT-036)
  Ctx c = ctx();
  if (doser_.busy()) doser_.abort(c, act_, "Not-Halt");
  if (auto fin = doser_.takeFinished()) {
    // Regler-Gaben gehören nicht zum Nutzerauftrag; die Regler werden unten zurückgesetzt.
    const bool controllerDose = fin->orderId.rfind("ec-", 0) == 0 || fin->orderId.rfind("ph-", 0) == 0;
    if (job_ && !controllerDose) onJobDose(c, *fin);
  }
  act_.stopAll(cat_, cfg_, clock_.nowMs());
  ec_.reset();
  ph_.reset();
  if (job_) finishJob("aborted", {"job.stopped", "Durch Not-Halt abgebrochen", json::object()});
  if (!stopped_) {
    log_.add(clock_.epoch(), "system", "alarm", "Not-Halt", "Alle Pumpen und Ausgänge aus. Ausgelöst: " + who);
    // A manual emergency stop stops everything, also after a power loss (PD-076).
    setFanSockets(false);
  }
  stopped_ = true;
  rt_.stopped = true;
  saveState();
  return Result::ok();
}

Result Hub::resume() {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  if (stopped_) {
    log_.add(clock_.epoch(), "system", "info", "Automatik fortgesetzt", "Not-Halt aufgehoben");
    setFanSockets(true);
  }
  stopped_ = false;
  rt_.stopped = false;
  saveState();
  return Result::ok();
}

Result Hub::maintenance(double minutes) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  if (!isNum(minutes) || minutes < 0 || minutes * 60 > kMaxMaintenanceS) return Result::fail(422, "maint.minutes", "0–240 min");
  maintenanceUntil_ = minutes > 0 ? clock_.epoch() + static_cast<Epoch>(minutes * 60) : 0;
  log_.add(clock_.epoch(), "system", "info", minutes > 0 ? "Pflegemodus" : "Pflegemodus beendet",
           minutes > 0 ? "Automatik ruht " + fmt(minutes, 0) + " min. Sperren und Sensorwahrheit bleiben aktiv." : "");
  return Result::ok();
}

Result Hub::manualMeasure(const json& j) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  std::string text;
  for (const char* k : {"ph", "ec"}) {
    double v = jnum(j, k);
    if (isNum(v)) {
      rt_.manual[k] = v;
      text += std::string(text.empty() ? "" : ", ") + (std::string(k) == "ph" ? "pH " : "EC ") + fmt(v, 2);
    }
  }
  if (text.empty()) return Result::fail(422, "manual.empty", "Kein Wert eingetragen");
  rt_.manualAt = clock_.epoch();
  stateDirty_ = true;
  log_.add(clock_.epoch(), "measure", "info", "Handmessung", text);
  return Result::ok();
}

Result Hub::growStart(const json& j) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  if (cfg_.grow.state == "running") return Result::fail(409, "grow.running", "Es läuft bereits ein Durchgang");
  GrowCfg g;
  g.state = "running";
  g.name = utf8Prefix(jstr(j, "name", "Durchgang"), 40);
  g.startedAt = clock_.epoch();
  g.phaseStartedAt = g.startedAt;
  const json phases = j.contains("phases") && j["phases"].is_array() ? j["phases"] : json::array();
  for (const auto& p : phases) {
    double days = jnum(p, "days", 0);
    json params = p.is_object() && p.contains("params") ? p["params"] : json::object();
    g.phases.push_back({jstr(p, "name"), isNum(days) && days >= 0 && days < 1000 ? static_cast<int>(days) : 0, params});
  }
  if (g.phases.empty()) return Result::fail(422, "grow.phases", "Mindestens eine Phase anlegen");
  Config next = cfg_;
  next.grow = g;
  auto errs = validateConfig(next, cat_);
  if (!errs.empty()) return errors(errs);
  cfg_.grow = g;
  saveConfig("");
  log_.add(clock_.epoch(), "grow", "info", "Durchgang gestartet", g.name + " · Phase „" + g.phases[0].name + "“ (Tag 1)");
  return Result::ok();
}

Result Hub::growNextPhase() {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  auto& g = cfg_.grow;
  if (g.state != "running") return Result::fail(409, "grow.state", "Kein laufender Durchgang");
  if (g.phase + 1 >= static_cast<int>(g.phases.size())) return Result::fail(409, "grow.last", "Letzte Phase erreicht");
  g.phase++;
  g.phaseStartedAt = clock_.epoch();
  saveConfig("");
  log_.add(clock_.epoch(), "grow", "info", "Phasenwechsel", "Phase „" + g.phases[static_cast<size_t>(g.phase)].name + "“");
  return Result::ok();
}

Result Hub::growHarvest() {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  auto& g = cfg_.grow;
  if (g.state != "running") return Result::fail(409, "grow.state", "Kein laufender Durchgang");
  g.harvestedAt = clock_.epoch();  // Ernte ist ein Ereignis, kein Dauerzustand (RAT-077)
  saveConfig("");
  log_.add(clock_.epoch(), "grow", "info", "Ernte erfasst", g.name);
  return Result::ok();
}

Result Hub::growComplete() {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  auto& g = cfg_.grow;
  if (g.state != "running") return Result::fail(409, "grow.state", "Kein laufender Durchgang");
  g.state = "completed";
  saveConfig("");
  log_.add(clock_.epoch(), "grow", "info", "Durchgang abgeschlossen", g.name);
  return Result::ok();
}

}  // namespace gc
