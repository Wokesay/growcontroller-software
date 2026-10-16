// SPDX-License-Identifier: AGPL-3.0-or-later
// Scenario tests against the digital twin: sequences end to end through the
// API, as the web app uses it. The expectations come from the domain rules
// (docs/INVARIANTS.md, docs/RATIONALE.md).
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <random>
#include <set>
#include <stdexcept>
#include <tuple>
#include <vector>

#include "client.hpp"

using gc::json;
using test::Client;

namespace {

constexpr const char* kDB = "DB-7A31C0";
constexpr const char* kA = "CAP-1F02A4";
constexpr const char* kB = "CAP-1F02B7";
constexpr const char* kC = "CAP-1F02C1";

// Wartet in Simulationszeit, bis die Bedingung erfüllt ist.
template <typename F>
bool until(sim::Simulation& s, F cond, gc::Ms maxMs, gc::Ms step = 1000) {
  for (gc::Ms t = 0; t < maxMs; t += step) {
    if (cond()) return true;
    s.step(step);
  }
  return cond();
}

json findEvents(Client& c, const std::string& type) { return c.ok("GET", "/api/v1/events?limit=500&type=" + type)["events"]; }

// Stufe 0 von Hand einrichten: Passwort, Geräte, Kanister, Einmessen, Rezept, Tank.
void setupStage0(sim::Simulation& s, Client& c) {
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  s.step(3000);
  for (const char* id : {kDB, kA, kB, kC}) c.ok("POST", std::string("/api/v1/devices/") + id + "/accept", json::object());
  c.ok("POST", "/api/v1/canisters", {{"name", "Teil A"}, {"pump", kA}, {"pair", "AB"}, {"capacityMl", 1000}});
  c.ok("POST", "/api/v1/canisters", {{"name", "Teil B"}, {"pump", kB}, {"pair", "AB"}, {"capacityMl", 1000}});
  c.ok("POST", "/api/v1/canisters", {{"name", "CalMag"}, {"pump", kC}, {"capacityMl", 1000}});
  c.ok("POST", "/api/v1/recipes",
       {{"name", "Wachstum"},
        {"steps", {{{"canister", "teil-a"}, {"mlPerL", 2.0}}, {{"canister", "teil-b"}, {"mlPerL", 2.0}}, {{"canister", "calmag"}, {"mlPerL", 0.5}}}}});
  c.ok("PUT", "/api/v1/tank", {{"capacityL", 60}});
}

// Measures a pump; `share` of what is in the cup is entered (1 = exact).
json calibrate(sim::Simulation& s, Client& c, const std::string& pump, double share = 1.0) {
  auto j = c.ok("POST", "/api/v1/pumps/" + pump + "/calibrate", {{"seconds", 30}})["job"];
  std::string id = j["id"];
  REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "waiting_user"; }, 60000));
  sim::Cap* cap = s.world().cap(pump);
  double cup = cap->trueFlow * static_cast<double>(cap->elapsed) / 60000.0;  // was im Messbecher ist
  return c.ok("POST", "/api/v1/jobs/" + id + "/result", {{"ml", cup * share}});
}

}  // namespace

TEST_CASE("Szenario Stufe 0: ohne Einmesswert wird nichts dosiert") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  setupStage0(s, c);
  s.world().fill(20, 0.02, 7.0);
  auto [st, j] = c.call("POST", "/api/v1/mix/start", {{"recipe", "wachstum"}, {"waterL", 20}});
  CHECK(st == 422);
  CHECK(j["error"]["text"].get<std::string>().find("eingemessen") != std::string::npos);
  s.step(5000);
  for (const char* id : {kA, kB, kC}) CHECK(s.world().cap(id)->state == 0);  // keine Pumpe lief
  CHECK(s.world().tank.ec + s.world().tank.pendingEc == doctest::Approx(0.02));
}

TEST_CASE("Szenario Stufe 0: Einmessen und geführtes Mischen treffen die Mengen, A:B bleibt gekoppelt") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  setupStage0(s, c);
  for (const char* id : {kA, kB, kC}) calibrate(s, c, id);
  for (const char* id : {kA, kB, kC}) CHECK(s.world().cap(id)->storedFlow == doctest::Approx(s.world().cap(id)->trueFlow).epsilon(0.01));
  // Einmessläufe sind kein Verbrauch (RAT-070): keine Dosier-Ereignisse
  CHECK(findEvents(c, "dose").empty());

  s.world().fill(20, 0.02, 7.0);
  auto plan = c.ok("POST", "/api/v1/mix/plan", {{"recipe", "wachstum"}, {"waterL", 20}});
  CHECK(plan["ok"] == true);
  CHECK(plan["steps"][0]["ml"].get<double>() == doctest::Approx(40.0));
  auto job = c.ok("POST", "/api/v1/mix/start", {{"recipe", "wachstum"}, {"waterL", 20}, {"guided", true}})["job"];
  std::string id = job["id"];
  // Ohne Umwälzpumpe wartet der Lauf zwischen den Gaben auf "umgerührt"
  for (int step = 0; step < 2; ++step) {
    REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "waiting_user"; }, 120000));
    c.ok("POST", "/api/v1/jobs/" + id + "/continue");
  }
  REQUIRE(until(s, [&] { return c.state()["job"].is_null(); }, 120000));
  CHECK(c.state()["lastJob"]["state"] == "done");
  // Zweiter Start direkt danach braucht eine Bestätigung (RAT-003)
  auto again = c.call("POST", "/api/v1/mix/start", {{"recipe", "wachstum"}, {"waterL", 20}});
  CHECK(again.first == 422);
  CHECK(again.second["error"]["key"] == "mix.recent");
  s.step(30 * 60 * 1000);  // durchmischen lassen
  double expected = 0.02 + 0.275 * 2.0 + 0.275 * 2.0 + 0.217 * 0.5;
  CHECK(s.world().tank.ec + s.world().tank.pendingEc == doctest::Approx(expected).epsilon(0.03));
  // Ist-Mengen aus den Ereignissen: A und B gleich (RAT-054)
  double a = 0, b = 0;
  for (const auto& e : findEvents(c, "dose")) {
    if (e["data"]["canister"] == "teil-a") a += e["data"]["ml"].get<double>();
    if (e["data"]["canister"] == "teil-b") b += e["data"]["ml"].get<double>();
  }
  CHECK(a == doctest::Approx(40.0).epsilon(0.03));
  CHECK(a / b == doctest::Approx(1.0).epsilon(0.03));
  // Vorrat je Lauf gebucht
  CHECK(c.state()["stock"]["teil-a"].get<double>() == doctest::Approx(1000.0 - a).epsilon(0.001));
}

TEST_CASE("Scenario: right after a mix that took longer than 30 minutes, another mix still needs confirmation, however it ended (RAT-003)") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  setupStage0(s, c);
  for (const char* id : {kA, kB, kC}) calibrate(s, c, id);
  s.world().fill(20, 0.02, 7.0);
  const json mix{{"recipe", "wachstum"}, {"waterL", 20}};
  const auto refusedAsRecent = [&] {
    const auto again = c.call("POST", "/api/v1/mix/start", mix);
    CHECK(again.first == 422);
    CHECK(again.second["error"]["key"] == "mix.recent");
  };
  const auto signInAgain = [&] {
    c.ok("POST", "/api/v1/auth/login", {{"password", "mein-passwort"}});
    s.step(5000);  // the devices are found again
  };
  std::string id = c.ok("POST", "/api/v1/mix/start", {{"recipe", "wachstum"}, {"waterL", 20}, {"guided", true}})["job"]["id"];
  // Stirring by hand takes its time: 40 minutes at the first prompt, Part A is in
  REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "waiting_user"; }, 120000));
  s.step(40 * 60 * 1000);

  SUBCASE("finished: counted from its end, also after a restart") {
    c.ok("POST", "/api/v1/jobs/" + id + "/continue");
    REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "waiting_user"; }, 120000));
    c.ok("POST", "/api/v1/jobs/" + id + "/continue");
    REQUIRE(until(s, [&] { return c.state()["job"].is_null(); }, 120000));
    REQUIRE(c.state()["lastJob"]["state"] == "done");
    refusedAsRecent();
    s.reboot();
    signInAgain();
    refusedAsRecent();
  }
  SUBCASE("interrupted by a restart: counted from the restart") {
    s.reboot();
    signInAgain();
    refusedAsRecent();
  }
  SUBCASE("stopped") {
    c.ok("POST", "/api/v1/stop");
    c.ok("POST", "/api/v1/resume");
    refusedAsRecent();
  }
  SUBCASE("failed part way: no new mix over it, and counted from when it is ended") {
    s.world().cap(kB)->blocked = true;
    c.ok("POST", "/api/v1/jobs/" + id + "/continue");
    REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "failed"; }, 180000));
    const auto over = c.call("POST", "/api/v1/mix/start", {{"recipe", "wachstum"}, {"waterL", 20}, {"confirmRepeat", true}});
    CHECK(over.first == 409);
    CHECK(over.second["error"]["key"] == "mix.failed_open");
    CHECK(c.state()["job"]["id"] == id);  // the failed mix stays, with what is in the tank
    s.step(10 * 60 * 1000);
    c.ok("POST", "/api/v1/jobs/" + id + "/abort");
    refusedAsRecent();
  }
  SUBCASE("failed part way, then a restart: reported as cut off, counted from the restart, and only once") {
    s.world().cap(kB)->blocked = true;
    c.ok("POST", "/api/v1/jobs/" + id + "/continue");
    REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "failed"; }, 180000));
    s.reboot();
    signInAgain();
    refusedAsRecent();
    bool reported = false;
    for (const auto& e : findEvents(c, "mix")) reported = reported || e["title"]["key"] == "ev.mix_reboot";
    CHECK(reported);
    // A later restart no longer moves the window: the mix was written as ended
    s.step(31 * 60 * 1000);
    s.reboot();
    signInAgain();
    s.world().cap(kB)->blocked = false;
    CHECK(c.call("POST", "/api/v1/mix/start", mix).first == 200);
  }
  SUBCASE("failed part way, then a pump is primed: the failed mix ends with what is in the tank, and the window counts") {
    s.world().cap(kB)->blocked = true;
    c.ok("POST", "/api/v1/jobs/" + id + "/continue");
    REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "failed"; }, 180000));
    s.world().cap(kB)->blocked = false;
    c.ok("POST", std::string("/api/v1/pumps/") + kB + "/prime", {{"seconds", 3}});
    REQUIRE(until(s, [&] { return c.state()["job"].is_null(); }, 60000));
    bool reported = false;
    for (const auto& e : findEvents(c, "mix")) reported = reported || e["title"]["key"] == "ev.mix.aborted";
    CHECK(reported);
    refusedAsRecent();
  }
}

TEST_CASE("Szenario: Pumpe blockiert im Mischlauf → Paar-Fehler, nachholen nach Behebung") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  setupStage0(s, c);
  for (const char* id : {kA, kB, kC}) calibrate(s, c, id);
  s.world().fill(20, 0.02, 7.0);
  s.world().cap(kB)->blocked = true;
  auto job = c.ok("POST", "/api/v1/mix/start", {{"recipe", "wachstum"}, {"waterL", 20}, {"guided", false}})["job"];
  std::string id = job["id"];
  REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "failed"; }, 180000));
  json msg = c.state()["job"]["message"];
  CHECK(msg["key"] == "job.pair_failed");
  CHECK(msg["args"]["partner"] == "Teil A");
  s.world().cap(kB)->blocked = false;
  c.ok("POST", "/api/v1/jobs/" + id + "/resume");
  REQUIRE(until(s, [&] { return c.state()["job"].is_null(); }, 240000));
  CHECK(c.state()["lastJob"]["state"] == "done");
}

TEST_CASE("Szenario: Stromausfall im Lauf → alles aus, nicht fortgesetzt, gemeldet (R6, RAT-007)") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  setupStage0(s, c);
  for (const char* id : {kA, kB, kC}) calibrate(s, c, id);
  s.world().fill(20, 0.02, 7.0);
  c.ok("POST", "/api/v1/mix/start", {{"recipe", "wachstum"}, {"waterL", 20}, {"guided", false}});
  REQUIRE(until(s, [&] { return s.world().cap(kA)->state == 1; }, 10000, 200));
  s.reboot();
  CHECK(s.world().cap(kA)->state != 1);
  Client again{s};
  again.ok("POST", "/api/v1/auth/login", {{"password", "mein-passwort"}});
  s.step(120000);
  for (const char* id : {kA, kB, kC}) CHECK(s.world().cap(id)->state != 1);
  CHECK(again.state()["job"].is_null());
  bool reported = false;
  for (const auto& e : findEvents(again, "mix")) reported = reported || e["title"]["key"] == "ev.mix_reboot";
  CHECK(reported);
}

TEST_CASE("Szenario: Pumpenkappe am Hub-Port wird nicht freigegeben (PD-012)") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  s.world().plug(2, "pump_cap");
  s.step(2000);
  auto port = c.state()["ports"][1];
  CHECK(port["state"] == "rejected");
  CHECK(port["message"]["text"].get<std::string>().find("Dosierblock") != std::string::npos);
}

TEST_CASE("Szenario Stufe 1: Regelung bringt pH und EC ins Ziel, ohne Rastung") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  s.fastForward(4);
  auto st = c.state();
  double ph = st["readings"]["tank.ph"]["value"], ec = st["readings"]["tank.ec"]["value"];
  CHECK(ph <= 5.8 + 0.15 + 0.05);
  CHECK(ph >= 5.8 - 0.3);
  CHECK(ec >= 1.4 - 0.1 - 0.05);
  CHECK(st["latches"].empty());
  CHECK(st["watchdog"]["overall"] == "ok");
  // pH zuletzt: jede pH-Gabe kommt nach der letzten EC-Gabe ihrer Runde (RAT-004)
  bool phDosed = false;
  for (const auto& e : findEvents(c, "dose")) phDosed = phDosed || e["data"]["purpose"] == "ph";
  CHECK(phDosed);
  // the hub's own pH doses are not announced and still never lock the reading (RAT-042):
  // the only jump is the one the demo puts into its run
  int jumps = 0;
  for (const auto& e : findEvents(c, "block")) jumps += e["title"]["key"] == "ev.jump" ? 1 : 0;
  CHECK(jumps == 1);
}

TEST_CASE("Szenario: EC-Gate – in Osmosewasser kein pH− (RAT-046)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  s.step(60000);
  c.ok("PATCH", "/api/v1/functions/ec_control", {{"enabled", false}});
  REQUIRE(until(s, [&] {
    auto st = c.state();
    return st["job"].is_null() && st["dosing"].is_null() && st["controllers"]["ph"]["state"] != "working";
  }, 4 * 3600 * 1000, 5000));
  s.world().fill(40, 0.02, 7.2);
  auto before = c.state()["eventId"].get<std::uint64_t>();
  s.step(20 * 60 * 1000);
  auto st = c.state();
  CHECK(st["controllers"]["ph"]["state"] == "blocked");
  CHECK(st["controllers"]["ph"]["line"]["key"] == "ph.gate");
  for (const auto& e : findEvents(c, "dose"))
    if (e["id"].get<std::uint64_t>() > before) CHECK(e["data"]["purpose"] != "ph");
}

TEST_CASE("Scenario: an EC round doses the recipe's parts but never a fixed addition (SD-057, RAT-085)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  s.step(60000);
  // CalMag in the demo's recipe becomes a fixed addition
  c.ok("POST", "/api/v1/recipes",
       {{"id", "wachstum"},
        {"steps",
         {{{"canister", "teil-a"}, {"mlPerL", 2.0}}, {{"canister", "teil-b"}, {"mlPerL", 2.0}}, {{"canister", "calmag"}, {"mlPerL", 0.6}, {"fixed", true}}}}});
  REQUIRE(until(s, [&] {
    auto st = c.state();
    return st["job"].is_null() && st["dosing"].is_null();
  }, 4 * 3600 * 1000, 5000));
  const auto before = c.state()["eventId"].get<std::uint64_t>();
  const double calmagBefore = c.state()["stock"]["calmag"].get<double>();
  s.world().fill(40, 0.6, 5.9);  // EC well below the target 1.4: an EC round follows
  std::set<std::string> dosed;
  // Until the round has dosed and settles: a dose is logged when it ends, and CalMag would come last
  REQUIRE(until(s, [&] {
    for (const auto& e : findEvents(c, "dose"))
      if (e["id"].get<std::uint64_t>() > before && e["data"]["purpose"] == "ec") dosed.insert(e["data"]["canister"].get<std::string>());
    return dosed.count("teil-a") && dosed.count("teil-b") && c.state()["controllers"]["ec"]["line"]["key"] == "ec.settling";
  }, 2 * 3600 * 1000, 10000));
  CHECK(dosed.count("calmag") == 0);
  CHECK(c.state()["stock"]["calmag"].get<double>() == doctest::Approx(calmagBefore));
  // A recipe of fixed amounts only gives EC control nothing to raise the EC with, and its line says so
  const gc::json only = c.ok("POST", "/api/v1/recipes", {{"name", "Nur CalMag"}, {"steps", {{{"canister", "calmag"}, {"mlPerL", 0.6}, {"fixed", true}}}}})["id"];
  c.ok("POST", "/api/v1/grow/complete", json::object());  // the phase's recipe would win over the function's (R4)
  c.ok("PATCH", "/api/v1/functions/ec_control", {{"params", {{"recipe", only}}}});
  s.world().fill(40, 0.6, 5.9);
  REQUIRE(until(s, [&] { return c.state()["controllers"]["ec"]["line"]["key"] == "ec.plan"; }, 2 * 3600 * 1000, 10000));
  CHECK(c.state()["controllers"]["ec"]["line"]["args"]["reason"]["key"] == "ec.recipe_no_scaled");
  // Fixed right after a round was planned, before its first dose: the round's queue empties, and it
  // ends instead of staying "dosing" and holding pH and refill
  auto calmagOnly = [&](bool fixed) {
    c.ok("POST", "/api/v1/recipes", {{"id", only}, {"steps", {{{"canister", "calmag"}, {"mlPerL", 0.6}, {"fixed", fixed}}}}});
  };
  calmagOnly(false);
  REQUIRE(until(s, [&] { return c.state()["controllers"]["ec"]["line"]["key"] == "ec.start"; }, 3600 * 1000, 1000));
  REQUIRE(c.state()["dosing"].is_null());
  calmagOnly(true);
  REQUIRE(until(s, [&] { return c.state()["controllers"]["ec"]["line"]["key"] == "ec.plan"; }, 60 * 1000, 1000));
  CHECK_FALSE(c.state()["controllers"]["ec"]["state"] == "working");
}

