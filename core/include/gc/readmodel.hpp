// SPDX-License-Identifier: AGPL-3.0-or-later
// Lesemodell der Messwerte (Regel R2): Watchdog und UI sehen nur diese
// Typen, keinen Bus und keine Aktoren.
#pragma once

#include <optional>
#include <string>

#include "gc/common.hpp"

namespace gc {

enum class Quality {
  NotBound,      // Rolle nicht zugeordnet ("nicht vorhanden" ist ein Befund, RAT-024)
  Offline,       // Gerät antwortet nicht ("Sensor liefert nicht")
  NoData,        // noch kein Messwert
  Stale,         // Messwert zu alt
  Frozen,        // Rohwert steht still, obwohl er rauschen müsste (RAT-023, RAT-059)
  Implausible,   // außerhalb des Plausibilitätsbands
  Jump,          // Sprungsperre (RAT-039, RAT-044)
  Uncalibrated,  // Kalibrierung fehlt oder ungültig: kein Wert für die Regelung (RAT-025)
  Ok
};
const char* qualityName(Quality q);
// Reason key of a value calibrated outside the hub: Uncalibrated, shown, never
// usable (RAT-025). The watchdog and the web app tell this case apart by it.
inline constexpr const char* kCalibratedElsewhere = "truth.external";

struct Reading {
  std::string role, capability, unit;
  int decimals = 2;
  std::optional<double> value;  // angezeigter Wert (bei Uncalibrated der Rohwert)
  Quality quality = Quality::NotBound;
  Msg reason;
  Ms ts = 0;
  Ms ageMs = -1;
  bool usable() const { return quality == Quality::Ok && value.has_value(); }
  // Passed every check the hub makes, though perhaps calibrated elsewhere:
  // what the history records (SD-039). Only for the history: control, the
  // watchdog and the resolver read usable() (tools/arch_check.sh).
  bool forHistory() const {
    return value.has_value() && (quality == Quality::Ok || (quality == Quality::Uncalibrated && reason.key == kCalibratedElsewhere));
  }
};

}  // namespace gc
