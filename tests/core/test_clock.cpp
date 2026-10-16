// SPDX-License-Identifier: AGPL-3.0-or-later
// Time base of the hub after a restart (PD-069, PD-073, SD-028): secured
// time, the clock that continues from the saved time, operating time, and
// intervals that an outage never stretches.
#include <doctest/doctest.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "client.hpp"
#include "fakes.hpp"
#include "gc/api.hpp"
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

// A storage whose locked files are there but cannot be read (as root's files
// after a start with sudo, or a read error of the flash): read() gives no
// value for them, as for a missing file. Every write to one is noted; a file
// written whole is the hub's own again and can be read.
struct LockedStorage : gc::MemoryStorage {
  std::set<std::string> locked;
  std::set<std::string> gone;  // not there (deleted, or a damaged entry the file system reports as not found)
  std::vector<std::string> writtenOver;
  std::optional<std::string> read(const std::string& n) override {
    return locked.count(n) || gone.count(n) ? std::nullopt : MemoryStorage::read(n);
  }
  bool unreadable(const std::string& n) override { return locked.count(n) > 0; }
  bool write(const std::string& n, const std::string& d) override {
    if (locked.erase(n)) writtenOver.push_back(n);
    gone.erase(n);
    return MemoryStorage::write(n, d);
  }
  bool append(const std::string& n, const std::string& d) override {
    if (locked.count(n)) writtenOver.push_back(n);
    gone.erase(n);  // an append creates it again
    return MemoryStorage::append(n, d);
  }
  std::optional<std::string> held(const std::string& n) { return MemoryStorage::read(n); }  // what is on "disk"
};

// The first password through the API, as the web app sets it.
int setupPassword(gc::Hub& h, test::Clock& clk, std::string* key = nullptr) {
  gc::Api api(h, clk);
  gc::ApiRequest r;
  r.method = "POST";
  r.path = "/api/v1/auth/setup";
  r.body = R"({"password":"mein-passwort"})";
  const gc::ApiResponse res = api.handle(r);
  if (key) *key = json::parse(res.body, nullptr, false).value("/error/key"_json_pointer, std::string());
  return res.status;
}

// The files a hub leaves after a run (settings, state, events and history,
// snapshots and journals; with a password, auth.json).
void finishedRun(gc::IStorage& store, test::Clock& clk, bool password = false) {
  gc::Catalog cat = gc::Catalog::builtin();
  test::FakeBus bus;
  gc::Hub first(cat, bus, store, clk, fakeRandom);
  first.boot();
  if (password) REQUIRE(setupPassword(first, clk) == 200);
  for (int i = 0; i < 5; ++i) {
    clk.ms += 60000;
    first.tick();
  }
  first.flush();
}

// Whether a mix now needs the confirmation against mixing twice (RAT-003);
// adds a bottle and a recipe the first time.
bool mixNeedsConfirmation(gc::Hub& h) {
  if (h.configJson()["recipes"].empty()) {
    REQUIRE(h.putCanister({{"name", "Teil A"}, {"capacityMl", 1000}}).status == 200);
    REQUIRE(h.putRecipe({{"name", "Wachstum"}, {"steps", {{{"canister", "teil-a"}, {"mlPerL", 1.0}}}}}).status == 200);
  }
  const json plan = h.mixPlan({{"recipe", "wachstum"}, {"waterL", 10}}).body;
  for (const auto& e : plan["errors"])
    if (e.value("key", std::string()) == "mix.recent") return true;
  return false;
}

// How often the hub said this event (since this time).
int said(gc::Hub& h, const std::string& title, gc::Epoch since = 0) {
  const json events = h.events(since, 9999999999, "", 2000)["events"];  // held: a loop over a temporary's part dangles
  int n = 0;
  for (const auto& e : events) n += sameEvent(e, title);
  return n;
}

// How often the hub said that this file cannot be read and is kept: by what
// it holds (config, state, auth, job, events, history), naming the file.
int keptSaid(gc::Hub& h, const std::string& file, gc::Epoch since = 0) {
  const std::string kind = file.substr(0, file.find('.'));
  const json events = h.events(since, 9999999999, "", 2000)["events"];
  int n = 0;
  for (const auto& e : events)
    n += sameEvent(e, "ev.kept." + kind) && e["text"]["args"].value("file", std::string()).find(file) != std::string::npos;
  return n;
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
    if (sameEvent(e, "ev.state_unreadable")) {
      alarm = true;
      CHECK(e["text"]["args"]["reason"]["key"] == "reason.not_json_object");  // both languages (SD-032)
    }
  CHECK(alarm);
  REQUIRE(h.resume().status == 200);
  CHECK(h.state()["stopped"] == false);
}

