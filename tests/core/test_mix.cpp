// SPDX-License-Identifier: AGPL-3.0-or-later
#include <doctest/doctest.h>

#include <algorithm>
#include <limits>
#include <set>
#include <utility>
#include <vector>

#include "gc/mix.hpp"

using namespace gc;

namespace {
struct Fix {
  Config cfg;
  RuntimeState rt;
  PumpMap pumps;
  Fix() {
    cfg.devices = {{"C1", "pump_cap", ""}, {"C2", "pump_cap", ""}, {"C3", "pump_cap", ""}, {"C4", "pump_cap", ""}};
    cfg.canisters = {{"a", "Teil A", "nutrient", "C1", "AB", "", kNaN},
                     {"b", "Teil B", "nutrient", "C2", "AB", "", kNaN},
                     {"cm", "CalMag", "nutrient", "C3", "", "", kNaN},
                     {"down", "pH−", "ph_down", "C4", "", "", kNaN}};
    cfg.recipes = {{"r", "Test", "", {{"a", 2.0}, {"b", 1.0}}}};
    cfg.tank().capacityL = 60;
    for (const char* id : {"C1", "C2", "C3", "C4"}) pumps[id] = {id, true, 50.0, ""};
  }
  MixPlan plan(double water, bool confirm = false) {
    return planMix(cfg, rt, pumps, {"r", water, "new", confirm}, 1790000000, false);
  }
};
}  // namespace

TEST_CASE("Mischen: Paar 2:1 ohne Rundung (M1-2)") {
  Fix f;
  auto p = f.plan(3.0);
  REQUIRE(p.ok);
  CHECK(p.steps[0].ml == doctest::Approx(6.0));
  CHECK(p.steps[1].ml == doctest::Approx(3.0));
  CHECK(p.after.key == "mix.after_manual");  // pH zuletzt, von Hand
}

TEST_CASE("Mischen: Wassermenge fehlt → nichts geplant (M1-4, RAT-006)") {
  Fix f;
  auto p = f.plan(kNaN);
  CHECK_FALSE(p.ok);
  CHECK(p.steps.empty());
  CHECK(p.errors[0].key == "mix.no_volume");
}

TEST_CASE("Mischen: ohne Einmesswert kein Auftrag (M3-4, strenger als RAT-015)") {
  Fix f;
  f.pumps["C2"].flowMlPerMin = kNaN;
  auto p = f.plan(10);
  CHECK_FALSE(p.ok);
  bool found = false;  // the bottle's own error, with the pump's reason (#73)
  for (const auto& e : p.errors) found = found || (e.key == "mix.part" && e.args["reason"]["key"] == "dose.no_flow");
  CHECK(found);
}

TEST_CASE("Mix plan: every error and warning comes in English by its key, with the bottle's name (#73, SD-083)") {
  Fix f;
  auto all = [](const MixPlan& p) {
    std::vector<Msg> m = p.errors;
    m.insert(m.end(), p.warnings.begin(), p.warnings.end());
    return m;
  };
  auto text = [&](const MixPlan& p, const std::string& key) {
    for (const auto& e : all(p))
      if (e.key == key) return e.text;
    return std::string("(no " + key + ")");
  };
  CHECK(text(f.plan(80), "mix.over_capacity") == "More than the tank holds (60 L). Check the amount (litres or gallons?)");
  f.rt.lastMixAt = 1790000000 - 600;
  CHECK(text(f.plan(10), "mix.recent") == "This tank was mixed 10\u00a0min ago. Dosing again doubles the nutrients.");
  f.rt.lastMixAt = 0;
  f.rt.stockMl["a"] = 5;
  CHECK(text(f.plan(10), "mix.stock") == "Teil A: not enough in the bottle (20 ml needed, 5 ml left) – refill it, then tap \"Bottle changed (full)\"");
  f.rt.stockMl.erase("a");
  CHECK(text(f.plan(0.4), "mix.small") == "Teil A: 0.8 ml – under 1 ml is not exact. Use more water or dose by hand.");
  f.pumps["C2"].online = false;
  CHECK(text(f.plan(10), "mix.pump_offline") == "Teil B: pump not found – plugged into the dosing block?");
  f.pumps["C2"].online = true;
  f.pumps["C1"].flowMlPerMin = kNaN;
  CHECK(text(f.plan(10), "mix.part") == "Teil A: Pump not calibrated – calibrate it under Devices first");
  f.pumps["C1"].flowMlPerMin = 50.0;
  f.cfg.recipes[0].steps.push_back({"down", 0.1});
  CHECK(text(f.plan(10), "mix.ph_in_recipe") == "pH− is a pH correction – it always comes last, not in the recipe");
  f.cfg.recipes[0].steps = {{"a", kNaN}, {"missing", 1.0}};
  CHECK(text(f.plan(10), "mix.amount_missing") == "Teil A: amount per litre missing");
  CHECK(text(f.plan(10), "mix.canister") == "A bottle of the recipe is missing – edit the recipe");
  CHECK(text(f.plan(kNaN), "mix.no_volume") == "Amount of water missing");
  CHECK(planMix(f.cfg, f.rt, f.pumps, {"none", 10.0, "new", false}, 1790000000, false).errors.at(0).text == "Recipe not found");
  // the reasons a dose cannot be split into runs, with the numbers they name
  Limits lim;
  Msg err;
  splitRuns(0.5, 50, lim, 0, err);
  CHECK(err.text == "0.5 ml is under 1.0\u00a0s of pumping – not exact enough");
  CHECK(err.args["s"] == doctest::Approx(1.0));
  splitRuns(500, 50, lim, 6, err);
  CHECK(err.text == "500.0 ml would need more than 6 runs – check the amount");
  splitRuns(10, kNaN, lim, 0, err);
  CHECK(err.text == "Pump not calibrated – calibrate it under Devices first");
  CHECK(planPhDose(kNaN, 5.8, 5.0, 20, 1.0).reason.text == "pH value missing");
  CHECK(planPhDose(6.5, 5.8, 5.0, kNaN, 1.0).reason.text == "Tank volume unknown");
  CHECK(planPhDose(5.5, 5.8, 5.0, 20, 1.0).reason.text == "pH is not above the target");
  // a key the table has, never German text written next to it
  f.cfg.recipes[0].steps = {{"a", 2.0}, {"b", 1.0}};
  f.pumps["C1"].flowMlPerMin = kNaN;
  f.rt.stockMl["b"] = 1;
  for (double water : {80.0, 10.0, 0.4})
    for (const auto& m : all(f.plan(water))) {
      INFO(m.key << ": " << m.text);
      for (const char* german : {"Pumpe", "fehlt", "Vorrat", "Menge", "Kanister", "nicht", " ist "})
        CHECK(m.text.find(german) == std::string::npos);
    }
}

