// SPDX-License-Identifier: AGPL-3.0-or-later
#include <doctest/doctest.h>

#include <cctype>
#include <set>

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
  CHECK(c.classesProviding("measure.ph").size() == 2);  // pH/EC-Kopf oder eigener pH-Kopf
  CHECK(c.role("zone.air_temp") != nullptr);
  CHECK(c.role("tent.air_temp") == nullptr);
}

TEST_CASE("Catalog: every device attaches to a hub port, the dosing block, the hub itself or the network (SD-031)") {
  // There is no collection box any more (PD-078).
  const std::set<std::string> kinds{"hub_port", "dosing_port", "builtin", "net"};
  const Catalog c = Catalog::builtin();
  for (const auto& [id, d] : c.deviceClasses) CHECK_MESSAGE(kinds.count(d.attach) == 1, (id + ": " + d.attach));
  CHECK(c.deviceClasses.at("head_climate").attach == "hub_port");
  CHECK(c.deviceClasses.at("head_co2").attach == "hub_port");
}

TEST_CASE("Catalog: recipe templates fit the name limit and cite their source") {
  // A recipe created from a template takes its name, cut at 40 bytes
  // (Hub::putRecipe); a cut name would no longer match the template.
  Catalog c = Catalog::builtin();
  REQUIRE(c.templates.contains("recipes"));
  for (const auto& t : c.templates["recipes"]) {
    for (const char* key : {"name", "nameEn"}) {
      if (!t.contains(key)) continue;
      INFO(t.value("id", std::string()) << " " << key);
      CHECK(t[key].get<std::string>().size() <= 40);
    }
    // Templates from a manufacturer chart (PD-030): not binding, with the
    // edition and the date of the source.
    if (!t.contains("sourceEn")) continue;
    INFO(t.value("id", std::string()));
    CHECK(t.value("noteEn", std::string()).find("Manufacturer data, not binding") != std::string::npos);
    CHECK(t.value("note", std::string()).find("Herstellerangabe, unverbindlich") != std::string::npos);
    const std::string src = t["sourceEn"].get<std::string>();
    CHECK(src.find("edition") != std::string::npos);
    bool dated = false;
    for (size_t i = 0; i + 10 <= src.size(); ++i)
      if (std::isdigit(static_cast<unsigned char>(src[i])) && src[i + 4] == '-' && src[i + 7] == '-') dated = true;
    CHECK(dated);
  }
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
  CHECK(c.schemaVersion == kSchemaVersion);
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

TEST_CASE("Recipes: a recipe keeps its chart EC and pH range; a missing one stays missing (SD-057)") {
  Config c = sample();
  json j = c;
  CHECK(j["recipes"][0]["refEc"].is_null());  // never 0 (R5)
  CHECK(j["recipes"][0]["ph"].is_null());
  Config d = configFromJson(j);
  CHECK(std::isnan(d.recipes[0].refEc));
  CHECK(std::isnan(d.recipes[0].phLow));
  CHECK(std::isnan(d.recipes[0].phHigh));
  c.recipes[0].refEc = 1.2;
  c.recipes[0].phLow = 5.8;
  c.recipes[0].phHigh = 6.2;
  d = configFromJson(json(c));
  CHECK(d.recipes[0].refEc == doctest::Approx(1.2));
  CHECK(d.recipes[0].phLow == doctest::Approx(5.8));
  CHECK(d.recipes[0].phHigh == doctest::Approx(6.2));
  CHECK(json(c) == json(d));
  // A file saved before has neither and loads as it was
  json old = sample();
  old["recipes"][0].erase("refEc");
  old["recipes"][0].erase("ph");
  Config e = configFromJson(old);
  CHECK(std::isnan(e.recipes[0].refEc));
  CHECK(std::isnan(e.recipes[0].phLow));
  // A range that is not two numbers is no range
  old["recipes"][0]["ph"] = json::array({5.8});
  CHECK(std::isnan(configFromJson(old).recipes[0].phLow));
  old["recipes"][0]["ph"] = "5.8-6.2";
  CHECK(std::isnan(configFromJson(old).recipes[0].phHigh));
}

TEST_CASE("Recipes: a reference far from the start effect is refused, naming the EC that would fit (SD-057, RAT-056)") {
  Catalog cat = Catalog::builtin();
  Config c = sample();  // 2 + 2 ml/L
  auto keys = [&] {
    std::set<std::string> out;
    for (const auto& m : validateConfig(c, cat)) out.insert(m.key);
    return out;
  };
  auto& r = c.recipes[0];
  // The effect it implies, EC over the sum of the amounts, lies within a quarter and four times the start effect
  r.refEc = 0.28;  // 0.07 per ml/L, just above a quarter of 0.275
  CHECK(keys().empty());
  r.refEc = 0.27;
  CHECK(keys().count("cfg.recipe.ref_far") == 1);
  r.refEc = 1.6;
  CHECK(keys().empty());
  r.steps = {{"a", 0.5}, {"b", 0.5}};
  r.refEc = 1.09;  // 1.09 per ml/L, just below four times 0.275
  CHECK(keys().empty());
  r.refEc = 1.12;  // too strong for the amounts: a concentrate, not a typo, may be the cause
  CHECK(keys().count("cfg.recipe.ref_strong") == 1);
  r.refEc = 1.5;
  CHECK(keys().count("cfg.recipe.ref_strong") == 1);
  CHECK(keys().count("cfg.recipe.ref_far") == 0);
  // The ends of the target's range pass when the amounts fit
  r.steps = {{"a", 0.5}, {"b", 0.5}};
  r.refEc = 0.2;
  CHECK(keys().empty());
  r.steps = {{"a", 4.0}, {"b", 4.0}};
  r.refEc = 4.0;
  CHECK(keys().empty());
  // It becomes a phase's target, so it lies in the target's range; never 0 or less
  r.refEc = 4.5;
  CHECK(keys().count("cfg.recipe.ref_range") == 1);
  for (double bad : {0.0, -1.0}) {
    r.refEc = bad;
    CHECK(keys().count("cfg.recipe.ref_range") == 1);
  }
  r.refEc = kNaN;  // no reference: nothing to check
  CHECK(keys().empty());
  // The message names the recipe, the amounts and the EC that fits them, within the target's range
  r.refEc = 0.27;
  r.steps = {{"a", 2.0}, {"b", 2.0}};
  bool found = false;
  for (const auto& m : validateConfig(c, cat))
    if (m.key == "cfg.recipe.ref_far") {
      found = true;
      CHECK(m.args["name"] == "Wachstum");
      CHECK(m.args["ec"].get<double>() == doctest::Approx(0.27));
      CHECK(m.args["sum"].get<double>() == doctest::Approx(4.0));
      CHECK(m.args["lo"].get<double>() == doctest::Approx(0.275));
      CHECK(m.args["hi"].get<double>() == doctest::Approx(4.0));  // 4.4 would fit, the target ends at 4.0
    }
  CHECK(found);
}

TEST_CASE("Recipes: a pH range is two values, low to high, within the pH target's range (SD-057, RAT-066)") {
  Catalog cat = Catalog::builtin();
  Config c = sample();
  auto band = [&] {
    for (const auto& m : validateConfig(c, cat))
      if (m.key == "cfg.recipe.ph_band") return true;
    return false;
  };
  auto& r = c.recipes[0];
  r.phLow = 5.8;
  r.phHigh = 6.2;
  CHECK_FALSE(band());
  r.phLow = r.phHigh = 6.0;  // one value is a range too
  CHECK_FALSE(band());
  r.phLow = 6.3;
  CHECK(band());
  r.phLow = 4.0;  // below the pH target's 4.5
  CHECK(band());
  r.phLow = 5.8;
  r.phHigh = 8.0;
  CHECK(band());
  r.phHigh = kNaN;  // half a range
  CHECK(band());
  r.phLow = r.phHigh = 4.5;  // the ends of the pH target's range pass
  CHECK_FALSE(band());
  r.phLow = r.phHigh = 7.5;
  CHECK_FALSE(band());
}

TEST_CASE("Catalog: every recipe template's chart EC and pH range would pass as a recipe's (SD-057)") {
  // Each template as a recipe of its own, checked the way a saved recipe is
  Catalog cat = Catalog::builtin();
  for (const auto& t : cat.templates["recipes"]) {
    INFO(t.value("id", std::string()));
    REQUIRE(t.contains("ec"));
    REQUIRE(t["ph"].size() == 2);
    Config c;
    RecipeCfg r{"r", "Vorlage", "", {}};
    for (const auto& s : t["steps"]) {
      const std::string role = s["role"].get<std::string>();
      c.canisters.push_back({role, role, "nutrient", "", s.value("pair", std::string()), "#111", 1000});
      r.steps.push_back({role, s["mlPerL"].get<double>()});
    }
    r.refEc = t["ec"].get<double>();
    r.phLow = t["ph"][0].get<double>();
    r.phHigh = t["ph"][1].get<double>();
    c.recipes = {r};
    for (const auto& m : validateConfig(c, cat)) {
      INFO(m.key << ": " << m.text);
      CHECK(m.key.rfind("cfg.recipe.", 0) != 0);
    }
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

TEST_CASE("Konfiguration: Grenzen dürfen nur verschärfen (R7)") {
  Catalog cat = Catalog::builtin();
  Config c = sample();
  auto hasKey = [&](const std::string& key) {
    for (const auto& m : validateConfig(c, cat))
      if (m.key == key) return true;
    return false;
  };
  c.limits.maxRunS = 600;
  CHECK(hasKey("cfg.limits.max_run"));
  c.limits = Limits{};
  c.limits.maxPartialRuns = 0;
  CHECK(hasKey("cfg.limits.runs"));
  c.limits = Limits{};
  c.limits.handDoseMaxMl = 1e6;
  CHECK(hasKey("cfg.limits.hand"));
  c.limits = Limits{};
  c.limits.minRunS = 0.1;
  CHECK(hasKey("cfg.limits.min_run"));
  // Gateway und Planung rechnen mit den festen Grenzen, auch wenn die Datei Unsinn enthält
  Limits bad;
  bad.maxRunS = 600;
  bad.minRunS = kNaN;
  bad.handDoseMaxMl = kNaN;
  bad.maxPartialRuns = 0;
  Limits b = bad.bounded();
  CHECK(b.maxRunS == doctest::Approx(kHardMaxRunS));
  CHECK(b.minRunS == doctest::Approx(kHardMinRunS));
  CHECK(b.handDoseMaxMl == doctest::Approx(5.0));
  CHECK(b.maxPartialRuns == 1);
  // Aus JSON: falscher Typ wirft nicht; es gilt die Vorgabe, Unsinn fällt bei der Prüfung auf
  json j = sample();
  j["limits"]["maxPartialRuns"] = "viele";
  Config fromFile;
  CHECK_NOTHROW(fromFile = configFromJson(j));
  CHECK(fromFile.limits.maxPartialRuns == kHardMaxPartialRuns);
  j["limits"]["maxPartialRuns"] = 1e9;
  c = configFromJson(j);
  CHECK(hasKey("cfg.limits.runs"));
}

TEST_CASE("Konfiguration: Phasenparameter im Katalogbereich (RAT-008, RAT-076)") {
  Catalog cat = Catalog::builtin();
  Config c = sample();
  c.grow.phases = {{"Blüte", 56, {{"ph_target", 5.9}, {"ec_target", 1.8}}}};
  CHECK(validateConfig(c, cat).empty());
  c.grow.phases[0].params["ph_tolerance"] = 0.0;
  CHECK_FALSE(validateConfig(c, cat).empty());
  c.grow.phases[0].params = {{"ph_target", 1.0}};
  CHECK_FALSE(validateConfig(c, cat).empty());
  c.grow.phases[0].params = {{"ec_floor", 2.0}};  // nicht phasenabhängig
  CHECK_FALSE(validateConfig(c, cat).empty());
  c.grow.phases[0].params = {{"ph_target", "sauer"}};
  CHECK_FALSE(validateConfig(c, cat).empty());
}

TEST_CASE("Konfiguration: Migration v1 → v2 (Zelt-Rollen werden Rollen der Zone)") {
  json v1 = {{"schemaVersion", 1},
             {"devices", {{{"id", "CLIM-1"}, {"class", "head_climate"}, {"name", "Klima"}}, {{"id", "PHEC-1"}, {"class", "head_ph_ec"}, {"name", "pH/EC"}}}},
             {"tanks", {{{"id", "t1"},
                         {"name", "Tank"},
                         {"roles", {{"tank.ph", {{"device", "PHEC-1"}, {"channel", 0}}},
                                    {"tent.air_temp", {{"device", "CLIM-1"}, {"channel", 0}}},
                                    {"tent.humidity", {{"device", "CLIM-1"}, {"channel", 0}}}}}}}}};
  Config c = configFromJson(v1);
  CHECK(c.schemaVersion == 2);
  REQUIRE(c.zones.size() == 1);
  CHECK(c.zone().tank == "t1");
  CHECK(c.zone().roles.count("zone.air_temp") == 1);
  CHECK(c.zone().roles.count("zone.humidity") == 1);
  CHECK(c.tank().roles.count("tent.air_temp") == 0);
  CHECK(c.tank().roles.count("tank.ph") == 1);
  REQUIRE(c.binding("zone.air_temp") != nullptr);
  CHECK(c.binding("zone.air_temp")->device == "CLIM-1");
  CHECK(c.binding("tank.ph")->device == "PHEC-1");
  CHECK(validateConfig(c, Catalog::builtin()).empty());
}

TEST_CASE("Konfiguration: Rollen am falschen Ort und unbekannte Zonenart werden abgelehnt") {
  Catalog cat = Catalog::builtin();
  Config c = sample();
  c.devices.push_back({"CLIM-1", "head_climate", "Klima"});
  c.tank().roles["zone.air_temp"] = {"CLIM-1", 0};
  CHECK_FALSE(validateConfig(c, cat).empty());
  c.tank().roles.erase("zone.air_temp");
  c.zone().roles["zone.air_temp"] = {"CLIM-1", 0};
  CHECK(validateConfig(c, cat).empty());
  c.zone().kind = "keller";
  CHECK_FALSE(validateConfig(c, cat).empty());
}

TEST_CASE("Runtime state: a damaged refill record is cleaned when read and never stops the hub (SD-080)") {
  const gc::RuntimeState s = gc::runtimeFromJson({{"refill", {{"fill", 5}, {"check", "x"}, {"unconfirmed", {{"startL", 20}}}, {"unjudged", -3}}}});
  CHECK(s.refill["fill"] == json::object());  // still counts as a fill a restart cut
  CHECK(s.refill["check"] == json::object());
  CHECK(s.refill["unconfirmed"]["startL"] == 20);
  CHECK(s.refill["unjudged"] == 0);
  CHECK(gc::runtimeFromJson({{"refill", 7}}).refill == json::object());
  CHECK(gc::runtimeFromJson({{"refill", {{"unjudged", 1}}}}).refill["unjudged"] == 1);
  // Written and read back unchanged
  gc::RuntimeState t;
  t.refill = {{"check", {{"due", 1790000600}, {"startL", 2.3}}}, {"unjudged", 1}};
  json j = t;
  CHECK(gc::runtimeFromJson(j).refill == t.refill);
}
