// SPDX-License-Identifier: AGPL-3.0-or-later
// Time base of the hub after a restart (PD-069, PD-073, SD-028): secured
// time, the clock that continues from the saved time, operating time, and
// intervals that an outage never stretches.
#include <doctest/doctest.h>

#include <string>

#include "client.hpp"
#include "fakes.hpp"
#include "gc/clock.hpp"
#include "gc/hub.hpp"

using gc::json;
using test::Client;

namespace {

// An event by its key (SD-032) or its title text, optionally for one output.
bool sameEvent(const json& e, const std::string& title, const std::string& label = "") {
  const json& t = e["title"];
  if (t.is_string()) return t == title;
  const bool hit = t.value("key", std::string()) == title || t.value("text", std::string()) == title;
  return hit && (label.empty() || (t.contains("args") && t["args"].value("label", std::string()) == label));
}

bool hasEvent(Client& c, const std::string& title) {
  const json events = c.ok("GET", "/api/v1/events?limit=2000")["events"];
  for (const auto& e : events)
    if (sameEvent(e, title)) return true;
  return false;
}

void fakeRandom(std::uint8_t* p, size_t n) {
  static std::uint8_t x = 7;
  for (size_t i = 0; i < n; ++i) p[i] = x += 31;
}

}  // namespace

TEST_CASE("Clock: a secured platform clock passes through, operating time continues") {
  test::Clock base;
  gc::HubClock c(base);
  c.start(gc::Stamp{1790000000, true, 500, 3}, 0, 4);
  base.ms = 10000;
  CHECK(c.secured());
  CHECK(c.epoch() == base.epoch());
  CHECK(c.operatingS() == 510);
  CHECK(c.stamp().boot == 4);
  CHECK(std::string(c.source()) == "secured");
}

TEST_CASE("Clock: without a secured time it continues from the saved time; the outage counts as 0") {
  test::Clock base;
  base.isSecured = false;
  base.start = 0;  // the device's own clock after a power-on reset: 1970
  base.ms = 5000;
  gc::HubClock c(base);
  c.start(gc::Stamp{1790003600, true, 7200, 1}, 0, 2);
  base.ms += 60000;
  CHECK_FALSE(c.secured());
  CHECK(c.epoch() == 1790003660);
  CHECK(c.operatingS() == 7260);
  CHECK(std::string(c.source()) == "continued");
  CHECK_FALSE(c.stamp().secured);
}

TEST_CASE("Clock: a newer event moves the start by at most an hour") {
  test::Clock base;
  base.isSecured = false;
  base.start = 0;
  gc::HubClock c(base);
  // The state was saved at :00, an event followed at :40 before the power failed.
  c.start(gc::Stamp{1790003600, false, 7200, 1}, 1790003640, 2);
  CHECK(c.epoch() == 1790003640);
  // An event far after the saved time is wrong (a broken file, a bad clock).
  c.start(gc::Stamp{1790003600, false, 7200, 1}, 1790003600 + 86400, 2);
  CHECK(c.epoch() == 1790003600);
  c.start(std::nullopt, 1790003640, 1);  // state lost, event log kept
  CHECK(c.epoch() == 1790003640);
  CHECK(c.operatingS() == 0);
  c.start(std::nullopt, 4102444800 + 50LL * 365 * 86400, 1);  // implausible: ignored
  CHECK(std::string(c.source()) == "unset");
}

TEST_CASE("Clock: nothing saved and no secured time leaves the platform clock, source unset") {
  test::Clock base;
  base.isSecured = false;
  base.start = 0;
  base.ms = 3000;
  gc::HubClock c(base);
  c.start(std::nullopt, 0, 1);
  CHECK(c.epoch() == 3);
  CHECK(std::string(c.source()) == "unset");
}

TEST_CASE("Clock: the secured flag changes only at poll, and a loss continues without a jump") {
  test::Clock base;
  base.isSecured = false;
  base.start = 1790040000;  // real time: 10 h after the save below
  gc::HubClock c(base);
  c.start(gc::Stamp{1790004000, true, 100, 1}, 0, 2);
  CHECK(c.epoch() == 1790004000);
  base.isSecured = true;
  CHECK_FALSE(c.secured());  // not before the next tick
  CHECK(c.epoch() == 1790004000);
  c.poll();
  CHECK(c.epoch() == 1790040000);
  CHECK(std::string(c.source()) == "secured");
  base.ms = 60000;
  base.isSecured = false;
  c.poll();
  c.lose(1790040060);
  base.ms = 70000;
  CHECK(c.epoch() == 1790040070);
  CHECK(std::string(c.source()) == "continued");
}