TEST_CASE("EC dose: a part that cannot be split names its bottle, in both languages by key (#73, RAT-054)") {
  Fix f;
  f.cfg.recipes[0] = {"r", "Test", "", {{"a", 1.0}, {"b", 1.0}}};
  f.pumps["C2"].flowMlPerMin = kNaN;
  auto d = planEcDose(f.cfg, f.pumps, f.cfg.recipes[0], 20, 1.0, 0.275, 0.275, 1.0);
  CHECK_FALSE(d.ok);
  CHECK(d.steps.empty());  // all or none (RAT-054)
  CHECK(d.reason.key == "mix.part");
  CHECK(d.reason.args["name"] == "Teil B");
  CHECK(d.reason.args["canister"] == "b");
  CHECK(d.reason.args["reason"]["key"] == "dose.no_flow");
  CHECK(d.reason.text == "Teil B: Pump not calibrated – calibrate it under Devices first");
}

TEST_CASE("Mischen: pH im Rezept wird abgelehnt (M1-3, RAT-004)") {
  Fix f;
  f.cfg.recipes[0].steps.push_back({"down", 0.1});
  auto p = f.plan(10);
  CHECK_FALSE(p.ok);
}

TEST_CASE("Mischen: Nutzvolumen, Doppelstart, Vorrat") {
  Fix f;
  CHECK(f.plan(80).errors[0].key == "mix.over_capacity");
  f.rt.lastMixAt = 1790000000 - 600;
  CHECK_FALSE(f.plan(10).ok);
  CHECK(f.plan(10, true).ok);  // nach ausdrücklicher Bestätigung
  f.rt.lastMixAt = 0;
  f.rt.stockMl["a"] = 5;
  CHECK_FALSE(f.plan(10).ok);
  // used up by the count: refused with what to do, not with a negative amount in the bottle (SD-066)
  f.rt.stockMl["a"] = -2;
  auto p = f.plan(10);
  CHECK_FALSE(p.ok);
  bool negative = false;
  for (const auto& e : p.errors) negative = negative || (e.key == "mix.stock_negative" && e.args["canister"] == "a");
  CHECK(negative);
}

TEST_CASE("Teilläufe: gleich groß, Untergrenze 1 s (M2-6, RAT-050/RAT-055)") {
  Limits lim;
  Msg err;
  auto r = splitRuns(100, 50, lim, 0, err);  // 120 s
  REQUIRE(r.size() == 2);
  CHECK(r[0] == 60000);
  CHECK(r[1] == 60000);
  splitRuns(0.5, 50, lim, 0, err);  // 0,6 s
  CHECK(err.key == "dose.too_small");
  err = {};
  splitRuns(500, 50, lim, 6, err);  // 10 Läufe > 6
  CHECK(err.key == "dose.too_large");
}

TEST_CASE("EC-Gabe: 0,8 × Lücke, gemeinsam skaliert (M5-1, RAT-054/RAT-055)") {
  Fix f;
  f.cfg.recipes[0] = {"r", "Test", "", {{"a", 1.0}, {"b", 1.0}}};
  auto d = planEcDose(f.cfg, f.pumps, f.cfg.recipes[0], 20, 1.0, 0.275, 0.275, 1.0);
  REQUIRE(d.ok);
  CHECK(d.steps[0].ml + d.steps[1].ml == doctest::Approx(58.18).epsilon(0.001));
  CHECK(d.steps[0].ml == doctest::Approx(d.steps[1].ml));
  // Deckel 0,4 mS/cm: beide gemeinsam gekürzt, Verhältnis bleibt
  auto c = planEcDose(f.cfg, f.pumps, f.cfg.recipes[0], 20, 1.0, 0.275, 0.275, 0.4);
  REQUIRE(c.ok);
  CHECK(c.factor == doctest::Approx(0.5));
  CHECK(c.steps[0].ml / c.steps[1].ml == doctest::Approx(1.0));
}

