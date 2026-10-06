#include <doctest/doctest.h>

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
  CHECK(ph.summary.text.find("Kopf pH/EC") != std::string::npos);
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
  CHECK(fn(w, "mix").summary.text.find("einmessen") != std::string::npos);
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