TEST_CASE("Restart: a run-time state that cannot be read keeps everything stopped and is kept until Stop or Resume (SD-081)") {
  LockedStorage store;
  test::Clock clk;
  finishedRun(store, clk);
  const auto kept = store.held("state.json");
  REQUIRE(kept);
  store.locked = {"state.json"};
  gc::Catalog cat = gc::Catalog::builtin();
  test::FakeBus bus;
  gc::Epoch since = 0;  // what the last start said is said from here
  const auto restart = [&] {
    clk.ms += 60000;
    auto h = std::make_unique<gc::Hub>(cat, bus, store, clk, fakeRandom);
    h->boot();
    since = h->state()["now"];
    return h;
  };
  auto h = restart();
  CHECK(h->state()["stopped"] == true);  // whether a Stop was in force is unknown (PD-076)
  CHECK(keptSaid(*h, "state.json", since) == 1);
  CHECK(said(*h, "ev.stop_kept", since) == 0);  // the alarm says why everything stays off
  for (int i = 0; i < 30; ++i) {
    clk.ms += 60000;
    h->tick();
  }
  h->flush();
  CHECK(store.writtenOver.empty());
  CHECK(store.held("state.json") == kept);

  SUBCASE("still unreadable after a restart: still stopped, still kept") {
    h = restart();
    CHECK(h->state()["stopped"] == true);
    CHECK(keptSaid(*h, "state.json", since) == 1);
    h->flush();
    CHECK(store.writtenOver.empty());
    CHECK(store.held("state.json") == kept);
  }
  SUBCASE("readable again: the restart goes on from it") {
    store.locked.clear();
    h = restart();
    CHECK(h->state()["stopped"] == false);
    CHECK(keptSaid(*h, "state.json", since) == 0);
  }
  SUBCASE("a Stop writes it anew: the Stop holds over a restart (PD-076)") {
    REQUIRE(h->stop(gc::Msg{"stop.app", "STOP button in the app", json::object()}).status == 200);
    CHECK(store.writtenOver == std::vector<std::string>{"state.json"});
    CHECK(said(*h, "ev.stop", since) == 1);  // logged, though the hub was stopped already (RAT-036)
    CHECK(said(*h, "ev.kept.state_replaced", since) == 1);  // the old locks and stock levels are gone
    h = restart();
    CHECK(h->state()["stopped"] == true);
    CHECK(keptSaid(*h, "state.json", since) == 0);
    CHECK(said(*h, "ev.stop_kept", since) == 1);
  }
  SUBCASE("Resume writes it anew, and from then on it is saved as usual") {
    REQUIRE(h->resume().status == 200);
    CHECK(store.writtenOver == std::vector<std::string>{"state.json"});
    REQUIRE(h->stop(gc::Msg{"stop.app", "STOP button in the app", json::object()}).status == 200);
    h = restart();
    CHECK(h->state()["stopped"] == true);
    REQUIRE(h->resume().status == 200);
    h = restart();
    CHECK(h->state()["stopped"] == false);
  }
}

