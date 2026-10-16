// SPDX-License-Identifier: AGPL-3.0-or-later
#include <doctest/doctest.h>

#include <cmath>

#include "fakes.hpp"
#include "gc/embedded.hpp"
#include "gc/messages.hpp"
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
  const Reading bad = f.run("tank.ph");
  CHECK(bad.quality == Quality::Implausible);
  // The reason as a message: the value at the sensor's decimals, its range, in the page's language (SD-083)
  CHECK(bad.reason.key == "truth.implausible");
  const double v = bad.reason.args["value"].get<double>();
  CHECK(v == std::round(v * 100) / 100);  // at the sensor's two decimals
  CHECK(bad.reason.args["min"] == 3);
  CHECK(bad.reason.args["max"] == 9);
  CHECK(bad.reason.text.find("outside the possible range 3–9 –") != std::string::npos);  // pH has no unit
}

TEST_CASE("Sensor truth: a jump other than pH names no dose of the hub's own, which announces itself (RAT-042, #162)") {
  Fix f;
  for (int i = 0; i < 6; ++i) {
    f.bus.set("PH-1", "measure.ec", 1.20 + 0.001 * i, f.clk.ms);
    f.run("tank.ec");
    f.clk.ms += 5000;
  }
  f.bus.set("PH-1", "measure.ec", 2.40, f.clk.ms);  // +1.2 mS/cm in seconds
  const Reading r = f.run("tank.ec");
  REQUIRE(r.quality == Quality::Jump);
  CHECK(r.reason.key == "truth.jump");
  CHECK(r.reason.text.find("dose") == std::string::npos);
  CHECK(r.reason.text.find("at the tank") != std::string::npos);
  CHECK(r.reason.text.find("near the plants") == std::string::npos);
  CHECK(r.reason.text.find("check the sensor") != std::string::npos);
  CHECK(r.reason.text.find("2.4\u00a0mS/cm") != std::string::npos);
}

TEST_CASE("Sensor truth: a jump of a reading in the grow area names work near the plants, not at the tank (RAT-042)") {
  Fix f;
  f.cfg.devices.push_back({"CL-1", "head_climate", "Climate"});
  f.cfg.rolesFor("zone.air_temp")["zone.air_temp"] = {"CL-1", 0};
  f.bus.head("CL-1", true, "head_climate");
  for (int i = 0; i < 6; ++i) {
    f.bus.set("CL-1", "measure.air_temp", 24.0 + 0.01 * i, f.clk.ms);
    f.run("zone.air_temp");
    f.clk.ms += 5000;
  }
  f.bus.set("CL-1", "measure.air_temp", 31.0, f.clk.ms);  // +7 °C in seconds
  const Reading r = f.run("zone.air_temp");
  REQUIRE(r.quality == Quality::Jump);
  CHECK(r.reason.key == "truth.jump_zone");
  CHECK(r.reason.text.find("near the plants") != std::string::npos);
  CHECK(r.reason.text.find("at the tank") == std::string::npos);
  CHECK(r.reason.text.find("31\u00a0°C") != std::string::npos);
}

TEST_CASE("Sensor truth: a jump lock whose values are gone shows \"–\" for them, never 0 and never a fall (R5, RAT-044)") {
  Fix f;
  f.ph(7.18);
  f.run("tank.ph");
  f.clk.ms += 5000;
  f.rt.jumpLocks["tank.ph"] = f.clk.epoch() + 900;  // a lock without the latch that holds from and to
  f.ph(7.181);
  const Reading r = f.run("tank.ph");
  REQUIRE(r.quality == Quality::Jump);
  CHECK(r.reason.key == "truth.jump_ph");
  CHECK(r.reason.args["from"].is_null());
  CHECK(r.reason.args["to"].is_null());
  CHECK(r.reason.text.find("from – to –") != std::string::npos);
}

