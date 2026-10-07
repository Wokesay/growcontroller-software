// SPDX-License-Identifier: AGPL-3.0-or-later
#include <doctest/doctest.h>

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