TEST_CASE("Restart: a run-time state that is missing on a hub that ran before is unknown: everything stays stopped and it is said (PD-076, RAT-003, SD-086)") {
  LockedStorage store;
  test::Clock clk;
  finishedRun(store, clk);
  REQUIRE(store.held("events.json"));
  REQUIRE(store.held("events.log"));
  gc::Catalog cat = gc::Catalog::builtin();
  test::FakeBus bus;
  SUBCASE("its events are there") {}
  SUBCASE("only the events' snapshot is there") { store.gone = {"events.log"}; }
  SUBCASE("only the events' journal is there") { store.gone = {"events.json"}; }
  SUBCASE("its events are kept unread (SD-081)") { store.locked = {"events.json", "events.log"}; }
  SUBCASE("a Stop was in force (PD-076)") {
    gc::Hub before(cat, bus, store, clk, fakeRandom);
    before.boot();
    REQUIRE(before.stop(gc::Msg{"stop.app", "STOP button in the app", json::object()}).status == 200);
    before.flush();
  }
  store.gone.insert("state.json");
  auto h = std::make_unique<gc::Hub>(cat, bus, store, clk, fakeRandom);
  h->boot();
  CHECK(h->state()["stopped"] == true);  // whether a Stop was in force is unknown (PD-076)
  CHECK(said(*h, "ev.state_missing") == 1);
  CHECK(said(*h, "ev.stop_kept") == 0);  // the alarm says why everything stays off
  CHECK(mixNeedsConfirmation(*h));  // a mix may have just ended (RAT-003)
  h->flush();
  const json saved = json::parse(store.held("state.json").value_or("{}"), nullptr, false);
  CHECK(saved.value("stopped", false) == true);  // written anew, with the Stop
  SUBCASE("the next start reads it: still stopped, said as a Stop that holds, not as missing again") {
    clk.ms += 60000;
    h = std::make_unique<gc::Hub>(cat, bus, store, clk, fakeRandom);
    h->boot();
    const gc::Epoch since = h->state()["now"];
    CHECK(h->state()["stopped"] == true);
    CHECK(said(*h, "ev.state_missing", since) == 0);
    CHECK(said(*h, "ev.stop_kept", since) == 1);
  }
  SUBCASE("after Resume the next start runs, with nothing to say") {
    REQUIRE(h->resume().status == 200);
    h->flush();
    clk.ms += 60000;
    h = std::make_unique<gc::Hub>(cat, bus, store, clk, fakeRandom);
    h->boot();
    const gc::Epoch since = h->state()["now"];
    CHECK(h->state()["stopped"] == false);
    CHECK(said(*h, "ev.state_missing", since) == 0);
    CHECK(said(*h, "ev.stop_kept", since) == 0);
  }
}

TEST_CASE("Restart: a hub with no files at all starts as a new one, not stopped (SD-086)") {
  gc::Catalog cat = gc::Catalog::builtin();
  gc::MemoryStorage store;
  test::FakeBus bus;
  test::Clock clk;
  SUBCASE("nothing there") {}
  SUBCASE("settings and a password put in place before the first start: no sign that it ran (SD-086)") {
    gc::Config cfg;
    cfg.system.name = "Zelt 2";
    store.write("config.json", json(cfg).dump());
    store.write("auth.json", "{}");
  }
  gc::Hub h(cat, bus, store, clk, fakeRandom);
  h.boot();
  CHECK(h.state()["stopped"] == false);
  CHECK(said(h, "ev.state_missing") == 0);
  CHECK(said(h, "ev.stop_kept") == 0);
  CHECK_FALSE(mixNeedsConfirmation(h));
  // what the next start takes as the sign that it ran: its first start leaves both
  CHECK(store.read("state.json"));
  CHECK(store.read("events.json"));
}

TEST_CASE("Restart: a run-time state that is broken or cannot be read errs on the safe side against mixing twice and a dry run (RAT-003, SD-067, SD-081)") {
  LockedStorage store;
  test::Clock clk;
  finishedRun(store, clk);
  SUBCASE("cannot be read") { store.locked = {"state.json"}; }
  SUBCASE("broken") { store.MemoryStorage::write("state.json", "[1, 2"); }
  SUBCASE("missing on a hub that ran before (#238)") { store.gone = {"state.json"}; }
  gc::Catalog cat = gc::Catalog::builtin();
  test::FakeBus bus;
  gc::Hub h(cat, bus, store, clk, fakeRandom);
  h.boot();
  const gc::Epoch bootAt = h.state()["now"];
  REQUIRE(h.resume().status == 200);
  clk.ms += 1000;
  h.tick();  // the first tick, a second after the start
  // a mix may have ended just before the restart: the window counts from the start
  CHECK(mixNeedsConfirmation(h));
  clk.ms += 31 * 60000;
  h.tick();
  CHECK_FALSE(mixNeedsConfirmation(h));
  // when a dry-run latch last released itself is unknown, it counts as the start: a dry-run
  // latch within the hour after it holds for a person (SD-067)
  const json saved = json::parse(store.held("state.json").value_or("{}"), nullptr, false);
  const json released = saved.value("/selfReleased/circulation.dry"_json_pointer, json());
  REQUIRE(released.is_number());
  CHECK(released.get<gc::Epoch>() >= bootAt);
  CHECK(released.get<gc::Epoch>() <= bootAt + 60);
}

