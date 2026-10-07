// SPDX-License-Identifier: AGPL-3.0-or-later
// Raumklima: Klima-Kopf im Simulator, abgeleiteter VPD (fester Code, R5),
// Verlauf, Wirkung von Licht, Abluft, Befeuchter und Entfeuchter.
#include <doctest/doctest.h>

#include <cmath>

#include "client.hpp"
#include "gc/truth.hpp"

using gc::json;
using test::Client;

namespace {

template <typename F>
bool until(sim::Simulation& s, F cond, gc::Ms maxMs, gc::Ms step = 1000) {
  for (gc::Ms t = 0; t < maxMs; t += step) {
    if (cond()) return true;
    s.step(step);
  }
  return cond();
}

std::string plugClimate(sim::Simulation& s, Client& c) {
  {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    s.control("plug", {{"port", 6}, {"class", "head_climate"}});
  }
  std::string id;
  REQUIRE(until(s, [&] {
    auto st = c.state();
    for (const auto& d : st["devices"])
      if (d["class"] == "head_climate" && d["online"] == true) id = d["id"];
    return !id.empty();
  }, 10000));
  c.ok("POST", "/api/v1/devices/" + id + "/accept", {{"name", ""}});
  return id;
}

}  // namespace

TEST_CASE("VPD: Formel nach FAO-56") {
  CHECK(gc::SensorTruth::saturationKPa(25.0) == doctest::Approx(3.168).epsilon(0.001));
  CHECK(gc::SensorTruth::airVpdKPa(25.0, 60.0) == doctest::Approx(1.267).epsilon(0.002));
  CHECK(gc::SensorTruth::airVpdKPa(20.0, 100.0) == doctest::Approx(0.0));
}

TEST_CASE("VPD: aus Klima-Kopf abgeleitet, im Verlauf, bei Ausfall eine Lücke") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  CHECK(c.state()["readings"]["zone.vpd"]["quality"] == "not_bound");
  auto id = plugClimate(s, c);
  REQUIRE(until(s, [&] { return c.state()["readings"]["zone.vpd"]["usable"] == true; }, 60000));
  auto r = c.state()["readings"];
  double t = r["zone.air_temp"]["value"], h = r["zone.humidity"]["value"], v = r["zone.vpd"]["value"];
  CHECK(v == doctest::Approx(gc::SensorTruth::airVpdKPa(t, h)).epsilon(0.001));
  s.step(15 * 60 * 1000);
  auto hist = c.ok("GET", "/api/v1/history?series=zone.vpd");
  bool any = false;
  for (const auto& x : hist["series"][0]["avg"]) any = any || x.is_number();
  CHECK(any);
  {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    s.control("fault", {{"device", id}, {"fault", "offline"}});
  }
  REQUIRE(until(s, [&] { return c.state()["readings"]["zone.vpd"]["usable"] == false; }, 300000));
  CHECK(c.state()["readings"]["zone.vpd"]["value"].is_null());  // nie 0 (R5)
}

TEST_CASE("Raumklima im Simulator: Licht wärmt, Befeuchter feuchtet, Abluft kühlt") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  plugClimate(s, c);
  json r;
  {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    r = s.control("net_add", {{"class", "shelly_strip4"},
                              {"loads", {{{"load", "light"}, {"watts", 240}}, {{"load", "exhaust"}, {"watts", 35}}, {{"load", "humidifier"}, {"watts", 30}}}}});
  }
  std::string strip = r["id"];
  s.step(3000);
  c.ok("POST", "/api/v1/devices/" + strip + "/accept", {{"name", ""}});
  c.ok("PUT", "/api/v1/roles/zone.light", {{"device", strip}, {"channel", 0}});
  c.ok("PUT", "/api/v1/roles/zone.exhaust", {{"device", strip}, {"channel", 1}});
  c.ok("PUT", "/api/v1/roles/zone.humidifier", {{"device", strip}, {"channel", 2}});
  const double t0 = s.world().room.temp;
  c.ok("POST", "/api/v1/roles/zone.light/switch", {{"on", true}});
  s.step(60 * 60 * 1000);
  const double tLight = s.world().room.temp;
  CHECK(tLight > t0 + 2.0);
  c.ok("POST", "/api/v1/roles/zone.exhaust/switch", {{"on", true}});
  s.step(30 * 60 * 1000);
  CHECK(s.world().room.temp < tLight);
  const double rh0 = s.world().room.rh;
  c.ok("POST", "/api/v1/roles/zone.humidifier/switch", {{"on", true}});
  s.step(10 * 60 * 1000);
  CHECK(s.world().room.rh > rh0);
}
