// SPDX-License-Identifier: AGPL-3.0-or-later
// Schicht 9: Watchdog. Bewertet nur (OK / Problem / neutral) und steuert
// keine Aktoren (Quelle: RAT-074). Dieser Header bindet ausschließlich
// Lesemodell und Konfiguration ein; kein Bus, kein Gateway, kein Regler.
// tools/arch_check.sh prüft das in der CI (Regel R2).
#pragma once

#include <map>
#include <string>
#include <vector>

#include "gc/catalog.hpp"
#include "gc/config.hpp"
#include "gc/readmodel.hpp"

namespace gc {

struct DeviceSeen {
  std::string id, name;
  bool online = false;
};

struct ControllerView {
  std::string id;
  Msg label;
  std::string state;  // state wie CtlStatus::state
  Msg line;
};

struct WatchInput {
  const Catalog& cat;
  const Config& cfg;
  const RuntimeState& rt;
  const std::map<std::string, Reading>& readings;
  std::vector<DeviceSeen> devices;
  std::vector<ControllerView> controllers;
  std::map<std::string, double> pumpFlow;  // Pumpe → Einmesswert (NaN = fehlt)
  Epoch epoch = 0;
  Epoch bootEpoch = 0;
  Epoch maintenanceUntil = 0;
  bool stopped = false;
};

struct Assessment {
  std::string id;
  Msg label;
  std::string status;  // ok | problem | neutral
  Msg text;
};

struct WatchResult {
  Epoch evaluatedAt = 0;
  std::string overall;  // ok | problem | neutral
  int ok = 0, problems = 0, neutral = 0;
  Msg headline;
  std::vector<Assessment> items;
};
void to_json(json& j, const WatchResult& w);

WatchResult evaluate(const WatchInput& in);

// Bewertung älter als 3 min gilt als fehlend und ist rot (Quelle: RAT-073 N3).
constexpr Epoch kWatchStaleS = 180;

}  // namespace gc