TEST_CASE("Restart: an unknown run-time state counts a mix as just ended also with the clock at 0 (RAT-003, SD-081)") {
  // a device right after power-on: no secured time, nothing saved with a time
  LockedStorage store;
  test::Clock clk;
  clk.start = 0;
  clk.isSecured = false;
  store.MemoryStorage::write("state.json", "{}");
  store.locked = {"state.json"};
  gc::Catalog cat = gc::Catalog::builtin();
  test::FakeBus bus;
  gc::Hub h(cat, bus, store, clk, fakeRandom);
  h.boot();
  REQUIRE(h.resume().status == 200);
  CHECK(h.state()["lastMixAt"].get<gc::Epoch>() > 0);  // 0 would mean "never mixed"
  CHECK(mixNeedsConfirmation(h));
}

TEST_CASE("Restart: each file the hub reads, when it cannot be read, is said once and never written over, nor its journal or snapshot (SD-081)") {
  // a snapshot and its journal belong together: neither is written without the other
  const std::map<std::string, std::string> partner = {
      {"events.json", "events.log"}, {"events.log", "events.json"}, {"history.bin", "history.log"}, {"history.log", "history.bin"}};
  for (const std::string& name : gc::Hub::files()) {
    CAPTURE(name);
    LockedStorage store;
    test::Clock clk;
    finishedRun(store, clk, true);
    if (!store.held(name)) store.MemoryStorage::write(name, "{}");  // one the run did not leave
    std::set<std::string> kept = {name};
    if (partner.count(name)) kept.insert(partner.at(name));
    std::map<std::string, std::optional<std::string>> before;
    for (const std::string& f : gc::Hub::files()) before[f] = store.held(f);
    store.locked = {name};  // only this one cannot be read
    gc::Catalog cat = gc::Catalog::builtin();
    test::FakeBus bus;
    gc::Hub h(cat, bus, store, clk, fakeRandom);
    h.boot();
    CHECK(keptSaid(h, name) == 1);
    for (const auto& [f, other] : partner)
      if (f != name && kept.count(f)) CHECK(keptSaid(h, f) == 0);  // one entry for the pair (docs/UX.md §1)
    // what would write each file: a change to the settings, a password, the ticks, the way out
    CHECK(h.setSystem({{"name", "Zelt 2"}}).status == 200);
    h.saveAuth();  // with auth.json kept, there is no password to save
    for (int i = 0; i < 30; ++i) {
      clk.ms += 60000;
      h.tick();
    }
    h.flush();
    CHECK(store.writtenOver.empty());
    for (const std::string& f : kept) CHECK_MESSAGE(store.held(f) == before[f], f);
    // the others are still saved
    if (name != "config.json") CHECK(store.held("config.json") != before["config.json"]);
    if (name != "state.json") CHECK(store.held("state.json") != before["state.json"]);
    if (!kept.count("events.log")) CHECK(store.held("events.log") != before["events.log"]);
    CHECK(said(h, "ev.storage_failed") == 0);  // said once as kept, not as a full card
    CHECK(said(h, "ev.credentials_lost") == 0);  // a factory reset would wipe what is kept
  }
}

TEST_CASE("Restart: a journal is not written while its snapshot is kept, also before the start ends (SD-081)") {
  // Factory settings with a password on the card: the start marks the
  // password as set and saves the settings, with the events, before it
  // reads the history.
  LockedStorage store;
  test::Clock clk;
  finishedRun(store, clk, true);
  store.MemoryStorage::write("config.json", "not json");
  for (const auto& [snapshot, journal] : {std::pair{"events.json", "events.log"}, std::pair{"history.bin", "history.log"}}) {
    CAPTURE(snapshot);
    LockedStorage copy = store;
    const auto snapshotBefore = copy.held(snapshot), journalBefore = copy.held(journal);
    copy.locked = {snapshot};
    gc::Catalog cat = gc::Catalog::builtin();
    test::FakeBus bus;
    gc::Hub h(cat, bus, copy, clk, fakeRandom);
    h.boot();
    CHECK(keptSaid(h, snapshot) == 1);
    h.flush();
    CHECK(copy.writtenOver.empty());
    CHECK(copy.held(snapshot) == snapshotBefore);
    CHECK(copy.held(journal) == journalBefore);
  }
  SUBCASE("both of a pair unreadable: one entry, naming both (docs/UX.md §1)") {
    store.locked = {"events.json", "events.log"};
    gc::Catalog cat = gc::Catalog::builtin();
    test::FakeBus bus;
    gc::Hub h(cat, bus, store, clk, fakeRandom);
    h.boot();
    CHECK(said(h, "ev.kept.events") == 1);
    CHECK(keptSaid(h, "events.json") == 1);
    CHECK(keptSaid(h, "events.log") == 1);
  }
}

