// SPDX-License-Identifier: AGPL-3.0-or-later
#include "scenario.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>

#ifdef _WIN32
#include <io.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace sim {

namespace fs = std::filesystem;
using gc::json;

// ------------------------------------------------------------------ Ablage

namespace {

std::error_code lastError() { return {errno ? errno : EIO, std::generic_category()}; }

// For the owner only; a planted link in the folder is not followed.
std::FILE* openForOwner(const fs::path& path, bool atEnd) {
#ifdef _WIN32
  return std::fopen(path.string().c_str(), atEnd ? "ab" : "wb");
#else
  int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC | O_NOFOLLOW | (atEnd ? O_APPEND : O_TRUNC), 0600);
  if (fd < 0) return nullptr;
  ::fchmod(fd, 0600);  // also a file left from an older version
  std::FILE* f = ::fdopen(fd, atEnd ? "ab" : "wb");
  if (!f) ::close(fd);
  return f;
#endif
}

// On disk when it returns: a power loss leaves the old file or the new one.
std::error_code replaceFile(const fs::path& target, const std::string& data) {
  const fs::path tmp = target.string() + ".tmp";
  errno = 0;
  std::FILE* f = openForOwner(tmp, false);
  if (!f) return lastError();
  bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size() && std::fflush(f) == 0;
#ifdef _WIN32
  ok = ok && _commit(_fileno(f)) == 0;
#else
  ok = ok && ::fsync(fileno(f)) == 0;
#endif
  std::error_code ec = ok ? std::error_code() : lastError();
  if (std::fclose(f) != 0 && !ec) ec = lastError();
  std::error_code rc;
  if (!ec) fs::rename(tmp, target, rc);  // atomar ersetzen
  if (ec || rc) {  // a failed write does not keep the space: on a nearly full card a STOP must still fit
    std::error_code ignored;
    fs::remove(tmp, ignored);
    return ec ? ec : rc;
  }
#ifndef _WIN32
  if (!rc) {  // the rename itself is on disk only with the folder
    int dir = ::open(target.parent_path().c_str(), O_RDONLY | O_CLOEXEC);
    if (dir >= 0) {
      ::fsync(dir);
      ::close(dir);
    }
  }
#endif
  return rc;
}

std::error_code appendFile(const fs::path& target, const std::string& data) {
  errno = 0;
  std::FILE* f = openForOwner(target, true);
  if (!f) return lastError();
  const bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
  const std::error_code ec = ok ? std::error_code() : lastError();
  if (std::fclose(f) != 0 && ok) return lastError();
  return ec;
}

}  // namespace

FileStorage::~FileStorage() {
  std::lock_guard<std::mutex> l(m_);
  release();
}

std::optional<std::string> FileStorage::read(const std::string& name) {
  std::lock_guard<std::mutex> l(m_);
  return readLocked(name);
}

std::optional<std::string> FileStorage::readLocked(const std::string& name) {
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
  if (onDisk() && holds_ == 0) {
    if (!toDisk(name, data, false) && onDisk()) return false;  // the cache keeps what is on disk
  } else if (onDisk() && std::find(dirty_.begin(), dirty_.end(), name) == dirty_.end()) {
    dirty_.push_back(name);  // held: written on release
  }
  cache_[name] = data;
  pending_.erase(name);  // the whole file replaces what was to be appended
  return true;
}

bool FileStorage::append(const std::string& name, const std::string& data) {
  std::lock_guard<std::mutex> l(m_);
  readLocked(name);  // reads stay right: the cache holds the whole file
  if (onDisk() && holds_ == 0) {
    if (!toDisk(name, data, true) && onDisk()) return false;
  } else if (onDisk() && std::find(dirty_.begin(), dirty_.end(), name) == dirty_.end()) {
    pending_[name] += data;  // held; a dirty file goes whole, with this in it
  }
  cache_[name] += data;
  return true;
}

bool FileStorage::ready() {
  std::lock_guard<std::mutex> l(m_);
  if (dir_.empty()) return true;
  std::error_code ec;
  if (fs::create_directories(dir_, ec) && !ec) {
#ifndef _WIN32
    std::error_code ignored;
    fs::permissions(dir_, fs::perms::owner_all, ignored);
#endif
  }
  if (ec || replaceFile(fs::path(dir_) / ".write-test", "")) return false;
  fs::remove(fs::path(dir_) / ".write-test", ec);
  used_ = true;  // from here a folder that goes away is a failed write, not memory only
#ifndef _WIN32
  for (const char* f : {"auth.json", "config.json", "state.json", "job.json", "events.json", "events.log", "history.bin", "history.log"})
    if (fs::exists(fs::path(dir_) / f, ec)) fs::permissions(fs::path(dir_) / f, fs::perms::owner_read | fs::perms::owner_write, ec);
#endif
  return true;
}

std::vector<std::string> FileStorage::hold(bool on) {
  std::lock_guard<std::mutex> l(m_);
  if (on) {
    ++holds_;
    return {};
  }
  return holds_ > 0 && --holds_ == 0 ? release() : std::vector<std::string>();
}

std::uint64_t FileStorage::bytesWritten() const {
  std::lock_guard<std::mutex> l(m_);
  return written_;
}