TEST_CASE("Sensor truth: a pH fall names the hub's own dose as one possible cause, and that the hub only lowers pH (RAT-041, RAT-042, #162)") {
  Fix f;
  for (int i = 0; i < 6; ++i) {
    f.ph(7.40 - 0.001 * i);  // ≈ pH 7.2
    f.run("tank.ph");
    f.clk.ms += 5000;
  }
  f.ph(6.18);  // ≈ −1.25 pH in seconds
  const Reading fell = f.run("tank.ph");
  REQUIRE(fell.quality == Quality::Jump);
  CHECK(fell.reason.key == "truth.jump_ph_fall");
  CHECK(fell.reason.args["to"].get<double>() < fell.reason.args["from"].get<double>());
  CHECK(fell.reason.text.find("the hub's pH dose") != std::string::npos);
  CHECK(fell.reason.text.find("Measure by hand") != std::string::npos);
  CHECK(fell.reason.text.find("the hub only lowers pH") != std::string::npos);
}

TEST_CASE("Sensor truth: a level jump asks about filling or draining by hand without maintenance mode (RAT-042)") {
  Fix f;
  f.cfg.devices.push_back({"LVL-1", "head_level", "Level"});
  f.cfg.tank().roles["tank.level"] = {"LVL-1", 0};
  f.cfg.calibrations["LVL-1"]["tank_curve"] = {{"points", {{0.0, 0.0}, {1.0, 100.0}}}};
  f.bus.head("LVL-1", true, "head_level");
  for (int i = 0; i < 6; ++i) {
    f.bus.set("LVL-1", "measure.level", 0.50 + 0.001 * i, f.clk.ms);  // ≈ 50 L
    f.run("tank.level");
    f.clk.ms += 5000;
  }
  f.bus.set("LVL-1", "measure.level", 0.80, f.clk.ms);  // +30 L in seconds
  const Reading r = f.run("tank.level");
  REQUIRE(r.quality == Quality::Jump);
  CHECK(r.reason.key == "truth.jump_level");
  CHECK(r.reason.text.find("Filled or drained by hand? Then start maintenance mode first next time.") != std::string::npos);
  CHECK(r.reason.text.find("80\u00a0L") != std::string::npos);
}

