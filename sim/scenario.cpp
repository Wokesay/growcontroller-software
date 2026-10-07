// SPDX-License-Identifier: AGPL-3.0-or-later
#include "scenario.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>

namespace sim {

namespace fs = std::filesystem;
using gc::json;

// ------------------------------------------------------------------ Ablage

std::optional<std::string> FileStorage::read(const std::string& name) {
  std::lock_guard<std::mutex> l(m_);
  auto it = cache_.find(name);
  if (it != cache_.end()) return it->second;
  if (dir_.empty()) return std::nullopt;
  std::ifstream f(fs::path(dir_) / name, std::ios::binary);
  if (!f) return std::nullopt;
  std::stringstream ss;
  ss << f.rdbuf();
  cache_[name] = ss.str();
  return cache_[name];
}

bool FileStorage::write(const std::string& name, const std::string& data) {
  std::lock_guard<std::mutex> l(m_);
  cache_[name] = data;
  dirty_.insert(name);
  return true;
}

void FileStorage::flush() {
  std::lock_guard<std::mutex> l(m_);
  if (dir_.empty() || memoryOnly_) {
    dirty_.clear();
    return;
  }
  // Nicht beschreibbarer Ordner (z. B. Programm in einem geschützten
  // Verzeichnis): einmal melden, dann nur im Speicher weiterlaufen.
  std::error_code ec;
  fs::create_directories(dir_, ec);
  for (const auto& name : dirty_) {
    if (ec) break;
    fs::path tmp = fs::path(dir_) / (name + ".tmp");
    {
      std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
      f << cache_[name];
      if (!f) ec = std::make_error_code(std::errc::io_error);
    }
    if (!ec) fs::rename(tmp, fs::path(dir_) / name, ec);  // atomar ersetzen
  }
  dirty_.clear();
  if (ec) {
    std::cerr << "Daten können nicht gespeichert werden (" << dir_ << ": " << ec.message() << "). Der Simulator läuft nur im Speicher weiter.\n";
    memoryOnly_ = true;
  }
}

// ------------------------------------------------------------------ Simulation

namespace {

gc::Epoch realNow() {
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

void randomBytes(std::uint8_t* p, size_t n) {
  static std::random_device rd;
  for (size_t i = 0; i < n; ++i) p[i] = static_cast<std::uint8_t>(rd());
}

}  // namespace

Simulation::Simulation(Options o) : opts_(std::move(o)), catalog_(gc::Catalog::builtin()) {
  bool fresh = true;
  if (!opts_.dataDir.empty() && fs::exists(fs::path(opts_.dataDir) / "config.json")) fresh = false;
  build(fresh);
}

Simulation::~Simulation() {
  if (hub_) hub_->flush();
  if (store_) {
    store_->write("world.json", world_.save().dump());
    store_->flush();
  }
}

void Simulation::createHub() {
  hub_ = std::make_unique<gc::Hub>(catalog_, *bus_, *store_, *clock_, randomBytes);
  hub_->setUpdater(updater_.get());
  hub_->setNetBus(net_.get());
  hub_->setPlatform({{"kind", "simulator"}, {"simulated", true}, {"scenario", opts_.scenario}});
  api_ = std::make_unique<gc::Api>(*hub_, *clock_);  // wall time comes from the hub (PD-069)
  hub_->boot();
}

void Simulation::build(bool fresh) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  bool demo = opts_.scenario == "demo";
  gc::Epoch start = opts_.startEpoch ? opts_.startEpoch
                                     : realNow() - (fresh && demo ? static_cast<gc::Epoch>(opts_.prefillHours * 3600) : 0);
  clock_ = std::make_unique<SimClock>(start);
  bus_ = std::make_unique<SimBus>(world_);
  net_ = std::make_unique<SimNetBus>(world_);
  store_ = std::make_unique<FileStorage>(opts_.dataDir);
  updater_ = std::make_unique<SimUpdater>(*clock_);
  if (!fresh) {
    if (auto w = store_->read("world.json")) world_.restore(json::parse(*w));
    else setupWorld(opts_.scenario);
    createHub();
    step(3000);
    return;
  }
  setupWorld(opts_.scenario);
  createHub();
  step(3000);
  if (demo) {
    configureDemo();
    fastForward(opts_.prefillHours);
  }
  store_->write("world.json", world_.save().dump());
  hub_->flush();
  store_->flush();
}

void Simulation::setupWorld(const std::string& name) {
  world_.reset();
  // Feste Kennungen, damit Konfigurationen über Neustarts des Simulators passen.
  world_.plug(1, "dosing_block", "DB-7A31C0");
  world_.plugCap("DB-7A31C0", 0, "grow_a", "CAP-1F02A4");
  world_.plugCap("DB-7A31C0", 1, "grow_b", "CAP-1F02B7");
  world_.plugCap("DB-7A31C0", 2, "calmag", "CAP-1F02C1");
  if (name == "stufe1" || name == "demo") {
    world_.plugCap("DB-7A31C0", 3, "ph_down", "CAP-1F02D9");
    world_.plug(3, "head_ph_ec", "PHEC-3F2A91");
    world_.fill(40, 0.02, 7.0);
  }
  if (name == "demo") {
    world_.plug(5, "head_level", "LVL-77B210");
    world_.plug(6, "head_climate", "CLIM-5D20C4");
    world_.addNetPlug("shelly_strip4", {{"light", 240}, {"exhaust", 35}, {"circulation_fan", 15}, {"humidifier", 30}});
    world_.fill(31, 0.02, 7.0);
  }
  for (auto& [p, d] : world_.ports) d.pluggedAt = -10000;
}

void Simulation::configureDemo() {
  auto& h = *hub_;
  if (opts_.password.empty()) opts_.password = "demo-passwort";
  h.auth().setInitialPassword(opts_.password);
  h.acceptDevice("DB-7A31C0", "Dosierblock 1");
  h.acceptDevice("CAP-1F02A4", "Pumpe grün");
  h.acceptDevice("CAP-1F02B7", "Pumpe orange");
  h.acceptDevice("CAP-1F02C1", "Pumpe blau");
  h.acceptDevice("CAP-1F02D9", "Pumpe rot");
  h.acceptDevice("PHEC-3F2A91", "pH/EC Tank");
  h.acceptDevice("LVL-77B210", "Füllstand Tank");
  h.acceptDevice(SimBus::kHubOut, "Hub-Ausgänge");
  h.bindRole("tank.circulation", SimBus::kHubOut, 0);
  h.bindRole("tank.inlet", SimBus::kHubOut, 1);
  // Raumklima und Steckdosenleiste: Licht, Abluft, Umluft, Befeuchter
  h.acceptDevice("CLIM-5D20C4", "Klima Raum");
  if (!world_.netPlugs.empty()) {
    const std::string strip = world_.netPlugs.front().id;
    h.acceptDevice(strip, "Leiste");
    h.bindRole("zone.light", strip, 0);
    h.bindRole("zone.exhaust", strip, 1);
    h.bindRole("zone.circulation_fan", strip, 2);
    h.bindRole("zone.humidifier", strip, 3);
    for (const char* r : {"zone.light", "zone.exhaust", "zone.circulation_fan"}) h.switchRole(r, true);
  }
  h.putTank({{"name", "Tank 1"}, {"capacityL", 60}, {"minL", 3}, {"water", "ro"}});
  h.putCanister({{"name", "Teil A"}, {"kind", "nutrient"}, {"pump", "CAP-1F02A4"}, {"pair", "AB"}, {"color", "#3f8f4a"}, {"capacityMl", 1000}, {"stockMl", 820}});
  h.putCanister({{"name", "Teil B"}, {"kind", "nutrient"}, {"pump", "CAP-1F02B7"}, {"pair", "AB"}, {"color", "#c47a2c"}, {"capacityMl", 1000}, {"stockMl", 790}});
  h.putCanister({{"name", "CalMag"}, {"kind", "nutrient"}, {"pump", "CAP-1F02C1"}, {"color", "#5b7fb8"}, {"capacityMl", 1000}, {"stockMl", 640}});
  h.putCanister({{"name", "pH−"}, {"kind", "ph_down"}, {"pump", "CAP-1F02D9"}, {"color", "#b8455b"}, {"capacityMl", 500}, {"stockMl", 310}});
  // Einmessen wie mit Messbecher: kleiner Messfehler von ±2 %
  std::mt19937 rng(7);
  std::uniform_real_distribution<double> err(0.98, 1.02);
  for (const char* id : {"CAP-1F02A4", "CAP-1F02B7", "CAP-1F02C1", "CAP-1F02D9"}) {
    Cap* c = world_.cap(id);
    std::string e;
    bus_->writePumpCalibration(id, c->trueFlow * err(rng), e);
  }
  const std::string veg = h.putRecipe({{"name", "Wachstum"}, {"steps", {{{"canister", "teil-a"}, {"mlPerL", 2.0}}, {{"canister", "teil-b"}, {"mlPerL", 2.0}}, {{"canister", "calmag"}, {"mlPerL", 0.6}}}}}).body.value("id", "");
  const std::string bloom = h.putRecipe({{"name", "Blüte"}, {"steps", {{{"canister", "teil-a"}, {"mlPerL", 2.6}}, {{"canister", "teil-b"}, {"mlPerL", 2.6}}, {{"canister", "calmag"}, {"mlPerL", 0.4}}}}}).body.value("id", "");
  // Sonden kalibrieren über den echten Ablauf (Puffer im Zwilling)
  auto probe = [&](const std::string& kind, const std::vector<std::pair<double, double>>& points) {
    h.probeCalibration({{"device", "PHEC-3F2A91"}, {"kind", kind}, {"action", "start"}});
    for (const auto& [buffer, ref] : points) {
      world_.probeKind = kind;
      world_.probeBuffer = buffer;
      step(6000);
      h.probeCalibration({{"device", "PHEC-3F2A91"}, {"kind", kind}, {"action", "point"}, {"reference", ref}});
    }
    world_.probeBuffer.reset();
    h.probeCalibration({{"device", "PHEC-3F2A91"}, {"kind", kind}, {"action", "commit"}});
  };
  probe("ph", {{7.0, 7.0}, {4.0, 4.0}});
  probe("ec", {{1.413, 1.413}});
  h.probeCalibration({{"device", "LVL-77B210"}, {"kind", "tank_curve"}, {"action", "start"}});
  for (double v : {0.0, 3.0, 15.0, 30.0, 45.0, 60.0}) {
    world_.fill(v, 0.02, 7.0);
    step(6000);
    h.probeCalibration({{"device", "LVL-77B210"}, {"kind", "tank_curve"}, {"action", "point"}, {"reference", v}});
  }
  h.probeCalibration({{"device", "LVL-77B210"}, {"kind", "tank_curve"}, {"action", "commit"}});
  world_.fill(31, 0.02, 7.0);
  step(6000);
  h.putFunction("circulation", {{"enabled", true}, {"params", {{"mode", "interval"}, {"on_min", 15}, {"period_min", 60}}}});
  h.putFunction("water_temp_watch", {{"enabled", true}});
  h.putFunction("refill", {{"enabled", true}, {"params", {{"start_below_l", 30}, {"target_l", 45}, {"flow_l_per_min", 2.0}}}});
  h.mixStart({{"recipe", veg}, {"waterL", 31}, {"mode", "new"}, {"guided", false}});
  h.putFunction("ec_control", {{"enabled", true}, {"params", {{"ec_target", 1.4}, {"recipe", veg}}}});
  h.putFunction("ph_control", {{"enabled", true}, {"params", {{"ph_target", 5.8}}}});
  h.growStart({{"name", "Durchgang 1"},
               {"phases",
                {{{"name", "Wachstum"}, {"days", 21}, {"params", {{"ec_target", 1.4}, {"ph_target", 5.8}, {"recipe", veg}}}},
                 {{"name", "Blüte"}, {"days", 56}, {"params", {{"ec_target", 1.7}, {"ph_target", 5.9}, {"recipe", bloom}}}}}}});
  h.manualMeasure({{"ph", 6.1}});
  h.completeSetup();
}

void Simulation::step(gc::Ms dt) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  while (dt > 0) {
    gc::Ms d = std::min<gc::Ms>(1000, dt);
    clock_->advance(d);
    hub_->tick();
    dt -= d;
  }
}

