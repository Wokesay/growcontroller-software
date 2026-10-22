// SPDX-License-Identifier: AGPL-3.0-or-later
#include <doctest/doctest.h>

#include <limits>

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
  bool found = false;
  for (const auto& e : p.errors) found = found || e.key == "dose.no_flow";
  CHECK(found);
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
