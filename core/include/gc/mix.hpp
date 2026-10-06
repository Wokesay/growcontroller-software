// Dosierplanung als reine Funktionen (gut testbar, keine Seiteneffekte).
//   Mischen nach Rezept (Stufe 0), EC-Nachdosierung und pH-Gabe (Stufe 1).
// Invarianten: pH immer zuletzt (RAT-004); gepaarte Nährstoffe
// an jeder Grenze gemeinsam skalieren, nie je Pumpe kappen oder runden
// (RAT-054); ohne Einmesswert keine Dosierung (strenger als RAT-015); fehlende
// Größe → Abbruch ohne Ersatzwert (RAT-006); unter 1 s Laufzeit nicht dosieren
// (RAT-050); große Gaben in gleiche Teilgaben (RAT-055).
#pragma once

#include <map>
#include <string>
#include <vector>

#include "gc/config.hpp"

namespace gc {

struct PumpInfo {
  std::string id;
  bool online = false;
  double flowMlPerMin = kNaN;  // Einmesswert aus dem ID-Chip (PD-010)
  std::string fault;
};
using PumpMap = std::map<std::string, PumpInfo>;

struct DoseStep {
  std::string canister, name, pump, pair, color;
  double mlPerL = kNaN;
  double ml = kNaN;
  double flowMlPerMin = kNaN;
  std::vector<Ms> runs;  // Teilläufe in ms, gleich groß
};
void to_json(json& j, const DoseStep& s);

// Teilt eine Gabe in gleiche Läufe ≤ maxRunS. Fehler: Laufzeit < minRunS,
// mehr Läufe als maxRuns (0 = unbegrenzt), Förderrate fehlt.
std::vector<Ms> splitRuns(double ml, double flowMlPerMin, const Limits& lim, int maxRuns, Msg& err);

struct MixRequest {
  std::string recipe;
  double waterL = kNaN;        // frisches Wasser für diesen Lauf
  std::string mode = "new";    // new = neu ansetzen | topup = auffüllen
  bool confirmRepeat = false;  // Rückfrage "vor kurzem gemischt" bestätigt
};

struct MixPlan {
  bool ok = false;
  std::string recipe, recipeName, mode;
  double waterL = kNaN;
  std::vector<DoseStep> steps;
  std::vector<Msg> errors, warnings;
  Msg after;  // Hinweis nach dem Lauf (pH zuletzt)
  double totalMl = 0;
  Ms totalMs = 0;
};
void to_json(json& j, const MixPlan& p);

MixPlan planMix(const Config& cfg, const RuntimeState& rt, const PumpMap& pumps, const MixRequest& req,
                Epoch now, bool phControlActive);

// EC anheben mit dem Rezept: Gabe = 0,8 × Lücke / Wirkung × V, Deckel in EC
// je Runde, alle Rezeptbestandteile gemeinsam skaliert (RAT-055, RAT-054).
struct EcDose {
  bool ok = false;
  Msg reason;
  double factor = 1.0;  // gemeinsamer Skalierungsfaktor durch Deckel
  std::vector<DoseStep> steps;
};
EcDose planEcDose(const Config& cfg, const PumpMap& pumps, const RecipeCfg& recipe, double volumeL, double gapEc,
                  double effectPerMlL, double maxEcStep);

// pH-Gabe: 0,8 × Lücke / Wirkung × V, Deckel min(0,3 pH Wirkung, 0,3 ml/L,
// Höchstmenge) (RAT-055, RAT-050).
struct PhDose {
  bool ok = false;
  Msg reason;
  double rawMl = kNaN, capMl = kNaN, ml = kNaN;
};
PhDose planPhDose(double ph, double target, double effectPerMlL, double volumeL, double maxMl);

// Wirkung klemmen: beobachtet < 0,25 × Start → Start; > 4 × Start → 4 × Start (RAT-055).
double clampEffect(double observed, double start);

}  // namespace gc
