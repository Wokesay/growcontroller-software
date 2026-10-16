// SPDX-License-Identifier: AGPL-3.0-or-later
#include <doctest/doctest.h>

#include <optional>
#include <set>

#include "fakes.hpp"
#include "gc/resolver.hpp"

using namespace gc;

namespace {
FunctionState fn(const std::vector<FunctionState>& v, const std::string& id) {
  for (const auto& f : v)
    if (f.id == id) return f;
  FAIL("Funktion fehlt");
  return {};
}
}  // namespace

TEST_CASE("Resolver: ohne Hardware ist eine Funktion nicht verfügbar, mit Shop-Hinweis") {
  Catalog cat = Catalog::builtin();
  Config cfg;
  SensorTruth truth(cat);
  auto v = resolveFunctions(cat, cfg, {}, {}, truth);
  const auto& ph = fn(v, "ph_control");
  CHECK(ph.setup == "unavailable");
  CHECK((ph.summary.key == "fn.unavailable" || ph.summary.key == "fn.unavailable_more"));
  CHECK(ph.summary.args["first"].dump().find("cat.class.head_ph_ec") != std::string::npos);  // the class, by the catalog's key (SD-083)
  CHECK(ph.summary.text.find("pH/EC sensor head") != std::string::npos);
  CHECK(ph.summary.text.find("You need") != std::string::npos);  // the hub's text is English; the page translates by key (SD-083)
  CHECK(fn(v, "mix").setup == "unavailable");
}

TEST_CASE("Resolver: Gerät erkannt, Einrichtung fehlt → einrichtbar mit Schritten") {
  Catalog cat = Catalog::builtin();
  Config cfg;
  SensorTruth truth(cat);
  DeviceReport cap;
  cap.id = "C1";
  cap.cls = "pump_cap";
  cap.online = true;
  auto v = resolveFunctions(cat, cfg, {cap}, pumpsFrom({cap}), truth);
  const auto& mix = fn(v, "mix");
  CHECK(mix.setup == "needs_setup");
  bool hasFix = false;
  for (const auto& c : mix.checks) hasFix = hasFix || (!c.ok && c.fix == "canisters");
  CHECK(hasFix);
}

TEST_CASE("Resolver: alles da → bereit; weiche Voraussetzung → eingeschränkt") {
  Catalog cat = Catalog::builtin();
  Config cfg;
  cfg.devices = {{"C1", "pump_cap", ""}};
  cfg.canisters = {{"a", "A", "nutrient", "C1", "", "", kNaN}};
  cfg.recipes = {{"r", "R", "", {{"a", 1.0}}}};
  SensorTruth truth(cat);
  DeviceReport cap;
  cap.id = "C1";
  cap.cls = "pump_cap";
  cap.online = true;
  cap.info = {{"flowMlPerMin", 50.0}};
  auto v = resolveFunctions(cat, cfg, {cap}, pumpsFrom({cap}), truth);
  CHECK(fn(v, "mix").setup == "limited");  // ohne Umwälzpumpe: Rühranweisung
  CHECK(fn(v, "manual_dose").setup == "ready");
  // Einmesswert fehlt → einrichtbar mit "Pumpe einmessen"
  cap.info = {{"flowMlPerMin", nullptr}};
  auto w = resolveFunctions(cat, cfg, {cap}, pumpsFrom({cap}), truth);
  CHECK(fn(w, "mix").setup == "needs_setup");
  CHECK((fn(w, "mix").summary.key == "fn.needs_setup" || fn(w, "mix").summary.key == "fn.needs_setup_more"));
  CHECK(fn(w, "mix").summary.args["first"]["key"] == "req.calibrate_pump");
}

