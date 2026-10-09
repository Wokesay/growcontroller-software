// SPDX-License-Identifier: AGPL-3.0-or-later
#include <doctest/doctest.h>

#include "fakes.hpp"
#include "gc/embedded.hpp"
#include "gc/truth.hpp"

using namespace gc;

namespace {
struct Fix {
  Catalog cat = Catalog::builtin();
  Config cfg;
  RuntimeState rt;
  test::FakeBus bus;
  test::Clock clk;
  SensorTruth truth{cat};
  Fix() {
    cfg.devices = {{"PH-1", "head_ph_ec", "pH/EC"}};
    cfg.tank().roles["tank.ph"] = {"PH-1", 0};
    cfg.tank().roles["tank.ec"] = {"PH-1", 0};
    cfg.calibrations["PH-1"]["ph"] = {{"points", {{7.18, 7.0}, {4.27, 4.0}}}};
    cfg.calibrations["PH-1"]["ec"] = {{"factor", 1.0}};
    bus.head("PH-1");
  }
  const Reading& run(const std::string& role) {
    truth.update(cfg, bus, rt, clk.nowMs(), clk.epoch());
    return truth.get(role);
  }
  void ph(double raw) { bus.set("PH-1", "measure.ph", raw, clk.ms); }
};
}  // namespace

TEST_CASE("Sensorwahrheit: Rolle ohne Gerät ist ein Befund, kein Wert") {
  Fix f;
  CHECK(f.run("tank.water_temp").quality == Quality::NotBound);
  CHECK_FALSE(f.run("tank.water_temp").value.has_value());
}

TEST_CASE("Sensorwahrheit: kein Messwert ist nie 0 (M8-1, RAT-006)") {
  Fix f;
  const auto& r = f.run("tank.ph");
  CHECK(r.quality == Quality::NoData);
  CHECK_FALSE(r.value.has_value());
  CHECK_FALSE(r.usable());
  f.bus.devs[0].online = false;
  CHECK(f.run("tank.ph").quality == Quality::Offline);
}

TEST_CASE("Sensorwahrheit: Kalibrierung über zwei Pufferpunkte") {
  Fix f;
  f.ph(7.18);
  CHECK(*f.run("tank.ph").value == doctest::Approx(7.0));
  f.clk.ms += 5000;
  f.ph(5.725);
  CHECK(*f.run("tank.ph").value == doctest::Approx(5.5).epsilon(0.001));
  CHECK(f.run("tank.ph").usable());
}

TEST_CASE("Sensorwahrheit: ohne gültige Kalibrierung kein Wert für die Regelung (RAT-025)") {
  Fix f;
  f.cfg.calibrations.erase("PH-1");
  f.ph(6.0);
  const auto& r = f.run("tank.ph");
  CHECK(r.quality == Quality::Uncalibrated);
  CHECK_FALSE(r.usable());
  // unplausible Steigung (Sonde defekt) zählt als ungültige Kalibrierung
  json bad = {{"points", {{7.0, 7.0}, {6.5, 4.0}}}};
  CHECK_FALSE(SensorTruth::calibrate("measure.ph", 6.0, &bad).has_value());
}

TEST_CASE("Sensorwahrheit: veraltet und unplausibel") {
  Fix f;
  f.ph(7.18);
  f.run("tank.ph");
  f.clk.ms += 61000;
  CHECK(f.run("tank.ph").quality == Quality::Stale);
  f.ph(9.9);  // ≈ pH 9,7 – außerhalb 3–9 (RAT-020)
  CHECK(f.run("tank.ph").quality == Quality::Implausible);
}

TEST_CASE("Sensorwahrheit: Sprungsperre, überlebt Neustart, frei nach 15 min Ruhe (M8-2)") {
  Fix f;
  for (int i = 0; i < 6; ++i) {
    f.ph(6.18 + 0.001 * i);  // ≈ pH 6,0
    f.run("tank.ph");
    f.clk.ms += 5000;
  }
  f.ph(7.40);  // ≈ +1,25 pH in Sekunden, ohne Dosierung
  CHECK(f.run("tank.ph").quality == Quality::Jump);
  CHECK(f.rt.jumpLocks.count("tank.ph") == 1);
  // "Neustart": neue Sensorwahrheit, gleicher Laufzeitzustand
  SensorTruth again(f.cat);
  f.clk.ms += 5000;
  f.ph(7.401);
  again.update(f.cfg, f.bus, f.rt, f.clk.nowMs(), f.clk.epoch());
  CHECK(again.get("tank.ph").quality == Quality::Jump);
  for (int i = 0; i < 200; ++i) {  // 16+ min ruhig
    f.clk.ms += 5000;
    f.ph(7.40 + 0.001 * (i % 3));
    again.update(f.cfg, f.bus, f.rt, f.clk.nowMs(), f.clk.epoch());
  }
  CHECK(again.get("tank.ph").quality == Quality::Ok);
  CHECK(f.rt.jumpLocks.empty());
}