TEST_CASE("Restart: a current job that cannot be read counts as a mix cut off by the restart against mixing twice (RAT-003, SD-081)") {
  LockedStorage store;
  test::Clock clk;
  finishedRun(store, clk);
  store.MemoryStorage::write("job.json", "{}");
  gc::Catalog cat = gc::Catalog::builtin();
  test::FakeBus bus;
  {
    gc::Hub h(cat, bus, store, clk, fakeRandom);
    h.boot();
    CHECK(h.state()["lastMixAt"] == 0);  // what the window counts from without the file
  }
  store.locked = {"job.json"};
  gc::Hub h(cat, bus, store, clk, fakeRandom);
  h.boot();
  CHECK(keptSaid(h, "job.json") == 1);
  CHECK(h.state()["lastMixAt"] == h.state()["now"]);
  CHECK(mixNeedsConfirmation(h));  // within 30 min of the start
  clk.ms += 31 * 60000;
  h.tick();
  CHECK_FALSE(mixNeedsConfirmation(h));
}

TEST_CASE("Restart: a lock set while a run-time state that cannot be read is kept writes it anew, with the Stop (RAT-044, SD-081)") {
  // a pH probe taken out to be cleaned while everything is off: the jump locks it
  LockedStorage store;
  test::Clock clk;
  gc::Config cfg;
  cfg.devices = {{"PH-1", "head_ph_ec", "pH/EC"}};
  cfg.tank().roles["tank.ph"] = {"PH-1", 0};
  cfg.calibrations["PH-1"]["ph"] = {{"points", {{7.18, 7.0}, {4.27, 4.0}}}};
  store.MemoryStorage::write("config.json", json(cfg).dump());
  store.MemoryStorage::write("state.json", R"({"stopped": false})");  // what it held: running
  store.locked = {"state.json"};
  gc::Catalog cat = gc::Catalog::builtin();
  test::FakeBus bus;
  bus.head("PH-1");
  auto h = std::make_unique<gc::Hub>(cat, bus, store, clk, fakeRandom);
  h->boot();
  REQUIRE(h->state()["stopped"] == true);
  for (int i = 0; i < 6; ++i) {
    bus.set("PH-1", "measure.ph", 6.18 + 0.001 * i, clk.ms);  // about pH 6.0
    clk.ms += 5000;
    h->tick();
  }
  CHECK(store.writtenOver.empty());  // nothing new yet: still kept
  bus.set("PH-1", "measure.ph", 7.40, clk.ms);
  clk.ms += 1000;
  h->tick();
  REQUIRE(h->state()["readings"]["tank.ph"]["quality"] == "jump");
  CHECK(store.writtenOver == std::vector<std::string>{"state.json"});
  CHECK(said(*h, "ev.kept.state_replaced") == 1);
  const json saved = json::parse(store.held("state.json").value_or("{}"), nullptr, false);
  CHECK(saved.value("stopped", false) == true);
  CHECK(saved.value("/jumpLocks/tank.ph"_json_pointer, json()).is_number());
  // a restart that reads it keeps both: stopped, and the reading locked
  h = std::make_unique<gc::Hub>(cat, bus, store, clk, fakeRandom);
  h->boot();
  CHECK(h->state()["stopped"] == true);
  bus.set("PH-1", "measure.ph", 7.401, clk.ms);
  clk.ms += 1000;
  h->tick();
  CHECK(h->state()["readings"]["tank.ph"]["quality"] == "jump");
}

