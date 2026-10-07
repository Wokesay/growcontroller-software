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

// Stückweise lineare Kennlinie aus Stützpunkten (Quelle: RAT-078):
// gültig ab 2 Punkten, Rohwerte streng steigend; unter dem ersten Punkt der
// erste Wert, über dem letzten mit dessen Steigung.
struct Curve {
  std::vector<std::pair<double, double>> points;  // (roh, Wert)
  static std::optional<Curve> fromJson(const json& j, std::string& err);
  double map(double raw) const;
};

class SensorTruth {
 public:
  SensorTruth(const Catalog& cat) : cat_(cat) {}

  // Einmal je Takt: liest alle gebundenen Mess-Rollen.
  void update(const Config& cfg, const IBus& bus, RuntimeState& rt, Ms now, Epoch epoch);
  // Eine eigene Dosierung erklärt einen Sprung bis zu diesem Zeitpunkt (RAT-042).
  // Prüft importierte Kalibrierdaten; Fehlertext oder nichts.
  static std::optional<std::string> checkCalibration(const std::string& kind, const json& data);
  void expectChange(const std::string& role, Ms until);

  const Reading& get(const std::string& role) const;
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
    Ms lastTs = 0;
    std::deque<std::pair<Ms, double>> window;  // für die Sprungprüfung
    Ms explainedUntil = 0;
  };
  const Catalog& cat_;
  std::map<std::string, Reading> readings_;
  std::map<std::string, Track> tracks_;
};

}  // namespace gc