TEST_CASE("Sensorwahrheit: Sprungsperre, überlebt Neustart, frei nach 15 min Ruhe (M8-2)") {
  Fix f;
  for (int i = 0; i < 6; ++i) {
    f.ph(6.18 + 0.001 * i);  // ≈ pH 6,0
    f.run("tank.ph");
    f.clk.ms += 5000;
  }
  f.ph(7.40);  // ≈ +1,25 pH in Sekunden, ohne Dosierung
  const Reading jumped = f.run("tank.ph");
  CHECK(jumped.quality == Quality::Jump);
  // A rise is never the hub's own dose, which only lowers pH: the reason names the probe and work at the tank (RAT-041, RAT-042)
  CHECK(jumped.reason.key == "truth.jump_ph");
  CHECK(jumped.reason.args["from"].is_number());
  CHECK(jumped.reason.args["to"].is_number());
  CHECK(jumped.reason.text.find("rose from") != std::string::npos);
  CHECK(jumped.reason.text.find("check the probe against a hand measurement") != std::string::npos);
  CHECK(jumped.reason.text.find("dose") == std::string::npos);
  // The lock lifts on time alone, so the text sets no condition on the probe (RAT-044)
  CHECK(jumped.reason.text.find("The lock lifts after 15\u00a0min steady.") != std::string::npos);
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
  // An announced change (mix, hand dose, EC round, inlet) explains a jump; the hub's own
  // pH dose is not announced (RAT-042)
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
  gc::Msg err;
  auto c = Curve::fromJson({{"points", {{0.5, 0.0}, {0.59, 3.0}, {1.07, 15.0}, {2.39, 48.0}}}}, err);
  REQUIRE(c);
  CHECK(c->map(0.4) == doctest::Approx(0.0));
  CHECK(c->map(0.59) == doctest::Approx(3.0));
  CHECK(c->map(0.83) == doctest::Approx(9.0));
  CHECK(c->map(2.79) == doctest::Approx(58.0));  // über dem letzten Punkt mit dessen Steigung
  CHECK_FALSE(Curve::fromJson({{"points", {{0.5, 0.0}, {0.502, 3.0}}}}, err));  // < 3 mV
  CHECK(err.key == "curve.order");
  CHECK_FALSE(Curve::fromJson({{"points", {{0.5, 0.0}}}}, err));
  CHECK(err.key == "curve.two");
  CHECK_FALSE(Curve::fromJson({{"points", {{0.5, 5.0}, {1.0, 3.0}}}}, err));
  CHECK(err.key == "curve.falls");
  CHECK_FALSE(Curve::fromJson({{"points", {{0.5, 0.0}, {"x", 3.0}}}}, err));
  CHECK(err.key == "curve.point");
  CHECK_FALSE(Curve::fromJson(gc::json::object(), err));
  CHECK(err.key == "curve.missing");
  CHECK(err.text == "No points recorded");  // the hub's text is English; the page translates by the key (SD-083)
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
  bus.head("HA-PH", true, "ext_ph");
  bus.head("HA-T", true, "ext_temp");
  bus.set("HA-PH", "measure.ph", 6.2, clk.ms);
  bus.set("HA-T", "measure.water_temp", 21.0, clk.ms);
  truth.update(cfg, bus, rt, clk.nowMs(), clk.epoch());
  const auto& r = truth.get("tank.ph");
  REQUIRE(r.value.has_value());
  CHECK(*r.value == doctest::Approx(6.2));  // shown
  CHECK(r.quality == Quality::Uncalibrated);
  CHECK(r.reason.key == kCalibratedElsewhere);  // the key the watchdog and the web app look for
  CHECK_FALSE(r.usable());                  // no value for control
  CHECK(truth.get("tank.water_temp").quality == Quality::Ok);  // needs no calibration anyway
  // A calibration stored for it changes nothing: the hub has not checked the one in use
  cfg.calibrations["HA-PH"]["ph"] = {{"points", {{7.0, 7.0}, {4.0, 4.0}}}};
  truth.update(cfg, bus, rt, clk.nowMs(), clk.epoch());
  CHECK_FALSE(truth.get("tank.ph").usable());
  // Freshness, plausibility and jumps still apply, so a broken probe shows as broken (RAT-021)
  clk.ms += 1000;
  bus.set("HA-PH", "measure.ph", 12.0, clk.ms);
  truth.update(cfg, bus, rt, clk.nowMs(), clk.epoch());
  CHECK(truth.get("tank.ph").quality == Quality::Implausible);
  auto steady = [&](double v, int minutes) {
    for (int i = 0; i < minutes; ++i) {
      clk.ms += 60 * 1000;
      bus.set("HA-PH", "measure.ph", v, clk.ms);
      truth.update(cfg, bus, rt, clk.nowMs(), clk.epoch());
    }
  };
  // No standstill check: what arrives is rounded and calibrated, not a raw
  // signal, and a steady tank is no fault (RAT-059); freshness covers silence
  steady(6.2, 17);
  CHECK(truth.get("tank.ph").reason.key == "truth.external");
  CHECK_FALSE(truth.get("tank.ph").usable());
  clk.ms += 61 * 1000;  // not reported again
  truth.update(cfg, bus, rt, clk.nowMs(), clk.epoch());
  CHECK(truth.get("tank.ph").quality == Quality::Stale);
  // A jump locks as for any probe (RAT-039) ...
  bus.set("HA-PH", "measure.ph", 6.2, clk.ms);
  truth.update(cfg, bus, rt, clk.nowMs(), clk.epoch());
  clk.ms += 60 * 1000;
  bus.set("HA-PH", "measure.ph", 7.4, clk.ms);
  truth.update(cfg, bus, rt, clk.nowMs(), clk.epoch());
  CHECK(truth.get("tank.ph").quality == Quality::Jump);
  CHECK(rt.jumpLocks.count("tank.ph") == 1);
  steady(7.4, 16);
  CHECK(truth.get("tank.ph").reason.key == "truth.external");
  CHECK(rt.jumpLocks.count("tank.ph") == 0);
  // ... and an announced change, such as calibrating in maintenance mode, explains it (RAT-042)
  truth.expectChange("tank.ph", clk.nowMs() + 5 * 60 * 1000);
  clk.ms += 1000;
  bus.set("HA-PH", "measure.ph", 6.0, clk.ms);
  truth.update(cfg, bus, rt, clk.nowMs(), clk.epoch());
  CHECK(truth.get("tank.ph").reason.key == "truth.external");
  CHECK(rt.jumpLocks.count("tank.ph") == 0);
  CHECK_FALSE(truth.get("tank.ph").usable());
}