TEST_CASE("EC-Deckel: kleine gelernte Wirkung weitet ihn nicht auf (M5-4, RAT-056)") {
  Fix f;
  f.cfg.recipes[0] = {"r", "Test", "", {{"a", 1.0}, {"b", 1.0}}};
  // Wirkung 0,3 × Start: roh 0,8 / 0,0825 = 9,7 ml/L; Deckel 1,0 / max(0,275; 0,0825) = 3,64 ml/L
  auto d = planEcDose(f.cfg, f.pumps, f.cfg.recipes[0], 20, 1.0, 0.0825, 0.275, 1.0);
  REQUIRE(d.ok);
  CHECK(d.steps[0].ml + d.steps[1].ml == doctest::Approx(1.0 / 0.275 * 20).epsilon(0.001));
  CHECK(d.steps[0].ml == doctest::Approx(d.steps[1].ml));
  // Fehlender Deckel heißt nie „ohne Deckel“
  auto n = planEcDose(f.cfg, f.pumps, f.cfg.recipes[0], 20, 5.0, 0.275, 0.275, kNaN);
  REQUIRE(n.ok);
  CHECK(n.steps[0].ml + n.steps[1].ml <= doctest::Approx(1.0 / 0.275 * 20));
}

TEST_CASE("EC effect of a recipe: a reference only ever makes a round smaller; the cap from the largest of start, reference and learned (SD-057, RAT-056, RAT-081)") {
  RecipeCfg r{"r", "Test", "", {{"a", 2.0}, {"b", 2.0}, {"cm", 1.0, true}}};
  SUBCASE("no reference, nothing learned: the start effect fixed in code") {
    const auto e = recipeEffect(r, kNaN);
    CHECK(e.start == doctest::Approx(kEcStartEffect));
    CHECK(e.dose == doctest::Approx(kEcStartEffect));
    CHECK(e.largest == doctest::Approx(kEcStartEffect));
  }
  SUBCASE("a strong reference: its effect over the scaled parts is the start and the cap's divisor") {
    r.refEc = 2.0;  // 2.0 / 4 ml/L = 0.5, the fixed part left out (RAT-085)
    const auto e = recipeEffect(r, kNaN);
    CHECK(e.start == doctest::Approx(0.5));
    CHECK(e.dose == doctest::Approx(0.5));
    CHECK(e.largest == doctest::Approx(0.5));
  }
  SUBCASE("a weak reference, or a slip like 1.2 typed for 2.1, never makes a round larger") {
    for (double ref : {0.6, 1.2}) {  // 0.15 and 0.3 per ml/L, the first below the start effect
      r.refEc = ref;
      const auto e = recipeEffect(r, kNaN);
      CHECK(e.start >= kEcStartEffect);
      CHECK(e.dose >= kEcStartEffect);
    }
    r.refEc = 0.6;
    CHECK(recipeEffect(r, kNaN).start == doctest::Approx(kEcStartEffect));
  }
  SUBCASE("a reference outside the band, as a file edited by hand may bring, counts as none") {
    for (double ref : {0.2, 5.0, -1.0, 0.0, std::numeric_limits<double>::infinity()}) {
      r.refEc = ref;
      const auto e = recipeEffect(r, kNaN);
      CHECK(e.start == doctest::Approx(kEcStartEffect));
      CHECK(e.largest == doctest::Approx(kEcStartEffect));
    }
  }
  SUBCASE("learned: the dose uses it within a quarter to four times the start effect fixed in code, below it the recipe's start") {
    r.refEc = 1.2;  // 0.3 per ml/L
    auto e = recipeEffect(r, 0.45);
    CHECK(e.dose == doctest::Approx(0.45));
    CHECK(e.largest == doctest::Approx(0.45));
    e = recipeEffect(r, 0.1);  // weaker than the reference, within the band: what was measured
    CHECK(e.dose == doctest::Approx(0.1));
    CHECK(e.largest == doctest::Approx(0.3));
    e = recipeEffect(r, 0.05);  // below a quarter of the start effect: the recipe's start
    CHECK(e.dose == doctest::Approx(0.3));
    CHECK(e.largest == doctest::Approx(0.3));
    e = recipeEffect(r, 3.0);  // above four times (a file edited by hand): four times, for the dose and the cap,
    CHECK(e.dose == doctest::Approx(4 * kEcStartEffect));  // so the cap never shrinks to where no round fits
    CHECK(e.largest == doctest::Approx(4 * kEcStartEffect));
  }
  SUBCASE("a reference at the band's low end does not keep a strong product from being learned") {
    r.refEc = 0.28;  // 0.07 per ml/L
    CHECK(clampEcEffect(0.495, recipeEffect(r, kNaN).start) == doctest::Approx(0.495));
    CHECK(clampEcEffect(2.0, recipeEffect(r, kNaN).start) == doctest::Approx(4 * kEcStartEffect));
  }
  SUBCASE("only fixed parts or no amounts: the reference implies nothing") {
    RecipeCfg f{"f", "Fixed", "", {{"cm", 1.0, true}}};
    f.refEc = 1.0;
    CHECK(recipeEffect(f, kNaN).start == doctest::Approx(kEcStartEffect));
  }
}

