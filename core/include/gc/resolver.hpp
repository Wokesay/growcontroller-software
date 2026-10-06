// Schicht 6: Resolver. Bewertet jede Funktion datengetrieben aus Katalog,
// Konfiguration, erkannten Geräten und Sensorwahrheit – auf zwei Achsen
// (Vorschlag architekt):
//   Einrichtung: unavailable → needs_setup → limited → ready
//   Laufzeit:    kommt vom Regler (off/idle/working/waiting/blocked/latched)
// Zu jedem fehlenden Punkt gibt es Klartext und einen Behebungsschritt
// ("Was fehlt dir?"). Daraus klappt die UI den Konfigurationsbaum auf und zu.
#pragma once

#include <string>
#include <vector>

#include "gc/bus.hpp"
#include "gc/catalog.hpp"
#include "gc/config.hpp"
#include "gc/mix.hpp"
#include "gc/truth.hpp"

namespace gc {

struct ReqResult {
  std::string level;  // hardware | setup | runtime
  bool ok = false;
  bool soft = false;
  std::string text;   // was erfüllt ist bzw. was fehlt
  std::string fix;    // Ziel in der UI, z. B. "devices", "canisters", "calibrate:PH-1"
  std::vector<std::string> shop;  // Geräteklassen, die das liefern würden
};

struct FunctionState {
  std::string id, label, group, text;
  int stage = 0;
  std::string setup;  // unavailable | needs_setup | limited | ready
  bool enabled = false;
  bool alwaysOn = false;
  std::vector<ReqResult> checks;
  Msg summary;
};
void to_json(json& j, const FunctionState& f);

std::vector<FunctionState> resolveFunctions(const Catalog& cat, const Config& cfg,
                                            const std::vector<DeviceReport>& devices, const PumpMap& pumps,
                                            const SensorTruth& truth);

// Pumpen-Übersicht aus den erkannten Geräten (Kappen am Dosierblock).
PumpMap pumpsFrom(const std::vector<DeviceReport>& devices);

}  // namespace gc
