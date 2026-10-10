// SPDX-License-Identifier: AGPL-3.0-or-later
// Simulation = Zwilling + Uhr + Bus + Speicher + Hub + API in einem Paket.
// Wird vom Host-Server (main.cpp) und von den Szenario-Tests genutzt.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>

#include "gc/api.hpp"
#include "netbus.hpp"
#include "simbus.hpp"
#include "updater.hpp"

namespace sim {

// Dateiablage (#68). Ohne Ordner nur im Speicher.
//   write():  on disk when it returns – temp file, fsync, rename, then the folder.
//   append(): added to the end at once, without fsync (journals: a torn end is
//             skipped when read).
//   hold():   while held (the simulator running faster than real time),
//             writes stay in memory and go to disk once each on release.
class FileStorage : public gc::IStorage {
 public:
  explicit FileStorage(std::string dir) : dir_(std::move(dir)) {}
  ~FileStorage() override;
  std::optional<std::string> read(const std::string& name) override;
  bool write(const std::string& name, const std::string& data) override;
  bool append(const std::string& name, const std::string& data) override;
  void hold(bool on);
  std::uint64_t bytesWritten() const;  // bytes handed to the disk

 private:
  bool memoryOnly() const { return dir_.empty() || memoryOnly_; }
  std::optional<std::string> readLocked(const std::string& name);
  bool toDisk(const std::string& name, const std::string& data, bool atEnd);
  void release();
  std::string dir_;
  bool memoryOnly_ = false;  // Ordner nicht beschreibbar: lesen ja, schreiben nein
  int holds_ = 0;
  std::map<std::string, std::string> cache_;
  std::set<std::string> dirty_;                 // written while held
  std::map<std::string, std::string> pending_;  // appended while held
  std::uint64_t written_ = 0;
  mutable std::mutex m_;
};

struct Options {
  std::string scenario = "demo";
  std::string dataDir;        // leer = nur im Speicher
  std::string password;       // nur Simulator: Passwort für das Demo-Szenario
  double prefillHours = 48;   // Verlauf für das Demo-Szenario vorrechnen
  gc::Epoch startEpoch = 0;   // 0 = jetzt minus Vorlauf
};

class Simulation {
 public:
  explicit Simulation(Options o);
  ~Simulation();

  void step(gc::Ms dt);                       // Uhr vorstellen, Hub tickt je ≤ 1 s
  void fastForward(double hours, gc::Ms tick = 5000);
  // Power failure: hub restarts, outputs without power. outageMs: how long
  // the power is gone (the world runs on, everything off); timeSecured:
  // whether the hub gets a secured time right away (PD-069, PD-073).
  // mainsLost=false: only the hub restarts (e.g. its watchdog); the sockets
  // keep their power and state.
  void reboot(gc::Ms outageMs = 0, bool timeSecured = true, bool mainsLost = true);
  void loadScenario(const std::string& name); // alles neu (Speicher leer)
  gc::json simState();
  gc::json control(const std::string& action, const gc::json& body);  // Störknöpfe

  gc::Hub& hub() { return *hub_; }
  gc::Api& api() { return *api_; }
  World& world() { return world_; }
  SimClock& clock() { return *clock_; }
  FileStorage& storage() { return *store_; }
  std::recursive_mutex& mutex() { return mtx_; }
  const std::string& scenario() const { return opts_.scenario; }
  double speed = 1.0;
  std::string demoPassword() const { return opts_.password; }

 private:
  void build(bool fresh);
  void setupWorld(const std::string& name);
  void configureDemo();
  void createHub();

  Options opts_;
  std::recursive_mutex mtx_;
  World world_;
  std::unique_ptr<SimClock> clock_;
  std::unique_ptr<SimBus> bus_;
  std::unique_ptr<SimNetBus> net_;
  std::unique_ptr<FileStorage> store_;
  std::unique_ptr<SimUpdater> updater_;
  std::unique_ptr<gc::Hub> hub_;
  std::unique_ptr<gc::Api> api_;
  gc::Catalog catalog_;
};

}  // namespace sim
