// SPDX-License-Identifier: AGPL-3.0-or-later
#include <doctest/doctest.h>

#include "gc/messages.hpp"
#include "gc/watchdog.hpp"

using namespace gc;

namespace {
struct Fix {
  Catalog cat = Catalog::builtin();
  Config cfg;
  RuntimeState rt;
  std::map<std::string, Reading> readings;
  Fix() {
    cfg.functions["ph_control"].enabled = true;
    Reading ph;
    ph.role = "tank.ph";
    ph.quality = Quality::Ok;
    ph.value = 5.85;
    readings["tank.ph"] = ph;
  }
  WatchResult eval(Epoch epoch = 1790010000, Epoch boot = 1790000000, Epoch maint = 0) {
    WatchInput in{cat, cfg, rt, readings, {}, {}, {}, epoch, boot, maint, false};
    return evaluate(in);
  }
  const Assessment* item(const WatchResult& w, const std::string& id) {
    for (const auto& a : w.items)
      if (a.id == id) return &a;
    return nullptr;
  }
};
}  // namespace

TEST_CASE("Watchdog: Wert im Band → OK") {
  Fix f;
  auto w = f.eval();
  CHECK(w.overall == "ok");
  CHECK(f.item(w, "band.ph")->status == "ok");
}

TEST_CASE("Watchdog: a value calibrated outside the hub says so; a problem only where a function needs it (RAT-015, RAT-021)") {
  Fix f;
  f.cfg.functions["ph_control"].enabled = false;
  Reading& ph = f.readings["tank.ph"];
  ph.quality = Quality::Uncalibrated;
  ph.reason = say(kCalibratedElsewhere);
  auto w = f.eval();
  CHECK(f.item(w, "reading.tank.ph")->status == "neutral");
  CHECK(f.item(w, "reading.tank.ph")->text.key == "watch.reading.external");
  CHECK(w.overall != "problem");
  f.cfg.functions["ph_control"].enabled = true;
  w = f.eval();
  CHECK(f.item(w, "reading.tank.ph")->status == "problem");
  CHECK(f.item(w, "reading.tank.ph")->text.key == "watch.reading.external");  // not "not calibrated": the hub cannot calibrate it
}

TEST_CASE("Watchdog: ungültiger Wert ist ein Problem, nie neutral (M9-1, RAT-015)") {
  Fix f;
  f.readings["tank.ph"].quality = Quality::Offline;
  f.readings["tank.ph"].value.reset();
  auto w = f.eval();
  CHECK(f.item(w, "reading.tank.ph")->status == "problem");
  CHECK(f.item(w, "band.ph")->status == "neutral");  // eine Ursache, eine Meldung
  CHECK(w.overall == "problem");
}

TEST_CASE("Watchdog: Alarmband weiter als Regelband (RAT-033)") {
  Fix f;
  f.readings["tank.ph"].value = 6.1;  // Ziel 5,8 ± 0,15 → Alarmband bis 6,15
  CHECK(f.item(f.eval(), "band.ph")->status == "ok");
  f.readings["tank.ph"].value = 6.3;
  CHECK(f.item(f.eval(), "band.ph")->status == "problem");
}

TEST_CASE("Watchdog: neutral mit Grund – Pflegemodus, Anlauf, kein Ziel") {
  Fix f;
  f.readings["tank.ph"].value = 7.5;
  CHECK(f.item(f.eval(1790010000, 1790000000, 1790020000), "band.ph")->text.key == "watch.maintenance");
  CHECK(f.item(f.eval(1790000060, 1790000000), "band.ph")->status == "neutral");
  f.cfg.functions["ph_control"].enabled = false;
  CHECK(f.item(f.eval(), "band.ph")->status == "neutral");
}

TEST_CASE("Watchdog: gesperrter Regler und fehlender Einmesswert sind Probleme") {
  Fix f;
  f.cfg.canisters = {{"a", "A", "nutrient", "C1", "", "", kNaN}};
  WatchInput in{f.cat, f.cfg, f.rt, f.readings, {}, {{"ph", say("watch.ctl.ph"), "blocked", say("ph.gate", {{"ec", 0.2}, {"floor", 0.5}})}}, {{"C1", kNaN}},
                1790010000, 1790000000, 0, false};
  auto w = evaluate(in);
  CHECK(f.item(w, "ctl.ph")->status == "problem");
  CHECK(f.item(w, "calib.a")->status == "problem");
}

