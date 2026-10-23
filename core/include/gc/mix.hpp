// SPDX-License-Identifier: AGPL-3.0-or-later
// Dosierplanung als reine Funktionen (gut testbar, keine Seiteneffekte).
//   Mischen nach Rezept (Stufe 0), EC-Nachdosierung und pH-Gabe (Stufe 1).
// Invariants (docs/RATIONALE.md): pH always last (RAT-004); scale paired
// nutrients together at every limit, never cap or round per pump (RAT-054);
// no dosing without a calibration value (stricter than RAT-015); a missing
// quantity aborts without a substitute (RAT-006); no runs under 1 s
// (RAT-050); large doses in equal partial runs (RAT-055).
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
  bool fixed = false;    // a fixed addition: per litre of fresh water, never scaled (SD-057, RAT-085)
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
  // A fixed pattern to a target EC (SD-057 way 2, SD-098): "recipe" = the recipe's amounts, "target" = scaled
  // to targetEc from the water's EC; "measured" = to EC control's aim from the tank's probe, a capped first
  // step that EC control closes (way 1, SD-102); missing values stay NaN, never 0 (R5)
  std::string amounts = "recipe";
  double targetEc = kNaN, waterEc = kNaN;
  double targetMin = kNaN, targetMax = kNaN;  // the EC target's range from the catalog
  // EC control switched on and not locked would take the tank to its own target afterwards (SD-089 S20)
  bool ecControlOn = false;
  double ecControlTarget = kNaN;
  double phEcFloor = kNaN;  // the EC below which pH control never doses (RAT-046); NaN with pH control off
  // Way 1, filled by the hub (SD-102): the tank's EC from its probe (NaN when not usable, with the reason),
  // where EC control aims and its tolerance and step limit, the recipe it doses, the litres the scaled
  // parts are worked out on (a usable level, else entered or known plus fresh) and those entered
  double probeEc = kNaN;
  Msg probeReason{};
  double ecAim = kNaN, ecTolerance = kNaN, maxEcStep = kNaN;
  std::string ecRecipe{};
  double tankL = kNaN, enteredL = kNaN;
  std::string litresFrom{};  // level | entered
  // A top-up without a usable level: the litres in the tank now, fresh water included, as entered (SD-102 D4)
  double tankNowL = kNaN;
  bool probeCalibrating = false, circulationBound = false, circulationOk = false, inletOpen = false;
  Msg circulationReason{};  // why the circulation pump may not run now
  bool maintenance = false, ecBusy = false;  // maintenance mode on; EC control settling a round or a cut
};

// Why way 1 cannot run now, apart from what the user enters (recipe, litres): EC control off or
// without a target, maintenance, a round of EC control, a probe calibrated, no valid EC, the
// circulation pump missing or blocked, the inlet open (SD-102 D1); empty when it can
std::vector<Msg> measuredBlocks(const MixRequest& req);

struct MixPlan {
  bool ok = false;
  std::string recipe, recipeName, mode;
  double waterL = kNaN;
  std::vector<DoseStep> steps;
  std::vector<Msg> errors, warnings;
  Msg after;  // Hinweis nach dem Lauf (pH zuletzt)
  double totalMl = 0;
  Ms totalMs = 0;
  // A fixed pattern to a target EC (SD-098): the common factor on the scaled parts (at most 1, missing while
  // refused) and the EC to expect; infos are notes, not warnings
  std::string amounts = "recipe";
  double targetEc = kNaN, waterEc = kNaN, factor = 1.0, expectedEc = kNaN;
  std::vector<Msg> infos;
  // Way 1 (SD-102): the litres the scaled parts are worked out on and where from, the effect used, and
  // whether the step limit cut the first step
  double tankL = kNaN, effect = kNaN, ecTarget = kNaN;  // ecTarget: EC control's set target, the aim below it
  std::string litresFrom;
  bool capped = false;
};
void to_json(json& j, const MixPlan& p);

MixPlan planMix(const Config& cfg, const RuntimeState& rt, const PumpMap& pumps, const MixRequest& req,
                Epoch now, bool phControlActive);

// EC anheben mit dem Rezept: Gabe = 0,8 × Lücke / Wirkung × V, Deckel in EC
// je Runde, alle Rezeptbestandteile gemeinsam skaliert (RAT-055, RAT-054).
// Fixed additions are left out: the round and the effect count the scaled
// parts only (SD-057, RAT-085).
struct EcDose {
  bool ok = false;
  Msg reason;
  double factor = 1.0;  // gemeinsamer Skalierungsfaktor durch Deckel
  bool limited = false;  // made smaller to fit the partial-run limit (SD-102)
  std::vector<DoseStep> steps;
};
// Deckel je Runde in ml/L: maxEcStep / max(Startwirkung, Wirkung). Eine klein
// gemessene Wirkung weitet den Deckel nie auf (RAT-056).
EcDose planEcDose(const Config& cfg, const PumpMap& pumps, const RecipeCfg& recipe, double volumeL, double gapEc,
                  double effectPerMlL, double startEffect, double maxEcStep);

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

// A recipe's EC effect, per ml/L of the parts EC control scales (RAT-085). Nothing learned, it
// starts from the larger of the start effect fixed in code and the effect its reference implies
// (if within a quarter to four times that start effect), so a reference only ever makes a round
// smaller. The dose uses the learned effect within that band; the cap converts with the larger of
// the start and the dose's effect, so neither a small reference nor a small learned effect widens
// it (SD-057, SD-092, RAT-056, RAT-081).
struct RecipeEffect {
  double start = kEcStartEffect;
  double dose = kEcStartEffect;
  double largest = kEcStartEffect;
};
RecipeEffect recipeEffect(const RecipeCfg& recipe, double learned);
// The effect a fixed pattern to a target EC works from (SD-098): the reference's over all the recipe's
// parts (fixed ones taken as strong as the rest, an assumption), NaN without a reference inside the
// band; a learned effect from the band's bottom counts only where it is larger (one above the band at
// its top, RAT-056), so it only makes amounts smaller. No floor at the start effect: without feedback,
// a weak product would never reach its target.
double patternEffect(const RecipeCfg& recipe, double learned);
// A learned EC effect within a quarter to four times the start effect fixed in code; below it, or
// missing, the recipe's start (RAT-055)
double clampEcEffect(double observed, double start);
// What EC control learned for a recipe; missing = nothing learned (R5)
double learnedEcEffect(const RuntimeState& rt, const std::string& recipe);
// What an edit of a recipe keeps of its learned EC effect (SD-092, RAT-085). A change of the parts
// EC control scales or of their ratio (not one of all amounts alike or the name) scales it up by
// what is no longer scaled (fixed now or removed), at most to the band's top, so a round doses no
// more than if it were kept; then it stays only if it is at least the recipe's new start, else
// nothing is learned. A reference raised above it forgets it too.
struct EffectEdit {
  bool changed = false;  // the scaled parts or their ratio changed: a round planned on them learns nothing (RAT-057)
  double learned = kNaN;
};
EffectEdit editEcEffect(const RecipeCfg& before, const RecipeCfg& after, double learned);

}  // namespace gc