TEST_CASE("Szenario: Sprungsperre – während der Sperre keine pH-Gabe, Ereignis sichtbar") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  // erst wenn nichts dosiert und keine eigene Gabe den Sprung erklären würde (M8-3)
  REQUIRE(until(s, [&] {
    auto st = c.state();
    return st["job"].is_null() && st["controllers"]["ph"]["state"] == "idle" && st["controllers"]["ec"]["state"] == "idle";
  }, 4 * 3600 * 1000, 5000));
  s.step(15 * 60 * 1000);
  auto before = c.state()["eventId"].get<std::uint64_t>();
  s.control("fault", {{"device", "PHEC-3F2A91"}, {"fault", "jump"}});
  s.step(60000);
  auto st = c.state();
  CHECK(st["readings"]["tank.ph"]["quality"] == "jump");
  CHECK(st["controllers"]["ph"]["state"] == "blocked");
  bool ev = false;
  for (const auto& e : findEvents(c, "block")) ev = ev || e["title"]["key"] == "ev.jump";
  CHECK(ev);
  s.step(10 * 60 * 1000);
  for (const auto& e : findEvents(c, "dose"))
    if (e["id"].get<std::uint64_t>() > before) CHECK(e["data"]["purpose"] != "ph");
}

// #234: the regression test of the fix covers a Home Assistant probe; this one
// is the hub's own, as in every published version up to 0.1.0-proto.4
TEST_CASE("Scenario: a calibration of the hub's own pH probe left open stops explaining jumps after 30 min (RAT-042)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  c.ok("POST", "/api/v1/probe", {{"device", "PHEC-3F2A91"}, {"kind", "ph"}, {"action", "start"}});
  // The window was closed without "cancel" (a lost tab). 45 min: idle after
  // 30 min, plus the 10 min its last announcement still runs, plus margin
  s.step(45 * 60 * 1000);
  constexpr gc::Ms kPoll = 5000;
  gc::Ms quiet = 0;
  REQUIRE(until(s, [&] {
    auto st = c.state();
    bool calm = st["job"].is_null() && st["outputs"]["tank.inlet"] == false && st["controllers"]["refill"]["state"] != "working";
    quiet = calm ? quiet + kPoll : 0;
    return quiet > 11 * gc::kMinute;  // nothing else explains a jump now
  }, 8 * 3600 * 1000, kPoll));
  auto st = c.state();
  REQUIRE(st["probeCalibration"]["PHEC-3F2A91:ph"]["idle"] == true);
  CHECK(st["controllers"]["ph"]["line"]["key"] == "ctl.calibrating");  // still open: pH control keeps waiting
  const auto before = st["eventId"].get<std::uint64_t>();
  s.control("fault", {{"device", "PHEC-3F2A91"}, {"fault", "jump"}});
  s.step(60000);
  st = c.state();
  CHECK(st["readings"]["tank.ph"]["quality"] == "jump");
  CHECK_FALSE(st["readings"]["tank.ph"]["usable"].get<bool>());
  // Cancelled now, pH control finds the jump instead of dosing on it
  c.ok("POST", "/api/v1/probe", {{"device", "PHEC-3F2A91"}, {"kind", "ph"}, {"action", "cancel"}});
  s.step(10 * 60 * 1000);
  st = c.state();
  CHECK(st["controllers"]["ph"]["state"] == "blocked");
  CHECK(st["controllers"]["ph"]["line"]["args"]["reason"]["key"] == "truth.jump");
  for (const auto& e : findEvents(c, "dose"))
    if (e["id"].get<std::uint64_t>() > before) CHECK(e["data"]["purpose"] != "ph");
}

TEST_CASE("Scenario: a pH jump while the hub's own pH dose settles still locks the reading (RAT-042, RAT-044)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  // An own pH dose is sized to move pH by at most 0.3 (RAT-050), so it does not explain a jump
  // of more than 1.0 (RAT-042). A mix announces pH for 10 min after it ends, the inlet for
  // 5 min and an EC round until 1 min after its settle time: start a pH round only after
  // 11 quiet minutes.
  constexpr gc::Ms kPoll = 5000;
  gc::Ms quiet = 0;
  REQUIRE(until(s, [&] {
    auto st = c.state();
    bool calm = st["job"].is_null() && st["outputs"]["tank.inlet"] == false &&
                st["controllers"]["refill"]["state"] != "working" && st["controllers"]["ec"]["state"] == "idle" &&
                st["controllers"]["ph"]["state"] == "idle";
    quiet = calm ? quiet + kPoll : 0;
    return quiet > 11 * gc::kMinute;
  }, 8 * 3600 * 1000, kPoll));
  s.world().tank.ph += 0.4;  // the solution drifts up
  REQUIRE(until(s, [&] { return c.state()["controllers"]["ph"]["line"]["key"] == "ph.settling"; },
                30 * 60 * 1000, kPoll));
  REQUIRE(c.state()["controllers"]["ec"]["state"] == "idle");
  auto before = c.state()["eventId"].get<std::uint64_t>();
  s.control("fault", {{"device", "PHEC-3F2A91"}, {"fault", "jump"}});
  s.step(60000);
  auto st = c.state();
  CHECK(st["controllers"]["ph"]["line"]["key"] == "ph.settling");  // the jump came while it settled
  CHECK(st["readings"]["tank.ph"]["quality"] == "jump");
  bool ev = false;
  for (const auto& e : findEvents(c, "block"))
    if (e["id"].get<std::uint64_t>() > before) ev = ev || e["title"]["key"] == "ev.jump";
  CHECK(ev);
  // past the settle time and while the lock holds (15 min), no pH dose on the jumped reading
  s.step(10 * 60 * 1000);
  st = c.state();
  CHECK(st["controllers"]["ph"]["state"] == "blocked");
  CHECK(st["controllers"]["ph"]["line"]["key"] == "ph.invalid");
  CHECK(st["controllers"]["ph"]["line"]["args"]["reason"]["key"] == "truth.jump");
  for (const auto& e : findEvents(c, "dose"))
    if (e["id"].get<std::uint64_t>() > before) CHECK(e["data"]["purpose"] != "ph");
}

TEST_CASE("Szenario: Trockenlauf – Umwälzpumpe aus, Rastung, Quittierung (M11-1, RAT-062)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  c.ok("PATCH", "/api/v1/functions/refill", {{"enabled", false}});
  c.ok("PATCH", "/api/v1/functions/circulation", {{"params", {{"mode", "always"}}}});
  s.step(20000);
  REQUIRE(c.state()["outputs"]["tank.circulation"] == true);
  s.world().fill(2.0, 1.2, 6.0);
  s.step(15000);
  auto st = c.state();
  CHECK(st["outputs"]["tank.circulation"] == false);
  CHECK(st["latches"].contains("circulation.dry"));
  const json dry = findEvents(c, "alarm").at(0);  // the cut-off by key (SD-032)
  CHECK(dry["title"]["key"] == "ev.circ.dry");
  CHECK(dry["text"]["key"] == "ev.circ.dry.low_self");  // it would release itself once refilled (SD-022)
  c.ok("POST", "/api/v1/latches/circulation.dry/ack");
  const json released = findEvents(c, "block").at(0);  // named as on the Release button (SD-032)
  CHECK(released["title"]["key"] == "ev.latch_released");
  CHECK(released["text"]["key"] == "latch.circulation.dry");
  s.step(5000);
  st = c.state();
  CHECK(st["outputs"]["tank.circulation"] == false);  // Einschaltsperre bleibt, solange der Pegel fehlt
  CHECK_FALSE(st["latches"].contains("circulation.dry"));
}

// SD-022 (#27): a dry-run latch caused only by a low level releases itself once a usable
// level has stayed at or above the minimum plus a margin for 5 min. Demo tank: 60 L,
// minimum 3 L, margin max(0.5 L, 2 % of 60 L) = 1.2 L, so 4.2 L.
namespace {
void circulateAlways(sim::Simulation& s, Client& c) {
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  c.ok("PATCH", "/api/v1/functions/refill", {{"enabled", false}});
  c.ok("PATCH", "/api/v1/functions/circulation", {{"params", {{"mode", "always"}}}});
  REQUIRE(until(s, [&] { return c.state()["outputs"]["tank.circulation"] == true; }, 60000));
}
void runDry(sim::Simulation& s, Client& c) {
  s.world().fill(2.0, 1.2, 6.0);
  REQUIRE(until(s, [&] { return c.state()["latches"].contains("circulation.dry"); }, 30000));
}
// The circulation pump on a smart plug, so a test can press its button or make it stick
std::string circulateOnPlug(sim::Simulation& s, Client& c) {
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  json r;
  {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    r = s.control("net_add", {{"class", "shelly_plug"}, {"loads", json::array({{{"load", "circulation"}, {"watts", 18}}})}});
  }
  const std::string id = r["id"];
  REQUIRE(until(s, [&] {
    const json st = c.state();
    for (const auto& d : st["devices"])
      if (d["id"] == id && d["online"] == true) return true;
    return false;
  }, 10000));
  c.ok("POST", "/api/v1/devices/" + id + "/accept", {{"name", ""}});
  c.ok("PUT", "/api/v1/roles/tank.circulation", {{"device", id}, {"channel", 0}});
  c.ok("PATCH", "/api/v1/functions/refill", {{"enabled", false}});
  c.ok("PATCH", "/api/v1/functions/circulation", {{"params", {{"mode", "always"}}}});
  REQUIRE(until(s, [&] { return c.state()["outputs"]["tank.circulation"] == true; }, 60000));
  return id;
}
}  // namespace

TEST_CASE("Scenario: a dry-run latch from a low level releases itself 5 min after the tank is back above minimum plus margin (SD-022, #27)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  circulateAlways(s, c);
  runDry(s, c);
  json st = c.state();
  CHECK(st["outputs"]["tank.circulation"] == false);
  CHECK(st["latches"]["circulation.dry"]["cause"] == "level");
  const json cut = findEvents(c, "alarm").at(0);
  CHECK(cut["title"]["key"] == "ev.circ.dry");
  CHECK(cut["text"]["key"] == "ev.circ.dry.low_self");  // says it releases itself, and from which level
  CHECK(cut["text"]["args"]["back"].get<double>() == doctest::Approx(4.2));
  // It needs no person: blocked, the line names the latch (the Release button stays)
  CHECK(st["controllers"]["circulation"]["state"] == "blocked");
  CHECK(st["controllers"]["circulation"]["line"]["key"] == "circ.latched");
  CHECK(st["controllers"]["circulation"]["line"]["args"]["reason"]["key"] == "act.circ.latched_self");
  // Above the minimum but within the margin: it holds
  s.world().fill(4.0, 1.2, 6.0);
  s.step(10 * 60 * 1000);
  CHECK(c.state()["latches"].contains("circulation.dry"));
  CHECK(c.state()["outputs"]["tank.circulation"] == false);
  // Back above 4.2 L: it holds for 5 min, then releases itself and says so; the pump runs again
  s.world().fill(10.0, 1.2, 6.0);
  s.step(4 * 60 * 1000);
  CHECK(c.state()["latches"].contains("circulation.dry"));
  REQUIRE(until(s, [&] { return !c.state()["latches"].contains("circulation.dry"); }, 3 * 60 * 1000));
  const json released = findEvents(c, "unblock").at(0);
  CHECK(released["title"]["key"] == "ev.circ.dry.released");
  CHECK(released["text"]["key"] == "ev.circ.dry.released.text");
  CHECK(released["data"]["role"] == "tank.circulation");
  REQUIRE(until(s, [&] { return c.state()["outputs"]["tank.circulation"] == true; }, 30000));
}

TEST_CASE("Scenario: a low level again within 60 min of releasing itself holds until acknowledged (SD-022, #27)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  circulateAlways(s, c);
  runDry(s, c);
  s.world().fill(10.0, 1.2, 6.0);
  REQUIRE(until(s, [&] { return !c.state()["latches"].contains("circulation.dry"); }, 10 * 60 * 1000));
  REQUIRE(until(s, [&] { return c.state()["outputs"]["tank.circulation"] == true; }, 30000));
  s.step(30 * 60 * 1000);
  runDry(s, c);  // 30-odd min later: something keeps draining the tank or the sensor flutters
  CHECK(findEvents(c, "alarm").at(0)["text"]["key"] == "ev.circ.dry.low_repeat");  // check for a leak, then tap Release
  CHECK(c.state()["controllers"]["circulation"]["line"]["args"]["reason"]["key"] == "act.circ.latched_repeat");
  CHECK(c.state()["controllers"]["circulation"]["state"] == "latched");  // a person is needed
  s.world().fill(10.0, 1.2, 6.0);
  s.step(15 * 60 * 1000);
  CHECK(c.state()["latches"].contains("circulation.dry"));
  CHECK(c.state()["outputs"]["tank.circulation"] == false);
  c.ok("POST", "/api/v1/latches/circulation.dry/ack");
  REQUIRE(until(s, [&] { return c.state()["outputs"]["tank.circulation"] == true; }, 30000));
  // More than 60 min after the release, a new low level may release itself again
  s.step(40 * 60 * 1000);
  runDry(s, c);
  CHECK(findEvents(c, "alarm").at(0)["text"]["key"] == "ev.circ.dry.low_self");
}

TEST_CASE("Scenario: no self-release in maintenance mode; 5 min of good readings after it ends (SD-022, #27)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  circulateAlways(s, c);
  runDry(s, c);
  c.ok("POST", "/api/v1/maintenance", {{"minutes", 30}});
  s.world().fill(10.0, 1.2, 6.0);
  s.step(20 * 60 * 1000);
  CHECK(c.state()["latches"].contains("circulation.dry"));
  c.ok("POST", "/api/v1/maintenance", {{"minutes", 0}});
  s.step(4 * 60 * 1000);
  CHECK(c.state()["latches"].contains("circulation.dry"));
  REQUIRE(until(s, [&] { return !c.state()["latches"].contains("circulation.dry"); }, 3 * 60 * 1000));
}

TEST_CASE("Scenario: a dry-run latch without a stored cause, or with anything else stored, holds until acknowledged after a restart (SD-067, RAT-063)") {
  const auto dir = std::filesystem::temp_directory_path() / ("gc-test-dry-cause-" + std::to_string(std::random_device{}()));
  std::filesystem::remove_all(dir);
  auto o = test::opts("demo");
  o.dataDir = (dir / "saved").string();
  {
    sim::Simulation s(o);  // latch, then save everything
    Client c{s};
    circulateAlways(s, c);
    runDry(s, c);
    s.step(10000);
  }
  // As a version before SD-022 saved it, with a reason to hold this version reads as text,
  // with a key from another version, damaged
  const std::vector<std::function<void(json&)>> forms = {
      [](json& l) { l.erase("cause"); },
      [](json& l) { l["cause"] = "current"; },
      [](json& l) { l["offFailed"] = "true"; },
      [](json& l) { l["future"] = 1; },
      [](json& l) { l = "Trockenlauf"; },
  };
  int n = 0;
  for (const auto& form : forms) {
    CAPTURE(n);
    const auto here = dir / std::to_string(n++);
    std::filesystem::copy(dir / "saved", here, std::filesystem::copy_options::recursive);
    {
      const auto file = here / "state.json";
      json st = json::parse(std::ifstream(file));
      REQUIRE(st["latches"]["circulation.dry"]["cause"] == "level");
      form(st["latches"]["circulation.dry"]);
      std::ofstream(file) << st.dump();
    }
    auto again = o;
    again.dataDir = here.string();
    again.startEpoch += 600;
    sim::Simulation s(again);
    Client c{s};
    c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
    REQUIRE(c.state()["latches"].contains("circulation.dry"));
    s.world().fill(10.0, 1.2, 6.0);
    s.step(15 * 60 * 1000);
    CHECK(c.state()["latches"].contains("circulation.dry"));
    CHECK(c.state()["controllers"]["circulation"]["line"]["args"]["reason"]["key"] == "act.circ.latched");
  }
  std::filesystem::remove_all(dir);
}

TEST_CASE("Scenario: any reading below the release level restarts the 5 min (SD-067)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  circulateAlways(s, c);
  runDry(s, c);
  s.world().fill(10.0, 1.2, 6.0);
  s.step(4 * 60 * 1000);
  s.world().fill(4.0, 1.2, 6.0);  // a dip within the margin
  s.step(30000);
  s.world().fill(10.0, 1.2, 6.0);
  s.step(3 * 60 * 1000);  // 5 min after the first rise, but not after the second
  CHECK(c.state()["latches"].contains("circulation.dry"));
  REQUIRE(until(s, [&] { return !c.state()["latches"].contains("circulation.dry"); }, 3 * 60 * 1000));
}