TEST_CASE("EC effect after an edit: scaled up by parts no longer scaled, kept only at or above the new start; never a larger round than keeping or forgetting it (SD-057, RAT-085)") {
  const RecipeCfg before{"r", "Test", "", {{"a", 2.0}, {"b", 2.0}, {"cm", 0.6}}};
  SUBCASE("the same parts in the same ratio: unchanged, also with a new name or a reference below it") {
    RecipeCfg after = before;
    for (auto& s : after.steps) s.mlPerL *= 1.5;
    after.name = "Renamed";
    after.refEc = 2.0;  // 0.29 per ml/L of 6.9
    const auto e = editEcEffect(before, after, 0.32);
    CHECK_FALSE(e.changed);
    CHECK(e.learned == doctest::Approx(0.32));
    // Below the start effect, and the start unchanged: what was measured stays
    RecipeCfg renamed = before;
    renamed.name = "Renamed";
    CHECK(editEcEffect(before, renamed, 0.2).learned == doctest::Approx(0.2));
  }
  SUBCASE("a reference raised above it, on the same parts: forgotten, as a stronger product in the same bottles may be") {
    RecipeCfg after = before;
    after.refEc = 2.3;  // 0.5 per ml/L of 4.6
    const auto e = editEcEffect(before, after, 0.3);
    CHECK_FALSE(e.changed);  // the round's parts are the same: what it measures holds
    CHECK(std::isnan(e.learned));
    CHECK(editEcEffect(before, after, 0.6).learned == doctest::Approx(0.6));  // above the new start: kept
  }
  SUBCASE("a part fixed or removed: scaled up by what left") {
    RecipeCfg fixed = before;
    fixed.steps[2].fixed = true;
    auto e = editEcEffect(before, fixed, 0.3);
    CHECK(e.changed);
    CHECK(e.learned == doctest::Approx(0.3 * 4.6 / 4.0));
    RecipeCfg removed = before;
    removed.steps.pop_back();
    e = editEcEffect(before, removed, 0.3);
    CHECK(e.changed);
    CHECK(e.learned == doctest::Approx(0.3 * 4.6 / 4.0));
    // At most to the band's top, so the cap never shrinks to where no round fits
    RecipeCfg dominant = before;
    dominant.steps[0].fixed = true;
    CHECK(editEcEffect(before, dominant, 1.0).learned == doctest::Approx(4 * kEcStartEffect));
  }
  SUBCASE("a ratio moved towards a stronger part is not bounded by the edit: kept as learned, the next round shows it") {
    const RecipeCfg was{"r", "Test", "", {{"a", 1.0}, {"b", 3.0}}};
    const RecipeCfg now{"r", "Test", "", {{"a", 3.0}, {"b", 1.0}}};
    const auto e = editEcEffect(was, now, 0.4);
    CHECK(e.changed);
    CHECK(e.learned == doctest::Approx(0.4));
  }
  SUBCASE("a new ratio: kept when at least the start, forgotten when below") {
    RecipeCfg after = before;
    after.steps[0].mlPerL = 3.0;
    auto e = editEcEffect(before, after, 0.5);
    CHECK(e.changed);
    CHECK(e.learned == doctest::Approx(0.5));
    e = editEcEffect(before, after, 0.2);
    CHECK(e.changed);
    CHECK(std::isnan(e.learned));
  }
  SUBCASE("nothing learned stays nothing learned; nothing scaled any more forgets it") {
    RecipeCfg after = before;
    after.steps[0].mlPerL = 3.0;
    CHECK(std::isnan(editEcEffect(before, after, kNaN).learned));
    RecipeCfg none{"r", "Test", "", {{"cm", 0.6, true}}};
    CHECK(std::isnan(editEcEffect(before, none, 0.5).learned));
  }
  SUBCASE("never a larger round than keeping it or forgetting it, nor a looser cap") {
    Fix f;
    for (double learned : {0.1, 0.2, 0.3, 0.5, 1.0}) {
      for (double ref : {kNaN, 1.2, 2.4}) {
        RecipeCfg after{"r", "Test", "", {{"a", 3.0}, {"b", 3.0}}};
        after.refEc = ref;
        f.cfg.recipes[0] = after;
        const auto e = editEcEffect(before, after, learned);
        auto ml = [&](double l) {
          const auto eff = recipeEffect(after, l);
          const auto d = planEcDose(f.cfg, f.pumps, after, 20, 0.6, eff.dose, eff.largest, 1.0);
          REQUIRE(d.ok);
          return d.steps[0].ml + d.steps[1].ml;
        };
        CHECK(ml(e.learned) <= doctest::Approx(std::min(ml(learned), ml(kNaN))));
        CHECK(recipeEffect(after, e.learned).largest >= doctest::Approx(recipeEffect(after, kNaN).largest));
      }
    }
  }
}

