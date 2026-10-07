// SPDX-License-Identifier: AGPL-3.0-or-later
#include <doctest/doctest.h>

#include <cmath>

#include "gc/events.hpp"
#include "gc/history.hpp"

using namespace gc;

TEST_CASE("Verlauf: feinste Stufe, Lücken bleiben Lücken") {
  History h;
  Epoch t0 = 1790000000 - 1790000000 % 3600;
  for (int i = 0; i < 360; ++i) h.add("ph", t0 + i * 10, i == 100 ? kNaN : 5.8 + 0.001 * i);
  auto p = h.query("ph", t0, t0 + 3599, 0);
  CHECK(p.stepS == 10);
  CHECK(p.t.size() == 360);
  CHECK(std::isnan(p.avg[100]));
  CHECK(p.avg[0] == doctest::Approx(5.8));
}

TEST_CASE("Verlauf: Ausdünnen behält Minimum und Maximum") {
  History h;
  Epoch t0 = 1790000000;
  for (int i = 0; i < 600; ++i) h.add("ec", t0 + i * 10, i == 300 ? 3.0 : 1.5);
  auto p = h.query("ec", t0, t0 + 6000, 50);
  CHECK(p.t.size() <= 50);
  double mx = 0;
  for (double v : p.max) mx = std::max(mx, v);
  CHECK(mx == doctest::Approx(3.0));
}

TEST_CASE("Verlauf: ältere Zeiträume kommen aus gröberen Stufen") {
  History h;
  Epoch t0 = 1790000000 - 1790000000 % 900;
  for (int i = 0; i < 3 * 24 * 360; ++i) h.add("t", t0 + i * 10, 20.0);  // 3 Tage
  Epoch now = t0 + 3 * 24 * 3600;
  CHECK(h.query("t", now - 3600, now, 0).stepS == 10);
  CHECK(h.query("t", now - 2 * 24 * 3600, now, 0).stepS == 60);
}

TEST_CASE("Verlauf: Sichern und Laden") {
  History h;
  for (int i = 0; i < 100; ++i) h.add("ph", 1790000000 + i * 10, 6.0);
  History k;
  REQUIRE(k.load(h.dump()));
  CHECK(k.query("ph", 1790000000, 1790001000, 0).t.size() == h.query("ph", 1790000000, 1790001000, 0).t.size());
  CHECK_FALSE(k.load("kaputt"));
}

TEST_CASE("Ereignislog: neueste zuerst, Filter, Obergrenze") {
  EventLog log(3);
  for (int i = 0; i < 5; ++i) log.add(100 + i, i % 2 ? "dose" : "mix", "info", "E" + std::to_string(i), "");
  auto all = log.query(0, 1000, "", 10);
  REQUIRE(all.size() == 3);
  CHECK(all[0].title == "E4");
  CHECK(log.query(0, 1000, "dose", 10).size() == 1);
  EventLog k;
  k.load(log.toJson());
  CHECK(k.lastId() == 5);
}