TEST_CASE("Scenario: a restart during the 5 min starts them again; the stored latch still releases itself (SD-067)") {
  const auto dir = std::filesystem::temp_directory_path() / ("gc-test-dry-reboot-" + std::to_string(std::random_device{}()));
  std::filesystem::remove_all(dir);
  auto o = test::opts("demo");
  o.dataDir = dir.string();
  sim::Simulation s(o);
  Client c{s};
  circulateAlways(s, c);
  runDry(s, c);
  s.world().fill(10.0, 1.2, 6.0);
  s.step(4 * 60 * 1000);
  s.reboot();
  Client again{s};
  again.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  s.step(3 * 60 * 1000);
  CHECK(again.state()["latches"].contains("circulation.dry"));
  REQUIRE(until(s, [&] { return !again.state()["latches"].contains("circulation.dry"); }, 3 * 60 * 1000));
  std::filesystem::remove_all(dir);
}

TEST_CASE("Scenario: a level that jumps while latched holds the latch until acknowledged (SD-067)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  circulateAlways(s, c);
  runDry(s, c);
  // The demo's setup fills the tank through the inlet, which explains level changes for
  // 5 min after it, on the clock the test then runs on: wait them out
  s.step(10 * 60 * 1000);
  s.world().fill(40.0, 1.2, 6.0);  // no inlet, no maintenance mode: a sensor stuck high looks the same
  s.step(30 * 60 * 1000);
  json st = c.state();
  CHECK(st["latches"].contains("circulation.dry"));
  CHECK(st["latches"]["circulation.dry"]["hold"] == "jump");
  CHECK(st["controllers"]["circulation"]["line"]["args"]["reason"]["key"] == "act.circ.latched_jump");
  CHECK(st["outputs"]["tank.circulation"] == false);
}

TEST_CASE("Scenario: a latch set is saved at once, not with the next save a minute later (RAT-063)") {
  const auto dir = std::filesystem::temp_directory_path() / ("gc-test-dry-save-" + std::to_string(std::random_device{}()));
  std::filesystem::remove_all(dir);
  auto o = test::opts("demo");
  o.dataDir = dir.string();
  sim::Simulation s(o);
  Client c{s};
  circulateAlways(s, c);
  s.step(61000);  // just after a regular save
  s.world().fill(2.0, 1.2, 6.0);
  REQUIRE(until(s, [&] { return c.state()["latches"].contains("circulation.dry"); }, 30000, 500));
  const json st = json::parse(std::ifstream(dir / "state.json"));
  CHECK(st["latches"].contains("circulation.dry"));
  // And a self-release, with its time for the repeat guard
  s.world().fill(10.0, 1.2, 6.0);
  REQUIRE(until(s, [&] { return !c.state()["latches"].contains("circulation.dry"); }, 10 * 60 * 1000, 500));
  const json after = json::parse(std::ifstream(dir / "state.json"));
  CHECK_FALSE(after["latches"].contains("circulation.dry"));
  CHECK(after["selfReleased"].contains("circulation.dry"));
  std::filesystem::remove_all(dir);
}

TEST_CASE("Scenario: a stored time of the last self-release that is not plausible holds the next latch (SD-067)") {
  const auto dir = std::filesystem::temp_directory_path() / ("gc-test-dry-implausible-" + std::to_string(std::random_device{}()));
  std::filesystem::remove_all(dir);
  auto o = test::opts("demo");
  o.dataDir = dir.string();
  {
    sim::Simulation s(o);
    Client c{s};
    circulateAlways(s, c);
    runDry(s, c);
    s.world().fill(10.0, 1.2, 6.0);
    REQUIRE(until(s, [&] { return !c.state()["latches"].contains("circulation.dry"); }, 10 * 60 * 1000));
  }
  {
    const auto file = dir / "state.json";
    json st = json::parse(std::ifstream(file));
    REQUIRE(st["selfReleased"].contains("circulation.dry"));
    st["selfReleased"]["circulation.dry"] = 1e300;  // damaged: out of any range
    std::ofstream(file) << st.dump();
  }
  o.startEpoch += 3 * 3600;  // hours later: a plausible time would allow the self-release again
  sim::Simulation s(o);
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  REQUIRE(until(s, [&] { return c.state()["outputs"]["tank.circulation"] == true; }, 60000));
  runDry(s, c);
  CHECK(c.state()["latches"]["circulation.dry"]["hold"] == "repeat");  // counted from now: one more careful hour
  // ... then the guard ages out instead of holding for good
  c.ok("POST", "/api/v1/latches/circulation.dry/ack");
  s.world().fill(10.0, 1.2, 6.0);
  REQUIRE(until(s, [&] { return c.state()["outputs"]["tank.circulation"] == true; }, 60000));
  s.step(61 * 60 * 1000);
  runDry(s, c);
  CHECK_FALSE(c.state()["latches"]["circulation.dry"].contains("hold"));
  std::filesystem::remove_all(dir);
}

TEST_CASE("Scenario: the release level grows with the tank: 0.5 L at least, 2 % of the capacity above 25 L (SD-067)") {
  // A tank without a capacity gets the 0.5 L floor
  for (const auto& [capacity, min, back] : std::vector<std::tuple<json, double, double>>{{10, 3, 3.5}, {200, 3, 7.0}, {nullptr, 3, 3.5}}) {
    CAPTURE(capacity.dump());
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    circulateAlways(s, c);
    c.ok("PUT", "/api/v1/tank", {{"name", "Tank 1"}, {"capacityL", capacity}, {"minL", min}, {"water", "ro"}});
    runDry(s, c);
    CHECK(c.state()["controllers"]["circulation"]["line"]["args"]["reason"]["args"]["back"].get<double>() == doctest::Approx(back));
  }
}

TEST_CASE("Scenario: no self-release above the capacity, nor while the pump's state is unknown (SD-067)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  circulateAlways(s, c);
  runDry(s, c);
  // Above the capacity (60 L): a sensor reading more than the tank holds is no refilled tank
  c.ok("POST", "/api/v1/maintenance", {{"minutes", 30}});  // no jump: maintenance explains it
  s.world().fill(65.0, 1.2, 6.0);
  s.step(10000);
  c.ok("POST", "/api/v1/maintenance", {{"minutes", 0}});
  s.step(15 * 60 * 1000);
  CHECK(c.state()["latches"].contains("circulation.dry"));
  c.ok("POST", "/api/v1/maintenance", {{"minutes", 30}});
  s.world().fill(30.0, 1.2, 6.0);
  s.step(10000);
  c.ok("POST", "/api/v1/maintenance", {{"minutes", 0}});
  REQUIRE(until(s, [&] { return !c.state()["latches"].contains("circulation.dry"); }, 6 * 60 * 1000));
}

TEST_CASE("Scenario: a latch that cannot release itself needs a person and says so (SD-067)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  circulateAlways(s, c);
  auto needsPerson = [&] {
    s.step(5000);
    const json st = c.state();
    CHECK(st["controllers"]["circulation"]["state"] == "latched");
    CHECK(st["controllers"]["circulation"]["line"]["args"]["reason"]["key"] == "act.circ.latched");
    auto [code, body] = c.call("POST", "/api/v1/roles/tank.circulation/switch", {{"on", true}});
    CHECK(code == 409);
    CHECK(body["error"]["key"] == "act.circ.latched");
  };
  SUBCASE("the release level lies above the capacity") {
    c.ok("PUT", "/api/v1/tank", {{"name", "Tank 1"}, {"capacityL", 3.4}, {"minL", 3}, {"water", "ro"}});
    runDry(s, c);
    CHECK(findEvents(c, "alarm").at(0)["text"]["key"] == "ev.circ.dry.low");  // "tap Release"
    needsPerson();
    s.world().fill(3.3, 1.2, 6.0);
    s.step(15 * 60 * 1000);
    CHECK(c.state()["latches"].contains("circulation.dry"));
  }
  SUBCASE("the level sensor is unbound while latched") {
    runDry(s, c);
    c.ok("DELETE", "/api/v1/roles/tank.level");
    needsPerson();
  }
  SUBCASE("the minimum is cleared while latched") {
    runDry(s, c);
    c.ok("PUT", "/api/v1/tank", {{"name", "Tank 1"}, {"capacityL", 60}, {"minL", nullptr}, {"water", "ro"}});
    needsPerson();
  }
}

TEST_CASE("Scenario: \"the pump did not stay off\" replaces a repeat hold and no later reason replaces it (SD-067)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  const std::string id = circulateOnPlug(s, c);
  runDry(s, c);
  s.world().fill(10.0, 1.2, 6.0);
  REQUIRE(until(s, [&] { return !c.state()["latches"].contains("circulation.dry"); }, 10 * 60 * 1000));
  REQUIRE(until(s, [&] { return c.state()["outputs"]["tank.circulation"] == true; }, 30000));
  s.step(5 * 60 * 1000);
  SUBCASE("pressed on at the plug during a repeat hold, then a level jump") {
    runDry(s, c);
    REQUIRE(c.state()["latches"]["circulation.dry"]["hold"] == "repeat");
    s.step(5000);
    {
      std::lock_guard<std::recursive_mutex> l(s.mutex());
      s.world().netPlug(id)->outlets[0].on = true;
    }
    s.step(5000);
    CHECK(c.state()["latches"]["circulation.dry"]["hold"] == "off_failed");
    auto [code, body] = c.call("POST", "/api/v1/roles/tank.circulation/switch", {{"on", true}});
    CHECK(code == 409);
    CHECK(body["error"]["key"] == "act.circ.latched_off");
    s.step(10 * 60 * 1000);
    s.world().fill(40.0, 1.2, 6.0);  // a jump: the first reason to look stays
    s.step(2 * 60 * 1000);
    CHECK(c.state()["latches"]["circulation.dry"]["hold"] == "off_failed");
  }
  SUBCASE("the plug sticks when a repeat latch cuts it") {
    {
      std::lock_guard<std::recursive_mutex> l(s.mutex());
      s.control("fault", {{"device", id}, {"fault", "stuck"}});
    }
    runDry(s, c);
    s.step(2000);
    CHECK(c.state()["latches"]["circulation.dry"]["hold"] == "off_failed");
  }
}

TEST_CASE("Scenario: a damaged time of the last self-release keeps the other latches and holds the next latch (SD-067)") {
  for (const json& damaged : {json{{"circulation.dry", "gestern"}}, json("kaputt"), json{{"circulation.dry", -5}}}) {
    CAPTURE(damaged.dump());
    const auto dir = std::filesystem::temp_directory_path() / ("gc-test-dry-damaged-" + std::to_string(std::random_device{}()));
    std::filesystem::remove_all(dir);
    auto o = test::opts("demo");
    o.dataDir = dir.string();
    {
      sim::Simulation s(o);
      Client c{s};
      circulateAlways(s, c);
      s.step(10000);
    }
    {
      const auto file = dir / "state.json";
      json st = json::parse(std::ifstream(file));
      st["selfReleased"] = damaged;
      st["latches"]["inlet.fault"] = {{"at", 1790000000}, {"why", "alt"}};
      std::ofstream(file) << st.dump();
    }
    o.startEpoch += 3 * 3600;
    sim::Simulation s(o);
    Client c{s};
    c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
    CHECK(c.state()["stopped"] == false);
    CHECK(c.state()["latches"].contains("inlet.fault"));
    REQUIRE(until(s, [&] { return c.state()["outputs"]["tank.circulation"] == true; }, 60000));
    // Down to the minimum in steps below the jump threshold, then a dry run
    for (double v : {s.world().tank.volumeL - 15, s.world().tank.volumeL - 30}) {
      if (v <= 3) break;
      s.world().fill(v, 1.2, 6.0);
      s.step(6 * 60 * 1000);
    }
    s.world().fill(std::min(2.0, s.world().tank.volumeL), 1.2, 6.0);
    REQUIRE(until(s, [&] { return c.state()["latches"].contains("circulation.dry"); }, 30000));
    CHECK(c.state()["latches"]["circulation.dry"]["hold"] == "repeat");
    std::filesystem::remove_all(dir);
  }
}

TEST_CASE("Scenario: a clock step keeps the hour of the repeat guard (SD-067)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  circulateAlways(s, c);
  runDry(s, c);
  s.world().fill(10.0, 1.2, 6.0);
  REQUIRE(until(s, [&] { return !c.state()["latches"].contains("circulation.dry"); }, 10 * 60 * 1000));
  REQUIRE(until(s, [&] { return c.state()["outputs"]["tank.circulation"] == true; }, 30000));
  s.control("time", {{"stepS", 2 * 3600}});  // the clock is set two hours ahead
  s.step(10 * 60 * 1000);
  runDry(s, c);
  CHECK(c.state()["latches"]["circulation.dry"]["hold"] == "repeat");
}

TEST_CASE("Scenario: during a Stop the latch may release itself; the pump starts only after Resume (SD-067)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  circulateAlways(s, c);
  runDry(s, c);
  c.ok("POST", "/api/v1/stop");
  s.world().fill(10.0, 1.2, 6.0);
  REQUIRE(until(s, [&] { return !c.state()["latches"].contains("circulation.dry"); }, 10 * 60 * 1000));
  s.step(60000);
  CHECK(c.state()["outputs"]["tank.circulation"] == false);
  c.ok("POST", "/api/v1/resume");
  REQUIRE(until(s, [&] { return c.state()["outputs"]["tank.circulation"] == true; }, 30000));
}

TEST_CASE("Scenario: the 60-min repeat guard survives a restart (SD-022, #27)") {
  const auto dir = std::filesystem::temp_directory_path() / ("gc-test-dry-repeat-" + std::to_string(std::random_device{}()));
  std::filesystem::remove_all(dir);
  auto o = test::opts("demo");
  o.dataDir = dir.string();
  {
    sim::Simulation s(o);
    Client c{s};
    circulateAlways(s, c);
    runDry(s, c);
    s.world().fill(10.0, 1.2, 6.0);
    REQUIRE(until(s, [&] { return !c.state()["latches"].contains("circulation.dry"); }, 10 * 60 * 1000));
  }
  o.startEpoch += 20 * 60;  // restarted 20-odd min after the release
  sim::Simulation s(o);
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  REQUIRE(until(s, [&] { return c.state()["outputs"]["tank.circulation"] == true; }, 60000));
  runDry(s, c);
  CHECK(findEvents(c, "alarm").at(0)["text"]["key"] == "ev.circ.dry.low_repeat");
  s.world().fill(10.0, 1.2, 6.0);
  s.step(15 * 60 * 1000);
  CHECK(c.state()["latches"].contains("circulation.dry"));
  std::filesystem::remove_all(dir);
}

TEST_CASE("Szenario: Zulauf – Füllstand fällt aus → Notabschaltung, kein automatischer Neuanlauf (RAT-031)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  REQUIRE(until(s, [&] { return c.state()["job"].is_null(); }, 600000));
  s.world().fill(25, 1.3, 5.9);
  // Läuft gerade eine EC-Runde, wartet der Zulauf, bis sie eingeschwungen ist (RAT-055).
  REQUIRE(until(s, [&] { return c.state()["outputs"]["tank.inlet"] == true; }, 15 * 60 * 1000, 5000));
  s.control("fault", {{"device", "LVL-77B210"}, {"fault", "offline"}});
  s.step(70000);
  auto st = c.state();
  CHECK(st["outputs"]["tank.inlet"] == false);
  CHECK(st["latches"].contains("inlet.fault"));
  CHECK(st["latches"]["inlet.fault"]["why"]["key"] == "why.level_invalid");
  const json cutoff = findEvents(c, "alarm").at(0);  // the cut-off by key (SD-032)
  CHECK(cutoff["title"]["key"] == "ev.inlet.cutoff");
  CHECK(cutoff["text"]["args"]["why"]["key"] == "why.level_invalid");
  s.control("fault", {{"device", "LVL-77B210"}, {"fault", "none"}});
  s.step(6 * 60 * 1000);
  CHECK(c.state()["outputs"]["tank.inlet"] == false);
  CHECK(c.state()["latches"].contains("inlet.fault"));  // no self-release at the inlet (RAT-031, SD-067)
  c.ok("POST", "/api/v1/latches/inlet.fault/ack");
  CHECK(findEvents(c, "block").at(0)["text"]["key"] == "latch.inlet.fault");  // named as on the Release button
}

namespace {
// As until(), with ticks 5 s apart: a refill's margins are a minute, and long runs cost less
template <typename F>
bool soon(sim::Simulation& s, F cond, gc::Ms maxMs, gc::Ms every = 30000) {
  for (gc::Ms t = 0; t < maxMs; t += every) {
    if (cond()) return true;
    s.step(every, 5000);
  }
  return cond();
}
json refillLine(Client& c) { return c.state()["controllers"]["refill"]["line"]; }
bool inletOpen(Client& c) { return c.state()["outputs"]["tank.inlet"] == true; }
bool inletLatched(Client& c) { return c.state()["latches"].contains("inlet.fault"); }
std::string latchWhy(Client& c) { return c.state()["latches"]["inlet.fault"]["why"]["key"]; }
// Demo after its lead-in, the tank at the given volume; refill from start to target if given
void demoAt(sim::Simulation& s, Client& c, double litres, double start = gc::kNaN, double target = gc::kNaN) {
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  REQUIRE(until(s, [&] { return c.state()["job"].is_null(); }, 600000, 10000));
  if (gc::isNum(start)) c.ok("PATCH", "/api/v1/functions/refill", {{"params", {{"start_below_l", start}, {"target_l", target}}}});
  s.world().fill(litres, 1.3, 5.9);
}
void freezeLevel(sim::Simulation& s) {
  s.step(10000, 5000);  // the reading has followed the new volume
  s.control("fault", {{"device", "LVL-77B210"}, {"fault", "frozen"}});
}
// An irrigation pump on a socket, bound to the zone
std::string irrigationPlug(sim::Simulation& s, Client& c) {
  json r;
  {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    r = s.control("net_add", {{"class", "shelly_plug"}, {"loads", json::array({{{"load", "irrigation"}, {"watts", 30}}})}});
  }
  const std::string id = r["id"];
  REQUIRE(until(s, [&] {
    const json st = c.state();
    for (const auto& d : st["devices"])
      if (d["id"] == id && d["online"] == true) return true;
    return false;
  }, 10000));
  c.ok("POST", "/api/v1/devices/" + id + "/accept", {{"name", ""}});
  c.ok("PUT", "/api/v1/roles/zone.irrigation_pump", {{"device", id}, {"channel", 0}});
  return id;
}
int refills(Client& c) {
  int n = 0;
  for (const auto& e : findEvents(c, "tank")) n += e["title"]["key"] == "ev.refilled" ? 1 : 0;
  return n;
}
json lastEvent(Client& c, const std::string& type, const std::string& title) {
  for (const auto& e : findEvents(c, type))
    if (e["title"]["key"] == title) return e;
  return nullptr;
}
}  // namespace