TEST_CASE("EC dose with a recipe's effect: a small reference or a small learned effect never widens the cap; it never goes without one (SD-057, RAT-056, R5)") {
  Fix f;
  f.cfg.recipes[0] = {"r", "Test", "", {{"a", 1.0}, {"b", 1.0}}};
  const double capMl = 1.0 / kEcStartEffect * 20;  // max_ec_step 1.0 over the start effect, 20 L
  for (double ref : {kNaN, 0.3, 0.55}) {  // none, 0.15 and 0.275 per ml/L
    for (double learned : {kNaN, 0.08, 0.2}) {
      f.cfg.recipes[0].refEc = ref;
      const auto e = recipeEffect(f.cfg.recipes[0], learned);
      for (double step : {1.0, kNaN, -1.0, std::numeric_limits<double>::infinity()}) {
        const auto d = planEcDose(f.cfg, f.pumps, f.cfg.recipes[0], 20, 3.0, e.dose, e.largest, step);
        REQUIRE(d.ok);
        CHECK(d.steps[0].ml + d.steps[1].ml <= doctest::Approx(capMl));
      }
    }
  }
  // Whatever start comes in, the cap is never wider than at the start effect fixed in code (RAT-056)
  for (double start : {kNaN, 0.1, -1.0}) {
    const auto d = planEcDose(f.cfg, f.pumps, f.cfg.recipes[0], 20, 3.0, 0.1, start, 1.0);
    REQUIRE(d.ok);
    CHECK(d.steps[0].ml + d.steps[1].ml <= doctest::Approx(capMl));
  }
  // A strong reference makes the cap smaller: 1.0 / 0.5 per ml/L
  f.cfg.recipes[0].refEc = 1.0;
  const auto e = recipeEffect(f.cfg.recipes[0], kNaN);
  const auto d = planEcDose(f.cfg, f.pumps, f.cfg.recipes[0], 20, 3.0, e.dose, e.largest, 1.0);
  REQUIRE(d.ok);
  CHECK(d.steps[0].ml + d.steps[1].ml == doctest::Approx(1.0 / 0.5 * 20));
}

TEST_CASE("EC dose: a fixed addition is neither dosed nor scaled; the round goes to the other parts in their ratio (SD-057, RAT-085)") {
  Fix f;
  f.cfg.recipes[0] = {"r", "Test", "", {{"a", 1.0}, {"b", 1.0}, {"cm", 0.5, true}}};
  auto d = planEcDose(f.cfg, f.pumps, f.cfg.recipes[0], 20, 1.0, 0.275, 0.275, 1.0);
  REQUIRE(d.ok);
  REQUIRE(d.steps.size() == 2);
  for (const auto& s : d.steps) CHECK(s.canister != "cm");
  // The same round as without the fixed part: 0.8 × gap / effect per ml/L of the scaled parts
  CHECK(d.steps[0].ml + d.steps[1].ml == doctest::Approx(58.18).epsilon(0.001));
  CHECK(d.steps[0].ml == doctest::Approx(d.steps[1].ml));
  // The fixed part's pump plays no part in the round
  f.pumps["C3"].online = false;
  f.pumps["C3"].flowMlPerMin = kNaN;
  CHECK(planEcDose(f.cfg, f.pumps, f.cfg.recipes[0], 20, 1.0, 0.275, 0.275, 1.0).ok);
  // Only fixed parts: nothing to scale, said so
  f.cfg.recipes[0] = {"r", "Test", "", {{"cm", 0.5, true}}};
  auto n = planEcDose(f.cfg, f.pumps, f.cfg.recipes[0], 20, 1.0, 0.275, 0.275, 1.0);
  CHECK_FALSE(n.ok);
  CHECK(n.reason.key == "ec.recipe_no_scaled");
  CHECK(n.steps.empty());
}

TEST_CASE("Mix: a fixed addition goes in per litre of fresh water, like every part, and the plan marks it (SD-057, RAT-085, RAT-011)") {
  Fix f;
  f.cfg.recipes[0] = {"r", "Test", "", {{"a", 2.0}, {"b", 1.0}, {"cm", 0.5, true}}};
  auto p = f.plan(20);
  REQUIRE(p.ok);
  REQUIRE(p.steps.size() == 3);
  CHECK(p.steps[2].ml == doctest::Approx(10.0));
  CHECK(p.steps[2].fixed);
  CHECK_FALSE(p.steps[0].fixed);
  CHECK(json(p.steps[2])["fixed"] == true);
  CHECK(json(p.steps[0])["fixed"] == false);
  // Topping up: on the fresh litres only
  f.rt.tankVolumeL = 30;
  auto t = planMix(f.cfg, f.rt, f.pumps, {"r", 10, "topup", false}, 1790000000, false);
  REQUIRE(t.ok);
  CHECK(t.steps[0].ml == doctest::Approx(20.0));
  CHECK(t.steps[2].ml == doctest::Approx(5.0));
}