TEST_CASE("Watchdog: a silent device is one problem titled with its readings; a sensor stale for its own reason keeps its item (SD-042, SD-047)") {
  Fix f;
  // Two sensors from Home Assistant share one device's sign-of-life text; EC has another device's
  f.cfg.devices = {{"ha.sensor.ph_mv", "ha_ph_mv", "pH", "sensor.hub_life", "ph"},
                   {"ha.sensor.water", "ha_water_temp", "Water", "sensor.hub_life", "wt"},
                   {"ha.sensor.ec", "ha_ec", "EC", "sensor.doser_life", "ec"}};
  f.cfg.tank().roles["tank.ph"] = {"ha.sensor.ph_mv", 0};
  f.cfg.tank().roles["tank.water_temp"] = {"ha.sensor.water", 0};
  f.cfg.tank().roles["tank.ec"] = {"ha.sensor.ec", 0};
  auto stale = [&](const std::string& role, Msg why) {
    Reading& r = f.readings[role];
    r.role = role;
    r.quality = Quality::Stale;
    r.value.reset();
    r.reason = std::move(why);
  };
  stale("tank.ph", say(kDeviceSilent, {{"ageS", 70}}));
  stale("tank.water_temp", say(kDeviceSilent, {{"ageS", 75}}));
  stale("tank.ec", say("truth.life_stale", {{"limitS", 60}}));
  auto eval = [&] {
    WatchInput in{f.cat, f.cfg, f.rt, f.readings, {}, {}, {}, 1790010000, 1790000000, 0, false};
    for (const auto& d : f.cfg.devices) in.devices.push_back({d.id, d.name, true});  // Home Assistant has them
    return evaluate(in);
  };
  WatchResult w = eval();
  const Assessment* dev = f.item(w, "life.sensor.hub_life");
  REQUIRE(dev != nullptr);
  CHECK(dev->status == "problem");
  // Titled with the readings it affects, as the items it replaces were
  CHECK(dev->label.key == "watch.life.label");
  const json sensors = dev->label.args["sensors"];
  REQUIRE(sensors.size() == 2);
  CHECK(sensors[0]["text"] == f.cat.role("tank.ph")->label);
  CHECK(sensors[1]["text"] == f.cat.role("tank.water_temp")->label);
  CHECK(dev->text.key == "watch.life.silent");
  CHECK(dev->text.args["source"] == "sensor.hub_life");
  CHECK(dev->text.args["ageS"] == 75);  // the oldest: nothing looks fresher than it is
  CHECK(f.item(w, "reading.tank.ph") == nullptr);
  CHECK(f.item(w, "reading.tank.water_temp") == nullptr);
  CHECK(f.item(w, "life.sensor.doser_life") == nullptr);
  // EC is stale for its own reason: its own item, with that reason
  REQUIRE(f.item(w, "reading.tank.ec") != nullptr);
  CHECK(f.item(w, "reading.tank.ec")->status == "problem");
  CHECK(f.item(w, "reading.tank.ec")->text.args["reason"]["key"] == "truth.life_stale");
  CHECK(f.item(w, "band.ph")->text.key == "watch.band.no_value");  // one cause, one message
  CHECK(w.problems == 2);
  // EC's device falls silent too, without a known age: a second item, the age stays missing (R5)
  stale("tank.ec", say(kDeviceSilent));
  w = eval();
  REQUIRE(f.item(w, "life.sensor.doser_life") != nullptr);
  CHECK(f.item(w, "life.sensor.doser_life")->text.args["ageS"].is_null());
  CHECK(f.item(w, "life.sensor.doser_life")->label.args["sensors"].size() == 1);
  CHECK(f.item(w, "reading.tank.ec") == nullptr);
  CHECK(w.problems == 2);
  // A sensor that says its device is silent but has no sign of life set any
  // more keeps its own item: nothing is hidden
  f.cfg.devices[1].life.clear();
  f.cfg.devices[1].lifeChannel.clear();
  w = eval();
  REQUIRE(f.item(w, "reading.tank.water_temp") != nullptr);
  CHECK(f.item(w, "reading.tank.water_temp")->status == "problem");
  REQUIRE(f.item(w, "life.sensor.hub_life") != nullptr);
  CHECK(f.item(w, "life.sensor.hub_life")->label.args["sensors"].size() == 1);
  CHECK(f.item(w, "life.sensor.hub_life")->text.args["ageS"] == 70);
  CHECK(w.problems == 3);
}