TEST_CASE("Scenario: a refill from a nearly empty tank fills in steps within the inlet's open time, checks each rise and reaches its target (#155, SD-082)") {
  // Demo: 60 L tank, inlet 2 L/min, the valve open at most 15 min, refill below 30 L to 45 L. From
  // 2.3 L that is 42.7 L or 21 min: planned so, the inlet's own limit cut it and latched (RAT-031).
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  demoAt(s, c, 2.3);
  REQUIRE(soon(s, [&] { return inletOpen(c); }, 15 * 60 * 1000, 5000));
  const size_t doses = findEvents(c, "dose").size();
  json line = refillLine(c);
  CHECK(line["key"] == "refill.filling_step");
  CHECK(line["args"]["step"] == 1);
  CHECK(line["args"]["steps"] == 2);
  CHECK(line["args"]["min"] == 15);
  const double from = line["args"]["from"].get<double>();
  CHECK(line["args"]["to"].get<double>() == doctest::Approx(from + 28.0));  // 14 min at 2 L/min: 1 min below the limit
  REQUIRE(soon(s, [&] { return !inletOpen(c); }, 20 * 60 * 1000, 5000));
  CHECK(s.world().tank.volumeL == doctest::Approx(2.3 + 28.0).epsilon(0.03));
  // The pause: the rise is checked at its end, then the next step; EC and pH wait meanwhile
  line = refillLine(c);
  CHECK(line["key"] == "refill.check_step");
  CHECK(line["args"]["next"] == 2);
  REQUIRE(soon(s, [&] { return inletOpen(c); }, 15 * 60 * 1000, 5000));
  line = refillLine(c);
  CHECK(line["key"] == "refill.filling_step");
  CHECK(line["args"]["step"] == 2);
  CHECK(line["args"]["to"].get<double>() == doctest::Approx(45.0).epsilon(0.03));
  REQUIRE(soon(s, [&] { return !inletOpen(c); }, 15 * 60 * 1000, 5000));
  CHECK(refillLine(c)["key"] == "refill.check");
  REQUIRE(soon(s, [&] { return refillLine(c)["key"] == "refill.ok"; }, 15 * 60 * 1000, 5000));
  CHECK(findEvents(c, "dose").size() == doses);  // nothing dosed from the first opening to the last check
  CHECK(c.state()["controllers"]["ec"]["line"]["key"] != "ec.wait_refill");  // then EC and pH go on
  CHECK(s.world().tank.volumeL == doctest::Approx(45.0).epsilon(0.03));
  CHECK_FALSE(inletLatched(c));
  CHECK(lastEvent(c, "alarm", "ev.inlet.cutoff").is_null());
  CHECK(lastEvent(c, "tank", "ev.refilled")["text"]["key"] == "ev.refilled.step");
  CHECK(refills(c) == 2);
  // With 40 s between ticks too (15 min is no multiple of it), the refill closes the valve before the limit does
  s.world().fill(2.3, 1.3, 5.9);
  s.fastForward(1.5, 40000);
  CHECK_FALSE(inletLatched(c));
  CHECK(s.world().tank.volumeL == doctest::Approx(45.0).epsilon(0.05));
}

TEST_CASE("Scenario: a level that does not rise after a refill latches the inlet; a slower inlet within reason does not (#155, SD-082, RAT-031)") {
  SUBCASE("a stuck level sensor: one step, then the latch, no second step") {
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    demoAt(s, c, 2.3);
    freezeLevel(s);
    REQUIRE(soon(s, [&] { return inletLatched(c); }, 40 * 60 * 1000, 10000));
    const json why = c.state()["latches"]["inlet.fault"]["why"];
    CHECK(why["key"] == "why.refill_no_rise");
    CHECK(why["args"]["expected"].get<double>() == doctest::Approx(28.0));
    CHECK(std::abs(why["args"]["rise"].get<double>()) < 1.0);
    CHECK(why["args"]["flow"] == doctest::Approx(2.0));
    CHECK(s.world().tank.volumeL == doctest::Approx(30.3).epsilon(0.03));  // one step only
    CHECK_FALSE(lastEvent(c, "alarm", "ev.refill.no_rise").is_null());
    s.fastForward(0.5, 10000);
    CHECK_FALSE(inletOpen(c));
    CHECK(s.world().tank.volumeL == doctest::Approx(30.3).epsilon(0.03));
    CHECK(refillLine(c)["key"] == "refill.latched");  // a person checks and releases it (RAT-031)
  }
  SUBCASE("an inlet at 40 % of the set rate latches after the first step") {
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    demoAt(s, c, 2.3);
    s.world().inletLpm = 0.8;
    REQUIRE(soon(s, [&] { return inletLatched(c); }, 40 * 60 * 1000, 10000));
    CHECK(latchWhy(c) == "why.refill_no_rise");
    CHECK(s.world().tank.volumeL == doctest::Approx(2.3 + 0.8 * 14).epsilon(0.05));
  }
  SUBCASE("an inlet at 93 % of the set rate passes and fills in steps") {
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    demoAt(s, c, 2.3);
    s.world().inletLpm = 1.86;
    s.fastForward(1.2, 5000);
    CHECK_FALSE(inletLatched(c));
    CHECK(s.world().tank.volumeL > 40.0);  // the total is computed once, so it ends a little short
    CHECK(s.world().tank.volumeL < 45.5);
  }
}

TEST_CASE("Scenario: the irrigation pump and the rise check of a refill (#155, SD-082)") {
  SUBCASE("too little rise while the pump ran is not judged; with the level still stuck it latches before another fill") {
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    demoAt(s, c, 28, 25, 30);
    irrigationPlug(s, c);
    s.world().fill(20, 1.3, 5.9);
    freezeLevel(s);
    REQUIRE(soon(s, [&] { return inletOpen(c); }, 15 * 60 * 1000, 5000));
    c.ok("POST", "/api/v1/roles/zone.irrigation_pump/switch", {{"on", true}});
    s.step(30000, 5000);
    c.ok("POST", "/api/v1/roles/zone.irrigation_pump/switch", {{"on", false}});
    REQUIRE(soon(s, [&] { return !lastEvent(c, "tank", "ev.refill.unjudged").is_null(); }, 30 * 60 * 1000, 10000));
    CHECK(lastEvent(c, "tank", "ev.refill.unjudged")["text"]["key"] == "refill.unjudged.irrigation");
    // A second fill would pass the target from where the first began: a person looks now. Not as "did not
    // rise": the pump drew from the tank
    REQUIRE(inletLatched(c));
    CHECK(latchWhy(c) == "why.refill_unconfirmed");
    CHECK(c.state()["latches"]["inlet.fault"]["why"]["args"]["why"]["key"] == "refill.unjudged.irrigation");
    CHECK(refills(c) == 1);
    CHECK(s.world().tank.volumeL < 30.0 + 0.3);
  }
  SUBCASE("refills not judged count across refills until one rises as it should: two in a row latch, even with the level back above the start level each time") {
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    demoAt(s, c, 35);
    irrigationPlug(s, c);
    // From 28 L to 45 L. A leak in the line loses 60 % of it and the pump runs in the pause: too little
    // rise, not judged, yet the level ends above the start level (30 L)
    const auto refill = [&](bool leak) {
      s.world().inletLpm = leak ? 0.8 : 2.0;
      s.world().fill(28, 1.3, 5.9);
      REQUIRE(soon(s, [&] { return inletOpen(c); }, 15 * 60 * 1000, 5000));
      REQUIRE(soon(s, [&] { return !inletOpen(c); }, 15 * 60 * 1000, 5000));
      s.step(60000, 5000);
      if (leak) {
        c.ok("POST", "/api/v1/roles/zone.irrigation_pump/switch", {{"on", true}});
        s.step(30000, 5000);
        c.ok("POST", "/api/v1/roles/zone.irrigation_pump/switch", {{"on", false}});
      }
      REQUIRE(soon(s, [&] { return refillLine(c)["key"] == "refill.ok" || inletLatched(c); }, 15 * 60 * 1000, 10000));
    };
    refill(true);
    CHECK_FALSE(inletLatched(c));
    refill(false);  // rises as it should: the count starts again
    refill(true);
    CHECK_FALSE(inletLatched(c));
    refill(true);
    REQUIRE(inletLatched(c));
    CHECK(latchWhy(c) == "why.refill_unchecked");
    CHECK(refills(c) == 4);
  }
  SUBCASE("the pump running only in the pause is counted too") {
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    demoAt(s, c, 28, 25, 30);
    irrigationPlug(s, c);
    s.world().fill(20, 1.3, 5.9);
    freezeLevel(s);
    REQUIRE(soon(s, [&] { return inletOpen(c); }, 15 * 60 * 1000, 5000));
    REQUIRE(soon(s, [&] { return !inletOpen(c); }, 15 * 60 * 1000, 5000));
    s.step(60000, 5000);
    c.ok("POST", "/api/v1/roles/zone.irrigation_pump/switch", {{"on", true}});
    s.step(30000, 5000);
    c.ok("POST", "/api/v1/roles/zone.irrigation_pump/switch", {{"on", false}});
    REQUIRE(soon(s, [&] { return !lastEvent(c, "tank", "ev.refill.unjudged").is_null(); }, 15 * 60 * 1000, 10000));
    CHECK(lastEvent(c, "tank", "ev.refill.unjudged")["text"]["key"] == "refill.unjudged.irrigation");
    CHECK(latchWhy(c) == "why.refill_unconfirmed");  // no second fill unchecked
    CHECK(refills(c) == 1);
  }
  SUBCASE("a pump whose state is not known and a stuck level: no second fill unchecked, the tank stays within its target") {
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    demoAt(s, c, 31);
    const std::string plug = irrigationPlug(s, c);
    s.control("fault", {{"device", plug}, {"fault", "offline"}});
    s.world().fill(29, 1.3, 5.9);  // refill below 30 L to 45 L
    freezeLevel(s);
    REQUIRE(soon(s, [&] { return inletLatched(c); }, 60 * 60 * 1000));
    CHECK(latchWhy(c) == "why.refill_unconfirmed");
    CHECK(lastEvent(c, "tank", "ev.refill.unjudged")["text"]["key"] == "refill.unjudged.irrigation");
    CHECK(refills(c) == 1);
    CHECK(s.world().tank.volumeL < 45.0 + 0.3);
  }
  SUBCASE("a pump bound but off does not keep a stuck level from latching") {
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    demoAt(s, c, 28, 25, 30);
    irrigationPlug(s, c);
    s.world().fill(20, 1.3, 5.9);
    freezeLevel(s);
    REQUIRE(soon(s, [&] { return inletLatched(c); }, 40 * 60 * 1000, 10000));
    CHECK(latchWhy(c) == "why.refill_no_rise");
    CHECK(refills(c) == 1);
  }
  SUBCASE("after the pump ran, the first fill waits until the level reading has settled") {
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    demoAt(s, c, 28, 25, 30);
    irrigationPlug(s, c);
    s.step(5 * 60 * 1000, 5000);
    c.ok("POST", "/api/v1/roles/zone.irrigation_pump/switch", {{"on", true}});
    s.world().fill(20, 1.3, 5.9);
    s.step(30000, 5000);
    c.ok("POST", "/api/v1/roles/zone.irrigation_pump/switch", {{"on", false}});
    s.step(30000, 5000);
    CHECK(refillLine(c)["key"] == "refill.settle");
    CHECK_FALSE(inletOpen(c));
    REQUIRE(soon(s, [&] { return inletOpen(c); }, 5 * 60 * 1000, 5000));
    // The pump runs during this fill, yet the level rose enough: that counts, nothing is left unjudged
    c.ok("POST", "/api/v1/roles/zone.irrigation_pump/switch", {{"on", true}});
    s.step(30000, 5000);
    c.ok("POST", "/api/v1/roles/zone.irrigation_pump/switch", {{"on", false}});
    REQUIRE(soon(s, [&] { return refillLine(c)["key"] == "refill.ok"; }, 30 * 60 * 1000));
    CHECK(lastEvent(c, "tank", "ev.refill.unjudged").is_null());
    CHECK_FALSE(inletLatched(c));
  }
  SUBCASE("a step after irrigation late in the pause waits too") {
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    demoAt(s, c, 31);
    irrigationPlug(s, c);
    s.world().fill(14, 1.3, 5.9);  // to 45 L in steps of 14 L at max_open_min 8
    c.ok("PATCH", "/api/v1/functions/refill", {{"params", {{"max_open_min", 8}}}});
    REQUIRE(soon(s, [&] { return inletOpen(c); }, 15 * 60 * 1000, 5000));
    REQUIRE(soon(s, [&] { return !inletOpen(c); }, 15 * 60 * 1000, 5000));
    s.step(9 * 60 * 1000, 5000);
    c.ok("POST", "/api/v1/roles/zone.irrigation_pump/switch", {{"on", true}});
    s.step(20000, 5000);
    c.ok("POST", "/api/v1/roles/zone.irrigation_pump/switch", {{"on", false}});
    REQUIRE(soon(s, [&] { return refillLine(c)["key"] == "refill.settle"; }, 2 * 60 * 1000, 5000));
    CHECK_FALSE(inletOpen(c));
    REQUIRE(soon(s, [&] { return inletOpen(c); }, 5 * 60 * 1000, 5000));
  }
  SUBCASE("after maintenance the fill waits its full settling time, however long irrigation kept it waiting before") {
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    demoAt(s, c, 28, 25, 30);
    for (const char* f : {"ec_control", "ph_control"}) c.ok("PATCH", std::string("/api/v1/functions/") + f, {{"enabled", false}});
    irrigationPlug(s, c);
    s.world().fill(20, 1.3, 5.9);
    c.ok("POST", "/api/v1/roles/zone.irrigation_pump/switch", {{"on", true}});
    s.step(60000, 5000);
    c.ok("POST", "/api/v1/roles/zone.irrigation_pump/switch", {{"on", false}});
    s.step(30000, 5000);
    REQUIRE(refillLine(c)["key"] == "refill.settle");  // after irrigation
    c.ok("POST", "/api/v1/maintenance", {{"minutes", 40}});
    s.step(10 * 60 * 1000, 5000);
    s.world().fill(22, 1.3, 5.9);  // filled by hand, still below the start level
    s.step(31 * 60 * 1000, 5000);  // maintenance ended a minute ago
    CHECK(refillLine(c)["key"] == "refill.settle");
    s.step(60000, 5000);
    CHECK_FALSE(inletOpen(c));  // two minutes after it, the reading may still follow the hand fill
    REQUIRE(soon(s, [&] { return inletOpen(c); }, 3 * 60 * 1000, 5000));
  }
  SUBCASE("after a Stop the fill waits its settling time too: a person may have been at the tank") {
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    demoAt(s, c, 28, 25, 30);
    for (const char* f : {"ec_control", "ph_control"}) c.ok("PATCH", std::string("/api/v1/functions/") + f, {{"enabled", false}});
    s.step(5 * 60 * 1000, 5000);
    c.ok("POST", "/api/v1/stop");
    s.step(60000, 5000);
    s.world().fill(22, 1.3, 5.9);  // drained by hand during the Stop
    s.step(60000, 5000);
    c.ok("POST", "/api/v1/resume");
    s.step(60000, 5000);
    CHECK(refillLine(c)["key"] == "refill.settle");
    CHECK_FALSE(inletOpen(c));
    REQUIRE(soon(s, [&] { return inletOpen(c); }, 3 * 60 * 1000, 5000));
  }
  SUBCASE("irrigation every few minutes keeps the first fill waiting at most half an hour") {
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    demoAt(s, c, 28, 25, 30);
    irrigationPlug(s, c);
    s.world().fill(20, 1.3, 5.9);
    bool opened = false;
    for (int i = 0; i < 18 && !opened; ++i) {  // on 1 min, off 1 min
      c.ok("POST", "/api/v1/roles/zone.irrigation_pump/switch", {{"on", true}});
      s.step(60000, 5000);
      c.ok("POST", "/api/v1/roles/zone.irrigation_pump/switch", {{"on", false}});
      s.step(60000, 5000);
      opened = inletOpen(c) || refills(c) > 0;
      if (i < 13) CHECK_FALSE(opened);  // waits while the reading does not settle
    }
    CHECK(opened);
  }
}

