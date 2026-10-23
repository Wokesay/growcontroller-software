// SPDX-License-Identifier: AGPL-3.0-or-later
// Sensorwahrheit (Schicht 4): Rohwert → kalibrierter Wert → Gültigkeit.
// Läuft immer, auch ohne Grow und im Pflegemodus, weil Dosier-Sperren davon
// abhängen (Quelle: RAT-075, RAT-023). Regelnde Funktionen lesen nur
// diesen Zustand (RAT-025). Ein fehlender Wert ist nie 0 (RAT-006).
#pragma once

#include <deque>
#include <map>
#include <string>

#include "gc/bus.hpp"
#include "gc/catalog.hpp"
#include "gc/config.hpp"
#include "gc/readmodel.hpp"

namespace gc {

// A jump lock lifts after 15 min of calm (RAT-044).
inline constexpr Epoch kJumpHoldS = 15 * 60;
// A sensor with a sign of life is stale when its last measurement is older
// than this (SD-042, RAT-027).
inline constexpr Ms kLifeLimit = 60 * kSecond;

// Stückweise lineare Kennlinie aus Stützpunkten (Quelle: RAT-078):
// gültig ab 2 Punkten, Rohwerte streng steigend; unter dem ersten Punkt der
// erste Wert, über dem letzten mit dessen Steigung.
struct Curve {
  std::vector<std::pair<double, double>> points;  // (roh, Wert)
  static std::optional<Curve> fromJson(const json& j, Msg& err);
  double map(double raw) const;
};

class SensorTruth {
 public:
  SensorTruth(const Catalog& cat) : cat_(cat) {}

  // Einmal je Takt: liest alle gebundenen Mess-Rollen.
  void update(const Config& cfg, const IBus& bus, RuntimeState& rt, Ms now, Epoch epoch);
  // Checks imported calibration data: the reason as a message, or nothing.
  static std::optional<Msg> checkCalibration(const std::string& kind, const json& data);
  // Eine eigene Dosierung erklärt einen Sprung bis zu diesem Zeitpunkt (RAT-042).
  void expectChange(const std::string& role, Ms until);
  // A reading at or below `upTo` counts as explained until then; one above it does not, and one not
  // known explains nothing. One caller per role: each call replaces the bound, renewed on every tick (SD-101).
  void expectChangeUpTo(const std::string& role, Ms until, double upTo);

  const Reading& get(const std::string& role) const;
  // How old a value of this role may be before it is stale: the catalog's
  // limit, for a sensor on its own schedule the one learned from its reports
  // (SD-035), for one with a sign of life 60 s after its last measurement (SD-042).
  Ms limit(const std::string& role) const;
  const std::map<std::string, Reading>& all() const { return readings_; }

  // Sättigungsdampfdruck in kPa (FAO-56, Gl. 11) und Luft-VPD (Quelle: RAT-017).
  static double saturationKPa(double tempC);
  static double airVpdKPa(double tempC, double rhPct);

  // Anwendung der Kalibrierung: nullopt, wenn sie fehlt oder ungültig ist.
  static std::optional<double> calibrate(const std::string& cap, double raw, const json* calib);

 private:
  void updateDerived();
  struct Track {
    double lastRaw = kNaN;
    Ms lastRawChange = 0;
    bool rawLife = false;  // lastRawChange counts measurements of a sign of life (SD-042)
    Ms lastTs = 0;
    std::deque<std::pair<Ms, double>> window;  // für die Sprungprüfung
    Ms explainedUntil = 0;
    Ms boundedUntil = 0;  // explained up to `bound` (SD-101)
    double bound = kNaN;
    // A sensor on its own schedule (SD-035): the device the gaps belong to,
    // its last report and the last gaps between reports
    bool ownSchedule = false;
    bool life = false;  // it has a sign of life (SD-042): nothing is learned
    std::string device;
    bool reported = false;
    Ms lastReport = 0;
    std::deque<Ms> gaps;
  };
  // How old a value may be before it is stale (SD-035)
  static Ms freshLimit(const CapabilityDef& cap, const Track* learned);
  const Catalog& cat_;
  std::map<std::string, Reading> readings_;
  std::map<std::string, Track> tracks_;
};

}  // namespace gc