TEST_CASE("Resolver: a ready function says whether it is on, a limited one why, and one head is needed once (SD-083)") {
  Catalog cat = Catalog::builtin();
  Config cfg;
  cfg.devices = {{"C1", "pump_cap", ""}};
  cfg.canisters = {{"a", "A", "nutrient", "C1", "", "", kNaN}};
  cfg.recipes = {{"r", "R", "", {{"a", 1.0}}}};
  SensorTruth truth(cat);
  DeviceReport cap;
  cap.id = "C1";
  cap.cls = "pump_cap";
  cap.online = true;
  cap.info = {{"flowMlPerMin", 50.0}};
  auto v = resolveFunctions(cat, cfg, {cap}, pumpsFrom({cap}), truth);
  REQUIRE(fn(v, "manual_dose").setup == "ready");
  const bool on = fn(v, "manual_dose").enabled || fn(v, "manual_dose").alwaysOn;
  CHECK(fn(v, "manual_dose").summary.key == (on ? "fn.on" : "fn.ready"));
  cfg.functions["manual_dose"].enabled = !on;
  v = resolveFunctions(cat, cfg, {cap}, pumpsFrom({cap}), truth);
  if (!fn(v, "manual_dose").alwaysOn) CHECK(fn(v, "manual_dose").summary.key == (on ? "fn.ready" : "fn.on"));
  // Limited: the soft requirement's reason, nested
  REQUIRE(fn(v, "mix").setup == "limited");
  CHECK(fn(v, "mix").summary.key == "fn.limited");
  CHECK(fn(v, "mix").summary.args["why"].is_object());
  CHECK(fn(v, "mix").summary.args["why"]["key"] == "req.why.stir");  // the catalog's reason with a key of its own
  CHECK(fn(v, "mix").summary.args["why"]["text"] == "Without a circulation pump, the mixing guide has you stir between doses");
  // No two lines of a function say the same, at any level
  Config none;
  for (const auto& f : resolveFunctions(cat, none, {}, {}, truth)) {
    std::set<std::string> lines;
    for (const auto& c : f.checks) CHECK_MESSAGE(lines.insert(c.level + json(c.text).dump()).second, f.id);
  }
}

TEST_CASE("Resolver: a ready function says switched off or on; a summary counts what else is open (SD-083)") {
  Catalog cat = Catalog::builtin();
  SensorTruth truth(cat);
  DeviceReport head;
  head.id = "PH-1";
  head.cls = "head_ph_ec";
  head.online = true;
  Config cfg;
  cfg.devices = {{"PH-1", "head_ph_ec", "pH/EC"}};
  auto resolve = [&] { return resolveFunctions(cat, cfg, {head}, pumpsFrom({head}), truth); };
  // Not always on: off and on are told apart
  cfg.tank().roles["tank.water_temp"] = {"PH-1", 0};
  cfg.functions["water_temp_watch"].enabled = false;
  REQUIRE(fn(resolve(), "water_temp_watch").setup == "ready");
  CHECK(fn(resolve(), "water_temp_watch").summary.key == "fn.ready");
  CHECK(fn(resolve(), "water_temp_watch").summary.text == "Ready, switched off");
  cfg.functions["water_temp_watch"].enabled = true;
  CHECK(fn(resolve(), "water_temp_watch").summary.key == "fn.on");
  // Two readings to assign: the first, and one more
  auto open = [](const FunctionState& f) {
    int n = 0;
    for (const auto& c : f.checks) n += !c.ok && !c.soft && c.level == "setup";
    return n;
  };
  FunctionState mon = fn(resolve(), "monitor_tank");
  REQUIRE(mon.setup == "needs_setup");
  REQUIRE(open(mon) == 2);
  CHECK(mon.summary.key == "fn.needs_setup_more");
  CHECK(mon.summary.args["more"] == 1);
  CHECK(mon.summary.args["first"]["key"] == "req.assign");
  // One left: no count
  cfg.tank().roles["tank.ph"] = {"PH-1", 0};
  mon = fn(resolve(), "monitor_tank");
  REQUIRE(open(mon) == 1);
  CHECK(mon.summary.key == "fn.needs_setup");
  CHECK(mon.summary.args["more"] == 0);
  // Without any device: what is missing, and how many more
  const FunctionState none = fn(resolveFunctions(cat, Config{}, {}, {}, truth), "ph_control");
  int hw = 0;
  for (const auto& c : none.checks) hw += !c.ok && c.level == "hardware";
  CHECK(none.summary.key == (hw > 1 ? "fn.unavailable_more" : "fn.unavailable"));
  if (hw > 1) CHECK(none.summary.args["more"] == hw - 1);
}

