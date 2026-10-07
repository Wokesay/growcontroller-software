// SPDX-License-Identifier: AGPL-3.0-or-later
// IBus-Umsetzung des Simulators: übersetzt den Zwilling in das, was der Hub
// über RS485/Modbus und die Port-Prüfmessung sehen würde.
#pragma once

#include <map>
#include <mutex>

#include "gc/bus.hpp"
#include "world.hpp"

namespace sim {

class SimClock : public gc::IClock {
 public:
  explicit SimClock(gc::Epoch start) : start_(start) {}
  gc::Ms nowMs() const override { return ms_; }
  gc::Epoch epoch() const override { return start_ + ms_ / 1000; }
  // The host clock counts as network time; a power failure can clear it
  // until the time is secured again (PD-069, PD-073).
  bool secured() const override { return secured_; }
  void setSecured(bool s) { secured_ = s; }
  void advance(gc::Ms dt) { ms_ += dt; }
  void set(gc::Ms ms) { ms_ = ms; }

 private:
  gc::Epoch start_;
  gc::Ms ms_ = 0;
  bool secured_ = true;
};

class SimBus : public gc::IBus {
 public:
  explicit SimBus(World& w) : w_(w) {}
  void poll(gc::Ms now) override;
  std::vector<gc::PortReport> ports() const override;
  std::vector<gc::DeviceReport> devices() const override;
  std::optional<gc::Sample> sample(const std::string& dev, const std::string& cap) const override;
  bool startRun(const std::string& pump, gc::Ms ms, const std::string& jobId, std::string& err) override;
  gc::RunStatus runStatus(const std::string& pump) const override;
  void stopAllPumps() override;
  bool setSwitch(const std::string& dev, int channel, bool on, std::string& err) override;
  std::optional<bool> switchState(const std::string& dev, int channel) const override;
  bool writePumpCalibration(const std::string& pump, double mlPerMin, std::string& err) override;

  static constexpr const char* kHubOut = "HUB-OUT";

 private:
  World& w_;
  mutable std::map<std::string, std::pair<gc::Ms, double>> cache_;
  mutable std::map<std::string, gc::RunStatus> lastRun_;  // letzter gelesener Laufstatus je Pumpe
};

}  // namespace sim
