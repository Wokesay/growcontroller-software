// SPDX-License-Identifier: AGPL-3.0-or-later
#include "gc/mix.hpp"

#include <algorithm>
#include <cmath>
#include <map>

#include "gc/messages.hpp"

namespace gc {

void to_json(json& j, const DoseStep& s) {
  j = {{"canister", s.canister}, {"name", s.name},   {"pump", s.pump},   {"pair", s.pair},
       {"color", s.color},       {"mlPerL", numOrNull(s.mlPerL)},       {"ml", numOrNull(s.ml)},
       {"flowMlPerMin", numOrNull(s.flowMlPerMin)}, {"runs", s.runs}, {"fixed", s.fixed}};
}

void to_json(json& j, const MixPlan& p) {
  j = {{"ok", p.ok},         {"recipe", p.recipe},   {"recipeName", p.recipeName},
       {"mode", p.mode},     {"waterL", numOrNull(p.waterL)}, {"steps", p.steps},
       {"errors", p.errors}, {"warnings", p.warnings},         {"after", p.after},
       {"totalMl", p.totalMl}, {"totalMs", p.totalMs},
       {"amounts", p.amounts}, {"targetEc", numOrNull(p.targetEc)}, {"waterEc", numOrNull(p.waterEc)},
       {"factor", numOrNull(p.factor)}, {"expectedEc", numOrNull(p.expectedEc)}, {"infos", p.infos}};
}

std::vector<Ms> splitRuns(double ml, double flowMlPerMin, const Limits& limits, int maxRuns, Msg& err) {
  const Limits lim = limits.bounded();
  if (!isNum(flowMlPerMin) || flowMlPerMin <= 0) {
    err = say("dose.no_flow");
    return {};
  }
  if (!isNum(ml) || ml <= 0) {
    err = say("dose.no_amount");
    return {};
  }
  double totalMs = ml / flowMlPerMin * 60000.0;
  if (totalMs < lim.minRunS * 1000.0) {
    err = say("dose.too_small", {{"ml", ml}, {"s", lim.minRunS}});
    return {};
  }
  constexpr int kMaxRunsPerStep = 200;  // Schutz gegen unsinnige Mengen, auch beim Mischen
  const int cap = maxRuns > 0 ? std::min(maxRuns, kMaxRunsPerStep) : kMaxRunsPerStep;
  const double needed = std::ceil(totalMs / (lim.maxRunS * 1000.0));
  const int n = needed > cap ? cap + 1 : static_cast<int>(needed);
  if (n > cap) {
    err = say("dose.too_large", {{"ml", ml}, {"runs", cap}});
    return {};
  }
  std::vector<Ms> runs;
  Ms total = static_cast<Ms>(std::llround(totalMs));
  for (int i = 0; i < n; ++i) runs.push_back(total / n + (i < total % n ? 1 : 0));
  return runs;
}

// The charts' amounts are made for water up to about this EC (SD-057, sources [1][2] there); above it, a new
// fill gets a note that part of the target comes from the water
constexpr double kChartWaterEcMax = 0.4;

MixPlan planMix(const Config& cfg, const RuntimeState& rt, const PumpMap& pumps, const MixRequest& req, Epoch now,
                bool phControlActive) {
  MixPlan p;
  p.recipe = req.recipe;
  p.mode = req.mode;
  p.waterL = req.waterL;

  // Each message in both languages by its key (SD-032); the hub's text is English
  const RecipeCfg* r = cfg.recipe(req.recipe);
  if (!r) {
    p.errors.push_back(say("mix.no_recipe"));
    return p;
  }
  p.recipeName = r->name;
  if (req.mode != "new" && req.mode != "topup") p.errors.push_back(say("mix.mode"));
  if (!isNum(req.waterL) || req.waterL <= 0) {
    p.errors.push_back(say("mix.no_volume"));  // never on with 0 or a stand-in (RAT-006)
    return p;
  }
  // A fixed pattern to a target EC (SD-057 way 2, SD-098): every scaled part by one factor, at most 1, so
  // never more than the recipe's own amounts; fixed parts per litre of fresh water; no feedback
  p.amounts = req.amounts;
  double factor = 1.0;
  if (req.amounts == "target") {
    p.targetEc = req.targetEc;
    p.waterEc = req.waterEc;
    p.factor = kNaN;  // worked out below; a refused plan has none (R5)
    double scaled = 0, fixedMl = 0;
    for (const auto& s : r->steps)
      if (isNum(s.mlPerL) && s.mlPerL > 0) (s.fixed ? fixedMl : scaled) += s.mlPerL;
    const double e = patternEffect(*r, learnedEcEffect(rt, r->id));
    const bool inRange = isNum(req.targetEc) && (!isNum(req.targetMin) || req.targetEc >= req.targetMin) &&
                         (!isNum(req.targetMax) || req.targetEc <= req.targetMax);
    // EC control would take the tank to its own target afterwards, over the one asked for (SD-089 S20)
    if (req.ecControlOn) p.errors.push_back(say("mix.target_ec_control", {{"target", numOrNull(req.ecControlTarget)}}));
    if (!isNum(e)) p.errors.push_back(say("mix.no_reference", {{"name", r->name}}));
    if (!inRange) p.errors.push_back(say("mix.target_range", {{"min", numOrNull(req.targetMin)}, {"max", numOrNull(req.targetMax)}}));
    if (!isNum(req.waterEc) || req.waterEc < 0)
      p.errors.push_back(say("mix.water_ec_missing"));  // never taken as 0 (R5)
    else if (isNum(req.targetMax) && req.waterEc > req.targetMax)  // a value in µS/cm, most likely
      p.errors.push_back(say("mix.water_ec_unit", {{"ec", req.waterEc}, {"max", req.targetMax}}));
    if (!p.errors.empty()) return p;
    const double gap = req.targetEc - req.waterEc - fixedMl * e;
    if (gap <= 0) {
      factor = 0;
      if (fixedMl <= 0) {
        p.errors.push_back(say("mix.at_target", {{"water", req.waterEc}, {"target", req.targetEc}}));
        return p;
      }
      p.warnings.push_back(say("mix.at_target_fixed", {{"water", req.waterEc}, {"target", req.targetEc}}));
    } else {
      factor = std::min(1.0, gap / e / scaled);
    }
    p.factor = factor;
    p.expectedEc = req.waterEc + e * (scaled * factor + fixedMl);
    if (gap / e / scaled > 1.0 + 1e-9) p.warnings.push_back(say("mix.capped", {{"target", req.targetEc}}));
    if (req.mode == "new" && req.waterEc > kChartWaterEcMax) p.warnings.push_back(say("mix.water_ec_high", {{"ec", req.waterEc}}));
    p.infos.push_back(say("mix.no_feedback"));
    // Below the pH lock's EC pH control never doses (RAT-046): then the pH is corrected by hand
    const bool phGate = phControlActive && isNum(req.phEcFloor) && p.expectedEc < req.phEcFloor;
    if (phGate)
      p.warnings.push_back(say("mix.ph_gate", {{"ec", p.expectedEc}, {"floor", req.phEcFloor}}));
    else if (phControlActive)
      p.infos.push_back(say("mix.ph_adds_ec"));
    p.after = say(phControlActive && !phGate ? "mix.after_auto_target" : "mix.after_manual_target");
  } else {
    if (req.amounts != "recipe") p.errors.push_back(say("mix.amounts"));
    p.after = say(phControlActive ? "mix.after_auto" : "mix.after_manual");
  }
  const auto& tank = cfg.tank();
  double after = req.mode == "topup" && isNum(rt.tankVolumeL) ? rt.tankVolumeL + req.waterL : req.waterL;
  if (isNum(tank.capacityL) && after > tank.capacityL)
    p.errors.push_back(say("mix.over_capacity", {{"capacityL", tank.capacityL}}));
  if (rt.lastMixAt > 0 && now - rt.lastMixAt < 30 * 60 && !req.confirmRepeat)
    p.errors.push_back(say("mix.recent", {{"minutesAgo", (now - rt.lastMixAt) / 60}}));

  for (const auto& s : r->steps) {
    DoseStep d;
    d.canister = s.canister;
    d.mlPerL = s.mlPerL;
    d.fixed = s.fixed;
    const CanisterCfg* k = cfg.canister(s.canister);
    if (!k) {
      p.errors.push_back(say("mix.canister"));
      continue;
    }
    d.name = k->name;
    d.pump = k->pump;
    d.pair = k->pair;
    d.color = k->color;
    if (k->kind != "nutrient") {
      p.errors.push_back(say("mix.ph_in_recipe", {{"name", k->name}, {"canister", k->id}}));
      continue;
    }
    if (!isNum(s.mlPerL) || s.mlPerL <= 0) {
      p.errors.push_back(say("mix.amount_missing", {{"name", k->name}, {"canister", k->id}}));
      continue;
    }
    d.ml = s.mlPerL * (s.fixed ? 1.0 : factor) * req.waterL;  // amounts count on the fresh water (RAT-011)
    if (!s.fixed && factor == 0) continue;  // a scaled part with nothing to add: the water is at the target (SD-098)
    if (k->pump.empty()) {
      p.errors.push_back(say("mix.no_pump", {{"name", k->name}, {"canister", k->id}}));
      p.steps.push_back(d);
      continue;
    }
    auto pit = pumps.find(k->pump);
    if (pit == pumps.end() || !pit->second.online) {
      p.errors.push_back(say("mix.pump_offline", {{"name", k->name}, {"canister", k->id}}));
      p.steps.push_back(d);
      continue;
    }
    d.flowMlPerMin = pit->second.flowMlPerMin;
    Msg err;
    d.runs = splitRuns(d.ml, d.flowMlPerMin, cfg.limits, 0, err);
    if (!err.key.empty()) p.errors.push_back(say("mix.part", {{"name", k->name}, {"reason", err}, {"canister", k->id}}));
    if (d.ml < 1.0) p.warnings.push_back(say("mix.small", {{"name", k->name}, {"ml", d.ml}, {"canister", k->id}}));
    auto st = rt.stockMl.find(k->id);
    if (st != rt.stockMl.end() && isNum(st->second) && st->second < 0.0)  // used up by the count (SD-066)
      p.errors.push_back(say("mix.stock_negative", {{"name", k->name}, {"canister", k->id}}));
    else if (st != rt.stockMl.end() && isNum(st->second) && st->second < d.ml)
      p.errors.push_back(say("mix.stock", {{"name", k->name}, {"need", d.ml}, {"have", st->second}, {"canister", k->id}}));
    p.totalMl += d.ml;
    for (Ms m : d.runs) p.totalMs += m + 3 * kSecond;
    p.steps.push_back(d);
  }
  p.ok = p.errors.empty() && !p.steps.empty();
  return p;
}

EcDose planEcDose(const Config& cfg, const PumpMap& pumps, const RecipeCfg& recipe, double volumeL, double gapEc,
                  double effectPerMlL, double startEffect, double maxEcStep) {
  EcDose d;
  if (!isNum(volumeL) || volumeL <= 0) {
    d.reason = say("ec.no_volume");
    return d;
  }
  if (!isNum(effectPerMlL) || effectPerMlL <= 0) {
    d.reason = say("ec.effect_unknown");
    return d;
  }
  // Only the parts EC control scales; a fixed addition goes in per litre of fresh water (RAT-085)
  double sumMlPerL = 0;
  bool anyFixed = false;
  for (const auto& s : recipe.steps) {
    anyFixed |= s.fixed;
    sumMlPerL += !s.fixed && isNum(s.mlPerL) ? s.mlPerL : 0;
  }
  if (sumMlPerL <= 0) {
    d.reason = anyFixed ? say("ec.recipe_no_scaled", {{"name", recipe.name}}) : say("ec.recipe");
    return d;
  }
  if (!isNum(gapEc) || gapEc <= 0) {
    d.reason = say("mix.ec_reached");
    return d;
  }
  if (!isNum(maxEcStep) || maxEcStep <= 0) maxEcStep = 1.0;  // Katalog-Vorgabe, nie ohne Deckel
  if (!isNum(startEffect) || startEffect <= 0) startEffect = kEcStartEffect;
  const double rawMlPerL = 0.8 * gapEc / effectPerMlL;
  // Never wider than at the start effect fixed in code, whatever comes in (RAT-056)
  const double capMlPerL = maxEcStep / std::max({kEcStartEffect, startEffect, effectPerMlL});
  const double totalMlPerL = std::min(rawMlPerL, capMlPerL);  // ml/L des ganzen Rezepts
  d.factor = totalMlPerL / rawMlPerL;
  for (const auto& s : recipe.steps) {
    const CanisterCfg* k = cfg.canister(s.canister);
    if (!k || s.fixed) continue;
    DoseStep ds;
    ds.canister = k->id;
    ds.name = k->name;
    ds.pump = k->pump;
    ds.pair = k->pair;
    ds.color = k->color;
    ds.mlPerL = s.mlPerL;
    ds.ml = totalMlPerL * (s.mlPerL / sumMlPerL) * volumeL;
    auto pit = pumps.find(k->pump);
    ds.flowMlPerMin = pit == pumps.end() ? kNaN : pit->second.flowMlPerMin;
    d.steps.push_back(ds);
  }
  // Teilläufe: ist ein Bestandteil zu klein oder zu groß, gilt das für alle
  // gemeinsam (nie einzeln kappen, RAT-054).
  for (auto& ds : d.steps) {
    Msg err;
    ds.runs = splitRuns(ds.ml, ds.flowMlPerMin, cfg.limits, cfg.limits.bounded().maxPartialRuns, err);
    if (!err.key.empty()) {
      d.reason = say("mix.part", {{"name", ds.name}, {"reason", err}, {"canister", ds.canister}});
      d.steps.clear();
      return d;
    }
  }
  d.ok = !d.steps.empty();
  return d;
}

double clampEffect(double observed, double start) {
  if (!isNum(observed) || observed <= 0) return start;
  if (observed < 0.25 * start) return start;
  if (observed > 4.0 * start) return 4.0 * start;
  return observed;
}

namespace {
// The parts EC control scales, by canister, as validateConfig counts them (RAT-085)
std::map<std::string, double> scaledParts(const RecipeCfg& recipe) {
  std::map<std::string, double> parts;
  for (const auto& s : recipe.steps)
    if (!s.fixed && isNum(s.mlPerL) && s.mlPerL > 0) parts[s.canister] += s.mlPerL;
  return parts;
}
double sumOf(const std::map<std::string, double>& parts) {
  double sum = 0;
  for (const auto& [can, ml] : parts) sum += ml;
  return sum;
}
}  // namespace

RecipeEffect recipeEffect(const RecipeCfg& recipe, double learned) {
  RecipeEffect e;
  const double sum = sumOf(scaledParts(recipe));
  // A reference outside the band (a file edited by hand, an import) counts as none
  const double ref = isNum(recipe.refEc) && recipe.refEc > 0 && sum > 0 ? recipe.refEc / sum : kNaN;
  if (isNum(ref) && ref >= kRefEffectMin * kEcStartEffect && ref <= kRefEffectMax * kEcStartEffect)
    e.start = std::max(kEcStartEffect, ref);
  e.dose = clampEcEffect(learned, e.start);
  // A stored value above the band counts at its top, as one learned there: the cap never shrinks so far
  // that no round fits and nothing could be learned again (RAT-056)
  e.largest = std::max(e.start, e.dose);
  return e;
}

double patternEffect(const RecipeCfg& recipe, double learned) {
  double scaled = 0, all = 0;
  for (const auto& s : recipe.steps)
    if (isNum(s.mlPerL) && s.mlPerL > 0) {
      all += s.mlPerL;
      if (!s.fixed) scaled += s.mlPerL;
    }
  // The reference must lie in the band over the scaled parts, as validateConfig checks it (RAT-056)
  if (!isNum(recipe.refEc) || recipe.refEc <= 0 || !isNum(all) || scaled <= 0) return kNaN;
  const double band = recipe.refEc / scaled;
  if (band < kRefEffectMin * kEcStartEffect || band > kRefEffectMax * kEcStartEffect) return kNaN;
  double e = recipe.refEc / all;
  // A learned value counts from the band's bottom; one stored above it counts at its top, as for a round (RAT-056)
  if (isNum(learned) && learned >= kRefEffectMin * kEcStartEffect) e = std::max(e, std::min(learned, kRefEffectMax * kEcStartEffect));
  return isNum(e) && e > 0 ? e : kNaN;
}

double clampEcEffect(double observed, double start) {
  if (!isNum(observed) || observed < kRefEffectMin * kEcStartEffect) return start;
  return std::min(observed, kRefEffectMax * kEcStartEffect);
}

double learnedEcEffect(const RuntimeState& rt, const std::string& recipe) {
  auto it = rt.ecEffects.find(recipe);
  return it == rt.ecEffects.end() ? kNaN : it->second;
}

EffectEdit editEcEffect(const RecipeCfg& before, const RecipeCfg& after, double learned) {
  EffectEdit e;
  const auto was = scaledParts(before), now = scaledParts(after);
  const double sumWas = sumOf(was), sumNow = sumOf(now);
  // The same parts in the same ratio (all amounts alike, or only the name or the reference): unchanged
  bool same = was.size() == now.size() && sumWas > 0 && sumNow > 0;
  for (auto w = was.begin(), n = now.begin(); same && w != was.end(); ++w, ++n)
    same = w->first == n->first && std::fabs(w->second / sumWas - n->second / sumNow) < 1e-9;
  if (same || (was.empty() && now.empty())) {
    // A higher reference on the same parts may be a stronger product in the same bottles: what was
    // learned below its new start is forgotten (SD-092)
    const double startNow = recipeEffect(after, kNaN).start;
    if (!(isNum(learned) && learned < startNow && startNow > recipeEffect(before, kNaN).start)) e.learned = learned;
    return e;
  }
  e.changed = true;
  if (!isNum(learned)) return e;
  double gone = 0;  // what was scaled and is no longer: fixed now or removed
  for (const auto& [can, ml] : was)
    if (!now.count(can)) gone += ml;
  if (gone >= sumWas || sumNow <= 0) return e;  // nothing it was learned on is scaled any more
  const double scaled = std::min(learned * sumWas / (sumWas - gone), kRefEffectMax * kEcStartEffect);
  if (scaled >= recipeEffect(after, kNaN).start) e.learned = scaled;
  return e;
}

PhDose planPhDose(double ph, double target, double effectPerMlL, double volumeL, double maxMl) {
  PhDose d;
  if (!isNum(ph) || !isNum(target)) {
    d.reason = say("ph.no_value");
    return d;
  }
  if (!isNum(volumeL) || volumeL <= 0) {
    d.reason = say("ph.no_volume");
    return d;
  }
  if (!isNum(effectPerMlL) || effectPerMlL <= 0) effectPerMlL = 5.0;  // kleinste Dosis (RAT-050)
  double gap = ph - target;
  if (gap <= 0) {
    d.reason = say("mix.ph_reached");
    return d;
  }
  d.rawMl = 0.8 * gap / effectPerMlL * volumeL;
  double capEffect = 0.3 / effectPerMlL * volumeL;
  double capPerL = 0.3 * volumeL;
  d.capMl = std::min({capEffect, capPerL, isNum(maxMl) ? maxMl : 20.0});
  d.ml = std::min(d.rawMl, d.capMl);
  d.ok = true;
  return d;
}

}  // namespace gc