void Simulation::fastForward(double hours, gc::Ms tick) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  gc::Ms total = static_cast<gc::Ms>(hours * 3600.0 * 1000.0);
  gc::Ms jumpAt = total * 5 / 8;  // eine Sprungsperre in den Verlauf legen
  for (gc::Ms t = 0; t < total; t += tick) {
    if (opts_.scenario == "demo" && t >= jumpAt && t < jumpAt + tick) {
      if (Device* d = world_.device("PHEC-3F2A91")) d->fault = "jump";
    }
    if (opts_.scenario == "demo" && t >= jumpAt + 90000 && t < jumpAt + 90000 + tick) {
      if (Device* d = world_.device("PHEC-3F2A91")) d->fault.clear();
    }
    clock_->advance(tick);
    hub_->tick();
  }
}

void Simulation::reboot(gc::Ms outageMs, bool timeSecured) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  // Stromausfall: Ausgänge stromlos, laufende Pumpen stehen (Dosierblock nach Reset aus)
  hub_->flush();
  api_.reset();
  hub_.reset();
  world_.stopAllPumps();
  world_.out[0] = world_.out[1] = false;
  world_.powerFail();
  if (outageMs > 0) {
    clock_->advance(outageMs);
    world_.advance(clock_->nowMs());  // the world runs on without power
  }
  world_.powerReturn();  // each socket takes its setting for after a power loss
  clock_->setSecured(timeSecured);
  createHub();
}