TEST_CASE("Sensor truth: a sensor on its own schedule is stale only when it misses its usual reports (SD-035)") {
  auto j = json::parse(gc::embedded::kCatalogJson);
  j["deviceClasses"]["ext_temp"] = {{"label", "Temperature from elsewhere"}, {"attach", "ha"}, {"provides", {"measure.water_temp"}}};
  j["deviceClasses"]["ext_temp"]["ownReportSchedule"] = true;  // ignored: the catalog cannot loosen freshness (R7)
  CHECK_FALSE(Catalog::fromJson(j).deviceClass("ext_temp")->ownReportSchedule);
  Catalog cat = Catalog::fromJson(j);
  cat.deviceClasses["ext_temp"].ownReportSchedule = true;  // set in code, as the Home Assistant adapter does
  REQUIRE(cat.capability("measure.water_temp")->maxAgeS == 120);
  Config cfg;
  RuntimeState rt;
  test::FakeBus bus;
  test::Clock clk;
  SensorTruth truth{cat};
  cfg.devices = {{"HA-T", "ext_temp", "Water"}, {"HA-T2", "ext_temp", "Water 2"}};
  cfg.tank().roles["tank.water_temp"] = {"HA-T", 0};
  bus.head("HA-T", true, "ext_temp");
  bus.head("HA-T2", true, "ext_temp");
  constexpr Ms kS = 1000, kMin = 60 * kS;
  auto quality = [&] {
    truth.update(cfg, bus, rt, clk.nowMs(), clk.epoch());
    return truth.get("tank.water_temp").quality;
  };
  // The sensor reports every `gap`, `n` times; the hub looks every second in between.
  auto reports = [&](const std::string& dev, Ms gap, int n) {
    for (int i = 0; i < n; ++i) {
      for (Ms t = 0; t < gap; t += kS) {
        clk.ms += kS;
        quality();
      }
      bus.set(dev, "measure.water_temp", 21.0, clk.ms);
      quality();
    }
  };
  // Is it still valid when its last report is `age` old?
  auto validAt = [&](Ms age) {
    const Ms last = bus.samples["HA-T|measure.water_temp"].ts;
    clk.ms = last + age;
    return quality() == Quality::Ok;
  };

  // Right after the start nothing is learned: a report 4 min old is no fault
  // (the catalog's 2 min would call it stale); 10 min is the rough limit.
  clk.ms = 30 * kMin;
  bus.set("HA-T", "measure.water_temp", 21.0, clk.ms - 4 * kMin);
  CHECK(quality() == Quality::Ok);
  CHECK(validAt(10 * kMin - kS));
  CHECK_FALSE(validAt(10 * kMin + kS));
  CHECK(truth.get("tank.water_temp").quality == Quality::Stale);

  // Every 5 min, learned after three gaps: stale after 3 × 5 = 15 min
  reports("HA-T", 5 * kMin, 3);
  CHECK(validAt(15 * kMin - kS));
  CHECK_FALSE(validAt(15 * kMin + kS));
  const Reading& stale = truth.get("tank.water_temp");
  CHECK(stale.reason.key == "truth.stale");
  CHECK(stale.reason.args.value("limitS", 0) == 900);  // the web app can say what was expected

  // Every 2 min: 6 min, valid up to the limit itself
  reports("HA-T", 2 * kMin, 11);
  CHECK(validAt(6 * kMin));
  CHECK_FALSE(validAt(6 * kMin + kS));

  // One outage (a restart of Home Assistant) does not loosen the limit
  reports("HA-T", 20 * kMin, 1);
  reports("HA-T", 2 * kMin, 2);
  CHECK(validAt(6 * kMin - kS));
  CHECK_FALSE(validAt(6 * kMin + kS));

  // Never sooner than 5 min: such sensors drop a few reports now and then
  // (RAT-027), so every 5 s gives 5 min, not 15 s or the catalog's 2 min
  reports("HA-T", 5 * kS, 11);
  CHECK(validAt(5 * kMin - kS));
  CHECK_FALSE(validAt(5 * kMin + kS));

  // Never more than 30 min, however rarely it reports
  reports("HA-T", 20 * kMin, 3);
  CHECK(validAt(30 * kMin - kS));
  CHECK_FALSE(validAt(30 * kMin + kS));

  // The same report dated again by a second or so (after a lost connection
  // the hub dates it anew, to Home Assistant's whole seconds) is no gap
  reports("HA-T", 3 * kMin, 10);  // 9 min
  const Ms last = clk.ms;
  for (int i = 0; i < 24; ++i) {
    bus.set("HA-T", "measure.water_temp", 21.0, last + (i % 2 ? 1500 : 800));
    clk.ms += kS;
    quality();
  }
  bus.set("HA-T", "measure.water_temp", 21.0, last);
  CHECK(validAt(8 * kMin));  // with a dozen gaps under a second learned it would be 5 min

  // "unavailable" in Home Assistant shows at once, whatever was learned
  bus.devs[0].online = false;
  CHECK(quality() == Quality::Offline);
  bus.devs[0].online = true;

  // Another sensor for the role starts over: 10 min until three gaps are seen
  reports("HA-T", 5 * kS, 11);  // 5 min
  bus.set("HA-T2", "measure.water_temp", 22.0, clk.ms - 8 * kMin);
  cfg.tank().roles["tank.water_temp"] = {"HA-T2", 0};
  CHECK(quality() == Quality::Ok);  // with what HA-T taught, 8 min would be stale
  const Ms start = clk.ms;
  bus.set("HA-T2", "measure.water_temp", 22.0, start);  // gap 1: 8 min
  quality();
  clk.ms += kMin;
  bus.set("HA-T2", "measure.water_temp", 22.0, clk.ms);  // gap 2: 1 min
  quality();
  // A clock step back is no gap: still two, so still 10 min
  clk.ms += kS;
  bus.set("HA-T2", "measure.water_temp", 22.0, start + 30 * kS);
  quality();
  clk.ms = start + 30 * kS + 7 * kMin;
  CHECK(quality() == Quality::Ok);  // learned from [8 min, 1 min, a step back] it would be 5 min
}