TEST_CASE("Sensorwahrheit: eigene Gabe erklärt die Änderung (M8-3)") {
  Fix f;
  for (int i = 0; i < 4; ++i) {
    f.ph(6.18);
    f.run("tank.ph");
    f.clk.ms += 5000;
  }
  f.truth.expectChange("tank.ph", f.clk.ms + 10 * kMinute);
  f.ph(4.9);
  CHECK(f.run("tank.ph").quality == Quality::Ok);
}

TEST_CASE("Sensorwahrheit: stillstehender Rohwert wird erkannt (RAT-023)") {
  Fix f;
  f.ph(6.18);
  f.run("tank.ph");
  for (int i = 0; i < 190; ++i) {
    f.clk.ms += 5000;
    f.ph(6.18);
    f.run("tank.ph");
  }
  CHECK(f.run("tank.ph").quality == Quality::Frozen);
}

TEST_CASE("Kennlinie: stückweise linear, streng steigend (RAT-078, M10-4/M10-5)") {
  std::string err;
  auto c = Curve::fromJson({{"points", {{0.5, 0.0}, {0.59, 3.0}, {1.07, 15.0}, {2.39, 48.0}}}}, err);
  REQUIRE(c);
  CHECK(c->map(0.4) == doctest::Approx(0.0));
  CHECK(c->map(0.59) == doctest::Approx(3.0));
  CHECK(c->map(0.83) == doctest::Approx(9.0));
  CHECK(c->map(2.79) == doctest::Approx(58.0));  // über dem letzten Punkt mit dessen Steigung
  CHECK_FALSE(Curve::fromJson({{"points", {{0.5, 0.0}, {0.502, 3.0}}}}, err));  // < 3 mV
  CHECK_FALSE(Curve::fromJson({{"points", {{0.5, 0.0}}}}, err));
  CHECK_FALSE(Curve::fromJson({{"points", {{0.5, 5.0}, {1.0, 3.0}}}}, err));
}

TEST_CASE("Sensor truth: a value calibrated elsewhere is shown, but not used for control (RAT-025)") {
  auto j = json::parse(gc::embedded::kCatalogJson);
  j["deviceClasses"]["ext_ph"] = {{"label", "pH from elsewhere"}, {"attach", "ha"}, {"provides", {"measure.ph"}}};
  j["deviceClasses"]["ext_temp"] = {{"label", "Temperature from elsewhere"}, {"attach", "ha"}, {"provides", {"measure.water_temp"}}};
  j["deviceClasses"]["ext_ph"]["externalCalibration"] = true;  // ignored: the catalog cannot loosen this (R7)
  CHECK_FALSE(Catalog::fromJson(j).deviceClass("ext_ph")->externalCalibration);
  Catalog cat = Catalog::fromJson(j);
  cat.deviceClasses["ext_ph"].externalCalibration = true;  // set in code, as the Home Assistant adapter does
  cat.deviceClasses["ext_temp"].externalCalibration = true;
  Config cfg;
  RuntimeState rt;
  test::FakeBus bus;
  test::Clock clk;
  SensorTruth truth{cat};
  cfg.devices = {{"HA-PH", "ext_ph", "pH"}, {"HA-T", "ext_temp", "Water"}};
  cfg.tank().roles["tank.ph"] = {"HA-PH", 0};
  cfg.tank().roles["tank.water_temp"] = {"HA-T", 0};
  bus.head("HA-PH");
  bus.head("HA-T");
  bus.set("HA-PH", "measure.ph", 6.2, clk.ms);
  bus.set("HA-T", "measure.water_temp", 21.0, clk.ms);
  truth.update(cfg, bus, rt, clk.nowMs(), clk.epoch());
  const auto& r = truth.get("tank.ph");
  REQUIRE(r.value.has_value());
  CHECK(*r.value == doctest::Approx(6.2));  // shown
  CHECK(r.quality == Quality::Uncalibrated);
  CHECK(r.reason.key == "truth.external");
  CHECK_FALSE(r.usable());                  // no value for control
  CHECK(truth.get("tank.water_temp").quality == Quality::Ok);  // needs no calibration anyway
  // A calibration stored for it changes nothing: the hub has not checked the one in use
  cfg.calibrations["HA-PH"]["ph"] = {{"points", {{7.0, 7.0}, {4.0, 4.0}}}};
  truth.update(cfg, bus, rt, clk.nowMs(), clk.epoch());
  CHECK_FALSE(truth.get("tank.ph").usable());
}