void Simulation::loadScenario(const std::string& name) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  api_.reset();
  hub_.reset();
  if (!opts_.dataDir.empty()) {
    // Nur die eigenen Dateien löschen, nie den ganzen Ordner.
    std::error_code ec;
    for (const char* f : {"config.json", "config.broken.json", "state.json", "auth.json", "events.json",
                          "history.bin", "job.json", "world.json"})
      for (const std::string& file : {std::string(f), std::string(f) + ".tmp"})
        fs::remove(fs::path(opts_.dataDir) / file, ec);
  }
  opts_.scenario = name;
  build(true);
}

json Simulation::simState() {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  return {{"scenario", opts_.scenario},
          {"speed", speed},
          {"simulated", true},
          {"epoch", clock_->epoch()},
          {"world", world_.toJson()}};
}

json Simulation::control(const std::string& action, const json& b) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  auto err = [](const std::string& t) { return json{{"error", {{"key", "sim"}, {"text", t}}}}; };
  if (action == "speed") {
    double s = gc::jnum(b, "speed", 1);
    if (s < 0 || s > 600) return err("Zeitraffer 0–600");
    speed = s;
  } else if (action == "plug") {
    if (!world_.plug(b.value("port", 0), b.value("class", std::string()))) return err("Port belegt oder ungültig");
  } else if (action == "unplug") {
    if (!world_.unplug(b.value("port", 0))) return err("Port leer");
  } else if (action == "cap") {
    if (!world_.plugCap(b.value("block", std::string()), b.value("slot", -1), b.value("liquid", std::string("grow_a"))))
      return err("Steckplatz belegt oder ungültig");
  } else if (action == "uncap") {
    if (!world_.unplugCap(b.value("block", std::string()), b.value("slot", -1))) return err("Steckplatz leer");
  } else if (action == "net_add") {
    // Steckdose im WLAN „einschalten“: Plug S (eine Dose) oder Leiste (vier)
    std::string cls = b.value("class", std::string("shelly_plug"));
    if (cls != "shelly_plug" && cls != "shelly_strip4") return err("Klasse: shelly_plug oder shelly_strip4");
    std::vector<std::pair<std::string, double>> loads;
    for (const auto& ld : b.value("loads", json::array())) loads.emplace_back(ld.value("load", std::string()), ld.value("watts", 0.0));
    std::string id = world_.addNetPlug(cls, loads);
    return {{"ok", true}, {"id", id}};
  } else if (action == "net_remove") {
    auto& v = world_.netPlugs;
    std::string dev = b.value("device", std::string());
    v.erase(std::remove_if(v.begin(), v.end(), [&](const NetPlug& p) { return p.id == dev; }), v.end());
  } else if (action == "fault") {
    std::string dev = b.value("device", std::string()), f = b.value("fault", std::string());
    if (NetPlug* np = world_.netPlug(dev)) {
      if (f != "none" && f != "offline" && f != "readonly" && f != "ignore" && f != "stuck")
        return err("Störung: offline, readonly, ignore, stuck oder none");
      np->fault = f == "none" ? "" : f;
    } else if (Cap* c = world_.cap(dev)) {
      c->blocked = f == "blocked";
    } else if (Device* d = world_.device(dev)) {
      // Erst den laufenden Wert festhalten, dann einfrieren (sonst friert NaN ein).
      if (f == "frozen") {
        d->fault.clear();
        d->frozenPh = world_.rawPh(d);
        d->frozenEc = world_.rawEc(d);
      }
      d->fault = f == "none" ? "" : f;
    } else {
      return err("Gerät nicht gefunden");
    }
  } else if (action == "water") {
    world_.fill(gc::jnum(b, "volumeL", world_.tank.volumeL), gc::jnum(b, "ec", 0.02), gc::jnum(b, "ph", 7.0));
  } else if (action == "probe") {
    world_.probeKind = b.value("kind", std::string("ph"));
    if (b.contains("buffer") && b["buffer"].is_number()) world_.probeBuffer = b["buffer"].get<double>();
    else world_.probeBuffer.reset();
  } else if (action == "reboot") {
    double outageMin = gc::jnum(b, "outageMin", 0);
    if (!gc::isNum(outageMin) || outageMin < 0 || outageMin > 72 * 60) return err("Ausfall 0–4320 min");
    reboot(static_cast<gc::Ms>(outageMin * 60000.0), gc::jbool(b, "timeSecured", true));
  } else if (action == "time") {
    // Network time arrives (or goes away) without a restart.
    clock_->setSecured(gc::jbool(b, "secured", true));
  } else if (action == "scenario") {
    std::string n = b.value("name", std::string("demo"));
    if (n != "demo" && n != "neu" && n != "stufe1") return err("Szenario: demo, neu oder stufe1");
    loadScenario(n);
  } else if (action == "advance") {
    double hours = gc::jnum(b, "hours", 1);
    if (hours <= 0 || hours > 72) return err("0–72 h");
    fastForward(hours);
  } else {
    return err("Unbekannte Aktion");
  }
  return simState();
}

}  // namespace sim