TEST_CASE("Sensor truth: the hub's own probes keep the catalog's limit, however they report (SD-035)") {
  Fix f;
  for (int i = 0; i < 6; ++i) {  // a probe the hub reads only every 5 min
    f.ph(7.18 + 0.001 * i);
    f.run("tank.ph");
    f.clk.ms += 5 * 60 * 1000;
  }
  f.ph(7.19);
  f.run("tank.ph");
  f.clk.ms += 59 * 1000;
  CHECK(f.run("tank.ph").quality == Quality::Ok);
  f.clk.ms += 2 * 1000;
  CHECK(f.run("tank.ph").quality == Quality::Stale);  // 60 s, as in the catalog
}

TEST_CASE("Sensor truth: VPD pairs the hub's own readings within 60 s, and readings on their own schedule while both are valid (SD-035)") {
  auto j = json::parse(gc::embedded::kCatalogJson);
  j["deviceClasses"]["ext_air"] = {{"label", "Air from elsewhere"}, {"attach", "ha"}, {"provides", {"measure.air_temp"}}};
  j["deviceClasses"]["ext_rh"] = {{"label", "Humidity from elsewhere"}, {"attach", "ha"}, {"provides", {"measure.humidity"}}};
  Catalog cat = Catalog::fromJson(j);
  cat.deviceClasses["ext_air"].ownReportSchedule = true;
  cat.deviceClasses["ext_rh"].ownReportSchedule = true;
  Config cfg;
  RuntimeState rt;
  test::FakeBus bus;
  test::Clock clk;
  SensorTruth truth{cat};
  constexpr Ms kS = 1000;
  clk.ms = 600 * kS;
  auto vpd = [&] {
    truth.update(cfg, bus, rt, clk.nowMs(), clk.epoch());
    return truth.get("zone.vpd");
  };
  // The hub's climate head: both readings come from one read, so 61 s apart means one is stuck
  cfg.devices = {{"CL-1", "head_climate", "Climate"}, {"CL-2", "head_climate", "Climate 2"}};
  cfg.zone().roles["zone.air_temp"] = {"CL-1", 0};
  cfg.zone().roles["zone.humidity"] = {"CL-2", 0};
  bus.head("CL-1", true, "head_climate");
  bus.head("CL-2", true, "head_climate");
  bus.set("CL-1", "measure.air_temp", 25.0, clk.ms - 61 * kS);
  bus.set("CL-2", "measure.humidity", 60.0, clk.ms);
  CHECK(vpd().reason.key == "truth.vpd.apart");
  CHECK_FALSE(vpd().value.has_value());
  bus.set("CL-1", "measure.air_temp", 25.0, clk.ms - 59 * kS);
  CHECK(vpd().usable());
  // Sensors in Home Assistant often report only on a change: a temperature
  // reported 2 min ago is still the current one while it is valid
  cfg.devices = {{"HA-AIR", "ext_air", "Air"}, {"HA-RH", "ext_rh", "Humidity"}};
  cfg.zone().roles["zone.air_temp"] = {"HA-AIR", 0};
  cfg.zone().roles["zone.humidity"] = {"HA-RH", 0};
  bus.head("HA-AIR", true, "ext_air");
  bus.head("HA-RH", true, "ext_rh");
  bus.set("HA-AIR", "measure.air_temp", 25.0, clk.ms - 120 * kS);
  bus.set("HA-RH", "measure.humidity", 60.0, clk.ms - 5 * kS);
  const Reading r = vpd();
  REQUIRE(truth.get("zone.air_temp").usable());
  REQUIRE(r.usable());
  CHECK(*r.value == doctest::Approx(SensorTruth::airVpdKPa(25.0, 60.0)));
  // Without a valid input there is no VPD, never 0 (R5)
  clk.ms += 10 * 60 * kS;  // the temperature is now stale
  bus.set("HA-RH", "measure.humidity", 60.0, clk.ms);
  const Reading none = vpd();
  REQUIRE(truth.get("zone.air_temp").quality == Quality::Stale);
  CHECK_FALSE(none.value.has_value());
}