std::vector<std::string> FileStorage::release() {
  // In the order first written: a snapshot before the journal it empties.
  // Behind a failed file, nothing of the same stem is written (events.json
  // before events.log, history.bin before history.log), so a journal is
  // never emptied behind a snapshot that failed.
  holds_ = 0;
  std::vector<std::string> failed;
  std::set<std::string> stems;
  const auto stem = [](const std::string& name) { return fs::path(name).stem().string(); };
  const auto put = [&](const std::string& name, const std::string& data, bool atEnd) {
    if (!stems.count(stem(name)) && (!onDisk() || toDisk(name, data, atEnd))) return;
    stems.insert(stem(name));
    failed.push_back(name);
  };
  for (const auto& name : dirty_) put(name, cache_[name], false);
  for (const auto& [name, data] : pending_) put(name, data, true);
  dirty_.clear();
  pending_.clear();
  if (!onDisk()) return {};  // memory only from now on: the cache is the only copy
  if (!failed.empty()) cache_.clear();  // read again from what is on disk
  return failed;
}

bool FileStorage::toDisk(const std::string& name, const std::string& data, bool atEnd) {
  std::error_code ec;
  if (fs::create_directories(dir_, ec) && !ec) {
#ifndef _WIN32
    std::error_code ignored;  // a file system without modes (FAT) still takes the files
    fs::permissions(dir_, fs::perms::owner_all, ignored);  // a new folder is the owner's only
#endif
  }
  if (ec && !used_) {
    // A folder that cannot be created at the start (e.g. under a protected path):
    // said once, then memory only. Once files were written, it is a failed write.
    std::cerr << "Data cannot be saved (" << dir_ << ": " << ec.message() << "). Running in memory only.\n";
    noFolder_ = true;
    return false;
  }
  if (!ec) ec = atEnd ? appendFile(fs::path(dir_) / name, data) : replaceFile(fs::path(dir_) / name, data);
  if (ec) {
    if (failing_.insert(name).second)
      std::cerr << "Cannot save " << name << " (" << dir_ << ": " << ec.message() << "); it is tried again.\n";
    return false;
  }
  if (failing_.erase(name)) std::cerr << "Saving " << name << " works again (" << dir_ << ").\n";
  used_ = true;
  written_ += data.size();
  return true;
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
  if (store_) store_->write("world.json", world_.save().dump());
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
  if (h.saveAuth()) h.markPasswordSet();  // on disk before the long prefill: a start cut short must not leave setup open
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
  // Hours in seconds: each file goes to disk once at the end, not on every tick;
  // what did not reach the disk is given to the hub, which writes it again.
  struct Held {
    FileStorage& store;
    gc::Hub& hub;
    Held(FileStorage& s, gc::Hub& h) : store(s), hub(h) { store.hold(true); }
    ~Held() {
      for (const auto& name : store.hold(false)) hub.storageFailed(name);
    }
  } held(*store_, *hub_);
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

void Simulation::reboot(gc::Ms outageMs, bool timeSecured, bool mainsLost) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  // Stromausfall: Ausgänge stromlos, laufende Pumpen stehen (Dosierblock nach Reset aus)
  hub_->flush();
  api_.reset();
  hub_.reset();
  world_.stopAllPumps();
  world_.out[0] = world_.out[1] = false;
  if (mainsLost) world_.powerFail();
  if (outageMs > 0) {
    clock_->advance(outageMs);
    world_.advance(clock_->nowMs());  // the world runs on without power
  }
  if (mainsLost) world_.powerReturn();  // each socket takes its setting for after a power loss
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
    for (const char* f : {"config.json", "config.broken.json", "state.json", "state.broken.json", "auth.json", "events.json",
                          "events.log", "history.bin", "history.log", "history.broken.bin", "job.json", "world.json"})
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
      if (f != "none" && f != "offline" && f != "readonly" && f != "ignore" && f != "stuck" && f != "crash")
        return err("Störung: offline, readonly, ignore, stuck, crash oder none");
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
    if ((b.contains("outageMin") && !b["outageMin"].is_number()) || (b.contains("timeSecured") && !b["timeSecured"].is_boolean()) ||
        (b.contains("mainsLost") && !b["mainsLost"].is_boolean()))
      return err("outageMin: Zahl, timeSecured und mainsLost: true/false");
    double outageMin = gc::jnum(b, "outageMin", 0);
    if (!gc::isNum(outageMin) || outageMin < 0 || outageMin > 72 * 60) return err("Ausfall 0–4320 min");
    reboot(static_cast<gc::Ms>(outageMin * 60000.0), gc::jbool(b, "timeSecured", true), gc::jbool(b, "mainsLost", true));
  } else if (action == "time") {
    // Network time arrives or goes away, or the clock is set, without a restart.
    if ((b.contains("secured") && !b["secured"].is_boolean()) || (b.contains("stepS") && !b["stepS"].is_number_integer()))
      return err("secured: true/false, stepS: ganze Sekunden");
    if (b.contains("secured")) clock_->setSecured(b["secured"].get<bool>());
    if (b.contains("stepS")) clock_->stepWall(b["stepS"].get<gc::Epoch>());
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