TEST_CASE("Scenario: small refills in a row are judged together, and the smallest open time still fills (#155, SD-082)") {
  SUBCASE("a stuck level with a small gap: the fills together never pass the target, then they are judged") {
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    demoAt(s, c, 37.5);
    freezeLevel(s);
    c.ok("PATCH", "/api/v1/functions/refill", {{"params", {{"start_below_l", 38}, {"target_l", 40}}}});
    REQUIRE(soon(s, [&] { return inletLatched(c); }, 60 * 60 * 1000, 10000));
    CHECK(latchWhy(c) == "why.refill_unconfirmed");  // too small to say "did not rise"
    CHECK(c.state()["latches"]["inlet.fault"]["why"]["args"]["why"]["key"] == "refill.unjudged.small");
    CHECK(s.world().tank.volumeL < 40.0 + 0.3);  // 2.5 L once: a second fill would pass the target
  }
  SUBCASE("a small fill that rose too little is not added to a later refill that does not follow at once") {
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    demoAt(s, c, 37.5, 38, 40);
    s.world().inletLpm = 0.8;  // 1 L of 2.5 L: too little, but too small to judge alone
    REQUIRE(soon(s, [&] { return inletOpen(c); }, 15 * 60 * 1000, 5000));
    REQUIRE(soon(s, [&] { return refillLine(c)["key"] == "refill.ok"; }, 30 * 60 * 1000, 10000));
    CHECK_FALSE(inletLatched(c));
    s.world().inletLpm = 2.0;
    s.fastForward(1.0, 10000);
    s.world().fill(30, 1.3, 5.9);  // the tank drew down meanwhile
    REQUIRE(soon(s, [&] { return inletOpen(c); }, 15 * 60 * 1000, 5000));
    REQUIRE(soon(s, [&] { return refillLine(c)["key"] == "refill.ok" || inletLatched(c); }, 40 * 60 * 1000, 10000));
    CHECK_FALSE(inletLatched(c));  // judged alone: 10 L of 10 L
    CHECK(s.world().tank.volumeL == doctest::Approx(40.0).epsilon(0.03));
  }
  SUBCASE("a new level curve while a small fill waits: it cannot be compared with the level now, so a person looks") {
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    demoAt(s, c, 28);
    freezeLevel(s);
    REQUIRE(soon(s, [&] { return inletOpen(c); }, 15 * 60 * 1000, 5000));
    s.step(60000, 5000);
    c.ok("POST", "/api/v1/roles/tank.inlet/switch", {{"on", false}});  // about 2 L: too small to judge alone
    s.step(60000, 5000);
    // The level is calibrated again in the pause: the same tank now reads 0.5 L more
    json cfg = c.ok("GET", "/api/v1/config/export");
    for (auto& p : cfg["calibrations"]["LVL-77B210"]["tank_curve"]["points"]) p[1] = p[1].get<double>() + 0.5;
    c.ok("POST", "/api/v1/config/import", cfg);
    REQUIRE(soon(s, [&] { return inletLatched(c); }, 30 * 60 * 1000, 10000));
    CHECK(latchWhy(c) == "why.refill_unconfirmed");
    CHECK(c.state()["latches"]["inlet.fault"]["why"]["args"]["why"]["key"] == "refill.unjudged.curve");
    CHECK(s.world().tank.volumeL < 30.0 + 0.3);  // no second fill
  }
  SUBCASE("a fill not judged for want of a level is judged when the level is back: it rose, so the refill goes on") {
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    demoAt(s, c, 2.3);
    c.ok("PATCH", "/api/v1/functions/refill", {{"params", {{"max_open_min", 8}}}});  // 14 L a fill
    REQUIRE(soon(s, [&] { return inletOpen(c); }, 15 * 60 * 1000, 5000));
    REQUIRE(soon(s, [&] { return !inletOpen(c); }, 10 * 60 * 1000, 5000));
    s.control("fault", {{"device", "LVL-77B210"}, {"fault", "offline"}});
    s.step(15 * 60 * 1000, 5000);
    CHECK(lastEvent(c, "tank", "ev.refill.unjudged")["text"]["key"] == "refill.unjudged.more");
    s.control("fault", {{"device", "LVL-77B210"}, {"fault", "none"}});
    s.fastForward(1.5, 5000);
    CHECK_FALSE(inletLatched(c));
    CHECK(s.world().tank.volumeL == doctest::Approx(45.0).epsilon(0.03));
  }
  SUBCASE("max_open_min 1: steps of 1 L reach the target, also with 20 s between ticks") {
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    demoAt(s, c, 28);
    c.ok("PATCH", "/api/v1/functions/refill", {{"params", {{"max_open_min", 1}}}});
    REQUIRE(soon(s, [&] { return inletOpen(c); }, 15 * 60 * 1000, 5000));
    CHECK(refillLine(c)["args"]["to"].get<double>() - refillLine(c)["args"]["from"].get<double>() == doctest::Approx(1.0));
    s.fastForward(4.0, 20000);
    CHECK_FALSE(inletLatched(c));
    CHECK(s.world().tank.volumeL == doctest::Approx(45.0).epsilon(0.05));
  }
  SUBCASE("max_open_min 1 and a stuck level: the small fills together stay within the open time, then they are judged") {
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    demoAt(s, c, 28);
    freezeLevel(s);
    c.ok("PATCH", "/api/v1/functions/refill", {{"params", {{"max_open_min", 1}}}});
    REQUIRE(soon(s, [&] { return inletLatched(c); }, 60 * 60 * 1000));
    CHECK(latchWhy(c) == "why.refill_unconfirmed");
    CHECK(refills(c) == 1);
    CHECK(s.world().tank.volumeL < 28.0 + 1.0 + 0.2);
  }
  SUBCASE("an inlet too slow for its open time is blocked and says so") {
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    demoAt(s, c, 25);
    c.ok("PATCH", "/api/v1/functions/refill", {{"params", {{"max_open_min", 1}, {"flow_l_per_min", 0.1}}}});
    s.step(60000, 5000);
    CHECK(refillLine(c)["key"] == "refill.too_slow");
    CHECK_FALSE(inletOpen(c));
  }
}

TEST_CASE("Scenario: a refill across a restart: steps end and are reported, a pending check is still judged, a cut fill counts as not judged (#155, SD-082, RAT-007)") {
  SUBCASE("a restart between steps ends them, reported; the check still passes") {
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    demoAt(s, c, 2.3);
    REQUIRE(soon(s, [&] { return inletOpen(c); }, 15 * 60 * 1000, 5000));
    REQUIRE(soon(s, [&] { return !inletOpen(c); }, 20 * 60 * 1000, 5000));
    s.step(60000, 5000);
    s.reboot(30000, true, false);
    c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
    s.fastForward(0.5, 5000);
    CHECK(s.world().tank.volumeL == doctest::Approx(30.3).epsilon(0.03));  // above the start level: rests
    CHECK_FALSE(inletLatched(c));
    CHECK(lastEvent(c, "tank", "ev.refill.restart")["text"]["args"]["step"] == 1);
  }
  SUBCASE("a power cut longer than the rest of a fill counts only what ran until then: no false latch") {
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    demoAt(s, c, 2.3);
    REQUIRE(soon(s, [&] { return inletOpen(c); }, 15 * 60 * 1000, 5000));
    s.step(60000, 5000);
    s.reboot(15 * 60 * 1000, true, true);
    c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
    s.fastForward(1.2, 5000);
    CHECK_FALSE(inletLatched(c));
    CHECK(s.world().tank.volumeL == doctest::Approx(45.0).epsilon(0.05));
  }
  SUBCASE("the open fill and its pending check are saved at once, not with the next save (RAT-063)") {
    struct Dir {
      std::filesystem::path path = std::filesystem::temp_directory_path() / ("gc-test-refill-save-" + std::to_string(std::random_device{}()));
      ~Dir() { std::filesystem::remove_all(path); }
    } d;
    const auto& dir = d.path;
    auto o = test::opts("demo");
    o.dataDir = dir.string();
    sim::Simulation s(o);
    Client c{s};
    demoAt(s, c, 2.3);
    REQUIRE(until(s, [&] { return inletOpen(c); }, 15 * 60 * 1000, 1000));
    CHECK(json::parse(std::ifstream(dir / "state.json"))["refill"].contains("fill"));
    REQUIRE(until(s, [&] { return !inletOpen(c); }, 20 * 60 * 1000, 1000));
    const json saved = json::parse(std::ifstream(dir / "state.json"));
    CHECK(saved["refill"].contains("check"));
    CHECK_FALSE(saved["refill"].contains("fill"));
  }
  SUBCASE("a power cut in the tick the valve opened: one minute is counted, no false latch") {
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    demoAt(s, c, 2.3);
    REQUIRE(until(s, [&] { return inletOpen(c); }, 15 * 60 * 1000, 1000));
    s.reboot(15 * 60 * 1000, true, true);
    c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
    s.fastForward(1.2, 5000);
    CHECK_FALSE(inletLatched(c));
    CHECK(s.world().tank.volumeL == doctest::Approx(45.0).epsilon(0.05));
  }
  SUBCASE("a clock step back during a fill, then a power cut: what ran is still counted from the fill's start") {
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    demoAt(s, c, 2.3);
    REQUIRE(soon(s, [&] { return inletOpen(c); }, 15 * 60 * 1000, 5000));
    s.step(60000, 5000);
    s.control("time", {{"stepS", -2 * 3600}});
    s.step(60000, 5000);
    s.reboot(15 * 60 * 1000, true, true);
    c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
    s.fastForward(1.2, 5000);
    CHECK_FALSE(inletLatched(c));
    CHECK(s.world().tank.volumeL == doctest::Approx(45.0).epsilon(0.05));
  }
  SUBCASE("a stuck level, the irrigation pump's state not known and a restart early in the fill: the next fill gets only what is left") {
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    demoAt(s, c, 35);
    const std::string id = irrigationPlug(s, c);
    s.control("fault", {{"device", id}, {"fault", "offline"}});
    s.world().fill(20, 1.3, 5.9);
    freezeLevel(s);
    REQUIRE(soon(s, [&] { return inletOpen(c); }, 15 * 60 * 1000, 5000));
    s.step(2 * 60 * 1000, 5000);
    s.reboot(10 * 60 * 1000, true, true);
    c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
    double most = 0;
    for (int i = 0; i < 12 && !inletLatched(c); ++i) {
      s.step(10 * 60 * 1000, 5000);
      most = std::max(most, s.world().tank.volumeL);
    }
    REQUIRE(inletLatched(c));
    CHECK(latchWhy(c) == "why.refill_unchecked");
    CHECK(most < 45.0 + 0.3);
  }
  SUBCASE("a stuck level and a restart late in the fill: nothing more fits, so it is judged at once, as cut by the restart") {
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    demoAt(s, c, 2.3);
    freezeLevel(s);
    REQUIRE(soon(s, [&] { return inletOpen(c); }, 15 * 60 * 1000, 5000));
    s.step(13 * 60 * 1000, 5000);
    s.reboot(0, true, true);
    c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
    REQUIRE(soon(s, [&] { return inletLatched(c); }, 60 * 60 * 1000, 10000));
    CHECK(latchWhy(c) == "why.refill_unconfirmed");
    CHECK(c.state()["latches"]["inlet.fault"]["why"]["args"]["why"]["key"] == "refill.unjudged.restart");
    CHECK(s.world().tank.volumeL < 2.3 + 28.0 + 0.5);
  }
  SUBCASE("a stuck level and a restart in the pause: the check is judged after it, no second fill") {
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    demoAt(s, c, 2.3);
    freezeLevel(s);
    REQUIRE(soon(s, [&] { return inletOpen(c); }, 15 * 60 * 1000, 5000));
    REQUIRE(soon(s, [&] { return !inletOpen(c); }, 20 * 60 * 1000, 5000));
    s.step(60000, 5000);
    s.reboot(0, true, true);
    c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
    REQUIRE(soon(s, [&] { return inletLatched(c); }, 30 * 60 * 1000, 10000));
    CHECK(latchWhy(c) == "why.refill_no_rise");
    CHECK(s.world().tank.volumeL == doctest::Approx(30.3).epsilon(0.03));
  }
  SUBCASE("a stuck level and restarts in the middle of fills: two cut fills, then a person looks") {
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    demoAt(s, c, 2.3);
    freezeLevel(s);
    for (int i = 0; i < 2; ++i) {
      REQUIRE(soon(s, [&] { return inletOpen(c); }, 30 * 60 * 1000, 5000));
      s.step(5 * 60 * 1000, 5000);
      s.reboot(0, true, true);
      c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});  // the level head stays stuck
    }
    REQUIRE(soon(s, [&] { return inletLatched(c); }, 30 * 60 * 1000, 10000));
    CHECK(latchWhy(c) == "why.refill_unchecked");
    CHECK(s.world().tank.volumeL < 2.3 + 2 * 10.0 + 1.0);  // two cut fills of 5 min, nothing more
  }
}

TEST_CASE("Scenario: what ends refill steps, an inlet closed outside the refill, and calibrations during the check (#155, SD-082)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  demoAt(s, c, 2.3);
  // A changed target ends the steps
  REQUIRE(soon(s, [&] { return inletOpen(c); }, 15 * 60 * 1000, 5000));
  REQUIRE(soon(s, [&] { return !inletOpen(c); }, 20 * 60 * 1000, 5000));
  c.ok("PATCH", "/api/v1/functions/refill", {{"params", {{"target_l", 44}}}});
  s.fastForward(0.4, 5000);
  CHECK(s.world().tank.volumeL == doctest::Approx(30.3).epsilon(0.03));
  CHECK_FALSE(inletLatched(c));
  // Closed outside the refill (in the app): not opened again at once; its rise is checked after the pause.
  // (The level drops by less than 20 L at a time: more would be a jump, which blocks the refill.)
  s.world().fill(15, 1.3, 5.9);
  REQUIRE(soon(s, [&] { return inletOpen(c); }, 15 * 60 * 1000, 5000));
  s.step(60000, 5000);
  c.ok("POST", "/api/v1/roles/tank.inlet/switch", {{"on", false}});
  s.step(60000, 5000);
  CHECK_FALSE(inletOpen(c));
  CHECK(refillLine(c)["key"] == "refill.check");
  // A pH calibration during the pause does not keep the rise from being checked; one of the level does
  REQUIRE(soon(s, [&] { return inletOpen(c); }, 15 * 60 * 1000, 5000));
  REQUIRE(soon(s, [&] { return !inletOpen(c); }, 20 * 60 * 1000, 5000));
  c.ok("POST", "/api/v1/probe", {{"device", "PHEC-3F2A91"}, {"kind", "ph"}, {"action", "start"}});
  s.fastForward(0.25, 5000);
  CHECK(lastEvent(c, "tank", "ev.refill.unjudged").is_null());
  c.ok("POST", "/api/v1/probe", {{"device", "PHEC-3F2A91"}, {"kind", "ph"}, {"action", "cancel"}});
  s.world().fill(s.world().tank.volumeL - 18, 1.3, 5.9);
  REQUIRE(soon(s, [&] { return inletOpen(c); }, 15 * 60 * 1000, 5000));
  REQUIRE(soon(s, [&] { return !inletOpen(c); }, 20 * 60 * 1000, 5000));
  c.ok("POST", "/api/v1/probe", {{"device", "LVL-77B210"}, {"kind", "tank_curve"}, {"action", "start"}});
  s.fastForward(0.25, 5000);
  CHECK(lastEvent(c, "tank", "ev.refill.unjudged").is_null());  // not judged: kept for the person, as through a Stop
  CHECK_FALSE(inletLatched(c));
}

TEST_CASE("Scenario: refill steps go on across a clock step in the pause, and a hand dose in the pause ends them (#155, SD-082)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  demoAt(s, c, 2.3);
  REQUIRE(soon(s, [&] { return inletOpen(c); }, 15 * 60 * 1000, 5000));
  REQUIRE(soon(s, [&] { return !inletOpen(c); }, 20 * 60 * 1000, 5000));
  // The clock steps 2 h ahead in the pause: the check stays at the end of the pause, then step 2
  s.step(60000, 5000);
  s.control("time", {{"stepS", 2 * 3600}});
  s.step(60000, 5000);
  CHECK(refillLine(c)["key"] == "refill.check_step");
  CHECK_FALSE(inletOpen(c));
  REQUIRE(soon(s, [&] { return refillLine(c)["key"] == "refill.ok"; }, 60 * 60 * 1000, 5000));
  CHECK(s.world().tank.volumeL == doctest::Approx(45.0).epsilon(0.03));
  CHECK_FALSE(inletLatched(c));
  // A hand dose that ends within the pause: the next step would dilute it, so the steps end. With the
  // valve open at most 8 min, 14 L fit in one fill: from 26 L that is two steps to 45 L.
  c.ok("PATCH", "/api/v1/functions/refill", {{"params", {{"max_open_min", 8}}}});
  s.world().fill(26, 1.3, 5.9);
  REQUIRE(soon(s, [&] { return inletOpen(c); }, 15 * 60 * 1000, 5000));
  CHECK(refillLine(c)["args"]["steps"] == 2);
  REQUIRE(soon(s, [&] { return !inletOpen(c); }, 10 * 60 * 1000, 5000));
  s.step(60000, 5000);
  c.ok("POST", "/api/v1/dose", {{"canister", "calmag"}, {"ml", 5}});
  REQUIRE(until(s, [&] { return c.state()["job"].is_null(); }, 8 * 60 * 1000, 1000));
  CHECK(refillLine(c)["key"] == "refill.check");
  s.fastForward(0.5, 5000);
  CHECK(s.world().tank.volumeL == doctest::Approx(40.0).epsilon(0.03));
  CHECK_FALSE(inletLatched(c));
}