TEST_CASE("Sensor truth: the history takes what passed every check, also calibrated elsewhere, and nothing else (SD-039)") {
  auto reading = [](Quality q, const char* key, std::optional<double> v) {
    Reading r;
    r.quality = q;
    r.reason.key = key;
    r.value = v;
    return r;
  };
  CHECK(reading(Quality::Ok, "truth.ok", 6.1).forHistory());
  CHECK(reading(Quality::Uncalibrated, kCalibratedElsewhere, 6.1).forHistory());
  // A probe of the hub without a calibration shows its raw signal: never as pH in the history
  CHECK_FALSE(reading(Quality::Uncalibrated, "truth.uncalibrated", 7.18).forHistory());
  CHECK_FALSE(reading(Quality::Stale, "truth.stale", 6.1).forHistory());
  CHECK_FALSE(reading(Quality::Implausible, "truth.implausible", 9.7).forHistory());
  CHECK_FALSE(reading(Quality::Jump, "truth.jump", 7.4).forHistory());
  CHECK_FALSE(reading(Quality::Frozen, "truth.frozen", 6.1).forHistory());
  CHECK_FALSE(reading(Quality::Ok, "truth.ok", std::nullopt).forHistory());
  CHECK_FALSE(reading(Quality::Uncalibrated, kCalibratedElsewhere, std::nullopt).forHistory());
  // As the sensor truth gives it for a hub probe without a calibration
  Fix f;
  f.cfg.calibrations.erase("PH-1");
  f.ph(7.18);
  const Reading& raw = f.run("tank.ph");
  REQUIRE(raw.value.has_value());  // shown as the raw signal
  CHECK_FALSE(raw.forHistory());
}

TEST_CASE("Sensor truth: VPD from one reading of the hub and one on its own schedule needs both valid only (SD-035)") {
  auto j = json::parse(gc::embedded::kCatalogJson);
  j["deviceClasses"]["ext_rh"] = {{"label", "Humidity from elsewhere"}, {"attach", "ha"}, {"provides", {"measure.humidity"}}};
  Catalog cat = Catalog::fromJson(j);
  cat.deviceClasses["ext_rh"].ownReportSchedule = true;
  Config cfg;
  RuntimeState rt;
  test::FakeBus bus;
  test::Clock clk;
  SensorTruth truth{cat};
  clk.ms = 600 * 1000;
  cfg.devices = {{"CL-1", "head_climate", "Climate"}, {"HA-RH", "ext_rh", "Humidity"}};
  cfg.zone().roles["zone.air_temp"] = {"CL-1", 0};
  cfg.zone().roles["zone.humidity"] = {"HA-RH", 0};
  bus.head("CL-1", true, "head_climate");
  bus.head("HA-RH", true, "ext_rh");
  bus.set("CL-1", "measure.air_temp", 25.0, clk.ms);
  bus.set("HA-RH", "measure.humidity", 60.0, clk.ms - 4 * 60 * 1000);  // reported on its last change
  truth.update(cfg, bus, rt, clk.nowMs(), clk.epoch());
  CHECK(truth.get("zone.vpd").usable());
}

