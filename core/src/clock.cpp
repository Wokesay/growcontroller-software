// SPDX-License-Identifier: AGPL-3.0-or-later
#include "gc/clock.hpp"

#include <algorithm>

namespace gc {

void to_json(json& j, const Stamp& s) { j = {{"at", s.at}, {"secured", s.secured}, {"operatingS", s.operatingS}}; }

std::optional<Stamp> stampFromJson(const json& j) {
  if (!j.is_object() || !j.contains("at") || !j["at"].is_number_integer() || !j.contains("operatingS") ||
      !j["operatingS"].is_number_integer())
    return std::nullopt;
  Stamp s;
  s.at = j["at"].get<Epoch>();
  s.secured = j.value("secured", false);
  s.operatingS = j["operatingS"].get<std::int64_t>();
  if (s.operatingS < 0) return std::nullopt;
  return s;
}

std::int64_t elapsedS(const Stamp& from, const Stamp& to) {
  const std::int64_t d = from.secured && to.secured ? to.at - from.at : to.operatingS - from.operatingS;
  return std::max<std::int64_t>(0, d);
}

void HubClock::start(const std::optional<Stamp>& saved, Epoch newestRecorded) {
  startMs_ = base_.nowMs();
  savedOperatingS_ = saved ? saved->operatingS : 0;
  continueFrom_.reset();
  if (saved) continueFrom_ = std::max(saved->at, newestRecorded);
  else if (newestRecorded > 0) continueFrom_ = newestRecorded;
}

Epoch HubClock::epoch() const {
  if (base_.secured() || !continueFrom_) return base_.epoch();
  return *continueFrom_ + (base_.nowMs() - startMs_) / 1000;
}

std::int64_t HubClock::operatingS() const { return savedOperatingS_ + (base_.nowMs() - startMs_) / 1000; }

const char* HubClock::source() const {
  if (base_.secured()) return "secured";
  return continueFrom_ ? "continued" : "unset";
}

}  // namespace gc