TEST_CASE("Scenario: a person's Stop, maintenance or switching the refill off keeps what flowed unconfirmed; a stuck level gets no second full fill (#155, SD-082)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  demoAt(s, c, 20);
  // Each fill to 45 L would add 25 L; the demo tank holds 60 L
  const auto stuckFill = [&] {
    freezeLevel(s);
    REQUIRE(soon(s, [&] { return inletOpen(c); }, 15 * 60 * 1000, 5000));
  };
  const auto pause = [&] {
    REQUIRE(soon(s, [&] { return !inletOpen(c); }, 15 * 60 * 1000, 5000));
    s.step(60000, 5000);
  };
  bool stuck = true;
  SUBCASE("a Stop during the fill, then Resume") {
    stuckFill();
    s.step(5 * 60 * 1000, 5000);
    c.ok("POST", "/api/v1/stop");
    s.step(60000, 5000);
    c.ok("POST", "/api/v1/resume");
  }
  SUBCASE("maintenance during the fill") {
    stuckFill();
    s.step(5 * 60 * 1000, 5000);
    c.ok("POST", "/api/v1/maintenance", {{"minutes", 5}});
  }
  SUBCASE("a level calibration started during the fill ends it") {
    stuckFill();
    s.step(5 * 60 * 1000, 5000);
    c.ok("POST", "/api/v1/probe", {{"device", "LVL-77B210"}, {"kind", "tank_curve"}, {"action", "start"}});
    s.step(10000, 5000);
    CHECK_FALSE(inletOpen(c));
    CHECK(refillLine(c)["key"] == "refill.calibrating");
    s.step(5 * 60 * 1000, 5000);
    c.ok("POST", "/api/v1/probe", {{"device", "LVL-77B210"}, {"kind", "tank_curve"}, {"action", "cancel"}});
  }
  SUBCASE("maintenance in the pause") {
    stuckFill();
    pause();
    c.ok("POST", "/api/v1/maintenance", {{"minutes", 5}});
  }
  SUBCASE("the refill switched off and on in the pause") {
    stuckFill();
    pause();
    c.ok("PATCH", "/api/v1/functions/refill", {{"enabled", false}});
    s.step(60000, 5000);
    c.ok("PATCH", "/api/v1/functions/refill", {{"enabled", true}});
  }
  SUBCASE("a level calibration during a fill with a sound sensor: its new curve shows the sensor follows, the refill fills again") {
    stuck = false;
    for (const char* f : {"ec_control", "ph_control"}) c.ok("PATCH", std::string("/api/v1/functions/") + f, {{"enabled", false}});
    REQUIRE(soon(s, [&] { return inletOpen(c); }, 15 * 60 * 1000, 5000));
    s.step(3 * 60 * 1000, 5000);
    const json lvl = {{"device", "LVL-77B210"}, {"kind", "tank_curve"}};
    const auto probe = [&](json body) {
      body.update(lvl);
      c.ok("POST", "/api/v1/probe", body);
    };
    probe({{"action", "start"}});
    s.step(10000, 5000);
    REQUIRE_FALSE(inletOpen(c));  // still below the start level: what flowed is kept
    s.step(3 * 60 * 1000, 5000);
    probe({{"action", "point"}, {"reference", s.world().tank.volumeL}});
    s.world().fill(15, 1.3, 5.9);  // the person drains for the second point, below the start level
    s.step(4 * 60 * 1000, 5000);
    probe({{"action", "point"}, {"reference", s.world().tank.volumeL}});
    probe({{"action", "commit"}});
  }
  SUBCASE("a level calibration committed, then the sensor sticks and a curve is imported before the next fill: only the committed curve counts") {
    for (const char* f : {"ec_control", "ph_control"}) c.ok("PATCH", std::string("/api/v1/functions/") + f, {{"enabled", false}});
    REQUIRE(soon(s, [&] { return inletOpen(c); }, 15 * 60 * 1000, 5000));
    s.step(3 * 60 * 1000, 5000);
    const json lvl = {{"device", "LVL-77B210"}, {"kind", "tank_curve"}};
    const auto probe = [&](json body) {
      body.update(lvl);
      c.ok("POST", "/api/v1/probe", body);
    };
    probe({{"action", "start"}});
    s.step(3 * 60 * 1000 + 10000, 5000);
    probe({{"action", "point"}, {"reference", s.world().tank.volumeL}});
    s.world().fill(15, 1.3, 5.9);
    s.step(4 * 60 * 1000, 5000);
    probe({{"action", "point"}, {"reference", s.world().tank.volumeL}});
    probe({{"action", "commit"}});
    freezeLevel(s);
    json cfg = c.ok("GET", "/api/v1/config/export");
    for (auto& p : cfg["calibrations"]["LVL-77B210"]["tank_curve"]["points"]) p[1] = p[1].get<double>() + 3.0;  // reads 3 L more
    c.ok("POST", "/api/v1/config/import", cfg);
    REQUIRE(soon(s, [&] { return inletLatched(c); }, 30 * 60 * 1000, 5000));
    CHECK(c.state()["latches"]["inlet.fault"]["why"]["args"]["why"]["key"] == "refill.unjudged.curve");
    CHECK(s.world().tank.volumeL < 15.5);  // no fill from the moved reading
  }
  SUBCASE("the refill switched off during the fill, then on") {
    stuckFill();
    s.step(5 * 60 * 1000, 5000);
    c.ok("PATCH", "/api/v1/functions/refill", {{"enabled", false}});
    s.step(60000, 5000);
    c.ok("PATCH", "/api/v1/functions/refill", {{"enabled", true}});
    s.step(60000, 5000);
    CHECK(refillLine(c)["key"] == "refill.cooldown");  // the pause, as after a Stop
  }
  SUBCASE("a Stop, and a level curve imported meanwhile: a new curve alone does not show the sensor follows") {
    stuckFill();
    s.step(5 * 60 * 1000, 5000);
    c.ok("POST", "/api/v1/stop");
    s.step(60000, 5000);
    json cfg = c.ok("GET", "/api/v1/config/export");
    for (auto& p : cfg["calibrations"]["LVL-77B210"]["tank_curve"]["points"]) p[1] = p[1].get<double>() + 1.5;  // reads 1.5 L more
    c.ok("POST", "/api/v1/config/import", cfg);
    s.step(60000, 5000);
    c.ok("POST", "/api/v1/resume");
  }
  SUBCASE("a level calibration cancelled, then a curve imported during maintenance: a cancelled session shows nothing of the sensor") {
    stuckFill();
    pause();
    c.ok("POST", "/api/v1/probe", {{"device", "LVL-77B210"}, {"kind", "tank_curve"}, {"action", "start"}});
    s.step(60000, 5000);
    c.ok("POST", "/api/v1/probe", {{"device", "LVL-77B210"}, {"kind", "tank_curve"}, {"action", "cancel"}});
    s.step(10000, 5000);
    c.ok("POST", "/api/v1/maintenance", {{"minutes", 5}});
    s.step(60000, 5000);
    json cfg = c.ok("GET", "/api/v1/config/export");
    for (auto& p : cfg["calibrations"]["LVL-77B210"]["tank_curve"]["points"]) p[1] = p[1].get<double>() + 3.0;  // reads 3 L more
    c.ok("POST", "/api/v1/config/import", cfg);
  }
  SUBCASE("a Stop during the fill, and another during the next, capped one: both are kept together") {
    stuckFill();
    s.step(5 * 60 * 1000, 5000);
    c.ok("POST", "/api/v1/stop");
    s.step(60000, 5000);
    c.ok("POST", "/api/v1/resume");
    REQUIRE(soon(s, [&] { return inletOpen(c); }, 20 * 60 * 1000, 5000));
    s.step(3 * 60 * 1000, 5000);
    c.ok("POST", "/api/v1/stop");
    s.step(60000, 5000);
    c.ok("POST", "/api/v1/resume");
  }
  SUBCASE("a water change during a Stop that began with the level head offline: its first reading counts") {
    stuck = false;
    REQUIRE(soon(s, [&] { return inletOpen(c); }, 15 * 60 * 1000, 5000));
    pause();
    s.control("fault", {{"device", "LVL-77B210"}, {"fault", "offline"}});
    s.step(2 * 60 * 1000, 5000);
    c.ok("POST", "/api/v1/stop");
    s.step(60000, 5000);
    s.control("fault", {{"device", "LVL-77B210"}, {"fault", "none"}});
    s.step(2 * 60 * 1000, 5000);
    s.world().fill(26, 1.3, 5.9);
    s.step(6 * 60 * 1000, 5000);
    s.world().fill(10, 1.3, 5.9);
    s.step(2 * 60 * 1000, 5000);
    c.ok("POST", "/api/v1/resume");
  }
  SUBCASE("a water change in maintenance: the level reading follows, so nothing is held against the next refill") {
    stuck = false;
    REQUIRE(soon(s, [&] { return inletOpen(c); }, 15 * 60 * 1000, 5000));
    pause();
    c.ok("POST", "/api/v1/maintenance", {{"minutes", 10}});
    s.step(5 * 60 * 1000, 5000);
    s.world().fill(26, 1.3, 5.9);  // drained (less than 20 L at a time: more would be a level jump)
    s.step(6 * 60 * 1000, 5000);
    s.world().fill(10, 1.3, 5.9);
  }
  SUBCASE("a level calibration in the pause: the refill waits for it, and a tank drained meanwhile is filled again") {
    stuck = false;
    for (const char* f : {"ec_control", "ph_control"}) c.ok("PATCH", std::string("/api/v1/functions/") + f, {{"enabled", false}});  // their waits would hide the line
    REQUIRE(soon(s, [&] { return inletOpen(c); }, 15 * 60 * 1000, 5000));
    pause();
    const json lvl = {{"device", "LVL-77B210"}, {"kind", "tank_curve"}};
    const auto probe = [&](json body) {
      body.update(lvl);
      c.ok("POST", "/api/v1/probe", body);
    };
    probe({{"action", "start"}});
    s.step(3 * 60 * 1000, 5000);
    probe({{"action", "point"}, {"reference", s.world().tank.volumeL}});
    s.world().fill(28, 1.3, 5.9);  // drained during the calibration
    s.step(15 * 60 * 1000, 5000);
    CHECK_FALSE(inletOpen(c));
    CHECK(refillLine(c)["key"] == "refill.calibrating");
    probe({{"action", "point"}, {"reference", s.world().tank.volumeL}});
    probe({{"action", "commit"}});  // a new curve, on which the reading moved: it follows the tank
    s.step(60000, 5000);
    CHECK(refillLine(c)["key"] == "refill.settle");  // then the reading settles first
    CHECK_FALSE(inletOpen(c));
  }
  double most = 0;
  for (int i = 0; i < 12 && !inletLatched(c); ++i) {
    s.step(10 * 60 * 1000, 5000);
    most = std::max(most, s.world().tank.volumeL);
  }
  if (stuck) {
    CHECK(inletLatched(c));
    CHECK(most < 45.0 + 0.3);
  } else {
    CHECK_FALSE(inletLatched(c));
    CHECK(s.world().tank.volumeL == doctest::Approx(45.0).epsilon(0.03));
  }
}

TEST_CASE("Scenario: the refill does not take over a valve opened by hand, and says it will lock (#155, SD-082)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  demoAt(s, c, 10);
  for (const char* f : {"ec_control", "ph_control"}) c.ok("PATCH", std::string("/api/v1/functions/") + f, {{"enabled", false}});  // their waits would hide the line
  c.ok("PATCH", "/api/v1/functions/refill", {{"enabled", false}});
  c.ok("POST", "/api/v1/roles/tank.inlet/switch", {{"on", true}});
  s.step(5 * 60 * 1000, 5000);
  c.ok("PATCH", "/api/v1/functions/refill", {{"enabled", true}});
  s.step(5000, 5000);
  const json line = refillLine(c);
  CHECK(line["key"] == "refill.open_by_hand_limit");
  CHECK(line["args"]["min"] == 15);
  // Closed by hand below the start level: the next fill waits until the reading has settled (RAT-038)
  c.ok("POST", "/api/v1/roles/tank.inlet/switch", {{"on", false}});
  s.step(60000, 5000);
  CHECK(refillLine(c)["key"] == "refill.settle");
  CHECK_FALSE(inletOpen(c));
  // So does a start: what happened before it is not known
  s.reboot(0, true, true);
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  s.step(2 * 60 * 1000, 5000);
  CHECK(refillLine(c)["key"] == "refill.settle");
  CHECK_FALSE(inletOpen(c));
  REQUIRE(soon(s, [&] { return inletOpen(c); }, 3 * 60 * 1000, 5000));
}

TEST_CASE("Scenario: the inlet opened by hand still closes and latches after max_open_min (RAT-031, RAT-032, #155)") {
  // The refill now plans within the limit; the limit itself stays for any other way the valve opens
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  demoAt(s, c, 10);
  c.ok("PATCH", "/api/v1/functions/refill", {{"enabled", false}});
  c.ok("POST", "/api/v1/roles/tank.inlet/switch", {{"on", true}});
  s.step(14 * 60 * 1000, 5000);
  CHECK(c.state()["outputs"]["tank.inlet"] == true);
  s.step(2 * 60 * 1000, 5000);
  const json st = c.state();
  CHECK(st["outputs"]["tank.inlet"] == false);
  REQUIRE(st["latches"].contains("inlet.fault"));
  CHECK(st["latches"]["inlet.fault"]["why"]["key"] == "why.open_too_long");
  CHECK(st["latches"]["inlet.fault"]["why"]["args"]["min"] == 15);
}

TEST_CASE("Scenario: someone guessing the password cannot keep the owner from stopping a running mix (SD-051)") {
  sim::Simulation s(test::opts("neu"));
  Client phone{s, {}, "192.168.1.20"};  // set up from the owner's phone
  setupStage0(s, phone);
  for (const char* id : {kA, kB, kC}) calibrate(s, phone, id);
  s.world().fill(20, 0.02, 7.0);
  phone.ok("POST", "/api/v1/mix/start", {{"recipe", "wachstum"}, {"waterL", 20}, {"guided", false}});
  REQUIRE(until(s, [&] { return s.world().cap(kA)->state == 1; }, 10000, 200));
  phone.ok("POST", "/api/v1/auth/logout");  // its session is gone
  Client guesser{s, {}, "192.168.1.66"};
  for (int i = 0; i < 6; ++i) guesser.call("POST", "/api/v1/auth/login", {{"password", "falsch"}});
  CHECK(guesser.call("POST", "/api/v1/auth/login", {{"password", "mein-passwort"}}).first == 429);
  phone.ok("POST", "/api/v1/auth/login", {{"password", "mein-passwort"}});
  phone.ok("POST", "/api/v1/stop");
  s.step(2000);
  for (const char* id : {kA, kB, kC}) CHECK(s.world().cap(id)->state != 1);
  CHECK(phone.state()["stopped"] == true);
}

TEST_CASE("Scenario: a Stop switches everything off and holds automation until Resume") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  s.step(30000);
  c.ok("POST", "/api/v1/stop");
  s.step(5000);
  auto st = c.state();
  CHECK(st["stopped"] == true);
  CHECK(st["outputs"]["tank.circulation"] == false);
  CHECK(st["job"].is_null());
  auto [code, refused] = c.call("POST", "/api/v1/dose", {{"canister", "teil-a"}, {"ml", 2}});
  CHECK(code != 200);
  CHECK(refused["error"]["key"] == "job.start_failed");
  CHECK(refused["error"]["args"]["reason"]["key"] == "act.stopped");
  // a mix is refused too, with a message in the page's language (SD-045: "Stop", never "emergency stop")
  auto [mixCode, mix] = c.call("POST", "/api/v1/mix/start", {{"recipe", "wachstum"}, {"waterL", 20}});
  CHECK(mixCode == 409);
  CHECK(mix["error"]["key"] == "act.stopped");
  CHECK(mix["error"]["text"] == "Stop active – tap Resume first");
  c.ok("POST", "/api/v1/resume");
  CHECK(c.state()["stopped"] == false);
}

TEST_CASE("Scenario: a Stop switches everything off before it books the running dose, and holds if booking fails (RAT-036)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  circulateAlways(s, c);
  auto handDose = [&] {
    REQUIRE(until(s, [&] {
      const json now = c.state();
      return now["job"].is_null() && now["dosing"].is_null();
    }, 10 * 60 * 1000));
    c.ok("POST", "/api/v1/dose", {{"canister", "teil-a"}, {"ml", 5}});
    REQUIRE(until(s, [&] { return s.world().cap(kA)->state == 1 && s.world().cap(kA)->elapsed > 2000; }, 20000, 200));
    REQUIRE(s.world().circulating());
  };

  auto count = [&](const std::string& type, const std::string& key) {
    size_t n = 0;
    for (const auto& e : findEvents(c, type)) n += e["title"]["key"] == key ? 1 : 0;
    return n;
  };

  // when the hub reads back the stopped run to book it, everything is already off
  handDose();
  REQUIRE(s.world().loadOn("light"));
  bool readBack = false, allOff = false, saved = false;
  s.world().onRunStatus = [&](const std::string&) {
    if (readBack) return;
    readBack = true;
    sim::World& w = s.world();
    allOff = !w.circulating() && !w.out[1] && !w.loadOn("light") && !w.loadOn("exhaust") && !w.loadOn("circulation_fan") &&
             w.cap(kA)->state != 1;
    saved = json::parse(s.storage().read("state.json").value_or("{}")).value("stopped", false);
  };
  c.ok("POST", "/api/v1/stop");
  s.world().onRunStatus = nullptr;
  REQUIRE(readBack);
  CHECK(allOff);
  CHECK(saved);
  c.ok("POST", "/api/v1/resume");
  REQUIRE(until(s, [&] { return c.state()["outputs"]["tank.circulation"] == true; }, 60000));

  // booking fails: the Stop still holds and is saved, the dose is dropped unbooked, and that is said
  handDose();
  const double stockBefore = c.state()["stock"]["teil-a"];  // a single run, booked only when it ends
  const size_t stops = count("system", "ev.stop");
  const size_t incomplete = count("dose", "ev.dose_incomplete");
  const json dropped = c.state()["dosing"]["order"];
  s.world().onRunStatus = [](const std::string&) { throw std::runtime_error("bus read failed"); };
  CHECK(c.call("POST", "/api/v1/stop").first == 200);
  json st = c.state();
  CHECK(st["stopped"] == true);
  CHECK(st["outputs"]["tank.circulation"] == false);
  CHECK_FALSE(s.world().circulating());
  CHECK(s.world().cap(kA)->state != 1);
  CHECK(st["job"].is_null());
  CHECK(st["dosing"].is_null());
  CHECK(json::parse(s.storage().read("state.json").value_or("{}")).value("stopped", false) == true);
  CHECK(count("system", "ev.stop") == stops + 1);
  bool said = false;
  for (const auto& e : findEvents(c, "system"))
    if (e["title"]["key"] == "ev.stop.unbooked") {
      said = true;
      CHECK(e["severity"] == "warn");
      CHECK(e["text"]["args"]["name"] == "Teil A");
      CHECK(e["text"]["args"]["reason"] == "bus read failed");
      CHECK(e["data"]["canister"] == "teil-a");
    }
  CHECK(said);
  // the read keeps failing: the dropped dose is not read again, so no internal error follows,
  // nothing switches on, and nothing of it is booked later
  s.step(5000);
  CHECK(count("system", "ev.tick_fault") == 0);
  s.world().onRunStatus = nullptr;
  s.step(5000);
  CHECK_FALSE(s.world().circulating());
  CHECK(c.state()["stock"]["teil-a"].get<double>() == doctest::Approx(stockBefore));
  CHECK(count("dose", "ev.dose_incomplete") == incomplete);
  // pressing Stop again logs no second stop
  CHECK(c.call("POST", "/api/v1/stop").first == 200);
  CHECK(count("system", "ev.stop") == stops + 1);
  // a Resume right away continues nothing of the dropped dose, and a new hand dose runs
  c.ok("POST", "/api/v1/resume");
  for (int i = 0; i < 30; ++i) {
    s.step(1000);
    const json d = c.state()["dosing"];
    CHECK((d.is_null() || d["order"] != dropped));
  }
  handDose();
}

