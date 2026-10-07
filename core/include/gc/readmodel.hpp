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

struct Reading {
  std::string role, capability, unit;
  int decimals = 2;
  std::optional<double> value;  // angezeigter Wert (bei Uncalibrated der Rohwert)
  Quality quality = Quality::NotBound;
  Msg reason;
  Ms ts = 0;
  Ms ageMs = -1;
  bool usable() const { return quality == Quality::Ok && value.has_value(); }
};

}  // namespace gc