namespace {
// A fixed pattern to a target EC (SD-057 way 2, SD-098): water EC entered, no probe
MixPlan pattern(Fix& f, double target, double waterEc, double water = 10, const std::string& mode = "new", bool phAuto = false) {
  MixRequest req{"r", water, mode, false};
  req.amounts = "target";
  req.targetEc = target;
  req.waterEc = waterEc;
  req.targetMin = 0.2;
  req.targetMax = 4.0;
  return planMix(f.cfg, f.rt, f.pumps, req, 1790000000, phAuto);
}
bool hasKey(const std::vector<Msg>& ms, const std::string& key) {
  return std::any_of(ms.begin(), ms.end(), [&](const Msg& m) { return m.key == key; });
}
double mlOf(const MixPlan& p, const std::string& canister) {
  for (const auto& s : p.steps)
    if (s.canister == canister) return s.ml;
  return kNaN;
}
}  // namespace

TEST_CASE("Fixed pattern to a target EC: from the reference, the pair in its ratio, the water's EC subtracted, fixed parts per fresh litre (SD-057, SD-098, RAT-087, RAT-054, RAT-011)") {
  Fix f;
  f.cfg.recipes[0].refEc = 0.9;  // 0.3 per ml/L over A 2 + B 1
  SUBCASE("at the reference with water at 0: exactly the recipe's amounts") {
    const auto p = pattern(f, 0.9, 0.0);
    REQUIRE(p.ok);
    CHECK(mlOf(p, "a") == doctest::Approx(20.0));
    CHECK(mlOf(p, "b") == doctest::Approx(10.0));
    CHECK(p.factor == doctest::Approx(1.0));
    CHECK(p.expectedEc == doctest::Approx(0.9));
    CHECK(hasKey(p.infos, "mix.no_feedback"));  // it runs without feedback and says so
    CHECK_FALSE(hasKey(p.warnings, "mix.capped"));
    CHECK(json(p)["factor"] == doctest::Approx(1.0));
  }
  SUBCASE("a lower target, or the water's own EC, scales every part alike") {
    for (const auto& [target, water] : std::vector<std::pair<double, double>>{{0.6, 0.0}, {0.9, 0.3}}) {
      const auto p = pattern(f, target, water);
      REQUIRE(p.ok);
      CHECK(p.factor == doctest::Approx(2.0 / 3.0));
      CHECK(mlOf(p, "a") == doctest::Approx(2.0 * mlOf(p, "b")));  // the pair stays 2:1
      CHECK(mlOf(p, "a") + mlOf(p, "b") == doctest::Approx(20.0));
      CHECK(p.expectedEc == doctest::Approx(target));
      CHECK_FALSE(hasKey(p.warnings, "mix.capped"));
    }
  }
  SUBCASE("a fixed part goes in per litre of fresh water; at the reference the scaled parts are the recipe's") {
    f.cfg.recipes[0].steps.push_back({"cm", 0.5, true});
    f.cfg.recipes[0].refEc = 1.05;  // 0.3 per ml/L over all 3.5 ml/L (an assumption: the fixed part as strong as the rest)
    auto p = pattern(f, 1.05, 0.0);
    REQUIRE(p.ok);
    CHECK(mlOf(p, "a") == doctest::Approx(20.0));
    CHECK(mlOf(p, "b") == doctest::Approx(10.0));
    CHECK(mlOf(p, "cm") == doctest::Approx(5.0));
    CHECK(p.expectedEc == doctest::Approx(1.05));  // the fixed part counted once
    p = pattern(f, 0.75, 0.0);  // (0.75 − 0.5 × 0.3) / 0.3 = 2 ml/L of 3: two thirds
    REQUIRE(p.ok);
    CHECK(p.factor == doctest::Approx(2.0 / 3.0));
    CHECK(mlOf(p, "cm") == doctest::Approx(5.0));
    CHECK(p.expectedEc == doctest::Approx(0.75));
  }
  SUBCASE("a learned effect with a fixed part: the fixed part counted with it, once") {
    f.cfg.recipes[0].steps.push_back({"cm", 0.5, true});
    f.cfg.recipes[0].refEc = 1.05;
    f.rt.ecEffects["r"] = 0.45;
    const auto p = pattern(f, 0.9, 0.0);  // (0.9 − 0.5 × 0.45) / 0.45 = 1.5 ml/L of 3: half
    REQUIRE(p.ok);
    CHECK(p.factor == doctest::Approx(0.5));
    CHECK(mlOf(p, "a") == doctest::Approx(10.0));
    CHECK(mlOf(p, "cm") == doctest::Approx(5.0));
    CHECK(p.expectedEc == doctest::Approx(0.9));
  }
  SUBCASE("on a top-up, per litre of the fresh water, a fixed part too (RAT-011)") {
    f.cfg.recipes[0].steps.push_back({"cm", 0.5, true});
    f.cfg.recipes[0].refEc = 1.05;
    f.rt.tankVolumeL = 20;
    const auto p = pattern(f, 0.75, 0.5, 10, "topup");  // (0.75 − 0.5 − 0.15) / 0.3 = 1/3 ml/L of 3
    REQUIRE(p.ok);
    CHECK(p.factor == doctest::Approx(1.0 / 9.0));
    CHECK(mlOf(p, "a") + mlOf(p, "b") == doctest::Approx(10.0 / 3.0));
    CHECK(mlOf(p, "cm") == doctest::Approx(5.0));
    CHECK_FALSE(hasKey(p.warnings, "mix.water_ec_high"));  // the note is for a new fill only
  }
}