TEST_CASE("Scenario: a control round a Stop cut settles with the circulation pump running before the next decision, as after a restart (RAT-036, RAT-052, SD-073)") {
  // While stopped nothing circulates, so time spent stopped mixes nothing in: after Resume the cut
  // round first settles, counted from when circulation runs, for its settle time but at least 15 min
  auto purpose = [](Client& c) {
    const json d = c.state()["dosing"];
    return d.is_null() ? std::string() : d["purpose"].get<std::string>();
  };
  {  // EC cut while its dose runs, a long Stop, a settle time above 15 min; pH waits meanwhile
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
    c.ok("PATCH", "/api/v1/functions/ec_control", {{"params", {{"settle_min", 20}}}});
    c.ok("PATCH", "/api/v1/functions/circulation", {{"enabled", false}});  // only a round asks for the pump
    REQUIRE(until(s, [&] { return c.state()["job"].is_null(); }, 60 * 60 * 1000, 5000));  // the demo's mix
    s.world().fill(40, 1.0, 6.3);  // EC low and pH high: both want to dose
    REQUIRE(until(s, [&] {
      return s.world().cap(kA)->state == 1 && s.world().cap(kA)->jobId.rfind("ec-", 0) == 0 && s.world().cap(kA)->elapsed > 1000;
    }, 60 * 60 * 1000, 200));
    const double stockBefore = c.state()["stock"]["teil-a"];
    c.ok("POST", "/api/v1/stop");
    CHECK(c.state()["stock"]["teil-a"].get<double>() < stockBefore);  // the cut part is booked (RAT-070)
    s.step(20 * 60 * 1000);  // a long Stop
    c.ok("POST", "/api/v1/resume");
    REQUIRE(until(s, [&] { return s.world().circulating(); }, 30000));  // asked for by the cut round
    s.step(5000);
    REQUIRE(c.state()["controllers"]["ec"]["line"]["key"] == "ec.settling");
    c.ok("PATCH", "/api/v1/functions/refill", {{"enabled", true}, {"params", {{"start_below_l", 45}, {"target_l", 48}}}});
    bool dosed = false, circOff = false, refilled = false;
    for (int t = 0; t < 19 * 60; t += 5) {  // 19 of the 20 min after circulation starts
      s.step(5000);
      dosed = dosed || !purpose(c).empty();
      circOff = circOff || !s.world().circulating();
      refilled = refilled || c.state()["outputs"]["tank.inlet"] == true;
    }
    CHECK_FALSE(dosed);  // neither EC nor pH
    CHECK_FALSE(circOff);
    CHECK_FALSE(refilled);  // refill waits while the cut round settles (RAT-055)
    CHECK(c.state()["controllers"]["ec"]["line"]["key"] == "ec.settling");
    CHECK(until(s, [&] { return purpose(c) == "ec"; }, 4 * 60 * 1000));  // then it decides again
  }
  {  // pH cut while its dose settles: 15 min with circulation before the next pH dose
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
    c.ok("PATCH", "/api/v1/functions/ec_control", {{"enabled", false}});
    REQUIRE(until(s, [&] { return c.state()["job"].is_null(); }, 60 * 60 * 1000, 5000));  // the demo's mix
    s.world().fill(40, 1.2, 7.0);
    const char* kD = "CAP-1F02D9";
    REQUIRE(until(s, [&] { return s.world().cap(kD)->state == 2 && s.world().cap(kD)->jobId.rfind("ph-", 0) == 0; },
                  60 * 60 * 1000, 1000));
    s.step(2000);
    REQUIRE(c.state()["controllers"]["ph"]["line"]["key"] == "ph.settling");
    c.ok("POST", "/api/v1/stop");
    s.step(5000);
    c.ok("POST", "/api/v1/resume");
    REQUIRE(until(s, [&] { return s.world().circulating(); }, 30000));
    bool dosed = false;
    for (int t = 0; t < 14 * 60; t += 5) {
      s.step(5000);
      dosed = dosed || purpose(c) == "ph";
    }
    CHECK_FALSE(dosed);
    CHECK(c.state()["controllers"]["ph"]["line"]["key"] == "ph.settling");
    CHECK(until(s, [&] { return purpose(c) == "ph"; }, 4 * 60 * 1000));
  }
  {  // the circulation pump cannot run at Resume: refill is not held, the line says why, nothing doses;
     // the settle time counts from when the pump runs
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    const std::string plug = circulateOnPlug(s, c);  // also switches refill off
    REQUIRE(until(s, [&] { return c.state()["job"].is_null(); }, 60 * 60 * 1000, 5000));  // the demo's mix
    s.world().fill(40, 1.0, 5.9);
    REQUIRE(until(s, [&] {
      return s.world().cap(kA)->state == 1 && s.world().cap(kA)->jobId.rfind("ec-", 0) == 0 && s.world().cap(kA)->elapsed > 1000;
    }, 60 * 60 * 1000, 200));
    c.ok("POST", "/api/v1/stop");
    {
      std::lock_guard<std::recursive_mutex> l(s.mutex());
      s.control("fault", {{"device", plug}, {"fault", "offline"}});
    }
    s.step(5000);
    c.ok("POST", "/api/v1/resume");
    s.step(10000);  // the cut round now waits for the pump
    REQUIRE(c.state()["controllers"]["ec"]["line"]["key"] == "ec.wait_circ");
    c.ok("PATCH", "/api/v1/functions/refill", {{"enabled", true}, {"params", {{"start_below_l", 45}, {"target_l", 48}}}});
    s.step(5000);
    // not held by the cut round, only by the reading's settling time after the Stop (SD-082)
    CHECK(c.state()["controllers"]["refill"]["line"]["key"] == "refill.settle");
    CHECK(until(s, [&] { return c.state()["outputs"]["tank.inlet"] == true; }, 4 * 60 * 1000));
    bool dosed = false;
    for (int t = 0; t < 4 * 60; t += 5) {
      s.step(5000);
      dosed = dosed || !purpose(c).empty();
    }
    CHECK_FALSE(dosed);
    CHECK(c.state()["controllers"]["ec"]["state"] == "blocked");
    CHECK(c.state()["controllers"]["ec"]["line"]["key"] == "ec.cut_no_circ");
    {
      std::lock_guard<std::recursive_mutex> l(s.mutex());
      s.control("fault", {{"device", plug}, {"fault", "none"}});
    }
    REQUIRE(until(s, [&] { return s.world().circulating(); }, 2 * 60 * 1000));
    for (int t = 0; t < 14 * 60; t += 5) {
      s.step(5000);
      dosed = dosed || !purpose(c).empty();
    }
    CHECK_FALSE(dosed);
    CHECK(c.state()["controllers"]["ec"]["line"]["key"] == "ec.settling");
    CHECK(until(s, [&] { return purpose(c) == "ec"; }, 5 * 60 * 1000));
  }
  {  // EC waits while a pH round the Stop cut waits for the pump and settles (RAT-057)
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
    REQUIRE(until(s, [&] { return c.state()["job"].is_null(); }, 60 * 60 * 1000, 5000));  // the demo's mix
    const double target = c.state()["controllers"]["ec"]["info"]["target"];
    s.world().fill(40, target, 7.0);  // EC on target, pH high: a pH round
    const char* kD = "CAP-1F02D9";
    REQUIRE(until(s, [&] { return s.world().cap(kD)->state == 2 && s.world().cap(kD)->jobId.rfind("ph-", 0) == 0; },
                  60 * 60 * 1000, 1000));
    s.step(2000);
    REQUIRE(c.state()["controllers"]["ph"]["line"]["key"] == "ph.settling");
    c.ok("POST", "/api/v1/stop");
    s.world().fill(40, target - 0.45, 6.8);  // now EC wants a round; both steps stay below the jump thresholds
    s.step(5000);
    c.ok("POST", "/api/v1/resume");
    bool ecDosed = false, waited = false;
    for (int t = 0; t < 14 * 60; t += 5) {
      s.step(5000);
      ecDosed = ecDosed || purpose(c) == "ec";
      waited = waited || c.state()["controllers"]["ec"]["line"]["key"] == "ec.wait_ph";
    }
    CHECK_FALSE(ecDosed);
    CHECK(waited);
  }
  {  // the hub refuses the pump at Resume (level too low): "blocked" at once, refill is not held
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
    c.ok("PATCH", "/api/v1/functions/circulation", {{"enabled", false}});  // only a round asks for the pump
    c.ok("PATCH", "/api/v1/functions/refill", {{"enabled", false}});
    REQUIRE(until(s, [&] { return c.state()["job"].is_null(); }, 60 * 60 * 1000, 5000));  // the demo's mix
    s.world().fill(40, 1.0, 6.3);
    REQUIRE(until(s, [&] {
      return s.world().cap(kA)->state == 1 && s.world().cap(kA)->jobId.rfind("ec-", 0) == 0 && s.world().cap(kA)->elapsed > 1000;
    }, 60 * 60 * 1000, 200));
    c.ok("POST", "/api/v1/stop");
    s.world().fill(3.2, 1.0, 6.3);  // below the pump's minimum (3 L + 0.5 L)
    s.step(20 * 60 * 1000);         // long enough for the level's jump lock to end
    c.ok("POST", "/api/v1/resume");
    s.step(10000);
    json ec = c.state()["controllers"]["ec"];
    CHECK(ec["state"] == "blocked");
    CHECK(ec["line"]["key"] == "ec.cut_circ_blocked");
    CHECK(c.state()["controllers"]["circulation"]["line"]["args"]["reason"]["key"] == "act.circ.low");  // why
    bool dosed = false;
    for (int t = 0; t < 3 * 60; t += 5) {  // past the 2 min a pump that does not come on gets
      s.step(5000);
      dosed = dosed || !purpose(c).empty();
    }
    CHECK_FALSE(dosed);
    CHECK(c.state()["controllers"]["ec"]["line"]["key"] == "ec.cut_circ_blocked");
    CHECK_FALSE(s.world().circulating());
    c.ok("PATCH", "/api/v1/functions/refill", {{"enabled", true}, {"params", {{"start_below_l", 45}, {"target_l", 48}}}});
    CHECK(until(s, [&] { return c.state()["outputs"]["tank.inlet"] == true; }, 60000));  // not held by the cut round
    CHECK(until(s, [&] { return s.world().circulating(); }, 2 * 60 * 1000));            // the level is back
    CHECK(until(s, [&] { return c.state()["controllers"]["ec"]["line"]["key"] == "ec.settling"; }, 30000));
  }
}

TEST_CASE("Szenario: Abbruch bucht, was schon gelaufen ist (RAT-070)") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  setupStage0(s, c);
  for (const char* id : {kA, kB, kC}) calibrate(s, c, id);
  s.world().fill(20, 0.02, 7.0);
  double before = c.state()["stock"]["teil-a"];
  std::string id = c.ok("POST", "/api/v1/mix/start", {{"recipe", "wachstum"}, {"waterL", 20}, {"guided", false}})["job"]["id"];
  REQUIRE(until(s, [&] { return s.world().cap(kA)->state == 1 && s.world().cap(kA)->elapsed > 10000; }, 30000, 200));
  c.ok("POST", "/api/v1/jobs/" + id + "/abort");
  CHECK(s.world().cap(kA)->state != 1);
  auto st = c.state();
  double after = st["stock"]["teil-a"];
  CHECK(after < before - 5);  // > 10 s bei 38–53 ml/min
  json m = st["lastJob"]["message"];
  CHECK(m["key"] == "job.aborted");
  CHECK(m["args"]["done"][0]["args"]["name"] == "Teil A");
  CHECK(findEvents(c, "mix").at(0)["title"]["key"] == "ev.mix.aborted");
  bool logged = false;
  for (const auto& e : findEvents(c, "dose"))
    if (e["data"]["canister"] == "teil-a" && e["data"]["ml"].get<double>() > 5) {
      logged = true;
      CHECK(e["title"]["key"] == "ev.dose_incomplete");  // cut short by the abort
      CHECK(e["text"]["key"] == "purpose.mix");
    }
  CHECK(logged);
}

TEST_CASE("Scenario: a stock booked below zero is kept and reported, never set to 0 (RAT-015, RAT-071)") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  setupStage0(s, c);
  for (const char* id : {kA, kB, kC}) calibrate(s, c, id);
  s.world().fill(20, 0.02, 7.0);
  const auto dose = [&](double ml) {
    c.ok("POST", "/api/v1/dose", {{"canister", "teil-a"}, {"ml", ml}});
    REQUIRE(until(s, [&] { return c.state()["dosing"].is_null() && c.state()["job"].is_null(); }, 60000, 500));
  };
  const auto reports = [&](std::uint64_t since) {
    int n = 0;
    for (const auto& e : findEvents(c, "tank"))
      if (e["id"].get<std::uint64_t>() > since && e["title"]["key"] == "ev.stock.below_zero") n++;
    return n;
  };
  const auto stockItem = [&] {
    s.step(60000);  // the watchdog evaluates again
    const json wd = c.state()["watchdog"];
    for (const auto& i : wd["items"])
      if (i["id"] == "stock.teil-a") return i;
    return json();
  };
  // exactly what is left goes out, or a little more: a rounding difference under 0.5 ml raises no alarm
  c.ok("POST", "/api/v1/canisters/teil-a/stock", {{"ml", 5}});
  auto before = c.state()["eventId"].get<std::uint64_t>();
  dose(5);
  CHECK(reports(before) == 0);
  CHECK(stockItem()["text"]["key"] == "watch.stock.low");
  c.ok("POST", "/api/v1/canisters/teil-a/stock", {{"ml", 4.7}});
  dose(5);
  CHECK(c.state()["stock"]["teil-a"].get<double>() == doctest::Approx(-0.3).epsilon(0.05));
  CHECK(reports(before) == 0);
  CHECK(stockItem()["text"]["key"] == "watch.stock.low");

  // the stock entered is too low: 3 ml left, but a 5 ml hand dose goes in
  c.ok("POST", "/api/v1/canisters/teil-a/stock", {{"ml", 3}});
  before = c.state()["eventId"].get<std::uint64_t>();
  dose(5);
  REQUIRE(c.state()["stock"]["teil-a"].is_number());
  CHECK(c.state()["stock"]["teil-a"].get<double>() == doctest::Approx(-2.0).epsilon(0.05));  // what the booking says, not 0
  CHECK(reports(before) == 1);
  for (const auto& e : findEvents(c, "tank"))
    if (e["id"].get<std::uint64_t>() > before && e["title"]["key"] == "ev.stock.below_zero") {
      CHECK(e["severity"] == "warn");
      CHECK(e["text"]["key"] == "ev.stock.below_zero.text");
      CHECK(e["text"]["args"]["name"] == "Teil A");
      CHECK(e["data"]["canister"] == "teil-a");
    }
  json item = stockItem();
  CHECK(item["status"] == "problem");
  CHECK(item["text"]["key"] == "watch.stock.negative");
  // a mix sees the stock used up and says so
  const auto refused = c.call("POST", "/api/v1/mix/start", {{"recipe", "wachstum"}, {"waterL", 20}});
  CHECK(refused.first == 422);
  CHECK(refused.second["error"]["key"] == "mix.stock_negative");
  CHECK(refused.second["error"]["args"]["name"] == "Teil A");  // the message keeps its values

  // a second dose from the negative stock is not reported again, also not after a restart
  dose(3);
  CHECK(c.state()["stock"]["teil-a"].get<double>() == doctest::Approx(-5.0).epsilon(0.05));
  // renaming the bottle sends the stock back unchanged, or leaves it empty: both keep it
  const json neg = c.state()["stock"]["teil-a"];
  c.ok("POST", "/api/v1/canisters", {{"id", "teil-a"}, {"name", "Teil A"}, {"stockMl", neg}});
  c.ok("POST", "/api/v1/canisters", {{"id", "teil-a"}, {"name", "Teil A"}, {"stockMl", nullptr}});
  CHECK(c.state()["stock"]["teil-a"] == neg);
  CHECK(c.call("POST", "/api/v1/canisters", {{"id", "teil-a"}, {"name", "Teil A"}, {"stockMl", neg.get<double>() + 1}}).first == 422);
  CHECK(c.state()["stock"]["teil-a"] == neg);
  s.reboot();
  c.ok("POST", "/api/v1/auth/login", {{"password", "mein-passwort"}});
  s.step(5000);  // the devices are found again
  CHECK(c.state()["stock"]["teil-a"].get<double>() == doctest::Approx(-5.0).epsilon(0.05));
  CHECK(stockItem()["text"]["key"] == "watch.stock.negative");
  dose(3);
  CHECK(reports(before) == 1);

  // a new bottle clears it; falling below zero again is reported again
  c.ok("POST", "/api/v1/canisters/teil-a/stock", {{"ml", 1000}});
  CHECK(c.state()["stock"]["teil-a"].get<double>() == doctest::Approx(1000.0));
  CHECK(stockItem().is_null());
  c.ok("POST", "/api/v1/canisters/teil-a/stock", {{"ml", 1}});
  dose(3);
  CHECK(reports(before) == 2);

  // falling below zero during a Stop: the pumps go off first, then the run is booked and reported
  c.ok("POST", "/api/v1/canisters/teil-a/stock", {{"ml", 2}});
  c.ok("POST", "/api/v1/dose", {{"canister", "teil-a"}, {"ml", 5}});  // the hand-dose cap
  REQUIRE(until(s, [&] { return s.world().cap(kA)->state == 1 && s.world().cap(kA)->elapsed > 4000; }, 20000, 200));
  c.ok("POST", "/api/v1/stop");
  CHECK(s.world().cap(kA)->state != 1);
  CHECK(c.state()["stopped"] == true);
  CHECK(c.state()["stock"]["teil-a"].get<double>() < -0.5);  // more than 2.8 ml ran in 4 s
  CHECK(reports(before) == 3);
  c.ok("POST", "/api/v1/resume");
}