TEST_CASE("Restart: a job started while job.json is kept writes it anew, so a restart during it is reported (R6, RAT-003, SD-081)") {
  LockedStorage store;
  test::Clock clk;
  gc::Config cfg;
  cfg.devices = {{"CAP-1", "pump_cap", "Pumpe"}};
  store.MemoryStorage::write("config.json", json(cfg).dump());
  store.MemoryStorage::write("job.json", R"({"type":"mix","state":"done"})");  // what it held: a job that ended
  store.locked = {"job.json"};
  gc::Catalog cat = gc::Catalog::builtin();
  test::FakeBus bus;
  bus.head("CAP-1", true, "pump_cap");
  bus.devs.back().info = {{"flowMlPerMin", 60.0}};  // calibrated
  {
    gc::Hub h(cat, bus, store, clk, fakeRandom);
    h.boot();
    clk.ms += 1000;
    h.tick();  // the pump is found
    REQUIRE(h.putCanister({{"name", "Teil A"}, {"pump", "CAP-1"}, {"capacityMl", 1000}}).status == 200);
    REQUIRE(h.putRecipe({{"name", "Wachstum"}, {"steps", {{{"canister", "teil-a"}, {"mlPerL", 1.0}}}}}).status == 200);
    const gc::Result started = h.mixStart({{"recipe", "wachstum"}, {"waterL", 10}, {"guided", true}, {"confirmRepeat", true}});
    REQUIRE_MESSAGE(started.status == 200, started.body.dump());
    CHECK(store.writtenOver == std::vector<std::string>{"job.json"});
  }  // the power is cut while it runs
  clk.ms += 60000;
  gc::Hub h(cat, bus, store, clk, fakeRandom);
  h.boot();
  const gc::Epoch since = h.state()["now"];
  CHECK(keptSaid(h, "job.json", since) == 0);
  CHECK(said(h, "ev.mix_reboot", since) == 1);  // reported as cut off
  CHECK(mixNeedsConfirmation(h));  // the window counts from the restart
}

TEST_CASE("Restart: a password file that cannot be read locks the first password and never says to reset (SD-081)") {
  LockedStorage store;
  test::Clock clk;
  finishedRun(store, clk, true);
  const auto kept = store.held("auth.json");
  REQUIRE(kept);
  SUBCASE("settings readable: the password is known to be set") {}
  SUBCASE("settings unreadable too: factory settings, the password is not known to be set") { store.locked.insert("config.json"); }
  store.locked.insert("auth.json");
  gc::Catalog cat = gc::Catalog::builtin();
  test::FakeBus bus;
  gc::Hub h(cat, bus, store, clk, fakeRandom);
  h.boot();
  CHECK(keptSaid(h, "auth.json") == 1);
  CHECK(said(h, "ev.credentials_lost") == 0);  // its advice, a factory reset, would wipe the password
  CHECK(h.armSetupCode().empty());  // no code for a setup that cannot happen (#79)
  std::string key;
  CHECK(setupPassword(h, clk, &key) == 423);
  CHECK(key == "auth.kept");
  h.flush();
  CHECK(store.held("auth.json") == kept);
  CHECK(store.writtenOver.empty());
}

TEST_CASE("Restart: the hub reads exactly the files it lists (#212)") {
  // what the simulator checks and removes (SD-074) comes from this list
  struct Reads : gc::MemoryStorage {
    std::set<std::string> names;
    std::optional<std::string> read(const std::string& n) override {
      names.insert(n);
      return MemoryStorage::read(n);
    }
  } store;
  test::Clock clk;
  finishedRun(store, clk);
  store.names.clear();
  gc::Catalog cat = gc::Catalog::builtin();
  test::FakeBus bus;
  gc::Hub h(cat, bus, store, clk, fakeRandom);
  h.boot();
  CHECK(store.names == std::set<std::string>(gc::Hub::files().begin(), gc::Hub::files().end()));
}

TEST_CASE("Restart: the hub writes only the files it lists and the copies of broken ones (#212)") {
  // what the simulator removes on a scenario change (SD-074) comes from these lists
  struct Writes : gc::MemoryStorage {
    std::set<std::string> names;
    bool write(const std::string& n, const std::string& d) override {
      names.insert(n);
      return MemoryStorage::write(n, d);
    }
    bool append(const std::string& n, const std::string& d) override {
      names.insert(n);
      return MemoryStorage::append(n, d);
    }
  } store;
  test::Clock clk;
  finishedRun(store, clk, true);
  for (const char* broken : {"config.json", "state.json", "history.bin"}) store.MemoryStorage::write(broken, "not what it should be");
  gc::Catalog cat = gc::Catalog::builtin();
  test::FakeBus bus;
  gc::Hub h(cat, bus, store, clk, fakeRandom);
  h.boot();
  REQUIRE(h.resume().status == 200);
  for (int i = 0; i < 30; ++i) {
    clk.ms += 60000;
    h.tick();
  }
  h.flush();
  std::set<std::string> own(gc::Hub::files().begin(), gc::Hub::files().end());
  own.insert(gc::Hub::brokenCopies().begin(), gc::Hub::brokenCopies().end());
  for (const std::string& n : store.names) CHECK_MESSAGE(own.count(n), n);
  for (const std::string& copy : gc::Hub::brokenCopies()) CHECK_MESSAGE(store.names.count(copy), copy);  // each one written here
}