TEST_CASE("Resolver: a bottle line names its kind; a pump to calibrate names its bottle, also with a | in the name (SD-083)") {
  Catalog cat = Catalog::builtin();
  SensorTruth truth(cat);
  DeviceReport cap;
  cap.id = "C1";
  cap.cls = "pump_cap";
  cap.online = true;
  Config cfg;
  auto keyOf = [&](const std::string& id, const std::string& key) -> std::optional<Msg> {
    for (const auto& c : fn(resolveFunctions(cat, cfg, {cap}, pumpsFrom({cap}), truth), id).checks)
      if (c.text.key == key) return c.text;
    return std::nullopt;
  };
  // No bottle yet
  CHECK(keyOf("manual_dose", "req.add_bottle"));
  auto nutrient = keyOf("mix", "req.add_bottle_kind");
  REQUIRE(nutrient);
  CHECK(nutrient->args["kind"]["key"] == "req.kind.nutrient");
  CHECK(nutrient->text == "Add a nutrient bottle and assign its pump");
  auto phDown = keyOf("ph_control", "req.add_bottle_kind");
  REQUIRE(phDown);
  CHECK(phDown->args["kind"]["key"] == "req.kind.ph_down");
  // A pH− bottle on a pump without a calibration value: that pump, by its bottle's name
  CanisterCfg k;
  k.id = "k1";
  k.name = "pH− | old";
  k.kind = "ph_down";
  k.pump = "C1";
  cfg.canisters = {k};
  auto calibrate = keyOf("ph_control", "req.calibrate_pump");
  REQUIRE(calibrate);
  CHECK(calibrate->args["name"] == "pH− | old");
  // Calibrated: done, by kind
  cap.info = {{"flowMlPerMin", 50.0}};
  CHECK(keyOf("ph_control", "req.pumps_calibrated_kind"));
}

TEST_CASE("Resolver: a level sensor is calibrated as a sensor, not as a probe (SD-083)") {
  Catalog cat = Catalog::builtin();
  Config cfg;
  SensorTruth truth(cat);
  DeviceReport lvl;
  lvl.id = "LVL-1";
  lvl.cls = "head_level";
  lvl.online = true;
  cfg.devices = {{"LVL-1", "head_level", "Level"}};
  cfg.tank().roles["tank.level"] = {"LVL-1", 0};
  auto keys = [&] {
    std::set<std::string> out;
    for (const auto& c : fn(resolveFunctions(cat, cfg, {lvl}, pumpsFrom({lvl}), truth), "refill").checks) out.insert(c.text.key);
    return out;
  };
  CHECK(keys().count("req.calibrate_level") == 1);
  CHECK(keys().count("req.calibrate") == 0);
  cfg.calibrations["LVL-1"]["tank_curve"] = {{"points", {{0.0, 0.0}, {1.0, 100.0}}}};
  CHECK(keys().count("req.calibrated_level") == 1);
  CHECK(keys().count("req.calibrated") == 0);
}

TEST_CASE("Resolver: pH regeln verlangt Kalibrierung, pH−, Umwälzen (Funktionskette)") {
  Catalog cat = Catalog::builtin();
  Config cfg;
  cfg.devices = {{"PH", "head_ph_ec", ""}, {"C1", "pump_cap", ""}};
  cfg.tank().roles["tank.ph"] = {"PH", 0};
  cfg.tank().roles["tank.ec"] = {"PH", 0};
  SensorTruth truth(cat);
  DeviceReport head;
  head.id = "PH";
  head.cls = "head_ph_ec";
  head.online = true;
  auto v = resolveFunctions(cat, cfg, {head}, {}, truth);
  const auto& ph = fn(v, "ph_control");
  CHECK(ph.setup == "needs_setup");
  int missing = 0;
  for (const auto& c : ph.checks) missing += (!c.ok && c.level == "setup");
  CHECK(missing >= 3);  // pH kalibrieren, EC kalibrieren, pH−, Umwälzen
}