TEST_CASE("Szenario: Kappe im Lauf abgezogen → Auftrag scheitert sofort, nichts hängt") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  setupStage0(s, c);
  for (const char* id : {kA, kB, kC}) calibrate(s, c, id);
  s.world().fill(20, 0.02, 7.0);
  c.ok("POST", "/api/v1/dose", {{"canister", "teil-a"}, {"ml", 4}});
  REQUIRE(until(s, [&] { return s.world().cap(kA)->state == 1 && s.world().cap(kA)->elapsed > 1000; }, 10000, 200));
  int slot = -1;
  REQUIRE(s.world().cap(kA, nullptr, &slot));
  REQUIRE(s.world().unplugCap(kDB, slot));
  s.step(2000);
  auto st = c.state();
  CHECK(st["dosing"].is_null());
  CHECK(st["lastJob"]["state"] == "failed");
  CHECK(st["lastJob"]["steps"][0]["mlDone"].get<double>() > 0);  // aus der Laufzeit geschätzt
  CHECK(c.call("POST", "/api/v1/dose", {{"canister", "calmag"}, {"ml", 2}}).first == 200);
}

TEST_CASE("Szenario: Dosierblock antwortet im Lauf nicht → Frist, Pumpe aus, als gelaufen gezählt") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  setupStage0(s, c);
  for (const char* id : {kA, kB, kC}) calibrate(s, c, id);
  s.world().fill(20, 0.02, 7.0);
  double before = c.state()["stock"]["teil-a"];
  std::string id = c.ok("POST", "/api/v1/dose", {{"canister", "teil-a"}, {"ml", 4}})["job"]["id"];
  REQUIRE(until(s, [&] { return s.world().cap(kA)->state == 1; }, 10000, 200));
  s.control("fault", {{"device", kDB}, {"fault", "offline"}});
  REQUIRE(until(s, [&] { return c.state()["lastJob"]["state"] == "failed"; }, 30000, 500));
  CHECK(c.state()["dosing"].is_null());
  CHECK(s.world().cap(kA)->state != 1);
  auto st = c.state();
  CHECK(st["lastJob"]["state"] == "failed");
  CHECK(st["lastJob"]["message"]["key"] == "job.dose_failed");
  CHECK(st["lastJob"]["message"]["args"]["reason"]["key"] == "dose.no_response");
  CHECK(st["stock"]["teil-a"].get<double>() <= before - 3.9);  // sichere Richtung: als gelaufen gebucht
  // Nachholen: kein Rest mehr → als erledigt werten; der Auftrag endet dabei
  s.control("fault", {{"device", kDB}, {"fault", "none"}});
  s.step(3000);
  auto r = c.ok("POST", "/api/v1/jobs/" + id + "/resume");
  CHECK(r["job"].is_null());
  CHECK(r["lastJob"]["state"] == "done");
  CHECK(c.state()["dosing"].is_null());
}

TEST_CASE("Scenario: a page left open still stops more than 12 hours after signing in; one unused for 12 hours cannot; none after a restart (SD-064)") {
  sim::Simulation s(test::opts("demo"));
  Client wall{s, "", "192.168.1.20"};
  Client idle{s, "", "192.168.1.21"};
  wall.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  idle.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  for (int i = 0; i < 13 * 6; ++i) {  // 13 hours; the page asks every 10 minutes
    s.fastForward(1.0 / 6);
    REQUIRE(wall.ok("GET", "/api/v1/auth/session")["authenticated"] == true);
  }
  CHECK(idle.call("POST", "/api/v1/stop").first == 401);
  CHECK(wall.state()["stopped"] == false);
  wall.ok("POST", "/api/v1/stop");
  CHECK(wall.state()["stopped"] == true);
  wall.ok("POST", "/api/v1/resume");
  // Sessions live in memory only: after a restart, sign in again
  s.reboot();
  CHECK(wall.ok("GET", "/api/v1/auth/session")["authenticated"] == false);
  CHECK(wall.call("POST", "/api/v1/stop").first == 401);
}

TEST_CASE("Szenario: Bus-Job-IDs der Regler sind nach einem Neustart neu (Vorschlag firmware)") {
  // Ohne Kennung je Start hieße die erste EC-Gabe nach jedem Neustart gleich
  // („ec-1#1“). Der Dosierblock hielte sie für eine Wiederholung und liefe nicht.
  sim::Simulation s(test::opts("demo"));
  auto job = [&] { return s.world().cap(kA)->jobId; };
  auto ecRound = [&](const std::string& before) {
    s.reboot();
    Client c{s};
    c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
    s.world().fill(40, 1.0, 5.9);
    REQUIRE(until(s, [&] { return job() != before && job().rfind("ec-", 0) == 0 && s.world().cap(kA)->state == 2; },
                  60 * 60 * 1000, 5000));
    CHECK(job().rfind("ec-1#", 0) == 0);  // erste EC-Gabe nach dem Start
    CHECK(s.world().cap(kA)->elapsed > 0);
    return job();
  };
  const std::string first = ecRound(job());
  const std::string second = ecRound(first);
  CHECK(first != second);
}

TEST_CASE("Ablage: nicht beschreibbarer Datenordner bricht den Simulator nicht ab") {
  // Ordner unter einer Datei lässt sich nie anlegen (auch nicht als root)
  auto base = std::filesystem::temp_directory_path() / "gc-ablage-test";
  std::filesystem::create_directories(base);
  auto blocker = base / "datei";
  { std::ofstream(blocker) << "x"; }
  sim::FileStorage st((blocker / "daten").string());
  CHECK_NOTHROW(st.write("config.json", "{}"));
  CHECK(st.read("config.json") == std::optional<std::string>("{}"));  // im Speicher weiter
  CHECK_NOTHROW(st.write("config.json", "{\"a\":1}"));
  CHECK_NOTHROW(st.append("history.log", "x"));
  CHECK(st.read("config.json") == std::optional<std::string>("{\"a\":1}"));
  std::filesystem::remove_all(base);
}

TEST_CASE("Sonden: pH und EC als getrennte Köpfe, Wassertemperatur vom EC-Kopf") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    s.control("plug", {{"port", 3}, {"class", "head_ph"}});
    s.control("plug", {{"port", 4}, {"class", "head_ec"}});
  }
  std::string ph, ec;
  REQUIRE(until(s, [&] {
    auto st = c.state();
    for (const auto& d : st["devices"]) {
      if (d["class"] == "head_ph" && d["online"] == true) ph = d["id"];
      if (d["class"] == "head_ec" && d["online"] == true) ec = d["id"];
    }
    return !ph.empty() && !ec.empty();
  }, 30000));
  c.ok("POST", "/api/v1/devices/" + ph + "/accept", {{"name", ""}});
  c.ok("POST", "/api/v1/devices/" + ec + "/accept", {{"name", ""}});
  auto roles = c.ok("GET", "/api/v1/config")["tanks"][0]["roles"];
  CHECK(roles["tank.ph"]["device"] == ph);
  CHECK(roles["tank.ec"]["device"] == ec);
  CHECK(roles["tank.water_temp"]["device"] == ec);
  // Beide liefern Werte
  REQUIRE(until(s, [&] {
    auto r = c.state()["readings"];
    return r["tank.ph"]["value"].is_number() && r["tank.ec"]["value"].is_number();
  }, 60000));
  // A probe calibration names its kind as a message, not as an ID (SD-032)
  s.world().fill(20, 1.2, 6.0);
  s.step(30000);
  REQUIRE(c.state()["readings"]["tank.ec"]["value"].is_number());
  const double now = c.state()["readings"]["tank.ec"]["value"];
  // Only the kinds the device class offers: a pH calibration on the EC head is refused
  auto [notOffered, why] = c.call("POST", "/api/v1/probe", {{"device", ec}, {"kind", "ph"}, {"action", "start"}});
  CHECK(notOffered == 422);
  CHECK(why["error"]["key"] == "probe.not_offered");
  CHECK(c.state()["probeCalibration"].empty());
  c.ok("POST", "/api/v1/probe", {{"device", ec}, {"kind", "ec"}, {"action", "start"}});
  c.ok("POST", "/api/v1/probe", {{"device", ec}, {"kind", "ec"}, {"action", "point"}, {"reference", now}});
  c.ok("POST", "/api/v1/probe", {{"device", ec}, {"kind", "ec"}, {"action", "commit"}});
  const json cal = findEvents(c, "calibration").at(0);
  CHECK(cal["title"]["key"] == "ev.probe_calibrated");
  CHECK(cal["text"]["args"]["kind"]["key"] == "kind.ec");
  // Fehler am pH-Kopf trifft nur pH
  {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    s.control("fault", {{"device", ph}, {"fault", "offline"}});
  }
  REQUIRE(until(s, [&] { return !c.state()["readings"]["tank.ph"]["value"].is_number(); }, 180000));
  CHECK(c.state()["readings"]["tank.ec"]["value"].is_number());
}

TEST_CASE("Zuordnung: gelöste Messrolle bleibt gelöst, wenn ein anderes Gerät übernommen wird") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  c.ok("DELETE", "/api/v1/roles/tank.ph");  // z. B. Sonde defekt
  {
    // Freier Anschluss: In der Demo steckt an 6 schon der Klima-Kopf.
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    auto r = s.control("plug", {{"port", 7}, {"class", "head_co2"}});
    REQUIRE_FALSE(r.contains("error"));
  }
  std::string co2;
  REQUIRE(until(s, [&] {
    auto st = c.state();
    for (const auto& d : st["devices"])
      if (d["class"] == "head_co2" && d["online"] == true && d["configured"] == false) co2 = d["id"];
    return !co2.empty();
  }, 30000));
  c.ok("POST", "/api/v1/devices/" + co2 + "/accept", {{"name", ""}});
  auto cfg = c.ok("GET", "/api/v1/config");
  CHECK_FALSE(cfg["tanks"][0]["roles"].contains("tank.ph"));
  CHECK(cfg["zones"][0]["roles"]["zone.co2"]["device"] == co2);
}

TEST_CASE("Simulator: „Wert friert“ hält den letzten Messwert fest") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    s.control("fault", {{"device", "PHEC-3F2A91"}, {"fault", "frozen"}});
  }
  s.step(10000);
  auto r = c.state()["readings"]["tank.ph"];
  CHECK(r["value"].is_number());
}

TEST_CASE("Messages: pump calibration and the Stop name their result and cause (SD-032)") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  s.world().cap(kB)->storedFlow = 0;  // a chip that reports 0: no previous rate, not "clearly different" (R5)
  setupStage0(s, c);
  json r = calibrate(s, c, kA);  // first calibration: no previous rate
  CHECK(r["changed"] == false);
  CHECK(r["message"]["key"] == "cal.done");
  CHECK(r["message"]["args"]["prev"].is_null());
  CHECK(r["message"]["args"]["flow"].get<double>() == doctest::Approx(r["flowMlPerMin"].get<double>()));
  r = calibrate(s, c, kB);
  CHECK(r["changed"] == false);
  CHECK(r["message"]["args"]["prev"].is_null());
  r = calibrate(s, c, kA, 0.5);  // half the amount: clearly different, check the tubing
  CHECK(r["changed"] == true);
  CHECK(r["message"]["key"] == "cal.done_changed");
  CHECK(r["message"]["args"]["prev"].is_number());
  calibrate(s, c, kC);
  calibrate(s, c, kA);
  // Cancelled at "how much is in the cup?": it went into the cup, so nothing into the tank
  const std::string cal = c.ok("POST", std::string("/api/v1/pumps/") + kC + "/calibrate", {{"seconds", 30}})["job"]["id"];
  REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "waiting_user"; }, 60000));
  c.ok("POST", "/api/v1/jobs/" + cal + "/abort");
  CHECK(c.state()["lastJob"]["message"]["key"] == "job.aborted_none");
  const json aborted = findEvents(c, "mix").at(0);
  CHECK(aborted["title"]["key"] == "ev.job.aborted");  // not a mix
  CHECK(aborted["text"]["key"] == "ev.contents_none");
  // Too small for one exact run: the start fails, the cause travels as the reason
  auto [code, tiny] = c.call("POST", "/api/v1/dose", {{"canister", "calmag"}, {"ml", 0.05}});
  CHECK(code == 422);
  CHECK(tiny["error"]["key"] == "job.start_failed");
  CHECK(tiny["error"]["args"]["reason"]["key"] == "dose.too_small");
  s.world().fill(20, 0.02, 7.0);
  c.ok("POST", "/api/v1/mix/start", {{"recipe", "wachstum"}, {"waterL", 20}, {"guided", false}});
  REQUIRE(until(s, [&] { return s.world().cap(kA)->state == 1; }, 30000, 200));
  c.ok("POST", "/api/v1/stop");
  CHECK(c.state()["lastJob"]["message"]["key"] == "job.emergency_stop");
  const json stop = findEvents(c, "system").at(0);
  CHECK(stop["title"]["key"] == "ev.stop");
  CHECK(stop["text"]["args"]["who"]["key"] == "stop.app");
}

TEST_CASE("Scenario: an unknown stock is neither booked nor judged during a hand dose, and the mix plan takes it; once known, it is booked (RAT-015, RAT-071, #184)") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  s.step(3000);
  for (const char* id : {kDB, kA}) c.ok("POST", std::string("/api/v1/devices/") + id + "/accept", json::object());
  c.ok("POST", "/api/v1/canisters", {{"name", "Teil A"}, {"pump", kA}, {"capacityMl", 1000}, {"stockMl", nullptr}});
  c.ok("POST", "/api/v1/recipes", {{"name", "Nur A"}, {"steps", {{{"canister", "teil-a"}, {"mlPerL", 2.0}}}}});
  c.ok("PUT", "/api/v1/tank", {{"capacityL", 60}});
  calibrate(s, c, kA);
  s.world().fill(20, 0.02, 7.0);
  REQUIRE(c.state()["stock"]["teil-a"].is_null());
  const auto dose = [&](double ml) {
    REQUIRE(until(s, [&] { return c.state()["dosing"].is_null() && c.state()["job"].is_null(); }, 10 * 60 * 1000));
    c.ok("POST", "/api/v1/dose", {{"canister", "teil-a"}, {"ml", ml}});
    REQUIRE(until(s, [&] { return c.state()["dosing"].is_null() && c.state()["job"].is_null(); }, 60000, 500));
  };
  dose(3);
  // The dose goes in and is logged, but nothing is booked against a stock nobody knows
  CHECK(c.state()["stock"]["teil-a"].is_null());
  bool logged = false;
  for (const auto& e : findEvents(c, "dose")) logged = logged || (e["data"]["canister"] == "teil-a" && e["data"]["ml"].get<double>() == doctest::Approx(3).epsilon(0.1));
  CHECK(logged);
  for (const auto& e : findEvents(c, "tank")) CHECK(e["title"]["key"] != "ev.stock.below_zero");
  s.step(60000);  // the watchdog evaluates again
  const json wd = c.state()["watchdog"];
  for (const auto& i : wd["items"]) CHECK(i["id"] != "stock.teil-a");
  // The mix plan neither refuses nor counts it
  const json plan = c.ok("POST", "/api/v1/mix/plan", {{"recipe", "nur-a"}, {"waterL", 20}});
  CHECK(plan["ok"] == true);
  for (const auto& e : plan.value("errors", json::array())) CHECK(e["key"].get<std::string>().rfind("mix.stock", 0) != 0);
  // Once someone enters it, doses are booked again
  c.ok("POST", "/api/v1/canisters/teil-a/stock", {{"ml", 1000}});
  dose(3);
  CHECK(c.state()["stock"]["teil-a"].get<double>() == doctest::Approx(997).epsilon(0.002));
}
