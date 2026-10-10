// SPDX-License-Identifier: AGPL-3.0-or-later
#include "gc/bus.hpp"

#include <cmath>

namespace gc {

const char* portStateName(PortState s) {
  switch (s) {
    case PortState::Empty: return "empty";
    case PortState::Checking: return "checking";
    case PortState::Ok: return "ok";
    case PortState::Fault: return "fault";
    case PortState::Rejected: return "rejected";
  }
  return "unknown";
}

std::optional<std::string> MemoryStorage::read(const std::string& name) {
  for (const auto& [n, d] : files_)
    if (n == name) return d;
  return std::nullopt;
}

bool MemoryStorage::write(const std::string& name, const std::string& data) {
  for (auto& [n, d] : files_)
    if (n == name) {
      d = data;
      return true;
    }
  files_.emplace_back(name, data);
  return true;
}

bool MemoryStorage::append(const std::string& name, const std::string& data) {
  for (auto& [n, d] : files_)
    if (n == name) {
      d += data;
      return true;
    }
  files_.emplace_back(name, data);
  return true;
}

bool sameSafety(const SwitchSafety& a, const SwitchSafety& b) {
  auto same = [](double x, double y) { return (std::isnan(x) && std::isnan(y)) || (!std::isnan(x) && !std::isnan(y) && std::fabs(x - y) < 0.5); };
  return a.powerOn == b.powerOn && same(a.autoOffS, b.autoOffS) && same(a.powerLimitW, b.powerLimitW);
}

const char* powerOnName(PowerOn p) {
  switch (p) {
    case PowerOn::On: return "on";
    case PowerOn::Restore: return "restore";
    case PowerOn::Off: break;
  }
  return "off";
}

std::optional<PowerOn> powerOnFromName(const std::string& s) {
  if (s == "off") return PowerOn::Off;
  if (s == "on") return PowerOn::On;
  if (s == "restore") return PowerOn::Restore;
  return std::nullopt;
}

}  // namespace gc