TEST_CASE("Clock: intervals within one start use operating time, across starts the secured wall time") {
  // Same start: operating time, whatever the wall clock did.
  const gc::Stamp a{1790000000, true, 1000, 5};
  gc::Stamp b{1790036000, true, 4600, 5};  // the wall clock stepped 9 h forward
  CHECK(gc::elapsedS(a, b) == 3600);
  // Across a restart with a 9 h outage: both secured → the wall time.
  b.boot = 6;
  CHECK(gc::elapsedS(a, b) == 36000);
  // One side not secured → operating time.
  b.secured = false;
  CHECK(gc::elapsedS(a, b) == 3600);
  // A dose on the continued clock (10 h behind), later in the same start
  // the time is secured and the clock jumps forward: 14 h later the
  // interval is 14 h, not 24 h, so one emergency dose per 24 h holds
  // (PD-063, PD-069).
  const gc::Stamp dose{1790000000 - 36000, false, 2000, 7};
  const gc::Stamp later{1790000000 + 14 * 3600, true, 2000 + 14 * 3600, 7};
  CHECK(gc::elapsedS(dose, later) == 14 * 3600);
  // Never shorter than the operating time, never negative.
  const gc::Stamp early{1790000000, true, 100, 1}, wrong{1789990000, true, 3700, 2};
  CHECK(gc::elapsedS(early, wrong) == 3600);
  CHECK(gc::elapsedS(b, a) == 0);
}

TEST_CASE("Clock: a stamp survives JSON; a missing, broken or implausible one stays unknown (R5)") {
  const gc::Stamp s{1790000000, true, 4242, 9};
  auto back = gc::stampFromJson(json(s));
  REQUIRE(back);
  CHECK(back->at == s.at);
  CHECK(back->secured);
  CHECK(back->operatingS == 4242);
  CHECK(back->boot == 9);
  CHECK_FALSE(gc::stampFromJson(json(nullptr)));
  CHECK_FALSE(gc::stampFromJson(json{{"at", 1790000000}}));
  CHECK_FALSE(gc::stampFromJson(json{{"at", "gestern"}, {"operatingS", 1}}));
  CHECK_FALSE(gc::stampFromJson(json{{"at", 1790000000}, {"operatingS", -5}}));
  CHECK_FALSE(gc::stampFromJson(json{{"at", 3}, {"operatingS", 5}}));             // 1970
  CHECK_FALSE(gc::stampFromJson(json{{"at", 7258118400}, {"operatingS", 5}}));    // 2200
  auto odd = gc::stampFromJson(json{{"at", 1790000000}, {"operatingS", 5}, {"secured", "yes"}});
  REQUIRE(odd);  // a wrong type never stops the start
  CHECK_FALSE(odd->secured);
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
  CHECK(hasEvent(c, "ev.clock.unsecured"));

  // Network time arrives: the platform time counts again, the jump is reported.
  {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    s.control("time", {{"secured", true}});
  }
  s.step(2000);
  st = c.state();
  CHECK(st["time"]["secured"] == true);
  CHECK(st["now"] == s.clock().epoch());
  CHECK(hasEvent(c, "ev.clock.secured"));
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
  CHECK_FALSE(hasEvent(c, "ev.clock.unsecured"));
  CHECK_FALSE(hasEvent(c, "ev.clock.secured"));
}

TEST_CASE("Clock jump: jump locks and maintenance keep their remaining time (RAT-044)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  s.step(90000);
  {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    s.control("reboot", {{"outageMin", 600}, {"timeSecured", false}});
  }
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  s.step(30000);
  {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    s.control("fault", {{"device", "PHEC-3F2A91"}, {"fault", "jump"}});
  }
  s.step(60000);
  REQUIRE(c.state()["readings"]["tank.ph"]["quality"] == "jump");
  {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    s.control("fault", {{"device", "PHEC-3F2A91"}, {"fault", "none"}});
  }
  s.step(60000);
  c.ok("POST", "/api/v1/maintenance", {{"minutes", 30}});
  auto st = c.state();
  const gc::Epoch rest = st["maintenanceUntil"].get<gc::Epoch>() - st["now"].get<gc::Epoch>();
  {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    s.control("time", {{"secured", true}});  // the clock jumps about 10 h forward
  }
  s.step(2000);
  st = c.state();
  CHECK(st["now"] == s.clock().epoch());
  const gc::Epoch restAfter = st["maintenanceUntil"].get<gc::Epoch>() - st["now"].get<gc::Epoch>();
  CHECK(restAfter <= rest);
  CHECK(restAfter >= rest - 4);
  CHECK(st["readings"]["tank.ph"]["quality"] == "jump");  // still locked, not lifted by the jump
}

