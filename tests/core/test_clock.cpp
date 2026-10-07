// SPDX-License-Identifier: AGPL-3.0-or-later
// Time base of the hub after a restart (PD-069, PD-073, SD-028): secured
// time, the clock that continues from the saved time, operating time, and
// intervals that an outage never stretches.
#include <doctest/doctest.h>

#include <string>

#include "client.hpp"
#include "fakes.hpp"
#include "gc/clock.hpp"

using gc::json;
using test::Client;

namespace {

bool hasEvent(Client& c, const std::string& title) {
  const json events = c.ok("GET", "/api/v1/events?limit=2000")["events"];
  for (const auto& e : events)
    if (e.value("title", std::string()) == title) return true;
  return false;
}

}  // namespace

TEST_CASE("Clock: a secured platform clock passes through, operating time continues") {
  test::Clock base;
  gc::HubClock c(base);
  c.start(gc::Stamp{1700000000, true, 500});
  base.ms = 10000;
  CHECK(c.secured());
  CHECK(c.epoch() == base.epoch());
  CHECK(c.operatingS() == 510);
  CHECK(std::string(c.source()) == "secured");
}

TEST_CASE("Clock: without a secured time it continues from the saved time; the outage counts as 0") {
  test::Clock base;
  base.isSecured = false;
  base.start = 0;  // the device's own clock after a power-on reset: 1970
  base.ms = 5000;
  gc::HubClock c(base);
  c.start(gc::Stamp{1790003600, true, 7200});
  base.ms += 60000;
  CHECK_FALSE(c.secured());
  CHECK(c.epoch() == 1790003660);
  CHECK(c.operatingS() == 7260);
  CHECK(std::string(c.source()) == "continued");
  CHECK_FALSE(c.stamp().secured);
}

TEST_CASE("Clock: the continued clock never starts before the newest event") {
  test::Clock base;
  base.isSecured = false;
  base.start = 0;
  gc::HubClock c(base);
  // The state was saved at :00, an event followed at :40 before the power failed.
  c.start(gc::Stamp{1790003600, false, 7200}, 1790003640);
  CHECK(c.epoch() == 1790003640);
  c.start(std::nullopt, 1790003640);  // state lost, event log kept
  CHECK(c.epoch() == 1790003640);
  CHECK(c.operatingS() == 0);
}

TEST_CASE("Clock: nothing saved and no secured time leaves the platform clock, source unset") {
  test::Clock base;
  base.isSecured = false;
  base.start = 0;
  base.ms = 3000;
  gc::HubClock c(base);
  c.start(std::nullopt);
  CHECK(c.epoch() == 3);
  CHECK(std::string(c.source()) == "unset");
}

TEST_CASE("Clock: once secured, the platform time counts again") {
  test::Clock base;
  base.isSecured = false;
  base.start = 1790040000;  // real time: 10 h after the save below
  gc::HubClock c(base);
  c.start(gc::Stamp{1790004000, true, 100});
  CHECK(c.epoch() == 1790004000);
  base.isSecured = true;
  CHECK(c.epoch() == 1790040000);
  CHECK(std::string(c.source()) == "secured");
}

TEST_CASE("Clock: intervals use wall time only if both moments were secured, otherwise operating time") {
  const gc::Stamp a{1790000000, true, 1000};
  gc::Stamp b{1790036000, true, 4600};  // 10 h on the wall, 1 h of operation (9 h outage)
  CHECK(gc::elapsedS(a, b) == 36000);
  b.secured = false;
  CHECK(gc::elapsedS(a, b) == 3600);
  // A dose on the continued clock (10 h behind), then the time is secured
  // and the clock jumps forward: 14 h later the interval is 14 h, not 24 h,
  // so the limit of one emergency dose per 24 h holds (PD-063, PD-069).
  const gc::Stamp dose{1790000000 - 36000, false, 2000};
  const gc::Stamp later{1790000000 + 14 * 3600, true, 2000 + 14 * 3600};
  CHECK(gc::elapsedS(dose, later) == 14 * 3600);
  CHECK(gc::elapsedS(b, a) == 0);  // never negative
}

TEST_CASE("Clock: a stamp survives JSON; a missing or broken one stays unknown (R5)") {
  const gc::Stamp s{1790000000, true, 4242};
  auto back = gc::stampFromJson(json(s));
  REQUIRE(back);
  CHECK(back->at == s.at);
  CHECK(back->secured);
  CHECK(back->operatingS == 4242);
  CHECK_FALSE(gc::stampFromJson(json(nullptr)));
  CHECK_FALSE(gc::stampFromJson(json{{"at", 0}}));
  CHECK_FALSE(gc::stampFromJson(json{{"at", "gestern"}, {"operatingS", 1}}));
  CHECK_FALSE(gc::stampFromJson(json{{"at", 1}, {"operatingS", -5}}));
}

TEST_CASE("Restart without a secured time: the hub continues its clock and says so (PD-069)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  s.step(90000);  // the state is saved at least once
  auto before = c.state();
  CHECK(before["time"]["secured"] == true);
  CHECK(before["time"]["source"] == "secured");
  const gc::Epoch t0 = before["now"];
  const std::int64_t op0 = before["time"]["operatingS"];
  {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    s.control("reboot", {{"outageMin", 600}, {"timeSecured", false}});
  }
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  auto st = c.state();
  CHECK(st["time"]["secured"] == false);
  CHECK(st["time"]["source"] == "continued");
  const gc::Epoch now = st["now"];
  CHECK(now >= t0);                                  // continues from the saved time
  CHECK(now < s.clock().epoch() - 9 * 3600);         // the 10 h outage does not count
  CHECK(st["time"]["operatingS"].get<std::int64_t>() >= op0);
  CHECK(st["time"]["operatingS"].get<std::int64_t>() < op0 + 3600);
  s.step(150000);
  CHECK(hasEvent(c, "Uhrzeit nicht gesichert"));

  // Network time arrives: the platform time counts again, the jump is reported.
  {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    s.control("time", {{"secured", true}});
  }
  s.step(2000);
  st = c.state();
  CHECK(st["time"]["secured"] == true);
  CHECK(st["now"] == s.clock().epoch());
  CHECK(hasEvent(c, "Uhrzeit gesichert"));
}

TEST_CASE("Restart with a secured time: no clock events, the outage shows in the wall time") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  s.step(90000);
  {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    s.control("reboot", {{"outageMin", 30}});
  }
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  s.step(150000);
  auto st = c.state();
  CHECK(st["time"]["secured"] == true);
  CHECK(st["now"] == s.clock().epoch());
  CHECK_FALSE(hasEvent(c, "Uhrzeit nicht gesichert"));
  CHECK_FALSE(hasEvent(c, "Uhrzeit gesichert"));
}