TEST_CASE("Restart: unreadable settings start with factory settings and name the reason (SD-032)") {
  gc::Catalog cat = gc::Catalog::builtin();
  gc::MemoryStorage store;
  test::FakeBus bus;
  test::Clock clk;
  store.write("config.json", "{\"tanks\": [");
  gc::Hub h(cat, bus, store, clk, fakeRandom);
  h.boot();
  CHECK(store.read("config.broken.json") == std::optional<std::string>("{\"tanks\": ["));
  bool alarm = false;
  const json events = h.events(0, 9999999999, "", 2000)["events"];
  for (const auto& e : events)
    if (sameEvent(e, "ev.config_unreadable")) {
      alarm = true;
      CHECK(e["text"]["args"]["reason"]["key"] == "reason.not_json");
    }
  CHECK(alarm);
}

TEST_CASE("Restart: settings with wrong types also start with factory settings and name the reason (SD-032)") {
  gc::Catalog cat = gc::Catalog::builtin();
  gc::MemoryStorage store;
  test::FakeBus bus;
  test::Clock clk;
  store.write("config.json", "{\"tanks\": 5}");
  gc::Hub h(cat, bus, store, clk, fakeRandom);
  h.boot();
  CHECK(store.read("config.broken.json") == std::optional<std::string>("{\"tanks\": 5}"));
  bool alarm = false;
  const json events = h.events(0, 9999999999, "", 2000)["events"];
  for (const auto& e : events)
    if (sameEvent(e, "ev.config_unreadable")) {
      alarm = true;
      CHECK(e["text"]["args"]["reason"]["key"] == "ev.plain");  // the library's own text, as it is
    }
  CHECK(alarm);
}

TEST_CASE("Restart: a saved time zone that is no zone name falls back to the default and says so; the rest of the settings stays") {
  for (const json& tz : {json(42), json("Mars Olympus"), json(nullptr)}) {
    CAPTURE(tz.dump());
    gc::Catalog cat = gc::Catalog::builtin();
    gc::MemoryStorage store;
    test::FakeBus bus;
    test::Clock clk;
    store.write("config.json", json{{"system", {{"name", "Gewächshaus"}, {"timezone", tz}}}}.dump());
    gc::Hub h(cat, bus, store, clk, fakeRandom);
    h.boot();
    CHECK_FALSE(store.read("config.broken.json"));  // no factory settings
    CHECK(h.config().system.name == "Gewächshaus");
    CHECK(h.config().system.timezone == gc::SystemCfg{}.timezone);
    int notes = 0;
    const json events = h.events(0, 9999999999, "", 2000)["events"];
    for (const auto& e : events) {
      CHECK_FALSE(sameEvent(e, "ev.config_unreadable"));
      if (sameEvent(e, "ev.cfg.timezone_reset")) {
        ++notes;
        CHECK(e["text"]["args"]["zone"] == (tz.is_string() ? tz.get<std::string>() : tz.dump()));
        CHECK(e["text"]["args"]["default"] == "Europe/Berlin");
      }
    }
    CHECK(notes == 1);
  }
  // A zone that is one: no note
  gc::Catalog cat = gc::Catalog::builtin();
  gc::MemoryStorage store;
  test::FakeBus bus;
  test::Clock clk;
  store.write("config.json", json{{"system", {{"timezone", "Asia/Tokyo"}}}}.dump());
  gc::Hub h(cat, bus, store, clk, fakeRandom);
  h.boot();
  CHECK(h.config().system.timezone == "Asia/Tokyo");
  const json events = h.events(0, 9999999999, "", 2000)["events"];
  for (const auto& e : events) CHECK_FALSE(sameEvent(e, "ev.cfg.timezone_reset"));
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