TEST_CASE("Sensor truth: the age says what it counts from; a sign of life sets the clock and the 60 s limit (SD-042)") {
  // The hub's own probe: the age of its reading and the catalog's limit, whatever a bus says (R7)
  {
    Fix f;
    f.clk.ms = 600000;
    gc::Sample own(7.18, f.clk.ms - 5000);
    own.life = gc::Sample::Life::Ok;
    own.measuredAt = f.clk.ms;
    f.bus.samples["PH-1|measure.ph"] = own;
    const Reading& r = f.run("tank.ph");
    CHECK(r.ageOf == "reading");
    CHECK(r.ageMs == 5000);
    CHECK(f.truth.limit("tank.ph") == 60000);
  }
  // A sensor on its own schedule (its class says so in code) with a sign of life
  auto j = json::parse(gc::embedded::kCatalogJson);
  j["deviceClasses"]["ext_temp"] = {{"label", "Temperature from elsewhere"}, {"attach", "ha"}, {"provides", {"measure.water_temp"}}};
  Catalog cat = Catalog::fromJson(j);
  cat.deviceClasses["ext_temp"].ownReportSchedule = true;
  Config cfg;
  RuntimeState rt;
  test::FakeBus bus;
  test::Clock clk;
  clk.ms = 600000;
  SensorTruth truth{cat};
  cfg.devices = {{"HA-T", "ext_temp", "Water"}};
  cfg.tank().roles["tank.water_temp"] = {"HA-T", 0};
  bus.head("HA-T", true, "ext_temp");
  auto run = [&]() -> const Reading& {
    truth.update(cfg, bus, rt, clk.nowMs(), clk.epoch());
    return truth.get("tank.water_temp");
  };
  bus.set("HA-T", "measure.water_temp", 21.5, clk.ms - 300000);  // reported 5 min ago, unchanged since
  CHECK(run().ageOf == "change");
  gc::Sample& s = bus.samples["HA-T|measure.water_temp"];
  s.life = gc::Sample::Life::Ok;
  s.measuredAt = clk.ms - 10000;  // but measured 10 s ago
  const Reading& r = run();
  CHECK(r.ageOf == "measurement");
  CHECK(r.ageMs == 10000);
  CHECK(r.quality == Quality::Ok);
  CHECK(truth.limit("tank.water_temp") == kLifeLimit);
  s.measuredAt = clk.ms - 60000;
  CHECK(run().quality == Quality::Ok);  // 60 s is still valid
  s.measuredAt = clk.ms - 61000;
  CHECK(run().reason.key == "truth.life_stale");
  // A fault of the sign of life is stale with its reason, never a value
  s.life = gc::Sample::Life::Fault;
  s.lifeFault = say("truth.life_unreadable");
  CHECK(run().quality == Quality::Stale);
  CHECK(run().reason.key == "truth.life_unreadable");
  CHECK_FALSE(run().usable());
  // Waiting: no value for 60 s, then stale
  s.life = gc::Sample::Life::Waiting;
  s.measuredAt = clk.ms - 30000;
  CHECK(run().quality == Quality::NoData);
  CHECK_FALSE(run().value.has_value());
  s.measuredAt = clk.ms - 61000;
  CHECK(run().reason.key == "truth.life_never");
  // A sign-of-life text older than 60 s outweighs everything else
  s.life = gc::Sample::Life::Ok;
  s.measuredAt = clk.ms - 1000;
  s.lifeText = true;
  s.lifeAt = clk.ms - 61000;
  CHECK(run().reason.key == kDeviceSilent);  // the key the watchdog looks for
  // Without it again, the learned limit applies and the age counts from the change
  bus.set("HA-T", "measure.water_temp", 21.5, clk.ms - 300000);
  CHECK(run().ageOf == "change");
  CHECK(truth.limit("tank.water_temp") == 10 * kMinute);
}
