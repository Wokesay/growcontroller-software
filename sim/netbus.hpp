// SPDX-License-Identifier: AGPL-3.0-or-later
// INetBus-Umsetzung des Simulators: schaltbare Steckdosen (Shelly Gen2+) im
// WLAN, wie der Hub sie über lokales RPC sehen würde. Auf dem Gerät folgt
// ein HTTP-RPC-Client mit mDNS und Digest (Konzept §2, §8 Schritt 7).
#pragma once

#include "gc/bus.hpp"
#include "world.hpp"

namespace sim {

class SimNetBus : public gc::INetBus {
 public:
  explicit SimNetBus(World& w) : w_(w) {}
  void poll(gc::Ms) override {}
  std::vector<gc::DeviceReport> devices() const override;
  bool owns(const std::string& dev) const override;
  bool setSwitch(const std::string& dev, int channel, bool on, std::string& err) override;
  std::optional<bool> switchState(const std::string& dev, int channel) const override;
  std::optional<double> powerW(const std::string& dev, int channel) const override;
  bool configure(const std::string& dev, int channel, const gc::SwitchSafety& s, std::string& err) override;
  std::optional<gc::SwitchSafety> readConfig(const std::string& dev, int channel) const override;

 private:
  NetOutlet* outlet(const std::string& dev, int channel, std::string& err) const;
  World& w_;
};

}  // namespace sim
