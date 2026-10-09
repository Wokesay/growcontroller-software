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