TEST_CASE("Fixed pattern to a target EC: never more than the recipe's amounts, whatever the reference or the learned effect (SD-098, RAT-087, RAT-056, RAT-066)") {
  Fix f;
  SUBCASE("a target above the chart: the recipe's amounts, and the plan says what to expect") {
    f.cfg.recipes[0].refEc = 0.9;
    const auto p = pattern(f, 2.0, 0.0);
    REQUIRE(p.ok);
    CHECK(p.factor == doctest::Approx(1.0));
    CHECK(mlOf(p, "a") == doctest::Approx(20.0));
    CHECK(p.expectedEc == doctest::Approx(0.9));
    CHECK(hasKey(p.warnings, "mix.capped"));
  }
  SUBCASE("a reference typed far too low (it passes the band) doses no more than the recipe") {
    f.cfg.recipes[0].refEc = 0.4;  // 0.13 per ml/L, about a third of the real one
    const auto p = pattern(f, 2.1, 0.0);
    REQUIRE(p.ok);
    CHECK(mlOf(p, "a") + mlOf(p, "b") <= doctest::Approx(30.0));
  }
  SUBCASE("no floor at the start effect: a weak product reaches its target") {
    f.cfg.recipes[0].refEc = 0.42;  // 0.14 per ml/L
    const auto p = pattern(f, 0.28, 0.0);
    REQUIRE(p.ok);
    CHECK(p.factor == doctest::Approx(2.0 / 3.0));
  }
  SUBCASE("a learned effect only makes the amounts smaller") {
    f.cfg.recipes[0].refEc = 0.9;
    f.rt.ecEffects["r"] = 0.45;  // stronger than the reference says
    auto p = pattern(f, 0.6, 0.0);
    REQUIRE(p.ok);
    CHECK(mlOf(p, "a") + mlOf(p, "b") == doctest::Approx(0.6 / 0.45 * 10));
    for (double learned : {0.2, 0.05}) {  // weaker, or below the band: changes nothing
      f.rt.ecEffects["r"] = learned;
      p = pattern(f, 0.6, 0.0);
      REQUIRE(p.ok);
      CHECK(mlOf(p, "a") + mlOf(p, "b") == doctest::Approx(20.0));
    }
    f.rt.ecEffects["r"] = 2.0;  // stored above the band: counts at its top, as for a round (RAT-056)
    p = pattern(f, 0.6, 0.0);
    REQUIRE(p.ok);
    CHECK(mlOf(p, "a") + mlOf(p, "b") == doctest::Approx(0.6 / 1.1 * 10));
  }
}

