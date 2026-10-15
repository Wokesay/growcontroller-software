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
#include <vector>

#include "gc/api.hpp"
#include "netbus.hpp"
#include "simbus.hpp"
#include "updater.hpp"

namespace sim {

// Random bytes from the operating system for salts, session tokens and the
// setup code (#79): getentropy(), on Windows the system's generator. Never
// std::random_device on the others: libstdc++ tries RDSEED first and takes the
// zero a Zen 5 processor reports as success (AMD-SB-7055). Aborts rather
// than going on without.
void randomBytes(std::uint8_t* p, size_t n);

// Files in a data folder (#68); without a folder in memory only.
//   write():  on disk when it returns true – temporary file, fsync, rename,
//             then the folder (Linux; best effort on macOS and Windows).
//   append(): added to the end at once, without fsync (journals: a torn end
//             is skipped when read).
//   hold():   while held (the simulator fast-forwarding), writes stay in memory
//             and go to disk once each on release, in the order first written;
//             a file is not written behind a failed one of the same stem (no
//             journal emptied behind its failed snapshot). The release
//             returns the files that did not reach the disk.
// A failed write returns false (the hub writes it again); files are for the
// owner only (0600, a new folder 0700). A folder that cannot be created
// means memory only, said once.
class FileStorage : public gc::IStorage {
 public:
  explicit FileStorage(std::string dir) : dir_(std::move(dir)) {}
  ~FileStorage() override;
  // The folder exists and takes a file; the hub's own files are made the owner's only.
  bool ready();
  std::optional<std::string> read(const std::string& name) override;
  bool write(const std::string& name, const std::string& data) override;
  bool append(const std::string& name, const std::string& data) override;
  std::vector<std::string> hold(bool on);
  std::uint64_t bytesWritten() const;  // bytes handed to the disk
  // It saves to its folder: one is given and, as far as a write has shown, it can be created.
  bool persistent() const;

 private:
  bool onDisk() const { return !dir_.empty() && !noFolder_; }
  std::optional<std::string> readLocked(const std::string& name);
  bool toDisk(const std::string& name, const std::string& data, bool atEnd);
  std::vector<std::string> release();
  std::string dir_;
  bool noFolder_ = false;          // the folder cannot be created at the start: memory only, said once
  bool used_ = false;              // files were written: a folder that goes away later is a failure
  std::set<std::string> failing_;  // files whose last write failed: each said once, and again when it works
  int holds_ = 0;
  std::map<std::string, std::string> cache_;
  std::vector<std::string> dirty_;              // written while held, in order
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
  // The data folder given; empty: none, or one it cannot read (SD-074).
  // Whether it saves there: storage().persistent().
  const std::string& dataDir() const { return opts_.dataDir; }
  double speed = 1.0;
  static constexpr double kMaxSpeed = 600;     // time-lapse, for --speed and the panel
  static constexpr double kMaxPrefillH = 1000; // hours of history computed ahead (--prefill)
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
