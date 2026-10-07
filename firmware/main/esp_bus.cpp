// SPDX-License-Identifier: AGPL-3.0-or-later
#include <ctime>

#include <driver/gpio.h>
#include <esp_timer.h>

#include "esp_platform.hpp"
#include "sdkconfig.h"

namespace gcfw {

namespace {
constexpr const char* kHubOut = "HUB-OUT";
const int kOutGpio[2] = {CONFIG_GC_OUT1_GPIO, CONFIG_GC_OUT2_GPIO};
}  // namespace

gc::Ms EspClock::nowMs() const { return esp_timer_get_time() / 1000; }
gc::Epoch EspClock::epoch() const { return static_cast<gc::Epoch>(std::time(nullptr)); }

void EspBus::init() {
  for (int i = 0; i < 2; ++i) {
    if (kOutGpio[i] < 0) continue;
    gpio_reset_pin(static_cast<gpio_num_t>(kOutGpio[i]));
    gpio_set_direction(static_cast<gpio_num_t>(kOutGpio[i]), GPIO_MODE_OUTPUT);
    gpio_set_level(static_cast<gpio_num_t>(kOutGpio[i]), 0);  // nach dem Start aus (R6)
  }
}

void EspBus::poll(gc::Ms) {}

std::vector<gc::PortReport> EspBus::ports() const {
  std::vector<gc::PortReport> out;
  for (int p = 1; p <= 8; ++p) {
    gc::PortReport r;
    r.port = p;
    r.state = gc::PortState::Empty;
    out.push_back(r);
  }
  return out;
}

std::vector<gc::DeviceReport> EspBus::devices() const {
  gc::DeviceReport hub;
  hub.id = kHubOut;
  hub.cls = "hub_outputs";
  hub.online = true;
  hub.fw = "0.1.0";
  return {hub};
}

std::optional<gc::Sample> EspBus::sample(const std::string&, const std::string&) const { return std::nullopt; }

bool EspBus::startRun(const std::string&, gc::Ms, const std::string&, std::string& err) {
  err = "Dosierblock-Treiber folgt (M1)";
  return false;
}

gc::RunStatus EspBus::runStatus(const std::string&) const { return {}; }

void EspBus::stopAllPumps() {}

bool EspBus::setSwitch(const std::string& dev, int channel, bool on, std::string& err) {
  if (dev != kHubOut || channel < 0 || channel > 1) {
    err = "unbekannter Ausgang";
    return false;
  }
  out_[channel] = on;
  if (kOutGpio[channel] >= 0) gpio_set_level(static_cast<gpio_num_t>(kOutGpio[channel]), on ? 1 : 0);
  return true;
}

std::optional<bool> EspBus::switchState(const std::string& dev, int channel) const {
  if (dev != kHubOut || channel < 0 || channel > 1) return std::nullopt;
  return out_[channel];
}

bool EspBus::writePumpCalibration(const std::string&, double, std::string& err) {
  err = "Dosierblock-Treiber folgt (M1)";
  return false;
}

}  // namespace gcfw
