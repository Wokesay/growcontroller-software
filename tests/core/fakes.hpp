// Testhilfen: steuerbare Uhr und ein minimaler Bus für Unit-Tests.
#pragma once

#include <map>

#include "gc/bus.hpp"
#include "gc/common.hpp"

namespace test {

struct Clock : gc::IClock {
  gc::Ms ms = 0;
  gc::Epoch start = 1790000000;
  gc::Ms nowMs() const override { return ms; }
  gc::Epoch epoch() const override { return start + ms / 1000; }
};

struct FakeBus : gc::IBus {
  std::vector<gc::DeviceReport> devs;
  std::map<std::string, gc::Sample> samples;  // "dev|cap"
  std::map<std::string, bool> switches;
  int runs = 0;
  void poll(gc::Ms) override {}
  std::vector<gc::PortReport> ports() const override { return {}; }
  std::vector<gc::DeviceReport> devices() const override { return devs; }
  std::optional<gc::Sample> sample(const std::string& d, const std::string& c) const override {
    auto it = samples.find(d + "|" + c);
    if (it == samples.end()) return std::nullopt;
    return it->second;
  }
  bool startRun(const std::string&, gc::Ms, const std::string&, std::string&) override {
    runs++;
    return true;
  }
  gc::RunStatus runStatus(const std::string&) const override { return {}; }
  void stopAllPumps() override {}
  bool setSwitch(const std::string& d, int ch, bool on, std::string&) override {
    switches[d + "|" + std::to_string(ch)] = on;
    return true;
  }
  std::optional<bool> switchState(const std::string& d, int ch) const override {
    auto it = switches.find(d + "|" + std::to_string(ch));
    if (it == switches.end()) return false;
    return it->second;
  }
  bool writePumpCalibration(const std::string&, double, std::string&) override { return true; }

  void head(const std::string& id, bool online = true) {
    gc::DeviceReport r;
    r.id = id;
    r.cls = "head_ph_ec";
    r.port = 3;
    r.online = online;
    devs.push_back(r);
  }
  void set(const std::string& dev, const std::string& cap, double raw, gc::Ms ts) { samples[dev + "|" + cap] = {raw, ts}; }
};

}  // namespace test