TEST_CASE("Fixed pattern to a target EC: refused without a reference, a target in range, the water's EC or with EC control on; water at the target gets only fixed parts; the notes on pH (SD-057, SD-098, RAT-087, R5, RAT-006, RAT-046)") {
  Fix f;
  SUBCASE("no reference, or one outside the band: refused, also with a learned effect; the recipe's amounts stay") {
    f.rt.ecEffects["r"] = 0.3;
    for (double ref : {kNaN, 0.1, 9.0}) {
      f.cfg.recipes[0].refEc = ref;
      const auto p = pattern(f, 0.9, 0.0);
      CHECK_FALSE(p.ok);
      CHECK(hasKey(p.errors, "mix.no_reference"));
      CHECK(f.plan(10).ok);
    }
  }
  f.cfg.recipes[0].refEc = 0.9;
  SUBCASE("a missing water EC is never taken as 0; an entered 0 is a value") {
    const auto p = pattern(f, 0.9, kNaN);
    CHECK_FALSE(p.ok);
    CHECK(hasKey(p.errors, "mix.water_ec_missing"));
    CHECK(pattern(f, 0.9, 0.0).ok);
    CHECK(hasKey(pattern(f, 0.9, -0.1).errors, "mix.water_ec_missing"));
    CHECK(json(p)["factor"].is_null());  // a refused plan works out no factor
  }
  SUBCASE("a water EC above the EC target's range is refused as a likely µS/cm") {
    CHECK(hasKey(pattern(f, 1.2, 300).errors, "mix.water_ec_unit"));
    CHECK_FALSE(hasKey(pattern(f, 1.2, 4.0).errors, "mix.water_ec_unit"));
  }
  SUBCASE("with EC control on and free, refused: it would take the tank to its own target (SD-089 S20)") {
    MixRequest req{"r", 10, "new", false};
    req.amounts = "target";
    req.targetEc = 0.6;
    req.waterEc = 0.0;
    req.ecControlOn = true;
    req.ecControlTarget = 1.4;
    const auto p = planMix(f.cfg, f.rt, f.pumps, req, 1790000000, false);
    CHECK_FALSE(p.ok);
    REQUIRE(hasKey(p.errors, "mix.target_ec_control"));
    CHECK(p.errors.front().args["target"] == doctest::Approx(1.4));
    CHECK(f.plan(10).ok);  // the recipe's amounts stay
  }
  SUBCASE("an unknown choice of amounts is refused") {
    MixRequest req{"r", 10, "new", false};
    req.amounts = "foo";
    CHECK(hasKey(planMix(f.cfg, f.rt, f.pumps, req, 1790000000, false).errors, "mix.amounts"));
  }
  SUBCASE("a target missing or outside the EC target's range is refused; its ends are in it") {
    for (double target : {kNaN, 0.1, 4.5}) CHECK(hasKey(pattern(f, target, 0.0).errors, "mix.target_range"));
    for (double target : {0.2, 4.0}) CHECK_FALSE(hasKey(pattern(f, target, 0.0).errors, "mix.target_range"));
  }
  SUBCASE("a target just above the water: parts below an accurate run refuse the plan, for both parts of the pair (RAT-054)") {
    const auto p = pattern(f, 0.205, 0.2);
    CHECK_FALSE(p.ok);
    std::set<std::string> parts;
    for (const auto& e : p.errors)
      if (e.key == "mix.part" && e.args["reason"]["key"] == "dose.too_small") parts.insert(e.args["canister"].get<std::string>());
    CHECK(parts == std::set<std::string>{"a", "b"});
  }
  SUBCASE("water at or above the target: nothing scaled; a fixed part still goes in") {
    auto p = pattern(f, 0.5, 0.6);
    CHECK_FALSE(p.ok);
    CHECK(hasKey(p.errors, "mix.at_target"));
    f.cfg.recipes[0].steps.push_back({"cm", 0.5, true});
    f.cfg.recipes[0].refEc = 1.05;
    p = pattern(f, 0.5, 0.6);
    REQUIRE(p.ok);
    CHECK(std::isnan(mlOf(p, "a")));
    CHECK(mlOf(p, "cm") == doctest::Approx(5.0));
    CHECK(hasKey(p.warnings, "mix.at_target_fixed"));
    p = pattern(f, 0.2, 0.1);  // below the target, but the fixed part's 0.15 reaches it
    REQUIRE(p.ok);
    CHECK(std::isnan(mlOf(p, "a")));
    CHECK(hasKey(p.warnings, "mix.at_target_fixed"));
  }
  SUBCASE("a new fill above 0.4 says so; with pH control on, the plan names the EC its pH− dose adds") {
    CHECK(hasKey(pattern(f, 1.2, 0.5).warnings, "mix.water_ec_high"));
    CHECK_FALSE(hasKey(pattern(f, 1.2, 0.3).warnings, "mix.water_ec_high"));
    auto p = pattern(f, 0.9, 0.0, 10, "new", true);
    CHECK(hasKey(p.infos, "mix.ph_adds_ec"));
    CHECK(p.after.key == "mix.after_auto_target");
    p = pattern(f, 0.9, 0.0);
    CHECK_FALSE(hasKey(p.infos, "mix.ph_adds_ec"));
    CHECK(p.after.key == "mix.after_manual_target");
  }
  SUBCASE("an EC to expect below pH control's EC lock: the pH by hand (RAT-046)") {
    MixRequest req{"r", 10, "new", false};
    req.amounts = "target";
    req.targetEc = 0.4;
    req.waterEc = 0.0;
    req.targetMin = 0.2;
    req.targetMax = 4.0;
    req.phEcFloor = 0.5;
    auto p = planMix(f.cfg, f.rt, f.pumps, req, 1790000000, true);
    REQUIRE(p.ok);
    CHECK(hasKey(p.warnings, "mix.ph_gate"));
    CHECK_FALSE(hasKey(p.infos, "mix.ph_adds_ec"));
    CHECK(p.after.key == "mix.after_manual_target");
    req.targetEc = 0.6;
    p = planMix(f.cfg, f.rt, f.pumps, req, 1790000000, true);
    CHECK_FALSE(hasKey(p.warnings, "mix.ph_gate"));
    CHECK(hasKey(p.infos, "mix.ph_adds_ec"));
  }
}

TEST_CASE("pH-Gabe: Deckel und Startwirkung (M4-1, M4-5, RAT-050)") {
  auto d = planPhDose(6.5, 5.9, 5.0, 20, 20);
  REQUIRE(d.ok);
  CHECK(d.rawMl == doctest::Approx(1.92));
  CHECK(d.ml == doctest::Approx(1.2));
  CHECK(clampEffect(0.5, 5.0) == doctest::Approx(5.0));
  CHECK(clampEffect(30, 5.0) == doctest::Approx(20.0));
  CHECK(clampEffect(kNaN, 5.0) == doctest::Approx(5.0));
  CHECK_FALSE(planPhDose(5.8, 5.9, 5.0, 20, 20).ok);
  CHECK_FALSE(planPhDose(6.5, 5.9, 5.0, kNaN, 20).ok);
}