TEST_CASE("Clock: a secured time lost while running continues without a jump") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  s.step(90000);
  {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    s.control("reboot", {{"outageMin", 600}});
  }
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  s.step(5000);
  {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    s.control("time", {{"secured", false}});
  }
  s.step(2000);
  auto st = c.state();
  CHECK(st["time"]["secured"] == false);
  CHECK(st["time"]["source"] == "continued");
  const gc::Epoch now = st["now"];
  CHECK(now >= s.clock().epoch() - 2);
  CHECK(now <= s.clock().epoch() + 2);
  CHECK(hasEvent(c, "ev.clock.lost"));
  s.step(5 * 60 * 1000);
  st = c.state();
  CHECK(st["watchdog"]["evaluatedAt"].get<gc::Epoch>() >= st["now"].get<gc::Epoch>() - 70);
}

TEST_CASE("Clock: a network time step while secured keeps deadlines and is reported") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  s.step(10000);
  c.ok("POST", "/api/v1/maintenance", {{"minutes", 30}});
  auto st = c.state();
  const gc::Epoch rest = st["maintenanceUntil"].get<gc::Epoch>() - st["now"].get<gc::Epoch>();
  {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    s.control("time", {{"stepS", 7200}});
  }
  s.step(2000);
  st = c.state();
  CHECK(st["now"] == s.clock().epoch());
  const gc::Epoch restAfter = st["maintenanceUntil"].get<gc::Epoch>() - st["now"].get<gc::Epoch>();
  CHECK(restAfter <= rest);
  CHECK(restAfter >= rest - 4);
  CHECK(hasEvent(c, "ev.clock.set"));
}

TEST_CASE("Clock: a platform time set before the secured flag follows keeps deadlines") {
  // On the device the network time is set first and marked secured a moment
  // later; a tick in between sees the new time while still unsecured.
  gc::Catalog cat = gc::Catalog::builtin();
  gc::MemoryStorage store;
  test::FakeBus bus;
  test::Clock clk;
  clk.isSecured = false;
  clk.start = 0;  // nothing saved, no network time yet
  gc::Hub h(cat, bus, store, clk, fakeRandom);
  h.boot();
  for (int i = 0; i < 3; ++i) {
    clk.ms += 1000;
    h.tick();
  }
  REQUIRE(h.maintenance(30).status == 200);
  auto rest = [&] {
    const json st = h.state();
    return st["maintenanceUntil"].get<gc::Epoch>() - st["now"].get<gc::Epoch>();
  };
  const gc::Epoch before = rest();
  clk.start = 1790000000;  // the platform sets the time ...
  clk.ms += 1000;
  h.tick();
  clk.isSecured = true;  // ... and marks it secured on a later tick
  clk.ms += 1000;
  h.tick();
  CHECK(h.now() == clk.epoch());
  CHECK(rest() <= before);
  CHECK(rest() >= before - 3);
}

TEST_CASE("Restart: an unreadable run-time state keeps everything stopped and is reported (PD-076)") {
  gc::Catalog cat = gc::Catalog::builtin();
  gc::MemoryStorage store;
  test::FakeBus bus;
  test::Clock clk;
  store.write("state.json", "[1, 2");
  gc::Hub h(cat, bus, store, clk, fakeRandom);
  h.boot();
  CHECK(h.state()["stopped"] == true);
  CHECK(store.read("state.broken.json") == std::optional<std::string>("[1, 2"));
  bool alarm = false;
  const json events = h.events(0, 9999999999, "", 2000)["events"];
  for (const auto& e : events)
    if (sameEvent(e, "ev.state_unreadable")) alarm = true;
  CHECK(alarm);
  REQUIRE(h.resume().status == 200);
  CHECK(h.state()["stopped"] == false);
}

TEST_CASE("Clock jump: a jump lock loaded before the time was known never holds longer than 15 min (RAT-044)") {
  gc::Catalog cat = gc::Catalog::builtin();
  gc::MemoryStorage store;
  test::FakeBus bus;
  test::Clock clk;
  clk.isSecured = false;
  clk.start = 0;                                                     // no network time yet
  store.write("state.json", R"({"jumpLocks": {"tank.ph": 1790000600}})");  // saved without a clock
  gc::Hub h(cat, bus, store, clk, fakeRandom);
  h.boot();
  clk.ms += 1000;
  h.tick();
  clk.start = 1790000000;
  clk.isSecured = true;
  clk.ms += 1000;
  h.tick();
  h.flush();
  const json st = json::parse(*store.read("state.json"));
  const gc::Epoch until = st["jumpLocks"]["tank.ph"].get<gc::Epoch>();
  CHECK(until - h.now() <= gc::kJumpHoldS);
}
