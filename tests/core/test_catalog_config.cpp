#include <doctest/doctest.h>

#include "gc/catalog.hpp"
#include "gc/config.hpp"

using namespace gc;

TEST_CASE("Katalog: eingebetteter Katalog lädt und ist in sich stimmig") {
  Catalog c = Catalog::builtin();
  CHECK(c.version >= 1);
  CHECK(c.function("mix") != nullptr);
  CHECK(c.function("ph_control") != nullptr);
  for (const auto& f : c.functions)
    for (const auto& r : f.hard)
      if (r.kind == Requirement::Kind::Role) CHECK(c.role(r.role) != nullptr);
  CHECK(c.classesProviding("measure.ph").size() == 1);
}

TEST_CASE("Katalog: unbekannte Capability wird abgelehnt") {
  json j = Catalog::builtin().raw;
  j["deviceClasses"]["x"] = {{"label", "X"}, {"provides", {"measure.unbekannt"}}};
  CHECK_THROWS(Catalog::fromJson(j));
}

namespace {
Config sample() {
  Config c;
  c.devices = {{"CAP-1", "pump_cap", "Kappe 1"}, {"CAP-2", "pump_cap", "Kappe 2"}, {"CAP-3", "pump_cap", "Kappe 3"}};
  c.canisters = {{"a", "Teil A", "nutrient", "CAP-1", "AB", "#111", 1000},
                 {"b", "Teil B", "nutrient", "CAP-2", "AB", "#222", 1000},
                 {"down", "pH−", "ph_down", "CAP-3", "", "#333", kNaN}};
  c.recipes = {{"r", "Wachstum", "", {{"a", 2.0}, {"b", 2.0}}}};
  c.tank().capacityL = 60;
  return c;
}
}  // namespace

TEST_CASE("Konfiguration: Hin- und Rückweg über JSON, fehlende Zahl bleibt fehlend") {
  Config c = sample();
  Config d = configFromJson(json(c));
  CHECK(d.canisters.size() == 3);
  CHECK(std::isnan(d.canisters[2].capacityMl));  // null, nicht 0 (RAT-006)
  CHECK(d.tank().capacityL == doctest::Approx(60));
  CHECK(json(c) == json(d));
}

TEST_CASE("Konfiguration: Migration v0 → v1 (einzelner Tank → Liste)") {
  json v0 = {{"tank", {{"id", "t1"}, {"name", "Alt"}, {"capacityL", 40}}}};
  Config c = configFromJson(v0);
  CHECK(c.schemaVersion == 1);
  CHECK(c.tanks.size() == 1);
  CHECK(c.tank().name == "Alt");
  CHECK_THROWS(configFromJson(json{{"schemaVersion", 99}}));
}

TEST_CASE("Konfiguration: Prüfung fachlich und sicherheitlich") {
  Catalog cat = Catalog::builtin();
  Config c = sample();
  CHECK(validateConfig(c, cat).empty());

  SUBCASE("pH-Korrektur im Rezept ist verboten (pH zuletzt, RAT-004)") {
    c.recipes[0].steps.push_back({"down", 0.1});
    CHECK_FALSE(validateConfig(c, cat).empty());
  }
  SUBCASE("Paar mit nur einem Partner im Rezept") {
    c.recipes[0].steps.pop_back();
    CHECK_FALSE(validateConfig(c, cat).empty());
  }
  SUBCASE("Parameter außerhalb des Bereichs, Toleranz 0 abgelehnt (RAT-008)") {
    c.functions["ph_control"].params["ph_tolerance"] = 0.0;
    CHECK_FALSE(validateConfig(c, cat).empty());
  }
  SUBCASE("Unbekannte Geräteklasse") {
    c.devices.push_back({"X-1", "kopf.zukunft", ""});
    CHECK_FALSE(validateConfig(c, cat).empty());
  }
  SUBCASE("Rolle an Gerät ohne passende Capability") {
    c.tank().roles["tank.ph"] = {"CAP-1", 0};
    CHECK_FALSE(validateConfig(c, cat).empty());
  }
}

TEST_CASE("Phasen liefern Parameter; der Name ändert nichts (M15-1, RAT-076)") {
  Catalog cat = Catalog::builtin();
  Config c = sample();
  c.functions["ph_control"].params["ph_target"] = 6.0;
  CHECK(effectiveParams(cat, c, "ph_control").num("ph_target") == doctest::Approx(6.0));
  c.grow.state = "running";
  c.grow.phases = {{"Blüte", 56, {{"ph_target", 5.9}, {"ec_floor", 2.0}}}};
  auto p = effectiveParams(cat, c, "ph_control");
  CHECK(p.num("ph_target") == doctest::Approx(5.9));
  CHECK(p.num("ec_floor") == doctest::Approx(0.5));  // kein Phasenparameter → nicht überschrieben
  c.grow.phases[0].name = "Irgendwas";
  CHECK(effectiveParams(cat, c, "ph_control").values() == p.values());
}
