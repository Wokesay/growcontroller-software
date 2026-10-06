#include "gc/mix.hpp"

#include <algorithm>
#include <cmath>

namespace gc {

void to_json(json& j, const DoseStep& s) {
  j = {{"canister", s.canister}, {"name", s.name},   {"pump", s.pump},   {"pair", s.pair},
       {"color", s.color},       {"mlPerL", numOrNull(s.mlPerL)},       {"ml", numOrNull(s.ml)},
       {"flowMlPerMin", numOrNull(s.flowMlPerMin)}, {"runs", s.runs}};
}

void to_json(json& j, const MixPlan& p) {
  j = {{"ok", p.ok},         {"recipe", p.recipe},   {"recipeName", p.recipeName},
       {"mode", p.mode},     {"waterL", numOrNull(p.waterL)}, {"steps", p.steps},
       {"errors", p.errors}, {"warnings", p.warnings},         {"after", p.after},
       {"totalMl", p.totalMl}, {"totalMs", p.totalMs}};
}

std::vector<Ms> splitRuns(double ml, double flowMlPerMin, const Limits& lim, int maxRuns, Msg& err) {
  if (!isNum(flowMlPerMin) || flowMlPerMin <= 0) {
    err = {"dose.no_flow", "Pumpe nicht eingemessen – ohne Einmesswert wird nicht dosiert", json::object()};
    return {};
  }
  if (!isNum(ml) || ml <= 0) {
    err = {"dose.no_amount", "Menge fehlt", json::object()};
    return {};
  }
  double totalMs = ml / flowMlPerMin * 60000.0;
  if (totalMs < lim.minRunS * 1000.0) {
    err = {"dose.too_small", fmt(ml, 1) + " ml sind unter " + fmt(lim.minRunS, 1) + " s Pumpenlauf – zu ungenau",
           {{"ml", ml}}};
    return {};
  }
  int n = static_cast<int>(std::ceil(totalMs / (lim.maxRunS * 1000.0)));
  if (maxRuns > 0 && n > maxRuns) {
    err = {"dose.too_large", fmt(ml, 1) + " ml brauchen mehr als " + std::to_string(maxRuns) + " Läufe", {{"ml", ml}}};
    return {};
  }
  std::vector<Ms> runs;
  Ms total = static_cast<Ms>(std::llround(totalMs));
  for (int i = 0; i < n; ++i) runs.push_back(total / n + (i < total % n ? 1 : 0));
  return runs;
}

MixPlan planMix(const Config& cfg, const RuntimeState& rt, const PumpMap& pumps, const MixRequest& req, Epoch now,
                bool phControlActive) {
  MixPlan p;
  p.recipe = req.recipe;
  p.mode = req.mode;
  p.waterL = req.waterL;
  auto error = [&](const std::string& key, const std::string& text, json args = json::object()) {
    p.errors.push_back({key, text, std::move(args)});
  };
  auto warn = [&](const std::string& key, const std::string& text, json args = json::object()) {
    p.warnings.push_back({key, text, std::move(args)});
  };

  const RecipeCfg* r = cfg.recipe(req.recipe);
  if (!r) {
    error("mix.no_recipe", "Rezept nicht gefunden");
    return p;
  }
  p.recipeName = r->name;
  if (req.mode != "new" && req.mode != "topup") error("mix.mode", "Unbekannter Modus");
  if (!isNum(req.waterL) || req.waterL <= 0) {
    error("mix.no_volume", "Wassermenge fehlt");  // nie mit 0 oder Ersatzwert weiter (RAT-006)
    return p;
  }
  const auto& tank = cfg.tank();
  double after = req.mode == "topup" && isNum(rt.tankVolumeL) ? rt.tankVolumeL + req.waterL : req.waterL;
  if (isNum(tank.capacityL) && after > tank.capacityL)
    error("mix.over_capacity",
          "Mehr als das Nutzvolumen des Tanks (" + fmt(tank.capacityL, 0) + " L). Menge prüfen (L oder Gallonen?)",
          {{"capacityL", tank.capacityL}});
  if (rt.lastMixAt > 0 && now - rt.lastMixAt < 30 * 60 && !req.confirmRepeat)
    error("mix.recent",
          "Dieser Tank wurde vor " + std::to_string((now - rt.lastMixAt) / 60) +
              " min gemischt. Noch einmal dosieren verdoppelt die Nährstoffe.",
          {{"minutesAgo", (now - rt.lastMixAt) / 60}});

  for (const auto& s : r->steps) {
    DoseStep d;
    d.canister = s.canister;
    d.mlPerL = s.mlPerL;
    const CanisterCfg* k = cfg.canister(s.canister);
    if (!k) {
      error("mix.canister", "Kanister im Rezept fehlt");
      continue;
    }
    d.name = k->name;
    d.pump = k->pump;
    d.pair = k->pair;
    d.color = k->color;
    if (k->kind != "nutrient") {
      error("mix.ph_in_recipe", k->name + " ist pH-Korrektur – die kommt immer zuletzt, nicht im Rezept");
      continue;
    }
    if (!isNum(s.mlPerL) || s.mlPerL <= 0) {
      error("mix.amount", k->name + ": Menge je Liter fehlt");
      continue;
    }
    d.ml = s.mlPerL * req.waterL;  // Mengen rechnen auf das frische Wasser (RAT-011)
    if (k->pump.empty()) {
      error("mix.no_pump", k->name + ": keine Pumpe zugeordnet", {{"canister", k->id}});
      p.steps.push_back(d);
      continue;
    }
    auto pit = pumps.find(k->pump);
    if (pit == pumps.end() || !pit->second.online) {
      error("mix.pump_offline", k->name + ": Pumpe nicht erkannt – Kappe gesteckt?", {{"canister", k->id}});
      p.steps.push_back(d);
      continue;
    }
    d.flowMlPerMin = pit->second.flowMlPerMin;
    Msg err;
    d.runs = splitRuns(d.ml, d.flowMlPerMin, cfg.limits, 0, err);
    if (!err.key.empty()) {
      err.text = k->name + ": " + err.text;
      err.args["canister"] = k->id;
      p.errors.push_back(err);
    }
    if (d.ml < 1.0) warn("mix.small", k->name + ": " + fmt(d.ml, 1) + " ml – unter 1 ml ungenau. Mehr Wasser oder von Hand dosieren.");
    auto st = rt.stockMl.find(k->id);
    if (st != rt.stockMl.end() && isNum(st->second) && st->second < d.ml)
      error("mix.stock", k->name + ": Vorrat reicht nicht (" + fmt(d.ml, 0) + " ml nötig, " + fmt(st->second, 0) +
                             " ml im Kanister)", {{"canister", k->id}});
    p.totalMl += d.ml;
    for (Ms m : d.runs) p.totalMs += m + 3 * kSecond;
    p.steps.push_back(d);
  }
  p.after = phControlActive
                ? Msg{"mix.after_auto", "pH zuletzt: Die pH-Regelung übernimmt nach dem Mischen.", json::object()}
                : Msg{"mix.after_manual", "pH zuletzt: Jetzt pH von Hand messen und eintragen.", json::object()};
  p.ok = p.errors.empty() && !p.steps.empty();
  return p;
}

EcDose planEcDose(const Config& cfg, const PumpMap& pumps, const RecipeCfg& recipe, double volumeL, double gapEc,
                  double effectPerMlL, double maxEcStep) {
  EcDose d;
  if (!isNum(volumeL) || volumeL <= 0) {
    d.reason = {"ec.no_volume", "Tankvolumen unbekannt", json::object()};
    return d;
  }
  if (!isNum(effectPerMlL) || effectPerMlL <= 0) {
    d.reason = {"ec.no_effect", "Wirkung unbekannt", json::object()};
    return d;
  }
  double sumMlPerL = 0;
  for (const auto& s : recipe.steps) sumMlPerL += isNum(s.mlPerL) ? s.mlPerL : 0;
  if (sumMlPerL <= 0) {
    d.reason = {"ec.recipe", "Rezept ohne Mengen", json::object()};
    return d;
  }
  double step = std::min(0.8 * gapEc, maxEcStep);
  d.factor = step / (0.8 * gapEc);
  double totalMlPerL = step / effectPerMlL;  // ml/L des ganzen Rezepts
  for (const auto& s : recipe.steps) {
    const CanisterCfg* k = cfg.canister(s.canister);
    if (!k) continue;
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
    ds.runs = splitRuns(ds.ml, ds.flowMlPerMin, cfg.limits, cfg.limits.maxPartialRuns, err);
    if (!err.key.empty()) {
      d.reason = {err.key, ds.name + ": " + err.text, err.args};
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

PhDose planPhDose(double ph, double target, double effectPerMlL, double volumeL, double maxMl) {
  PhDose d;
  if (!isNum(ph) || !isNum(target)) {
    d.reason = {"ph.no_value", "pH-Wert fehlt", json::object()};
    return d;
  }
  if (!isNum(volumeL) || volumeL <= 0) {
    d.reason = {"ph.no_volume", "Tankvolumen unbekannt", json::object()};
    return d;
  }
  if (!isNum(effectPerMlL) || effectPerMlL <= 0) effectPerMlL = 5.0;  // kleinste Dosis (RAT-050)
  double gap = ph - target;
  if (gap <= 0) {
    d.reason = {"ph.below", "pH liegt nicht über dem Ziel", json::object()};
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
