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
#include "gc/messages.hpp"

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
  CHECK(j["error"]["key"] == "mix.part");  // the bottle's own refusal, with the pump's reason (#73)
  CHECK(j["error"]["args"]["reason"]["key"] == "dose.no_flow");
  CHECK(j["error"]["text"].get<std::string>().find("Pump not calibrated") != std::string::npos);
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

TEST_CASE("Scenario stage 0: a fixed pattern to a target EC without a probe lands where the plan says, the pair together (SD-057, SD-098, RAT-087, RAT-054)") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  setupStage0(s, c);
  for (const char* id : {kA, kB, kC}) calibrate(s, c, id);
  // The chart EC 1.2 over 4.5 ml/L: 0.267 per ml/L; the twin's parts give 1.21 (SIMULATOR.md)
  json steps = {{{"canister", "teil-a"}, {"mlPerL", 2.0}}, {{"canister", "teil-b"}, {"mlPerL", 2.0}}, {{"canister", "calmag"}, {"mlPerL", 0.5}}};
  c.ok("POST", "/api/v1/recipes", {{"id", "wachstum"}, {"steps", steps}, {"refEc", 1.2}});
  for (double waterEc : {0.02, 0.0}) {
    CAPTURE(waterEc);
    s.world().fill(20, 0.02, 7.0);  // the inlet's 0.02, entered as measured or as 0
    json req = {{"recipe", "wachstum"}, {"waterL", 20}, {"confirmRepeat", true}, {"amounts", "target"}, {"targetEc", 0.9}, {"waterEc", waterEc}};
    const json plan = c.ok("POST", "/api/v1/mix/plan", req);
    REQUIRE(plan["ok"] == true);
    CHECK(plan["factor"].get<double>() == doctest::Approx((0.9 - waterEc) / (1.2 / 4.5) / 4.5));
    const double stockA = c.state()["stock"]["teil-a"].get<double>();
    auto doses = [&](const char* canister) {
      double ml = 0;
      for (const auto& e : findEvents(c, "dose"))
        if (e["data"]["canister"] == canister) ml += e["data"]["ml"].get<double>();
      return ml;
    };
    const double a0 = doses("teil-a"), b0 = doses("teil-b");
    const json job = c.ok("POST", "/api/v1/mix/start", req)["job"];
    CHECK(job["info"]["expectedEc"].get<double>() == doctest::Approx(0.9));
    const std::string id = job["id"];
    for (int step = 0; step < 2; ++step) {
      REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "waiting_user"; }, 120000));
      c.ok("POST", "/api/v1/jobs/" + id + "/continue");
    }
    REQUIRE(until(s, [&] { return c.state()["job"].is_null(); }, 120000));
    CHECK(c.state()["lastJob"]["state"] == "done");
    s.step(30 * 60 * 1000);
    // Without measuring, it lands where the plan says plus the water's EC it was not told of
    const double ec = s.world().tank.ec + s.world().tank.pendingEc;
    CHECK(ec == doctest::Approx(0.9 + 0.02 - waterEc).epsilon(0.015));  // tells 0.02 apart
    const double a = doses("teil-a") - a0, b = doses("teil-b") - b0;
    CHECK(a == doctest::Approx(plan["steps"][0]["ml"].get<double>()).epsilon(0.03));
    CHECK(a / b == doctest::Approx(1.0).epsilon(0.03));
    CHECK(stockA - c.state()["stock"]["teil-a"].get<double>() == doctest::Approx(a).epsilon(0.001));
    s.step(31 * 60 * 1000);  // the next mix needs no repeat confirmation beyond the one sent
  }
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
  // Stirring by hand takes its time: 20 minutes at each prompt, each within the 30 min a prompt
  // waits (SD-057); Part A and Part B are in, CalMag comes last
  for (int prompt = 0; prompt < 2; ++prompt) {
    REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "waiting_user"; }, 120000));
    s.step(20 * 60 * 1000);
    if (prompt == 0) c.ok("POST", "/api/v1/jobs/" + id + "/continue");
  }

  SUBCASE("finished: counted from its end, also after a restart") {
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
    s.world().cap(kC)->blocked = true;
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
    s.world().cap(kC)->blocked = true;
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
    s.world().cap(kC)->blocked = false;
    CHECK(c.call("POST", "/api/v1/mix/start", mix).first == 200);
  }
  SUBCASE("failed part way, then a pump is primed: the failed mix ends with what is in the tank, and the window counts") {
    s.world().cap(kC)->blocked = true;
    c.ok("POST", "/api/v1/jobs/" + id + "/continue");
    REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "failed"; }, 180000));
    s.world().cap(kC)->blocked = false;
    c.ok("POST", std::string("/api/v1/pumps/") + kC + "/prime", {{"seconds", 3}});
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

namespace {
// The newest dose event and its id (0 with none): far cheaper to poll than the state
json lastDose(Client& c) {
  const json e = c.ok("GET", "/api/v1/events?limit=1&type=dose")["events"];
  return e.empty() ? json::object() : e[0];
}
std::uint64_t lastDoseId(Client& c) { return lastDose(c).value("id", std::uint64_t{0}); }
bool logged(Client& c, const std::string& key, const std::string& field = "title") {
  for (const auto& e : findEvents(c, "mix"))
    if (e[field]["key"] == key) return true;
  return false;
}
// The demo after its own mix, with no control dose running: a mix does not start over one
void demoCalm(sim::Simulation& s, Client& c) {
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  REQUIRE(until(s, [&] {
    const json st = c.state();
    return st["job"].is_null() && st["dosing"].is_null();
  }, 60 * 60 * 1000, 30000));
}
json startMix(Client& c, double waterL = 20) {
  return c.ok("POST", "/api/v1/mix/start", {{"recipe", "veg"}, {"waterL", waterL}, {"guided", false}, {"confirmRepeat", true}})["job"];
}
}  // namespace

namespace {
// Way 1 on the demo (SD-102): fresh water in, refill off, the probe's reading usable again after the jump.
// EC control is off meanwhile, else it would raise the fresh water itself; startMeasured switches it on
// and starts in the same moment, with no tick between
void freshForWay1(sim::Simulation& s, Client& c, double litres = 40) {
  demoCalm(s, c);
  c.ok("PATCH", "/api/v1/functions/refill", {{"enabled", false}});
  c.ok("PATCH", "/api/v1/functions/ec_control", {{"enabled", false}});
  s.world().fill(litres, 0.02, 6.5);
  REQUIRE(until(s, [&] {
    const json r = c.state()["readings"]["tank.ec"];
    return r["usable"] == true && r["value"].get<double>() < 0.2;  // the fresh water's reading, not the old one
  }, 60 * 60 * 1000, 10000));
}
json measuredPlan(Client& c, double waterL = 40, json extra = json::object()) {
  c.ok("PATCH", "/api/v1/functions/ec_control", {{"enabled", true}});
  json body = {{"recipe", "veg"}, {"waterL", waterL}, {"confirmRepeat", true}, {"amounts", "measured"}};
  body.update(extra);
  return c.ok("POST", "/api/v1/mix/plan", body);
}
json startMeasured(Client& c, double waterL = 40, json extra = json::object()) {
  c.ok("PATCH", "/api/v1/functions/ec_control", {{"enabled", true}});
  json body = {{"recipe", "veg"}, {"waterL", waterL}, {"guided", false}, {"confirmRepeat", true}, {"amounts", "measured"}};
  body.update(extra);
  return c.ok("POST", "/api/v1/mix/start", body)["job"];
}
// The events of one kind since an event ID, with the given title key
json eventsSince(Client& c, const std::string& type, std::uint64_t since, const std::string& key) {
  json out = json::array();
  for (const auto& e : findEvents(c, type))
    if (e["id"].get<std::uint64_t>() > since && e["title"]["key"] == key) out.push_back(e);
  return out;
}
}  // namespace

TEST_CASE("Scenario: mixing to the target with the probe measures first for the measuring time, doses a capped first step in the recipe's ratio from what it measured, and hands it to EC control, which learns it and closes the rest (SD-057, SD-102, RAT-054, RAT-056, RAT-057, RAT-088)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  freshForWay1(s, c);
  c.ok("PATCH", "/api/v1/functions/ec_control", {{"params", {{"max_ec_step", 0.5}, {"settle_min", 5}}}});
  const json plan = measuredPlan(c);
  REQUIRE(plan["ok"] == true);
  CHECK(plan["waterEc"].get<double>() == doctest::Approx(c.state()["readings"]["tank.ec"]["value"].get<double>()));
  CHECK(plan["litresFrom"] == "level");
  CHECK(plan["capped"] == true);  // 0.8 × the gap of about 1.2 is above the step limit of 0.5
  CHECK(plan["expectedEc"].get<double>() == doctest::Approx(plan["waterEc"].get<double>() + 0.5));
  const auto before = c.state()["eventId"].get<std::uint64_t>();
  const auto dosesBefore = lastDoseId(c);
  const json learnedBefore = c.state()["effects"]["ecByRecipe"].value("veg", json());
  const json job = startMeasured(c);
  CHECK(job["state"] == "measuring");
  CHECK(job["message"]["key"] == "mix.measuring");
  s.step(5000);
  const int left = c.state()["job"]["waitLeftS"].get<int>();
  CHECK(left > 280);  // the longer of EC control's 5 min settle time and 100 s per 20 L, from when the pump runs (D3)
  CHECK(left <= 300);
  CHECK(c.state()["outputs"]["tank.circulation"] == true);
  // Water not yet mixed moves the probe while it measures: announced, no jump lock, the plan follows it (D3)
  {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    s.world().tank.ec = 0.6;
  }
  s.step(280 * 1000);
  CHECK(c.state()["job"]["state"] == "measuring");
  CHECK(lastDoseId(c) == dosesBefore);  // nothing doses while it measures
  REQUIRE(until(s, [&] { return c.state()["job"]["state"] != "measuring"; }, 30 * 60 * 1000, 2000));
  CHECK(eventsSince(c, "block", before, "ev.jump").empty());
  const json measuredEv = eventsSince(c, "mix", before, "ev.mix.measured");
  REQUIRE(measuredEv.size() == 1);
  const double measuredEc = measuredEv[0]["data"]["ec"].get<double>();
  CHECK(measuredEc == doctest::Approx(0.6).epsilon(0.05));
  CHECK(c.state()["job"]["info"]["capped"] == true);
  REQUIRE(until(s, [&] { return c.state()["job"].is_null(); }, 2 * 3600 * 1000, 10000));
  const json last = c.state()["lastJob"];
  CHECK(last["state"] == "done");
  CHECK(last["message"]["key"] == "mix.done_handover");
  CHECK(last["message"]["args"]["after"]["key"] == "mix.after_handover_auto");
  double a = 0, b = 0;
  for (const auto& e : findEvents(c, "dose"))
    if (e["id"].get<std::uint64_t>() > before && e["data"]["purpose"] == "mix") {
      if (e["data"]["canister"] == "part-a") a += e["data"]["ml"].get<double>();
      if (e["data"]["canister"] == "part-b") b += e["data"]["ml"].get<double>();
    }
  REQUIRE(a > 0);
  CHECK(a / b == doctest::Approx(1.0).epsilon(0.03));  // the pair in its ratio (RAT-054)
  // EC control judges the first step as its first round and learns it into the recipe (D7)
  s.step(30 * 1000);
  const json learned = c.state()["effects"]["ecByRecipe"].value("veg", json());
  REQUIRE(learned.is_number());
  CHECK(learned != learnedBefore);
  CHECK(learned.get<double>() == doctest::Approx(0.27).epsilon(0.15));  // the twin's A, B and CalMag (RAT-055, RAT-080)
  // … and closes the rest to its target in rounds of its own
  REQUIRE(until(s, [&] {
    return !eventsSince(c, "control", before, "ev.ec.done").empty() || !eventsSince(c, "control", before, "ev.ec.max").empty();
  }, 4 * 3600 * 1000, 30000));
  const json done = eventsSince(c, "control", before, "ev.ec.done");
  REQUIRE(done.size() == 1);
  CHECK(done[0]["data"]["from"].get<double>() == doctest::Approx(measuredEc).epsilon(0.05));  // from what it measured, not the first step
  CHECK(done[0]["data"]["rounds"].get<int>() >= 2);  // round 1 was the mix's
  CHECK(c.state()["latches"].count("ec.no_effect") == 0);
}

TEST_CASE("Scenario: mixing to the target with the probe judges its first step: a probe that reads about 0 or pumps that deliver too little lock EC control at once and learn nothing; one far too strong is named; after fresh water or in maintenance it is neither judged nor learned; the inlet while it measures ends it; a probe lost at the hand-over leaves EC control waiting (SD-102 D6, D7, RAT-057, RAT-088)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  freshForWay1(s, c);
  auto probeFault = [&](const char* fault) {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    s.control("fault", {{"device", "PHEC-3F2A91"}, {"fault", fault}});
  };
  auto flowTimes = [&](double k) {  // the pumps deliver k times what their calibration says
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    for (const char* id : {"CAP-1F02A4", "CAP-1F02B7", "CAP-1F02C1"}) s.world().cap(id)->trueFlow *= k;
  };
  const json learnedBefore = c.state()["effects"]["ecByRecipe"].value("veg", json());
  const auto before = c.state()["eventId"].get<std::uint64_t>();
  auto locked = [&] { return c.state()["latches"].count("ec.no_effect") == 1; };
  auto handedOver = [&] {
    REQUIRE(until(s, [&] { return c.state()["job"].is_null(); }, 2 * 3600 * 1000, 10000));
    return c.state()["lastJob"]["message"];
  };
  SUBCASE("a dry probe: the first step goes in, EC does not rise, EC control locks at once") {
    probeFault("ec_zero");
    s.step(60 * 1000);
    REQUIRE(until(s, [&] { return c.state()["readings"]["tank.ec"]["usable"] == true; }, 60 * 60 * 1000, 10000));
    startMeasured(c);
    CHECK(handedOver()["key"] == "mix.done_handover_locked");  // the card says so, not that EC top-up closes the rest
    CHECK(locked());
    CHECK(eventsSince(c, "alarm", before, "ev.ec.first_no_effect").size() == 1);
    CHECK(c.state()["effects"]["ecByRecipe"].value("veg", json()) == learnedBefore);  // an invalid rise is never learned
    const auto doses = lastDoseId(c);
    s.step(60 * 60 * 1000);
    CHECK(lastDoseId(c) == doses);  // no round at the full step limit follows
  }
  SUBCASE("pumps delivering a sixth: a rise above 0.02 but below a quarter of the one expected locks at once") {
    flowTimes(1.0 / 6);
    startMeasured(c);
    CHECK(handedOver()["key"] == "mix.done_handover_locked");
    CHECK(locked());
    const json ev = eventsSince(c, "alarm", before, "ev.ec.first_no_effect");
    REQUIRE(ev.size() == 1);
    CHECK(ev[0]["text"]["key"] == "ev.ec.first_no_effect.text");
    CHECK(c.state()["effects"]["ecByRecipe"].value("veg", json()) == learnedBefore);
  }
  SUBCASE("pumps delivering half: no lock, EC control closes the rest") {
    flowTimes(0.5);
    startMeasured(c);
    CHECK(handedOver()["key"] == "mix.done_handover");
    s.step(60 * 1000);
    CHECK_FALSE(locked());
  }
  SUBCASE("pumps delivering five times as much on a small gap: named, not locked") {
    {
      std::lock_guard<std::recursive_mutex> l(s.mutex());
      s.world().tank.ec = 0.95;  // about 0.3 below the aim: a first step of about 0.25 expected
    }
    REQUIRE(until(s, [&] {  // the jump lock over, nothing of control's own dosing
      const json st = c.state();
      return st["readings"]["tank.ec"]["usable"] == true && st["dosing"].is_null();
    }, 60 * 60 * 1000, 10000));
    flowTimes(5);
    startMeasured(c);
    CHECK(handedOver()["key"] == "mix.done_handover");
    CHECK(eventsSince(c, "control", before, "ev.ec.first_strong").size() == 1);
    CHECK_FALSE(locked());
  }
  SUBCASE("a dry probe with the inlet opened while it doses: the rise says nothing, neither judged nor learned") {
    probeFault("ec_zero");
    s.step(60 * 1000);
    REQUIRE(until(s, [&] { return c.state()["readings"]["tank.ec"]["usable"] == true; }, 60 * 60 * 1000, 10000));
    startMeasured(c);
    REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "running"; }, 30 * 60 * 1000, 2000));
    c.ok("POST", "/api/v1/roles/tank.inlet/test");
    CHECK(handedOver()["key"] == "mix.done_handover");
    s.step(60 * 1000);
    CHECK(eventsSince(c, "alarm", before, "ev.ec.first_no_effect").empty());
    CHECK(c.state()["effects"]["ecByRecipe"].value("veg", json()) == learnedBefore);
  }
  SUBCASE("a dry probe with maintenance mode on while it doses: work at the tank, neither judged nor learned") {
    probeFault("ec_zero");
    s.step(60 * 1000);
    REQUIRE(until(s, [&] { return c.state()["readings"]["tank.ec"]["usable"] == true; }, 60 * 60 * 1000, 10000));
    startMeasured(c);
    REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "running"; }, 30 * 60 * 1000, 2000));
    c.ok("POST", "/api/v1/maintenance", {{"minutes", 1}});
    handedOver();
    CHECK(eventsSince(c, "alarm", before, "ev.ec.first_no_effect").empty());
    CHECK(c.state()["effects"]["ecByRecipe"].value("veg", json()) == learnedBefore);
  }
  SUBCASE("the litres fall by more than a tenth while it measures: nothing confirmed them, it ends with nothing in") {
    const auto doses = lastDoseId(c);
    startMeasured(c);
    for (int i = 0; i < 20; ++i) {  // a slow leak or a flood drawn off, below any jump
      {
        std::lock_guard<std::recursive_mutex> l(s.mutex());
        s.world().tank.volumeL -= 0.4;
      }
      s.step(10000, 5000);
    }
    REQUIRE(until(s, [&] { return c.state()["job"].is_null(); }, 30 * 60 * 1000, 5000));
    CHECK(c.state()["lastJob"]["message"]["args"]["reason"]["key"] == "mix.measure_litres");
    CHECK(lastDoseId(c) == doses);
  }
  SUBCASE("the inlet opened while it measures: more water on top of what it measures ends it with nothing in") {
    const auto doses = lastDoseId(c);
    startMeasured(c);
    c.ok("POST", "/api/v1/roles/tank.inlet/test");
    s.step(5000);
    CHECK(c.state()["lastJob"]["message"]["key"] == "mix.measure_inlet");
    CHECK(lastDoseId(c) == doses);
  }
  SUBCASE("the probe goes offline while the mix mixes in: EC control waits for it, nothing judged or learned") {
    startMeasured(c);
    REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "settling"; }, 60 * 60 * 1000, 5000));
    probeFault("offline");
    const json m = handedOver();
    CHECK(m["key"] == "mix.done_handover_no_ec");
    s.step(60 * 1000);
    CHECK_FALSE(locked());
    CHECK(c.state()["effects"]["ecByRecipe"].value("veg", json()) == learnedBefore);
  }
  SUBCASE("EC top-up switched off while it doses: the rest waits, said so") {
    startMeasured(c);
    REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "running"; }, 30 * 60 * 1000, 2000));
    c.ok("PATCH", "/api/v1/functions/ec_control", {{"enabled", false}});
    CHECK(handedOver()["key"] == "mix.done_handover_off");
    CHECK(c.state()["effects"]["ecByRecipe"].value("veg", json()) == learnedBefore);
  }
}

TEST_CASE("Scenario: mixing to the target with the probe learns from all it put in, a fixed part as strong as the rest (SD-102 D7, RAT-085)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  freshForWay1(s, c);
  c.ok("POST", "/api/v1/recipes",
       {{"id", "veg"},
        {"steps",
         {{{"canister", "part-a"}, {"mlPerL", 2.0}}, {{"canister", "part-b"}, {"mlPerL", 2.0}}, {{"canister", "calmag"}, {"mlPerL", 0.6}, {"fixed", true}}}}});
  startMeasured(c);
  REQUIRE(until(s, [&] { return c.state()["job"].is_null(); }, 2 * 3600 * 1000, 10000));
  REQUIRE(c.state()["lastJob"]["message"]["key"] == "mix.done_handover");
  s.step(30 * 1000);
  const json learned = c.state()["effects"]["ecByRecipe"].value("veg", json());
  REQUIRE(learned.is_number());
  // Over the scaled parts alone it would read about 0.32: the fixed part's rise counted as theirs
  CHECK(learned.get<double>() == doctest::Approx(0.275).epsilon(0.12));
}

TEST_CASE("Scenario: mixing to the target with the probe ends with nothing in, as never started: cancelled, stopped, restarted, the probe lost, refused or at the target after measuring, or a calibration while it measures; its window undone, its litres kept (SD-102, R6, RAT-003)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  freshForWay1(s, c);
  // Without a level the hub keeps the litres itself: what way 1 booked must come back
  c.call("DELETE", "/api/v1/roles/tank.level");
  s.step(5000);
  const auto before = c.state()["eventId"].get<std::uint64_t>();
  const json lastMixBefore = c.state()["lastMixAt"];
  const json job = startMeasured(c);
  const std::string id = job["id"];
  CHECK(c.state()["tank"]["volumeL"].get<double>() == doctest::Approx(40));
  const auto doses = lastDoseId(c);
  std::string key;
  SUBCASE("cancelled") {
    c.ok("POST", "/api/v1/jobs/" + id + "/abort");
    key = "job.aborted_none";
  }
  SUBCASE("stopped") {
    c.ok("POST", "/api/v1/stop");
    s.step(5000);
    key = "job.emergency_stop";
    c.ok("POST", "/api/v1/resume");
  }
  SUBCASE("restarted: said so") {
    s.reboot();
    c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
    s.step(5000);
    CHECK(!eventsSince(c, "mix", 0, "ev.mix_reboot").empty());
    bool said = false;
    for (const auto& e : findEvents(c, "mix")) said = said || e["text"]["key"] == "ev.reboot.text_measuring";
    CHECK(said);
    key = "";  // the card of a mix the restart cut is not kept
  }
  SUBCASE("EC top-up switched off while it measures: refused after measuring") {
    c.ok("PATCH", "/api/v1/functions/ec_control", {{"enabled", false}});
    REQUIRE(until(s, [&] { return c.state()["job"].is_null(); }, 30 * 60 * 1000, 2000));
    CHECK(c.state()["lastJob"]["message"]["args"]["reason"]["key"] == "mix.measured_off");
    key = "mix.measure_refused";
  }
  SUBCASE("the tank at the target once measured: nothing needed, said as such") {
    {
      std::lock_guard<std::recursive_mutex> l(s.mutex());
      s.world().tank.ec = 1.45;  // water already mixed to the target lay apart from the probe
    }
    REQUIRE(until(s, [&] { return c.state()["job"].is_null(); }, 30 * 60 * 1000, 2000));
    key = "mix.measure_at_target";
  }
  SUBCASE("the probe goes offline while it measures: the reason is named") {
    {
      std::lock_guard<std::recursive_mutex> l(s.mutex());
      s.control("fault", {{"device", "PHEC-3F2A91"}, {"fault", "offline"}});
    }
    REQUIRE(until(s, [&] { return c.state()["job"].is_null(); }, 30 * 60 * 1000, 2000));
    key = "mix.measure_failed";
  }
  SUBCASE("a probe calibration started while it measures") {
    c.ok("POST", "/api/v1/probe", {{"device", "PHEC-3F2A91"}, {"kind", "ec"}, {"action", "start"}});
    s.step(5000);
    key = "mix.measure_calibrating";
    c.ok("POST", "/api/v1/probe", {{"device", "PHEC-3F2A91"}, {"kind", "ec"}, {"action", "cancel"}});
  }
  s.step(5000);
  const json st = c.state();
  if (!key.empty()) {
    CHECK(st["lastJob"]["state"] == "aborted");
    CHECK(st["lastJob"]["message"]["key"] == key);
  }
  CHECK(lastDoseId(c) == doses);
  CHECK(st["tank"]["volumeL"].get<double>() == doctest::Approx(40));  // the fresh water is in: its litres stay (D8)
  CHECK(st["lastMixAt"] == lastMixBefore);  // the window against mixing twice is undone (RAT-003)
  // Nothing went in, so EC control is not told to settle a cut mix first (SD-057)
  CHECK(st["controllers"]["ec"]["line"]["key"].get<std::string>().rfind("ec.mixcut", 0) != 0);
  CHECK(eventsSince(c, "mix", before, "ev.mix.measured").empty());
}

TEST_CASE("Scenario: a top-up to the target with the probe and no usable level asks for the litres in the tank now and doses on them (SD-102 D4, RAT-011)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  freshForWay1(s, c, 30);
  c.call("DELETE", "/api/v1/roles/tank.level");
  s.step(5000);
  {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    s.world().fill(10, 0.02, 6.5);  // 10 L of fresh water on top: 40 L in the tank, the hub knew 30 from the last mix or nothing
  }
  json plan = measuredPlan(c, 10, {{"mode", "topup"}});
  CHECK(plan["ok"] == false);
  bool asked = false;
  for (const auto& e : plan["errors"]) asked = asked || e["key"] == "mix.measured_tank_now";
  CHECK(asked);
  plan = measuredPlan(c, 10, {{"mode", "topup"}, {"tankNowL", 40}});
  REQUIRE(plan["ok"] == true);
  CHECK(plan["tankL"].get<double>() == doctest::Approx(40));
  CHECK(plan["litresFrom"] == "entered");
  startMeasured(c, 10, {{"mode", "topup"}, {"tankNowL", 40}});
  CHECK(c.state()["tank"]["volumeL"].get<double>() == doctest::Approx(40));  // as entered, not known plus fresh
  REQUIRE(until(s, [&] { return c.state()["job"].is_null(); }, 2 * 3600 * 1000, 10000));
  CHECK(c.state()["lastJob"]["message"]["key"] == "mix.done_handover");
}

TEST_CASE("Scenario: a mix to the target with the probe stopped with a pair half in holds EC and pH until it is resumed, which completes the pair; one stopped with the pair complete is not resumed once EC control has dosed (SD-102, RAT-054)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  freshForWay1(s, c);
  auto block = [&](const char* cap, bool on) {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    s.world().cap(cap)->blocked = on;
  };
  const auto before = c.state()["eventId"].get<std::uint64_t>();
  SUBCASE("part B fails: EC control waits, resuming completes B in the pair's ratio") {
    block("CAP-1F02B7", true);
    const std::string id = startMeasured(c)["id"];
    REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "failed"; }, 60 * 60 * 1000, 5000));
    s.step(30 * 60 * 1000, 10000);
    const json st = c.state();
    CHECK(st["controllers"]["ec"]["line"]["key"] == "ec.wait_pair");
    bool said = false;  // the checklist says why, not that a mix is running
    for (const auto& ch : st["controllers"]["ec"]["checks"]) said = said || ch.dump().find("check.pair_open") != std::string::npos;
    CHECK(said);
    CHECK(st["controllers"]["refill"]["line"]["key"] != "refill.wait_dose");  // the refill is not held by it
    CHECK_FALSE(st["job"].contains("noResume"));
    for (const auto& e : findEvents(c, "dose")) CHECK_FALSE((e["id"].get<std::uint64_t>() > before && e["data"]["purpose"] == "ec"));
    block("CAP-1F02B7", false);
    c.ok("POST", "/api/v1/jobs/" + id + "/resume");
    REQUIRE(until(s, [&] { return c.state()["job"].is_null(); }, 2 * 3600 * 1000, 10000));
    double a = 0, b = 0;
    for (const auto& e : findEvents(c, "dose"))
      if (e["id"].get<std::uint64_t>() > before && e["data"]["purpose"] == "mix") {
        if (e["data"]["canister"] == "part-a") a += e["data"]["ml"].get<double>();
        if (e["data"]["canister"] == "part-b") b += e["data"]["ml"].get<double>();
      }
    REQUIRE(b > 0);
    CHECK(a / b == doctest::Approx(1.0).epsilon(0.03));
  }
  SUBCASE("part B fails, then a restart: the report names the part still missing, as an alarm") {
    block("CAP-1F02B7", true);
    startMeasured(c);
    REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "failed"; }, 60 * 60 * 1000, 5000));
    s.reboot();
    c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
    s.step(5000);
    bool named = false;
    for (const auto& e : findEvents(c, "mix"))
      if (e["text"]["key"] == "ev.reboot.text_pair") named = e["severity"] == "alarm" && e["text"].dump().find("Part B") != std::string::npos;
    CHECK(named);
  }
  SUBCASE("part B fails, then a Stop: the Stop's message names the part still missing") {
    block("CAP-1F02B7", true);
    startMeasured(c);
    REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "failed"; }, 60 * 60 * 1000, 5000));
    c.ok("POST", "/api/v1/stop");
    s.step(5000);
    const json last = c.state()["lastJob"];
    CHECK(last["message"]["key"] == "job.emergency_stop_pair");
    CHECK(last["message"]["args"]["partners"].dump().find("Part B") != std::string::npos);
    c.ok("POST", "/api/v1/resume");
  }
  SUBCASE("CalMag fails after the pair: EC control goes on, and the mix is not resumed over its rounds") {
    block("CAP-1F02C1", true);
    const std::string id = startMeasured(c)["id"];
    REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "failed"; }, 60 * 60 * 1000, 5000));
    block("CAP-1F02C1", false);
    REQUIRE(until(s, [&] { return lastDose(c).value("data", json::object()).value("purpose", "") == "ec"; }, 60 * 60 * 1000, 5000));
    CHECK(c.state()["job"]["noResume"]["key"] == "mix.resume_ec_dosed");  // the card says so instead of offering it
    const auto r = c.call("POST", "/api/v1/jobs/" + id + "/resume");
    CHECK(r.first == 409);
    CHECK(r.second["error"]["key"] == "mix.resume_ec_dosed");
  }
}

TEST_CASE("Scenario: an EC round that would need more partial runs than allowed is made smaller by one factor and says so (SD-102, RAT-054)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  demoCalm(s, c);
  c.ok("PATCH", "/api/v1/functions/refill", {{"enabled", false}});
  {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    for (const char* id : {"CAP-1F02A4", "CAP-1F02B7", "CAP-1F02C1"}) s.world().cap(id)->trueFlow = s.world().cap(id)->storedFlow = 5;  // slow pumps
  }
  s.world().fill(40, 0.02, 6.5);
  const auto filled = c.state()["eventId"].get<std::uint64_t>();
  REQUIRE(until(s, [&] { return c.state()["controllers"]["ec"]["line"]["key"] == "ec.dosing_limited"; }, 2 * 3600 * 1000, 5000));
  CHECK(c.state()["controllers"]["ec"]["line"]["args"]["runs"] == 6);
  REQUIRE(until(s, [&] { return c.state()["controllers"]["ec"]["line"]["key"] != "ec.dosing_limited"; }, 3600 * 1000, 5000));
  double a = 0, b = 0;
  for (const auto& e : findEvents(c, "dose"))
    if (e["id"].get<std::uint64_t>() > filled && e["data"]["purpose"] == "ec") {
      if (e["data"]["canister"] == "part-a") a += e["data"]["ml"].get<double>();
      if (e["data"]["canister"] == "part-b") b += e["data"]["ml"].get<double>();
    }
  REQUIRE(b > 0);
  CHECK(a / b == doctest::Approx(1.0).epsilon(0.03));  // one factor for the pair (RAT-054)
}

TEST_CASE("Scenario: pumps far too slow for the tank: rounds made smaller that raise EC by less than 0.06 each are judged and learned together and do not lock EC control (SD-102, RAT-055, PD-317)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  demoCalm(s, c);
  c.ok("PATCH", "/api/v1/functions/refill", {{"enabled", false}});
  {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    for (const char* id : {"CAP-1F02A4", "CAP-1F02B7", "CAP-1F02C1"}) s.world().cap(id)->trueFlow = s.world().cap(id)->storedFlow = 0.5;
  }
  s.world().fill(200, 0.4, 6.0);
  const auto before = c.state()["eventId"].get<std::uint64_t>();
  REQUIRE(until(s, [&] { return c.state()["controllers"]["ec"]["line"]["key"] == "ec.dosing_limited"; }, 2 * 3600 * 1000, 5000));
  const json learnedBefore = c.state()["effects"]["ecByRecipe"].value("veg", json());
  s.step(4 * 3600 * 1000, 10000);  // rounds raising EC by about 0.01 each, judged and learned 7 at a time
  CHECK(c.state()["latches"].count("ec.no_effect") == 0);
  int rounds = 0;
  for (const auto& e : findEvents(c, "dose")) rounds += e["id"].get<std::uint64_t>() > before && e["data"]["purpose"] == "ec" && e["data"]["canister"] == "part-a";
  CHECK(rounds >= 7);
  const json learned = c.state()["effects"]["ecByRecipe"].value("veg", json());
  REQUIRE(learned.is_number());
  CHECK(learned != learnedBefore);  // learned from the rounds together
  CHECK(learned.get<double>() == doctest::Approx(0.27).epsilon(0.3));
}

namespace {
std::string irrigationPlug(sim::Simulation& s, Client& c);
}  // namespace

TEST_CASE("Scenario: EC control locks after two rounds without effect; rounds made smaller for slow pumps that are too small to judge alone count together (SD-102, RAT-055)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  demoCalm(s, c);
  c.ok("PATCH", "/api/v1/functions/refill", {{"enabled", false}});
  {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    s.control("fault", {{"device", "PHEC-3F2A91"}, {"fault", "ec_zero"}});  // dry: reads about 0 whatever goes in
  }
  bool slowed = false;
  int expected = 2;  // rounds until the lock
  std::function<void()> whileRound2Settles;  // what happens to round 2
  std::optional<json> keeps;                 // the recipe's learned effect, when no round may learn
  auto slow = [&](double mlPerMin) {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    for (const char* id : {"CAP-1F02A4", "CAP-1F02B7", "CAP-1F02C1"}) s.world().cap(id)->trueFlow = s.world().cap(id)->storedFlow = mlPerMin;
    slowed = true;
  };
  SUBCASE("normal pumps") {}
  SUBCASE("pumps slowed to 3 ml/min: rounds made smaller, each should still raise EC by far more than 0.06") { slow(3); }
  SUBCASE("pumps slowed to 0.3 ml/min: each round should raise EC by less than 0.06, two of them together by more") {
    slow(0.3);
    expected = 4;
  }
  SUBCASE("0.3 ml/min, a hand dose while round 2 settles: round 2 is not clean and left out, the sum goes on") {
    slow(0.3);
    expected = 5;
    whileRound2Settles = [&] { c.ok("POST", "/api/v1/dose", {{"canister", "calmag"}, {"ml", 0.3}}); };
  }
  SUBCASE("0.3 ml/min, the recipe's ratio edited while round 2 settles: the sum starts afresh (RAT-057)") {
    slow(0.3);
    expected = 6;
    whileRound2Settles = [&] {
      gc::json veg;
      const gc::json cfg = c.ok("GET", "/api/v1/config");
      for (const auto& r : cfg["recipes"])
        if (r["id"] == "veg") veg = r;
      veg["steps"][2]["mlPerL"] = 0.5;
      c.ok("POST", "/api/v1/recipes", veg);
    };
  }
  SUBCASE("0.3 ml/min, the watering pump pressed while round 2 settles: rounds 2 and 3, which watering made unclean, make the sums they are in count half (SD-103)") {
    // EC control off until a watering pump is bound and its 10 min from binding have run out
    c.ok("PATCH", "/api/v1/functions/ec_control", {{"enabled", false}});
    const std::string plug = irrigationPlug(s, c);
    s.step(11 * 60 * 1000, 5000);
    c.ok("PATCH", "/api/v1/functions/ec_control", {{"enabled", true}});
    slow(0.3);
    expected = 6;  // 1 and 2, 3 and 4 count 1 point each (round 3 starts less than 10 min after the press), 5 and 6 count 2
    whileRound2Settles = [&s, plug] {
      std::lock_guard<std::recursive_mutex> l(s.mutex());
      auto& o = s.world().netPlug(plug)->outlets[0];
      o.on = true;
      o.onSince = s.world().now();
      ++o.switchOns;
    };
  }
  SUBCASE("0.3 ml/min, the watering pump's plug offline for the whole series: every round watered, each sum counts half, the lock after 8 rounds (SD-103)") {
    c.ok("PATCH", "/api/v1/functions/ec_control", {{"enabled", false}});
    const std::string plug = irrigationPlug(s, c);
    s.step(11 * 60 * 1000, 5000);
    s.control("fault", {{"device", plug}, {"fault", "offline"}});
    s.step(60 * 1000, 5000);
    REQUIRE(c.state()["outputs"]["zone.irrigation_pump"].is_null());  // its state not known
    keeps = c.state()["effects"]["ecByRecipe"].value("veg", json());  // a sum with watering in it learns nothing
    c.ok("PATCH", "/api/v1/functions/ec_control", {{"enabled", true}});
    slow(0.3);
    expected = 8;
  }
  const auto before = c.state()["eventId"].get<std::uint64_t>();
  auto rounds = [&] {
    int n = 0;
    for (const auto& e : findEvents(c, "dose")) n += e["id"].get<std::uint64_t>() > before && e["data"]["purpose"] == "ec" && e["data"]["canister"] == "part-a";
    return n;
  };
  bool limited = false, happened = false;
  REQUIRE(until(s, [&] {
    const gc::json st = c.state();
    limited = limited || st["controllers"]["ec"]["line"]["key"] == "ec.dosing_limited";
    if (whileRound2Settles && !happened && st["controllers"]["ec"]["line"]["key"] == "ec.settling" && rounds() == 2) {
      whileRound2Settles();
      happened = true;
    }
    return st["latches"].count("ec.no_effect") == 1;
  }, 6 * 3600 * 1000, 10000));
  CHECK(happened == static_cast<bool>(whileRound2Settles));
  CHECK(limited == slowed);  // the slowed rounds were made smaller (D10), the normal ones not
  CHECK(rounds() == expected);
  if (keeps) CHECK(c.state()["effects"]["ecByRecipe"].value("veg", json()) == *keeps);
  CHECK(eventsSince(c, "alarm", before, "ev.ec.no_effect").size() == 1);
  s.step(3600 * 1000, 10000);  // locked: nothing more goes in
  CHECK(rounds() == expected);
}

TEST_CASE("Scenario: a recipe that learned a small effect: slow pumps' small rounds are expected at that effect and do not lock a working probe (SD-102, RAT-055, RAT-057)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  demoCalm(s, c);
  c.ok("PATCH", "/api/v1/functions/refill", {{"enabled", false}});
  // A weak line: the chart EC implies 0.6 per ml/L, the twin's parts give 0.1
  gc::json veg;
  const gc::json cfg = c.ok("GET", "/api/v1/config");
  for (const auto& r : cfg["recipes"])
    if (r["id"] == "veg") veg = r;
  veg["refEc"] = 2.8;
  c.ok("POST", "/api/v1/recipes", veg);
  {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    for (const char* liquid : {"grow_a", "grow_b", "calmag"}) s.world().liquids[liquid].ecPerMlL = 0.1;
  }
  s.world().fill(31, 0.4, 6.0);
  auto learned = [&] { return c.state()["effects"]["ecByRecipe"].value("veg", json()); };
  // A round at the pumps' own speed learns the small effect; the pumps slow down once it is in
  REQUIRE(until(s, [&] { return c.state()["controllers"]["ec"]["line"]["key"] == "ec.settling"; }, 2 * 3600 * 1000, 5000));
  {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    for (const char* id : {"CAP-1F02A4", "CAP-1F02B7", "CAP-1F02C1"}) s.world().cap(id)->trueFlow = s.world().cap(id)->storedFlow = 0.15;
  }
  const auto before = c.state()["eventId"].get<std::uint64_t>();
  REQUIRE(until(s, [&] { return learned().is_number() && learned().get<double>() < 0.2; }, 3600 * 1000, 10000));
  CHECK(learned().get<double>() == doctest::Approx(0.1).epsilon(0.3));
  auto rounds = [&] {
    int n = 0;
    for (const auto& e : findEvents(c, "dose")) n += e["id"].get<std::uint64_t>() > before && e["data"]["purpose"] == "ec" && e["data"]["canister"] == "part-a";
    return n;
  };
  // At the chart's 0.6 two rounds would make a sum that rose by a sixth of what it should, twice: a lock
  // after 4. At the learned 0.1 about ten make one, and it rose as it should.
  const json afterRound1 = learned();
  bool limited = false;
  REQUIRE(until(s, [&] {
    limited = limited || c.state()["controllers"]["ec"]["line"]["key"] == "ec.dosing_limited";
    return learned() != afterRound1 || c.state()["latches"].count("ec.no_effect") == 1;
  }, 8 * 3600 * 1000, 10000));
  CHECK(limited);
  CHECK(c.state()["latches"].count("ec.no_effect") == 0);
  CHECK(rounds() >= 9);  // the small rounds it took to make a sum
  CHECK(learned().get<double>() == doctest::Approx(0.1).epsilon(0.3));  // learned from the sum
}

TEST_CASE("Scenario: a mix with the circulation pump ends only once mixed in; control and refill wait for it (SD-057, RAT-052)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  demoCalm(s, c);
  c.ok("PATCH", "/api/v1/functions/ec_control", {{"params", {{"settle_min", 12}}}});
  c.ok("PATCH", "/api/v1/functions/circulation", {{"enabled", false}});  // only the mix asks for the pump
  c.ok("PATCH", "/api/v1/functions/refill", {{"enabled", false}});
  s.world().fill(40, 0.02, 7.0);
  const std::string id = startMix(c)["id"];
  REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "settling"; }, 20 * 60 * 1000, 5000));
  const json job = c.state()["job"];
  CHECK(job["message"]["key"] == "mix.settling");
  CHECK(job["message"]["args"]["min"] == 12);  // the longer of EC control's 12 and pH control's 5 min
  CHECK(job["waitLeftS"].get<int>() > 11 * 60);
  CHECK(job["waitLeftS"].get<int>() <= 12 * 60);
  CHECK(job["circulating"] == true);
  CHECK(job["pumpStands"] == false);
  CHECK(c.call("POST", "/api/v1/jobs/" + id + "/continue").first == 409);  // it cannot be skipped
  const auto doneLogged = [&] {
    for (const auto& e : findEvents(c, "mix"))
      if (e["title"]["key"] == "ev.mix.done" && e["data"]["job"] == id) return true;
    return false;
  };
  CHECK_FALSE(doneLogged());
  // Refill wants to start (level below its start), but waits like EC and pH (RAT-055)
  c.ok("PATCH", "/api/v1/functions/refill", {{"enabled", true}, {"params", {{"start_below_l", 45}, {"target_l", 48}}}});
  const auto doses = lastDoseId(c);
  const double litres = s.world().tank.volumeL;
  bool circOff = false;
  for (int t = 0; t < 11 * 60; t += 5) {
    s.step(5000);
    circOff = circOff || !s.world().circulating();
  }
  CHECK(lastDoseId(c) == doses);  // neither EC nor pH
  CHECK(c.state()["dosing"].is_null());
  CHECK_FALSE(circOff);  // the mix keeps the pump on
  CHECK(s.world().tank.volumeL < litres + 0.5);  // no refill: 2 L/min would show
  CHECK(c.state()["job"]["state"] == "settling");
  REQUIRE(until(s, [&] { return c.state()["job"].is_null(); }, 2 * 60 * 1000));
  CHECK(c.state()["lastJob"]["state"] == "done");
  CHECK(c.state()["lastJob"]["message"]["key"] == "mix.done");
  CHECK(doneLogged());
  CHECK(until(s, [&] { return c.state()["outputs"]["tank.inlet"] == true; }, 2 * 60 * 1000));  // then refill
}

TEST_CASE("Scenario: a mix mixes in for the longer settle time of the controllers switched on, at least 240 s with pH control and 100 s per 20 L in the tank (SD-057, RAT-052, RAT-058)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  demoCalm(s, c);
  c.ok("PATCH", "/api/v1/functions/circulation", {{"enabled", false}});
  c.ok("PATCH", "/api/v1/functions/refill", {{"enabled", false}});
  const auto settle = [&](const char* fn, json v) { c.ok("PATCH", std::string("/api/v1/functions/") + fn, {{"params", {{"settle_min", v}}}}); };
  const auto off = [&](const char* fn) { c.ok("PATCH", std::string("/api/v1/functions/") + fn, {{"enabled", false}}); };
  double level = 20;
  int expected = 0;
  std::string text = "mix.settling";
  SUBCASE("pH's longer settle time") {
    settle("ec_control", 3);
    settle("ph_control", 9);
    expected = 9;
  }
  SUBCASE("only the controllers switched on count") {
    settle("ec_control", 3);
    settle("ph_control", 9);
    off("ph_control");
    expected = 3;
  }
  SUBCASE("with no control switched on, 5 min") {
    off("ec_control");
    off("ph_control");
    expected = 5;
    text = "mix.settling_plain";  // no control to promise: "then you can measure"
  }
  SUBCASE("with pH control, 240 s at least") {
    settle("ec_control", 2);
    settle("ph_control", 2);
    expected = 4;
  }
  SUBCASE("100 s per 20 L of what the level sensor reads, not of the mix's water") {
    settle("ec_control", 2);
    off("ph_control");
    level = 58;
    expected = 5;  // 290 s
  }
  s.world().fill(level, 0.02, 7.0);
  // A large step locks the level as a jump until 15 min of quiet (RAT-042), and the pump with it
  s.step((level > 40 ? 16 : 1) * 60000);
  REQUIRE(until(s, [&] { return c.state()["dosing"].is_null(); }, 30 * 60 * 1000, 5000));  // a mix does not start over a dose
  startMix(c);
  REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "settling"; }, 20 * 60 * 1000, 5000));
  const json job = c.state()["job"];
  REQUIRE(job["message"]["key"] == text);
  CHECK(job["message"]["args"].value("min", -1) == expected);
  CHECK(job["waitLeftS"].get<int>() > expected * 60 - 61);
  CHECK(job["waitLeftS"].get<int>() <= expected * 60);
}

TEST_CASE("Scenario: stirring by hand waits 30 min at most; the mix then stops, open to go on or to end, and names the parts of a pair still missing (SD-057, RAT-054)") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  setupStage0(s, c);
  for (const char* id : {kA, kB, kC}) calibrate(s, c, id);
  s.world().fill(20, 0.02, 7.0);
  const std::string id = c.ok("POST", "/api/v1/mix/start", {{"recipe", "wachstum"}, {"waterL", 20}, {"guided", true}})["job"]["id"];
  REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "waiting_user"; }, 120000));  // Teil A is in
  CHECK(c.state()["job"]["waitLeftS"].get<int>() > 29 * 60);
  s.step(29 * 60 * 1000);
  CHECK(c.state()["job"]["state"] == "waiting_user");
  s.step(61 * 1000);
  json job = c.state()["job"];
  REQUIRE(job["state"] == "failed");
  CHECK(job["message"]["key"] == "mix.stir_timeout_pair");
  CHECK(job["message"]["args"]["min"] == 30);
  CHECK(job["message"]["args"]["name"] == "Teil A");
  REQUIRE(job["message"]["args"]["partners"].size() == 1);
  CHECK(job["message"]["args"]["partners"][0]["args"]["name"] == "Teil B");
  CHECK(job["message"]["args"]["partners"][0]["args"]["ml"].get<double>() == doctest::Approx(40.0));
  const auto stopped = [&](const char* severity) {
    int n = 0;
    for (const auto& e : findEvents(c, "mix")) n += e["title"]["key"] == "ev.mix.stir_timeout" && e["severity"] == severity;
    return n;
  };
  CHECK(stopped("alarm") == 1);  // a pair half in
  for (const char* p : {kA, kB, kC}) CHECK(s.world().cap(p)->state != 1);
  CHECK(c.call("POST", "/api/v1/jobs/" + id + "/continue").first == 409);
  // Never mixed over (RAT-003)
  const auto over = c.call("POST", "/api/v1/mix/start", {{"recipe", "wachstum"}, {"waterL", 20}, {"confirmRepeat", true}});
  CHECK(over.first == 409);
  CHECK(over.second["error"]["key"] == "mix.failed_open");
  SUBCASE("going on doses the next part") {
    c.ok("POST", "/api/v1/jobs/" + id + "/resume");
    CHECK(logged(c, "ev.mix.resumed.next", "text"));
    REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "waiting_user"; }, 120000));  // Teil B is in
    s.step(31 * 60 * 1000);
    job = c.state()["job"];
    REQUIRE(job["state"] == "failed");
    CHECK(job["message"]["key"] == "mix.stir_timeout");  // the pair is complete: none to name
    CHECK(job["message"]["args"]["name"] == "Teil B");
    CHECK(stopped("warn") == 1);
    c.ok("POST", "/api/v1/jobs/" + id + "/resume");
    REQUIRE(until(s, [&] { return c.state()["job"].is_null(); }, 120000));  // CalMag comes last: no stirring after it
    CHECK(c.state()["lastJob"]["state"] == "done");
  }
  SUBCASE("ending it names the part still missing") {
    c.ok("POST", "/api/v1/jobs/" + id + "/abort");
    const json last = c.state()["lastJob"];
    CHECK(last["state"] == "aborted");
    CHECK(last["message"]["key"] == "job.aborted_pair");
    REQUIRE(last["message"]["args"]["partners"].size() == 1);
    CHECK(last["message"]["args"]["partners"][0]["args"]["name"] == "Teil B");
  }
}

TEST_CASE("Scenario: catching up the last part of a guided mix ends the mix when only a rest below an accurate run is left (SD-057)") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  setupStage0(s, c);
  for (const char* id : {kA, kB, kC}) calibrate(s, c, id);
  s.world().fill(20, 0.02, 7.0);
  const std::string id = c.ok("POST", "/api/v1/mix/start", {{"recipe", "wachstum"}, {"waterL", 20}, {"guided", true}})["job"]["id"];
  for (int part = 0; part < 2; ++part) {  // Teil A and Teil B, stirred
    REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "waiting_user"; }, 120000));
    c.ok("POST", "/api/v1/jobs/" + id + "/continue");
  }
  // CalMag, the last part, is cut about half a second before its end
  sim::Cap* cap = s.world().cap(kC);
  REQUIRE(until(s, [&] { return cap->state == 1 && cap->requested > 1000 && cap->elapsed >= cap->requested - 500; }, 120000, 100));
  cap->blocked = true;
  REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "failed"; }, 60000, 100));
  cap->blocked = false;
  c.ok("POST", "/api/v1/jobs/" + id + "/resume");  // the rest counts as in, and the mix ends with it
  CHECK(c.state()["job"].is_null());
  CHECK(c.state()["lastJob"]["state"] == "done");
  CHECK(c.state()["lastJob"]["message"]["key"] == "mix.done");
  CHECK(findEvents(c, "mix").at(0)["title"]["key"] == "ev.mix.done");  // no "resumed" after it
}

TEST_CASE("Scenario: a stop for stirring between the parts of a pair names the part still missing, whatever comes between them (SD-057, RAT-054, RAT-080)") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  setupStage0(s, c);
  for (const char* id : {kA, kB, kC}) calibrate(s, c, id);
  c.ok("POST", "/api/v1/recipes",
       {{"id", "wachstum"},
        {"steps", {{{"canister", "teil-a"}, {"mlPerL", 2.0}}, {{"canister", "calmag"}, {"mlPerL", 0.5}}, {{"canister", "teil-b"}, {"mlPerL", 2.0}}}}});
  s.world().fill(20, 0.02, 7.0);
  const std::string id = c.ok("POST", "/api/v1/mix/start", {{"recipe", "wachstum"}, {"waterL", 20}, {"guided", true}})["job"]["id"];
  REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "waiting_user"; }, 120000));  // Teil A is in
  c.ok("POST", "/api/v1/jobs/" + id + "/continue");
  REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "waiting_user"; }, 120000));  // CalMag is in
  s.step(31 * 60 * 1000);
  const json job = c.state()["job"];
  REQUIRE(job["state"] == "failed");
  CHECK(job["message"]["key"] == "mix.stir_timeout_pair");
  CHECK(job["message"]["args"]["name"] == "CalMag");
  REQUIRE(job["message"]["args"]["partners"].size() == 1);
  CHECK(job["message"]["args"]["partners"][0]["args"]["name"] == "Teil B");
}

TEST_CASE("Scenario: a mix cut with nutrients in leaves EC to settle with the circulation pump first, and pH after EC: ended, stopped, failed or restarted (SD-057, SD-073, RAT-004, R6)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  demoCalm(s, c);
  c.ok("PATCH", "/api/v1/functions/refill", {{"enabled", false}});
  s.world().fill(40, 0.02, 7.5);  // EC low and pH high after the cut: both want to dose
  const auto relogin = [&] {
    c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
    s.step(5000);  // the devices are found again
  };
  std::string id;
  bool failedOpen = false;
  SUBCASE("ended after the first part") {
    id = startMix(c)["id"];
    REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "mixing"; }, 5 * 60 * 1000, 5000));
    c.ok("POST", "/api/v1/jobs/" + id + "/abort");
  }
  SUBCASE("ended while it mixes in at its end") {
    id = startMix(c)["id"];
    REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "settling"; }, 20 * 60 * 1000, 5000));
    c.ok("POST", "/api/v1/jobs/" + id + "/abort");
  }
  SUBCASE("stopped after the first part") {
    id = startMix(c)["id"];
    REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "mixing"; }, 5 * 60 * 1000, 5000));
    c.ok("POST", "/api/v1/stop");
    s.step(5000);
    c.ok("POST", "/api/v1/resume");
  }
  SUBCASE("stopped while it mixes in at its end") {
    id = startMix(c)["id"];
    REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "settling"; }, 20 * 60 * 1000, 5000));
    c.ok("POST", "/api/v1/stop");
    s.step(5000);
    c.ok("POST", "/api/v1/resume");
  }
  SUBCASE("a part fails after the first: the mix stays open") {
    s.world().cap(kB)->blocked = true;
    id = startMix(c)["id"];
    REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "failed"; }, 10 * 60 * 1000, 5000));
    CHECK(c.state()["job"]["message"]["key"] == "job.pair_failed");
    failedOpen = true;
  }
  SUBCASE("an internal error after the first part") {
    id = startMix(c)["id"];
    REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "mixing"; }, 5 * 60 * 1000, 5000));
    const std::string strip = s.world().netPlugs.front().id;
    {
      std::lock_guard<std::recursive_mutex> l(s.mutex());
      s.control("fault", {{"device", strip}, {"fault", "crash"}});
    }
    s.step(2000);
    {
      std::lock_guard<std::recursive_mutex> l(s.mutex());
      s.control("fault", {{"device", strip}, {"fault", "none"}});
    }
    s.step(2000);
    CHECK(c.state()["lastJob"]["message"]["key"] == "job.internal");
  }
  SUBCASE("restarted after the first part") {
    id = startMix(c)["id"];
    REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "mixing"; }, 5 * 60 * 1000, 5000));
    s.reboot();
    relogin();
    CHECK(logged(c, "ev.mix_reboot"));
  }
  SUBCASE("restarted while it mixes in at its end: said so") {
    id = startMix(c)["id"];
    REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "settling"; }, 20 * 60 * 1000, 5000));
    s.reboot();
    relogin();
    CHECK(logged(c, "ev.reboot.text_settling", "text"));
  }
  const json job = c.state()["job"];
  REQUIRE((failedOpen ? job["state"] == "failed" : job.is_null()));
  REQUIRE(until(s, [&] { return s.world().circulating(); }, 60000));  // asked for by EC control
  s.step(5000);
  CHECK(c.state()["controllers"]["ec"]["line"]["key"] == "ec.mixcut_settling");
  const auto doses = lastDoseId(c);
  s.step(14 * 60 * 1000);  // at least 15 min with the pump running
  CHECK(lastDoseId(c) == doses);
  CHECK(c.state()["dosing"].is_null());
  // Then EC first, as after every round: pH waits for it (RAT-004). A mix left open with half a pair
  // holds neither yet (#261), so that case shows only the wait.
  if (failedOpen) return;
  REQUIRE(until(s, [&] { return lastDoseId(c) != doses; }, 4 * 60 * 1000));
  CHECK(lastDose(c)["data"]["purpose"] == "ec");
}

TEST_CASE("Scenario: with EC control off, pH carries the mark of a cut mix itself, for at least its own settle time (SD-057)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  demoCalm(s, c);
  c.ok("PATCH", "/api/v1/functions/refill", {{"enabled", false}});
  c.ok("PATCH", "/api/v1/functions/ec_control", {{"enabled", false}});
  c.ok("PATCH", "/api/v1/functions/ph_control", {{"params", {{"settle_min", 25}}}});
  s.world().fill(40, 0.02, 7.0);
  // pH with no round of its own when the mix is cut
  REQUIRE(until(s, [&] {
    const json st = c.state();
    return st["dosing"].is_null() && st["controllers"]["ph"]["state"] != "working";
  }, 4 * 3600 * 1000, 5000));
  const std::string id = startMix(c)["id"];
  REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "settling"; }, 20 * 60 * 1000, 5000));
  c.ok("POST", "/api/v1/jobs/" + id + "/abort");
  REQUIRE(until(s, [&] { return s.world().circulating(); }, 60000));
  s.step(5000);
  CHECK(c.state()["controllers"]["ph"]["line"]["key"] == "ph.mixcut_settling");
  const auto doses = lastDoseId(c);
  s.step(24 * 60 * 1000);  // pH's 25 min, longer than the 15 min of a cut
  CHECK(lastDoseId(c) == doses);
  CHECK(c.state()["dosing"].is_null());
  CHECK(c.state()["controllers"]["ph"]["line"]["key"] == "ph.mixcut_settling");
  CHECK(until(s, [&] { return c.state()["controllers"]["ph"]["line"]["key"] != "ph.mixcut_settling"; }, 3 * 60 * 1000));  // then it decides
}

TEST_CASE("Scenario: after a cut mix both controllers wait for the longer settle time, also the one whose own is shorter (SD-057)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  demoCalm(s, c);
  c.ok("PATCH", "/api/v1/functions/refill", {{"enabled", false}});
  c.ok("PATCH", "/api/v1/functions/ec_control", {{"params", {{"settle_min", 4}}}});
  c.ok("PATCH", "/api/v1/functions/ph_control", {{"params", {{"settle_min", 25}}}});
  s.world().fill(40, 0.02, 7.0);
  const std::string id = startMix(c)["id"];
  REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "settling"; }, 40 * 60 * 1000, 5000));
  c.ok("POST", "/api/v1/jobs/" + id + "/abort");
  REQUIRE(until(s, [&] { return s.world().circulating(); }, 60000));
  const auto doses = lastDoseId(c);
  s.step(20 * 60 * 1000);  // past EC's 15 min of a cut, short of pH's 25
  CHECK(c.state()["controllers"]["ec"]["line"]["key"] == "ec.mixcut_settling");
  CHECK(lastDoseId(c) == doses);
  CHECK(c.state()["dosing"].is_null());
  CHECK(until(s, [&] { return c.state()["controllers"]["ec"]["line"]["key"].get<std::string>().rfind("ec.mixcut_", 0) != 0; }, 8 * 60 * 1000, 5000));  // then it decides
}

TEST_CASE("Scenario: a mix cut with EC and pH already on target logs no correction of their own (SD-057)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  demoCalm(s, c);
  c.ok("PATCH", "/api/v1/functions/refill", {{"enabled", false}});
  c.ok("POST", "/api/v1/grow/complete", json::object());  // the phase's targets would win over the functions' (R4)
  s.world().fill(20, 0.02, 7.0);
  const std::string id = startMix(c)["id"];
  REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "settling"; }, 20 * 60 * 1000, 5000));
  s.step(3 * 60 * 1000);  // mixed in: the readings are what the controllers will see
  const json r = c.state()["readings"];
  const double ec = r["tank.ec"]["value"], ph = r["tank.ph"]["value"];
  c.ok("PATCH", "/api/v1/functions/ec_control", {{"params", {{"ec_target", std::round(ec * 20) / 20}, {"ec_tolerance", 0.3}}}});
  c.ok("PATCH", "/api/v1/functions/ph_control", {{"params", {{"ph_target", std::round(ph * 20) / 20}, {"ph_tolerance", 0.3}}}});
  const auto before = c.state()["eventId"].get<std::uint64_t>();
  c.ok("POST", "/api/v1/jobs/" + id + "/abort");
  REQUIRE(until(s, [&] { return c.state()["controllers"]["ec"]["line"]["key"] == "ec.mixcut_settling"; }, 60000));
  SUBCASE("left to settle") {}
  SUBCASE("a Stop while it waits keeps it a wait") {
    s.step(5 * 60 * 1000);
    c.ok("POST", "/api/v1/stop");
    s.step(5000);
    c.ok("POST", "/api/v1/resume");
    REQUIRE(until(s, [&] { return c.state()["controllers"]["ec"]["line"]["key"] == "ec.mixcut_settling"; }, 60000));
  }
  REQUIRE(until(s, [&] { return c.state()["controllers"]["ec"]["line"]["key"] == "ec.ok"; }, 20 * 60 * 1000, 5000));
  s.step(60000);
  CHECK(c.state()["controllers"]["ph"]["line"]["key"] == "ph.ok");
  const json events = c.ok("GET", "/api/v1/events?limit=500&type=control")["events"];
  for (const auto& e : events) {
    if (e["id"].get<std::uint64_t>() <= before) continue;
    CHECK(e["title"]["key"] != "ev.ec.done");
    CHECK(e["title"]["key"] != "ev.ph.done");
  }
}

TEST_CASE("Szenario: Pumpenkappe am Hub-Port wird nicht freigegeben (PD-012)") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  s.world().plug(2, "pump_cap");
  s.step(2000);
  auto port = c.state()["ports"][1];
  CHECK(port["state"] == "rejected");
  CHECK(port["message"]["key"] == "port.cap_on_hub");
  CHECK(port["message"]["text"].get<std::string>().find("dosing block") != std::string::npos);
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

TEST_CASE("Scenario: while a job or EC control doses, a manual dose, a pump calibration and priming are refused as such, and nothing starts (SD-083)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  std::string pump;
  const gc::json cfg = c.ok("GET", "/api/v1/config");
  for (const auto& k : cfg["canisters"])
    if (k["id"] == "part-a") pump = k["pump"];
  REQUIRE_FALSE(pump.empty());
  const std::vector<std::pair<std::string, gc::json>> calls = {{"/api/v1/dose", {{"canister", "part-a"}, {"ml", 2}}},
                                                               {"/api/v1/pumps/" + pump + "/calibrate", {{"seconds", 30}}},
                                                               {"/api/v1/pumps/" + pump + "/prime", {{"seconds", 3}}}};
  // A job of the user: the demo starts with a mix
  const gc::json job = c.state()["job"];
  REQUIRE_FALSE(job.is_null());
  for (const auto& call : calls) {
    const std::string& path = call.first;  // no structured binding: CAPTURE reads it in a lambda (#309)
    const gc::json& body = call.second;
    CAPTURE(path);
    auto [st, res] = c.call("POST", path, body);
    CHECK(st == 409);
    CHECK(res["error"]["key"] == "job.busy");
    CHECK(c.state()["job"]["id"] == job["id"]);  // still the same job
  }
  REQUIRE(until(s, [&] {
    auto st = c.state();
    return st["job"].is_null() && st["dosing"].is_null();
  }, 4 * 3600 * 1000, 5000));
  s.world().fill(40, 0.6, 5.9);  // EC well below the target: an EC round follows
  REQUIRE(until(s, [&] { return !c.state()["dosing"].is_null(); }, 2 * 3600 * 1000, 500));
  REQUIRE(c.state()["dosing"]["purpose"] == "ec");
  // EC control dosing
  for (const auto& call : calls) {
    const std::string& path = call.first;  // no structured binding: CAPTURE reads it in a lambda (#309)
    const gc::json& body = call.second;
    CAPTURE(path);
    auto [st, res] = c.call("POST", path, body);
    CHECK(st == 409);
    CHECK(res["error"]["key"] == "job.dosing");
    CHECK(c.state()["job"].is_null());  // no job of its own was started
  }
}

TEST_CASE("Scenario: an EC round doses the recipe's parts but never a fixed addition (SD-057, RAT-085)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  s.step(60000);
  // CalMag in the demo's recipe becomes a fixed addition
  c.ok("POST", "/api/v1/recipes",
       {{"id", "veg"},
        {"steps",
         {{{"canister", "part-a"}, {"mlPerL", 2.0}}, {{"canister", "part-b"}, {"mlPerL", 2.0}}, {{"canister", "calmag"}, {"mlPerL", 0.6}, {"fixed", true}}}}});
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
    return dosed.count("part-a") && dosed.count("part-b") && c.state()["controllers"]["ec"]["line"]["key"] == "ec.settling";
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
  CHECK(st["controllers"]["ph"]["line"]["args"]["reason"]["key"] == "truth.jump_ph");  // a rise
  for (const auto& e : findEvents(c, "dose"))
    if (e["id"].get<std::uint64_t>() > before) CHECK(e["data"]["purpose"] != "ph");
}

TEST_CASE("Scenario: maintenance explains jumps only while it lasts; ended early, a jump locks again (RAT-042, #292)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  // Nothing else may explain a pH jump: a mix announces pH for 10 min after it ends, the inlet
  // until 5 min after it closes and an EC round until 1 min after its settle time
  constexpr gc::Ms kPoll = 5000;
  auto calm = [&] {
    auto st = c.state();
    return st["job"].is_null() && st["outputs"]["tank.inlet"] == false &&
           st["controllers"]["refill"]["state"] != "working" && st["controllers"]["ec"]["state"] == "idle" &&
           st["controllers"]["ph"]["state"] == "idle";
  };
  gc::Ms quiet = 0;
  REQUIRE(until(s, [&] {
    quiet = calm() ? quiet + kPoll : 0;
    return quiet > 11 * gc::kMinute;
  }, 8 * 3600 * 1000, kPoll));
  c.ok("POST", "/api/v1/maintenance", {{"minutes", 240}});
  s.step(60000);
  auto jump = [&](bool maintenanceOn) {
    if (maintenanceOn) {  // control waits in maintenance mode; nothing of its own runs
      const json st = c.state();
      REQUIRE((st["job"].is_null() && st["outputs"]["tank.inlet"] == false));
    } else {
      REQUIRE(calm());
    }
    s.control("fault", {{"device", "PHEC-3F2A91"}, {"fault", "jump"}});
    s.step(60000);
    return c.state();
  };
  SUBCASE("ended early: a jump 15 s later locks") {
    c.ok("POST", "/api/v1/maintenance", {{"minutes", 0}});
    s.step(15000);
    auto st = jump(false);
    CHECK(st["readings"]["tank.ph"]["quality"] == "jump");
    CHECK(st["latches"].contains("jump.tank.ph"));
  }
  SUBCASE("shortened to a minute that runs out: a jump right after its end locks, with nothing added") {
    c.ok("POST", "/api/v1/maintenance", {{"minutes", 1}});
    s.step(62000);
    const json before = c.state();
    REQUIRE(before["maintenanceUntil"].get<gc::Epoch>() <= before["now"].get<gc::Epoch>());  // ran out
    auto st = jump(false);
    CHECK(st["readings"]["tank.ph"]["quality"] == "jump");
    CHECK(st["latches"].contains("jump.tank.ph"));
  }
  SUBCASE("still on: a jump is explained") {
    s.step(2 * 60000);
    const json before = c.state();
    REQUIRE(before["maintenanceUntil"].get<gc::Epoch>() > before["now"].get<gc::Epoch>() + 60);
    auto st = jump(true);
    CHECK(st["readings"]["tank.ph"]["quality"] != "jump");
    CHECK_FALSE(st["latches"].contains("jump.tank.ph"));
  }
}

TEST_CASE("Scenario: a pH jump while the hub's own pH dose settles still locks the reading (RAT-042, RAT-044)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  // An own pH dose is sized to move pH by at most 0.3 (RAT-050), so it does not explain a jump
  // of more than 1.0 (RAT-042). A mix announces pH for 10 min after it ends, the inlet until
  // 5 min after it closes and an EC round until 1 min after its settle time (its EC until
  // about 10 min after its last busy tick): start a pH round only after 11 quiet minutes.
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
  CHECK(st["controllers"]["ph"]["line"]["args"]["reason"]["key"] == "truth.jump_ph");  // a rise
  for (const auto& e : findEvents(c, "dose"))
    if (e["id"].get<std::uint64_t>() > before) CHECK(e["data"]["purpose"] != "ph");
  // Every event of the run fits its template, so it keeps its key, and its language, after a restart (SD-083)
  const json events = c.ok("GET", "/api/v1/events?limit=500")["events"];  // held: a loop over a temporary's member dangles
  for (const auto& e : events)
    for (const char* part : {"title", "text"}) {
      const json& m = e[part];
      if (m.is_object() && !gc::jstr(m, "key").empty())
        CHECK_MESSAGE(gc::fitsTemplate(gc::jstr(m, "key"), m.contains("args") ? m["args"] : json::object()), m.dump());
    }
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

TEST_CASE("Scenario: a pump that is not running while a mix mixes in at its end: the card says so, and EC control waits for the pump afterwards (SD-057)") {
  // The plug goes offline, so the hub reads the pump as unknown and takes it as not running (R5)
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  const std::string plug = circulateOnPlug(s, c);  // also switches refill off
  demoCalm(s, c);
  s.world().fill(40, 0.02, 7.5);
  startMix(c);
  REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "settling"; }, 20 * 60 * 1000, 5000));
  const auto plugFault = [&](const char* fault) {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    s.control("fault", {{"device", plug}, {"fault", fault}});
  };
  SUBCASE("a dropout under 10 s is no pump standing still: the mix ends mixed") {
    plugFault("offline");
    for (int t = 0; t < 8; ++t) {
      s.step(1000);
      const json job = c.state()["job"];
      CHECK(job["message"]["key"] == "mix.settling");
      CHECK(job["pumpStands"] == false);
      CHECK(job["circulating"].is_null());  // what it reads: unknown (R5)
    }
    plugFault("none");
    REQUIRE(until(s, [&] { return c.state()["job"].is_null(); }, 10 * 60 * 1000, 5000));
    CHECK(c.state()["lastJob"]["message"]["key"] == "mix.done");
    const std::string line = c.state()["controllers"]["ec"]["line"]["key"];
    CHECK(line.rfind("ec.mixcut_", 0) != 0);
  }
  SUBCASE("off for 10 s: the card says so, says it again when the pump runs, and the mix ends not mixed yet") {
    plugFault("offline");
    s.step(12000);  // 10 s after the first tick that reads it unknown, not later
    REQUIRE(c.state()["job"]["message"]["key"] == "mix.settling_no_circ");
    CHECK(c.state()["job"]["pumpStands"] == true);
    CHECK(c.state()["job"]["circulating"].is_null());  // offline: unknown, never "off" (R5)
    plugFault("none");
    REQUIRE(until(s, [&] { return c.state()["job"]["message"]["key"] == "mix.settling_resumed"; }, 30000));
    CHECK(c.state()["job"]["pumpStands"] == false);
    CHECK(c.state()["job"]["circulating"] == true);
    CHECK_FALSE(c.state()["job"]["message"]["args"].contains("min"));  // no minutes that stop counting
    plugFault("offline");
    REQUIRE(until(s, [&] { return c.state()["job"].is_null(); }, 10 * 60 * 1000, 5000));  // the mix still ends
    CHECK(c.state()["lastJob"]["state"] == "done");
    CHECK(c.state()["lastJob"]["message"]["key"] == "mix.done_unmixed");
    const auto doses = lastDoseId(c);
    s.step(3 * 60 * 1000);
    CHECK(lastDoseId(c) == doses);
    CHECK(c.state()["dosing"].is_null());
    const std::string line = c.state()["controllers"]["ec"]["line"]["key"];
    CHECK(line.rfind("ec.mixcut_", 0) == 0);  // blocked, and why
    plugFault("none");
    REQUIRE(until(s, [&] { return s.world().circulating(); }, 2 * 60 * 1000));
    s.step(14 * 60 * 1000);
    CHECK(lastDoseId(c) == doses);
    CHECK(c.state()["dosing"].is_null());
    CHECK(c.state()["controllers"]["ec"]["line"]["key"] == "ec.mixcut_settling");
  }
}

TEST_CASE("Scenario: a locked controller waits for a person, not for the mixed tank: the mix's texts promise no control, and a cut marks none (SD-057)") {
  const auto dir = std::filesystem::temp_directory_path() / "gc-test-mix-locked-controller";
  std::filesystem::remove_all(dir);
  auto o = test::opts("demo");
  o.dataDir = dir.string();
  {
    sim::Simulation s(o);
    Client c{s};
    demoCalm(s, c);
    c.ok("PATCH", "/api/v1/functions/refill", {{"enabled", false}});
    c.ok("PATCH", "/api/v1/functions/ph_control", {{"enabled", false}});
  }
  {
    const auto file = dir / "state.json";
    json st = json::parse(std::ifstream(file));
    st["latches"]["ec.no_effect"] = {{"at", o.startEpoch}};  // as EC control leaves it when its doses do nothing
    std::ofstream(file) << st.dump();
  }
  sim::Simulation s(o);
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  s.step(5000);  // the devices are found again
  REQUIRE(c.state()["latches"].contains("ec.no_effect"));
  s.world().fill(40, 0.02, 7.0);
  const std::string id = startMix(c)["id"];
  REQUIRE(until(s, [&] { return c.state()["job"]["state"] == "settling"; }, 20 * 60 * 1000, 5000));
  CHECK(c.state()["job"]["message"]["key"] == "mix.settling_plain");
  c.ok("POST", "/api/v1/jobs/" + id + "/abort");
  s.step(60000);
  // No mark: once someone acknowledges the latch, EC goes on without settling a cut first
  c.ok("POST", "/api/v1/latches/ec.no_effect/ack");
  s.step(5000);
  const std::string line = c.state()["controllers"]["ec"]["line"]["key"];
  CHECK(line.rfind("ec.mixcut_", 0) != 0);
  std::filesystem::remove_all(dir);
}

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
  for (const auto& row : std::vector<std::tuple<json, double, double>>{{10, 3, 3.5}, {200, 3, 7.0}, {nullptr, 3, 3.5}}) {
    const json& capacity = std::get<0>(row);  // no structured binding: CAPTURE reads it in a lambda (#309)
    const double min = std::get<1>(row), back = std::get<2>(row);
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
  REQUIRE(until(s, [&] { return c.state()["job"].is_null(); }, 30 * 60 * 1000));  // the demo's mix, mixed in
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
  REQUIRE(until(s, [&] { return c.state()["job"].is_null(); }, 30 * 60 * 1000, 10000));  // the demo's mix, mixed in
  if (gc::isNum(start)) c.ok("PATCH", "/api/v1/functions/refill", {{"params", {{"start_below_l", start}, {"target_l", target}}}});
  // A change above the level's jump threshold (20 L) is made by hand in a minute of maintenance mode,
  // which explains it and then runs out; unexplained, it would lock the level for 15 min (RAT-042)
  const bool byHand = std::abs(s.world().tank.volumeL - litres) > 20.0;
  if (byHand) c.ok("POST", "/api/v1/maintenance", {{"minutes", 1}});
  s.world().fill(litres, 1.3, 5.9);
  if (byHand) s.step(61000);
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

TEST_CASE("Scenario: a cold refill trips no water temperature jump lock: the open inlet explains the change until 5 min after it closes (#272, RAT-042)") {
  // Demo from 2.3 L: 28 L of tap water at 10 °C (assumption) cool the tank by about 10 °C, more than
  // the jump threshold of 3 °C in 5 min. The open inlet announces the change, as it does for EC, pH
  // and level. The refill starts only after the demo's own calibrations at setup, which explain
  // every reading of the probe for 10 min.
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  demoAt(s, c, 2.3);
  c.ok("PATCH", "/api/v1/functions/refill", {{"enabled", false}});
  s.world().inletTempC = 10.0;
  s.step(16 * 60 * 1000, 5000);
  const auto waterJumps = [&] {
    int n = 0;
    for (const auto& e : findEvents(c, "block"))
      n += e["title"]["key"] == "ev.jump" && e["data"]["role"] == "tank.water_temp" ? 1 : 0;
    return n;
  };
  const auto quality = [&] { return c.state()["readings"]["tank.water_temp"]["quality"].get<std::string>(); };
  REQUIRE(waterJumps() == 0);
  REQUIRE(quality() == "ok");
  const double before = s.world().tank.temp;

  SUBCASE("the same fall with the valve closed is a jump") {
    s.world().tank.tempDev -= 10.0;
    REQUIRE(soon(s, [&] { return quality() == "jump"; }, 2 * 60 * 1000, 5000));
    CHECK(waterJumps() == 1);
  }

  SUBCASE("the open inlet explains the fall, and its announcement ends 5 min after the valve closes") {
    c.ok("PATCH", "/api/v1/functions/refill", {{"enabled", true}});
    bool locked = false;
    const auto watch = [&] { locked = locked || quality() == "jump"; };
    REQUIRE(soon(s, [&] { watch(); return inletOpen(c); }, 5 * 60 * 1000, 5000));
    REQUIRE(soon(s, [&] { watch(); return !inletOpen(c); }, 20 * 60 * 1000, 5000));
    CHECK(s.world().tank.temp < before - 3.0);  // the fall a jump lock would see
    CHECK_FALSE(locked);
    CHECK(waterJumps() == 0);
    // The refill's pause after its first step: 3 min after the valve closed a fall is still explained,
    // 7 min after it a fall is a jump. A jump inside the tail is not locked: an open point of the
    // jump lock (INVARIANTS M8)
    REQUIRE(refillLine(c)["key"] == "refill.check_step");
    s.step(3 * 60 * 1000, 5000);
    s.world().tank.tempDev -= 4.0;
    s.step(60 * 1000, 5000);
    CHECK(quality() == "ok");
    CHECK(waterJumps() == 0);
    s.step(3 * 60 * 1000, 5000);
    REQUIRE(refillLine(c)["key"] == "refill.check_step");  // the valve is still closed
    CHECK(quality() == "ok");
    CHECK(waterJumps() == 0);
    s.world().tank.tempDev -= 4.0;
    REQUIRE(soon(s, [&] { return quality() == "jump"; }, 60 * 1000, 5000));
    CHECK(waterJumps() == 1);
  }

  SUBCASE("an inlet opened by hand explains the fall too") {
    c.ok("POST", "/api/v1/roles/tank.inlet/switch", {{"on", true}});
    bool locked = false;
    const auto watch = [&] { locked = locked || quality() == "jump"; return false; };
    soon(s, watch, 4 * 60 * 1000, 5000);
    c.ok("POST", "/api/v1/roles/tank.inlet/switch", {{"on", false}});
    soon(s, watch, 10 * 60 * 1000, 5000);
    CHECK(s.world().tank.temp < before - 3.0);
    CHECK_FALSE(locked);
    CHECK(waterJumps() == 0);
  }
}

TEST_CASE("Scenario: a flood and its drain-back trip no level jump lock: the watering pump explains a fall of the level, and a rise back to where it started, while it runs and until 10 min after it stops (#324, SD-101, RAT-042)") {
  // Demo tank of 60 L at 55 L. In ebb and flow a flood can take more than the level's jump threshold
  // (20 L in 5 min) out of the tank within minutes, and the drain-back brings it back as fast. The twin's
  // irrigation draws no water, so the test changes the tank's volume itself; the numbers are assumptions
  // (docs/SIMULATOR.md). EC and pH control and the refill are off: watering waits for no mixing time
  // (SD-093), and an open inlet would explain the level.
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  demoAt(s, c, 55);
  for (const char* f : {"ec_control", "ph_control", "refill"}) c.ok("PATCH", std::string("/api/v1/functions/") + f, {{"enabled", false}});
  const std::string plug = irrigationPlug(s, c);
  s.step(16 * 60 * 1000, 5000);  // a round EC or pH began before has mixed in, and the level has been calm
  const auto quality = [&](const char* role) { return c.state()["readings"][role]["quality"].get<std::string>(); };
  const auto levelJumps = [&] {
    int n = 0;
    for (const auto& e : findEvents(c, "block"))
      n += e["title"]["key"] == "ev.jump" && e["data"]["role"] == "tank.level" ? 1 : 0;
    return n;
  };
  const auto pump = [&](bool on) { c.ok("POST", "/api/v1/roles/zone.irrigation_pump/switch", {{"on", on}}); };
  const auto pumpOn = [&] { return c.state()["outputs"]["zone.irrigation_pump"] == true; };
  const auto cutWhy = [&] {
    const json e = lastEvent(c, "block", "ev.irrigation.off");
    return e.is_null() ? std::string() : e["text"]["args"]["why"]["key"].get<std::string>();
  };
  REQUIRE(quality("tank.level") == "ok");
  REQUIRE(levelJumps() == 0);
  // A deep flood: 38 L out within 4 min, from 55 to 17 L
  const auto flood = [&](double litres, const std::function<void()>& each) {
    for (int i = 0; i < 48; ++i) {
      s.world().tank.volumeL -= litres / 48;
      s.step(5000, 5000);
      each();
    }
  };
  // Back with a time constant of 5 min (as SD-099 assumes), 63 % of it within the first 5 min
  const auto drainBack = [&](double litres, const std::function<void()>& each) {
    double back = litres;
    for (int i = 0; i < 12 * 25; ++i) {
      const double dV = back * (1 - std::exp(-5.0 / 300.0));
      back -= dV;
      s.world().tank.volumeL += dV;
      s.step(5000, 5000);
      each();
    }
  };
  const auto nothing = [] {};

  SUBCASE("the same flood with the pump off is a jump") {
    flood(38, nothing);
    REQUIRE(soon(s, [&] { return quality("tank.level") == "jump"; }, 60 * 1000, 5000));
    CHECK(levelJumps() == 1);
  }

  SUBCASE("the same drain-back with the pump off is a jump") {
    c.ok("POST", "/api/v1/maintenance", {{"minutes", 1}});  // to 17 L by hand, explained
    s.world().tank.volumeL = 17.0;
    s.step(2 * 60 * 1000, 5000);
    REQUIRE(quality("tank.level") == "ok");
    drainBack(36, nothing);  // 23 L within the first 5 min
    CHECK(levelJumps() == 1);
  }

  SUBCASE("a flood and its drain-back lock nothing, also with a lagging reading, and the pump runs on until switched off") {
    // The real reading lags 1.5–2 min (RAT-038); the twin's follows at once unless a test sets a lag
    for (const double lagS : {0.0, 105.0}) {
      CAPTURE(lagS);
      s.world().levelLagS = lagS;
      const double before = s.world().tank.volumeL;
      pump(true);
      bool locked = false, cut = false;
      const auto watch = [&] {
        locked = locked || quality("tank.level") != "ok";
        cut = cut || !pumpOn();
      };
      flood(38, watch);
      soon(s, [&] { watch(); return false; }, 60 * 1000, 5000);  // flooded, the table holds the water
      CHECK_FALSE(cut);  // without the fix for #324 the gateway cut it as "level invalid" mid-flood
      pump(false);
      drainBack(36, [&] { locked = locked || quality("tank.level") != "ok"; });  // 2 L stay in the substrate
      CHECK(s.world().tank.volumeL == doctest::Approx(before - 2.2).epsilon(0.01));
      CHECK_FALSE(locked);
      CHECK(levelJumps() == 0);
      CHECK(cutWhy().empty());
    }
  }

  SUBCASE("a level that jumps up while the pump runs still locks, and the pump goes off as for a reading not valid") {
    pump(true);
    flood(38, nothing);
    REQUIRE(quality("tank.level") == "ok");
    REQUIRE(levelJumps() == 0);
    s.world().tank.volumeL += 25.0;  // a sensor stuck high reads the same: 25 L above the lowest reading of the run
    REQUIRE(soon(s, [&] { return quality("tank.level") == "jump"; }, 60 * 1000, 5000));
    CHECK(levelJumps() == 1);
    REQUIRE(soon(s, [&] { return !pumpOn(); }, 60 * 1000, 5000));
    CHECK(cutWhy() == "why.level_invalid");
  }

  SUBCASE("after the stop, a rise back to just below where the run started is explained") {
    pump(true);
    flood(38, nothing);
    pump(false);
    s.step(60 * 1000, 5000);
    s.world().tank.volumeL += 36.0;  // at once, to 53 L
    s.step(60 * 1000, 5000);
    CHECK(quality("tank.level") == "ok");
    CHECK(levelJumps() == 0);
  }

  SUBCASE("after the stop, a rise past where the run started is a jump: drain-back cannot bring more than the flood took") {
    pump(true);
    flood(38, nothing);
    pump(false);
    s.step(60 * 1000, 5000);
    REQUIRE(quality("tank.level") == "ok");
    REQUIRE(levelJumps() == 0);
    s.world().tank.volumeL += 42.0;  // at once, to 59 L, 2 L past the bound of 55 + 2 L
    REQUIRE(soon(s, [&] { return quality("tank.level") == "jump"; }, 60 * 1000, 5000));
    CHECK(levelJumps() == 1);
  }

  SUBCASE("a run within the 10 min continues the series: the drain-back of both may rise back to where the first started") {
    pump(true);
    flood(38, nothing);  // from 55 to 17 L
    pump(false);
    s.step(60 * 1000, 5000);
    pump(true);  // from 17 L
    s.step(30000, 5000);
    pump(false);
    s.step(60 * 1000, 5000);
    REQUIRE(quality("tank.level") == "ok");
    s.world().tank.volumeL += 36.0;  // at once, to 53 L: far above where the second run started
    s.step(60 * 1000, 5000);
    CHECK(quality("tank.level") == "ok");
    CHECK(levelJumps() == 0);
  }

  SUBCASE("a run after the 10 min starts a new series: a rise past where it started is a jump") {
    pump(true);  // from 55 L
    s.step(30000, 5000);
    pump(false);
    s.step(11 * 60 * 1000, 5000);
    c.ok("POST", "/api/v1/maintenance", {{"minutes", 1}});  // to 30 L by hand, explained
    s.world().tank.volumeL = 30.0;
    s.step(2 * 60 * 1000, 5000);
    REQUIRE(quality("tank.level") == "ok");
    pump(true);  // from 30 L
    s.step(30000, 5000);
    pump(false);
    s.step(60 * 1000, 5000);
    s.world().tank.volumeL += 25.0;  // to 55 L, past 30 + 2 L
    REQUIRE(soon(s, [&] { return quality("tank.level") == "jump"; }, 60 * 1000, 5000));
    CHECK(levelJumps() == 1);
  }

  SUBCASE("drip shots every 8 min: only runs started within the last 30 min count, so a reading back at where the first shots started is a jump") {
    for (int k = 0; k < 6; ++k) {  // from 55 L down to 25 L, 5 L a shot; nothing drains back
      pump(true);
      for (int i = 0; i < 6; ++i) {
        s.world().tank.volumeL -= 5.0 / 6;
        s.step(5000, 5000);
      }
      pump(false);
      s.step(8 * 60 * 1000 - 30000, 5000);
    }
    REQUIRE(quality("tank.level") == "ok");
    REQUIRE(levelJumps() == 0);
    // 48 min after the first shot: the shots of the last 30 min started at 40 L at most
    s.world().tank.volumeL += 25.0;  // to 50 L
    REQUIRE(soon(s, [&] { return quality("tank.level") == "jump"; }, 60 * 1000, 5000));
    CHECK(levelJumps() == 1);
  }

  SUBCASE("the pump explains while it runs and 10 min after it stops: a fall 9 min after a 5 min run is explained, a rise 11 min after it is a jump") {
    // 9 min after the stop is 14 min after the start, so the announcement is renewed while it runs. The
    // 10 min count from the stop on the hub's clock; a reading that lags moves the edge.
    pump(true);
    s.step(5 * 60 * 1000, 5000);
    pump(false);
    s.step(9 * 60 * 1000, 5000);
    s.world().tank.volumeL -= 25.0;
    s.step(60 * 1000, 5000);
    CHECK(quality("tank.level") == "ok");
    CHECK(levelJumps() == 0);
    s.step(60 * 1000, 5000);
    s.world().tank.volumeL += 25.0;
    REQUIRE(soon(s, [&] { return quality("tank.level") == "jump"; }, 60 * 1000, 5000));
    CHECK(levelJumps() == 1);
  }

  SUBCASE("a run that starts with the level not usable explains nothing for its series, also not the runs before it") {
    pump(true);  // from 55 L
    s.step(30000, 5000);
    pump(false);
    s.step(10000, 5000);
    s.control("fault", {{"device", "LVL-77B210"}, {"fault", "offline"}});
    REQUIRE(soon(s, [&] { return quality("tank.level") != "ok"; }, 3 * 60 * 1000, 5000));
    s.step(60 * 1000, 5000);  // a run of its own: one within a minute of the last start is the same run
    {  // pressed on at the plug: the gateway switches it off again, as the level is not valid
      std::lock_guard<std::recursive_mutex> l(s.mutex());
      s.world().netPlug(plug)->outlets[0].on = true;
    }
    REQUIRE(soon(s, [&] { return cutWhy() == "why.level_invalid"; }, 60 * 1000, 5000));
    REQUIRE_FALSE(pumpOn());
    s.control("fault", {{"device", "LVL-77B210"}, {"fault", "none"}});
    REQUIRE(soon(s, [&] { return quality("tank.level") == "ok"; }, 2 * 60 * 1000, 5000));
    REQUIRE(levelJumps() == 0);
    flood(38, nothing);  // within the 10 min of both runs
    REQUIRE(soon(s, [&] { return quality("tank.level") == "jump"; }, 60 * 1000, 5000));
    CHECK(levelJumps() == 1);
  }

  SUBCASE("a pump whose state is not known explains nothing") {
    pump(true);
    s.step(30000, 5000);
    s.control("fault", {{"device", plug}, {"fault", "offline"}});
    s.step(11 * 60 * 1000, 5000);  // what it explained while it read as on has run out
    REQUIRE(c.state()["outputs"]["zone.irrigation_pump"].is_null());
    flood(38, nothing);
    REQUIRE(soon(s, [&] { return quality("tank.level") == "jump"; }, 60 * 1000, 5000));
    CHECK(levelJumps() == 1);
  }

  SUBCASE("the pump explains only the level: an EC, pH or water temperature jump while it runs and after it still locks") {
    pump(true);
    s.step(30000, 5000);
    s.world().tank.ec += 1.0;
    REQUIRE(soon(s, [&] { return quality("tank.ec") == "jump"; }, 60 * 1000, 5000));
    // A probe without a usable reading stops watering (SD-093)
    REQUIRE(soon(s, [&] { return !pumpOn(); }, 60 * 1000, 5000));
    CHECK_FALSE(lastEvent(c, "block", "ev.irrigation.water").is_null());
    s.world().tank.ph += 1.5;
    REQUIRE(soon(s, [&] { return quality("tank.ph") == "jump"; }, 5 * 60 * 1000, 5000));
    s.world().tank.tempDev += 4.0;
    REQUIRE(soon(s, [&] { return quality("tank.water_temp") == "jump"; }, 60 * 1000, 5000));
    CHECK(quality("tank.level") == "ok");
  }

  SUBCASE("a flood that ran the circulation pump dry: after the drain-back its latch releases itself; a reading past where the run started holds it for a person (SD-067)") {
    c.ok("PATCH", "/api/v1/functions/circulation", {{"params", {{"mode", "always"}}}});
    REQUIRE(soon(s, [&] { return c.state()["outputs"]["tank.circulation"] == true; }, 60 * 1000, 5000));
    pump(true);
    flood(53, nothing);  // to 2 L, below the minimum of 3 L
    REQUIRE(c.state()["latches"].contains("circulation.dry"));
    CHECK(cutWhy() == "why.irrigation_low");  // the minimum's own cut, the level still valid
    SUBCASE("the drain-back") {
      drainBack(51, nothing);
      CHECK(levelJumps() == 0);
      CHECK_FALSE(c.state()["latches"].contains("circulation.dry"));
      CHECK_FALSE(lastEvent(c, "unblock", "ev.circ.dry.released").is_null());
    }
    SUBCASE("a reading past where the run started") {
      s.step(60 * 1000, 5000);
      s.world().tank.volumeL = 59.0;
      s.step(30 * 60 * 1000, 5000);
      CHECK(levelJumps() == 1);
      const json st = c.state();
      REQUIRE(st["latches"].contains("circulation.dry"));
      CHECK(st["latches"]["circulation.dry"]["hold"] == "jump");
    }
  }
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
    s.step(4 * 60 * 1000, 5000);  // from 3 min after the stop it waits for the level to stop rising (SD-099)
    CHECK(refillLine(c)["key"] == "refill.drain_back");
    CHECK_FALSE(inletOpen(c));
    REQUIRE(soon(s, [&] { return inletOpen(c); }, 3 * 60 * 1000, 5000));
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
    REQUIRE(soon(s, [&] { return inletOpen(c); }, 7 * 60 * 1000, 5000));  // the first verdict on the level comes 6 min after the stop (SD-099)
  }
  SUBCASE("after maintenance the fill waits its full settling time, however long irrigation kept it waiting before") {
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    demoAt(s, c, 28, 25, 30);
    for (const char* f : {"ec_control", "ph_control"}) c.ok("PATCH", std::string("/api/v1/functions/") + f, {{"enabled", false}});
    irrigationPlug(s, c);
    s.step(10 * 60 * 1000, 5000);  // a round EC or pH began before has mixed in: watering waits for it (SD-093)
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
  SUBCASE("ebb and flow: while water drains back the fill waits until the level stops rising, so it does not overfill (#294, SD-099)") {
    // The twin's own level reading follows at once; the real one lags 1.5–2 min (RAT-038). A sensor that
    // reports every 45 s never counts as calm and waits the full half hour.
    for (const auto& timing : std::vector<std::pair<double, int>>{{0.0, 5}, {105.0, 5}, {0.0, 45}}) {
      const double lagS = timing.first;  // no structured binding: CAPTURE reads it in a lambda (#309)
      const int reportS = timing.second;
      CAPTURE(lagS);
      CAPTURE(reportS);
      sim::Simulation s(test::opts("demo"));
      Client c{s};
      demoAt(s, c, 28, 25, 30);
      for (const char* f : {"ec_control", "ph_control"}) c.ok("PATCH", std::string("/api/v1/functions/") + f, {{"enabled", false}});
      irrigationPlug(s, c);
      s.world().levelLagS = lagS;
      s.world().levelSampleMs = reportS * 1000;
      s.step(10 * 60 * 1000, 5000);  // a round EC or pH began before has mixed in: watering waits for it (SD-093)
      c.ok("POST", "/api/v1/roles/zone.irrigation_pump/switch", {{"on", true}});
      for (int i = 0; i < 24; ++i) {  // the flood takes 12 L in 2 min (the twin's irrigation draws none itself)
        s.world().tank.volumeL -= 0.5;
        s.step(5000, 5000);
      }
      c.ok("POST", "/api/v1/roles/zone.irrigation_pump/switch", {{"on", false}});
      // 8 L drain back, with a time constant of 4 min (an assumption); 2 L stay in the substrate, so the
      // tank ends below the start level and is filled
      double back = 8.0;
      bool openedEarly = false, opened = false;
      for (int i = 0; i < 12 * 25; ++i) {
        const double dV = back * (1 - std::exp(-5.0 / 240.0));
        back -= dV;
        s.world().tank.volumeL += dV;
        s.step(5000, 5000);
        opened = opened || inletOpen(c);
        openedEarly = openedEarly || (inletOpen(c) && back > 1.5);
        if (i == 12 * 5) CHECK(refillLine(c)["key"] == "refill.drain_back");  // 5 min after the pump stopped
      }
      CHECK_FALSE(openedEarly);  // without SD-099 it opened 3 min after the pump stopped, with 3.8 L still to come
      if (reportS > 30) CHECK_FALSE(opened);  // a value held between reports is not taken as calm
      REQUIRE(soon(s, [&] { return refills(c) >= 1 && !inletOpen(c); }, 30 * 60 * 1000, 10000));
      CHECK(s.world().tank.volumeL <= 30.0 + 1.0);
      CHECK_FALSE(inletLatched(c));
    }
  }
  SUBCASE("run-to-waste drip: a level that falls after irrigation is calm, the fill plans at the first verdict 6 min after the pump stopped (SD-099)") {
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    demoAt(s, c, 28, 25, 30);
    for (const char* f : {"ec_control", "ph_control"}) c.ok("PATCH", std::string("/api/v1/functions/") + f, {{"enabled", false}});
    irrigationPlug(s, c);
    s.step(10 * 60 * 1000, 5000);
    c.ok("POST", "/api/v1/roles/zone.irrigation_pump/switch", {{"on", true}});
    s.world().tank.volumeL = 20.0;  // what the drippers took
    s.step(60000, 5000);
    c.ok("POST", "/api/v1/roles/zone.irrigation_pump/switch", {{"on", false}});
    int seconds = 0;
    for (; seconds < 8 * 60 && !inletOpen(c); seconds += 5) {
      s.world().tank.volumeL -= 0.04;  // the last drops still leave: 0.5 L a minute
      s.step(5000, 5000);
    }
    CHECK(seconds >= 6 * 60 - 5);
    CHECK(seconds <= 6 * 60 + 30);
  }
  SUBCASE("a level still rising half an hour after irrigation: the fill goes ahead, marked as irrigated (SD-099)") {
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    demoAt(s, c, 28, 25, 30);
    for (const char* f : {"ec_control", "ph_control"}) c.ok("PATCH", std::string("/api/v1/functions/") + f, {{"enabled", false}});
    irrigationPlug(s, c);
    s.step(10 * 60 * 1000, 5000);
    c.ok("POST", "/api/v1/roles/zone.irrigation_pump/switch", {{"on", true}});
    s.world().tank.volumeL = 12.0;  // the flood, well below the start level
    s.step(60000, 5000);
    c.ok("POST", "/api/v1/roles/zone.irrigation_pump/switch", {{"on", false}});
    bool opened = false;
    int minutes = 0;
    for (; minutes < 40 && !opened; ++minutes) {  // 0.35 L a minute come back, well more than calm allows
      for (int i = 0; i < 12 && !opened; ++i) {
        s.world().tank.volumeL += 0.35 / 12;
        s.step(5000, 5000);
        opened = inletOpen(c) || refills(c) > 0;
      }
      if (minutes == 10) CHECK(refillLine(c)["key"] == "refill.drain_back");
    }
    CHECK(minutes >= 29);  // it waited while the level rose
    CHECK(minutes <= 31);  // at most half an hour from the start of the wait
    REQUIRE(opened);
    freezeLevel(s);  // the fill shows no rise: as it is marked, it is not judged as "did not rise"
    REQUIRE(soon(s, [&] { return !lastEvent(c, "tank", "ev.refill.unjudged").is_null() || inletLatched(c); }, 30 * 60 * 1000, 10000));
    CHECK(lastEvent(c, "tank", "ev.refill.unjudged")["text"]["key"] == "refill.unjudged.irrigation");
  }
  SUBCASE("a Stop during the drain-back: the half hour counts from the pump's stop, and the fill is marked as irrigated (SD-099)") {
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    demoAt(s, c, 28, 25, 30);
    for (const char* f : {"ec_control", "ph_control"}) c.ok("PATCH", std::string("/api/v1/functions/") + f, {{"enabled", false}});
    irrigationPlug(s, c);
    s.step(10 * 60 * 1000, 5000);
    c.ok("POST", "/api/v1/roles/zone.irrigation_pump/switch", {{"on", true}});
    s.world().tank.volumeL = 12.0;
    s.step(60000, 5000);
    c.ok("POST", "/api/v1/roles/zone.irrigation_pump/switch", {{"on", false}});
    const auto rise = [&](int minutes) {  // 0.35 L a minute come back, well more than calm allows
      for (int i = 0; i < minutes * 12 && !inletOpen(c); ++i) {
        s.world().tank.volumeL += 0.35 / 12;
        s.step(5000, 5000);
      }
    };
    rise(2);
    c.ok("POST", "/api/v1/stop");
    rise(5);
    c.ok("POST", "/api/v1/resume");
    rise(10);  // the plain 3 min after the Stop, then the wait for the level, which began only now
    CHECK(refillLine(c)["key"] == "refill.drain_back");
    rise(12);  // 29 min after the pump stopped
    CHECK_FALSE(inletOpen(c));
    rise(3);
    REQUIRE(inletOpen(c));  // 30 min after the pump's stop, not 30 min after the wait began
    freezeLevel(s);
    REQUIRE(soon(s, [&] { return !lastEvent(c, "tank", "ev.refill.unjudged").is_null() || inletLatched(c); }, 30 * 60 * 1000, 10000));
    CHECK(lastEvent(c, "tank", "ev.refill.unjudged")["text"]["key"] == "refill.unjudged.irrigation");
  }
  SUBCASE("irrigation every few minutes keeps the first fill waiting at most half an hour, and EC with it") {
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    demoAt(s, c, 28, 25, 30);
    s.world().fill(28, 1.4, 5.8);  // EC and pH on target: no round runs when the irrigation begins
    irrigationPlug(s, c);
    REQUIRE(soon(s, [&] {
      const json ctl = c.state()["controllers"];
      return ctl["ec"]["state"] == "idle" && ctl["ph"]["state"] == "idle";
    }, 60 * 60 * 1000, 5000));
    s.world().fill(20, 0.9, 5.9);  // EC well below its target too
    const size_t doses = findEvents(c, "dose").size();
    bool opened = false;
    for (int i = 0; i < 18 && !opened; ++i) {  // on 1 min, off 1 min
      c.ok("POST", "/api/v1/roles/zone.irrigation_pump/switch", {{"on", true}});
      s.step(60000, 5000);
      c.ok("POST", "/api/v1/roles/zone.irrigation_pump/switch", {{"on", false}});
      s.step(60000, 5000);
      opened = inletOpen(c) || refills(c) > 0;
      if (i < 13) {
        CHECK_FALSE(opened);  // waits while the reading does not settle
        CHECK(c.state()["controllers"]["ec"]["line"]["key"] == "ec.wait_refill");  // the fill would dilute a round (SD-090)
      }
    }
    CHECK(opened);
    CHECK(findEvents(c, "dose").size() == doses);
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

TEST_CASE("Scenario: after an unknown run-time state the refill waits for a person to release the inlet, not for Resume alone, and the latch never releases itself (RAT-031, RAT-063, SD-087)") {
  struct Dir {
    std::filesystem::path path = std::filesystem::temp_directory_path() / ("gc-test-unknown-inlet-" + std::to_string(std::random_device{}()));
    ~Dir() { std::filesystem::remove_all(path); }
  } d;
  auto o = test::opts("demo");
  o.dataDir = d.path.string();
  {
    sim::Simulation s(o);  // a hub that ran, with its inlet
    Client c{s};
    demoAt(s, c, 40.0);
    s.step(60000, 5000);
  }
  const auto file = d.path / "state.json";
  // one that cannot be read: tests/core/test_clock.cpp (the simulator then runs in memory, SD-074)
  SUBCASE("broken") { std::ofstream(file) << "[1, 2"; }
  SUBCASE("missing (SD-086)") { std::filesystem::remove(file); }
  o.startEpoch += 600;
  sim::Simulation s(o);
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  REQUIRE(inletLatched(c));
  CHECK(latchWhy(c) == "why.state_unknown");
  c.ok("POST", "/api/v1/resume");
  s.world().fill(2.3, 1.3, 5.9);  // nearly empty: a refill would start
  CHECK_FALSE(soon(s, [&] { return inletOpen(c); }, 30 * 60 * 1000, 5000));
  CHECK(refillLine(c)["key"] == "refill.latched_unknown");
  CHECK(latchWhy(c) == "why.state_unknown");  // with a valid level all along: no self-release (SD-067)
  // opened at the valve itself: closed again, naming the reason (RAT-032)
  s.world().out[1] = true;
  s.step(5000);
  CHECK_FALSE(s.world().out[1]);
  const json cut = lastEvent(c, "alarm", "ev.inlet.cutoff");
  REQUIRE(cut.is_object());
  CHECK(cut["text"]["key"] == "ev.inlet.cutoff.latched");
  CHECK(cut["text"]["args"]["why"]["key"] == "why.state_unknown");
  // a person releases it: the refill waits for the level to settle, then fills (SD-088)
  c.ok("POST", "/api/v1/latches/inlet.fault/ack");
  s.step(5000);
  CHECK(refillLine(c)["key"] == "refill.settle");
  CHECK_FALSE(soon(s, [&] { return inletOpen(c); }, 2 * 60 * 1000, 5000));
  CHECK(soon(s, [&] { return inletOpen(c); }, 20 * 60 * 1000, 5000));
}

TEST_CASE("Scenario: the first fill after a person releases the inlet waits for the level to settle and plans from a level read after the release (#288, RAT-031, RAT-038, SD-088)") {
  // Demo: 60 L tank, refill below 30 L to 45 L at 2 L/min, the valve open at most 15 min
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  demoAt(s, c, 10.0);
  // a valve opened by hand stays open too long: the inlet latches, at about 40 L
  c.ok("PATCH", "/api/v1/functions/refill", {{"enabled", false}});
  c.ok("POST", "/api/v1/roles/tank.inlet/switch", {{"on", true}});
  REQUIRE(soon(s, [&] { return inletLatched(c); }, 20 * 60 * 1000, 5000));
  CHECK(latchWhy(c) == "why.open_too_long");
  c.ok("PATCH", "/api/v1/functions/refill", {{"enabled", true}});
  s.world().fill(25.0, 1.3, 5.9);  // below the start: a refill is due once released
  s.step(15 * 60 * 1000, 5000);     // long past any wait after the valve was open by hand
  REQUIRE_FALSE(inletOpen(c));
  // the person checks the tank and releases the inlet
  c.ok("POST", "/api/v1/latches/inlet.fault/ack");
  s.step(5000);
  CHECK_FALSE(inletOpen(c));
  CHECK(refillLine(c)["key"] == "refill.settle");  // as after a Stop: a person may be at the tank
  SUBCASE("the tank bailed a minute after the release: planned from the new level, no false \"did not rise\"") {
    s.step(55000, 5000);
    REQUIRE_FALSE(inletOpen(c));
    s.world().fill(10.0, 1.3, 5.9);
    CHECK_FALSE(soon(s, [&] { return inletOpen(c); }, 90 * 1000, 5000));
    REQUIRE(soon(s, [&] { return inletOpen(c); }, 2 * 60 * 1000, 5000));
    const json line = refillLine(c);
    CHECK(line["key"] == "refill.filling_step");
    CHECK(line["args"]["from"].get<double>() == doctest::Approx(10.0).epsilon(0.1));  // not the 25 L before
    REQUIRE(soon(s, [&] { return refillLine(c)["key"] == "refill.ok"; }, 60 * 60 * 1000, 5000));
    CHECK_FALSE(inletLatched(c));
    CHECK(s.world().tank.volumeL == doctest::Approx(45.0).epsilon(0.05));
  }
  SUBCASE("the tank filled by hand right after the release: no fill on top of it") {
    s.world().fill(40.0, 1.3, 5.9);
    CHECK_FALSE(soon(s, [&] { return inletOpen(c); }, 10 * 60 * 1000, 5000));
    CHECK(refillLine(c)["key"] == "refill.ok");
    CHECK_FALSE(inletLatched(c));
    CHECK(s.world().tank.volumeL == doctest::Approx(40.0).epsilon(0.05));
  }
}

TEST_CASE("Scenario: while a due fill waits for the level to settle, EC and pH wait too, so the fill dilutes no round (#289, RAT-055, SD-090)") {
  // Demo: refill below 30 L to 45 L, EC target 1.4
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  demoAt(s, c, 40.0);
  c.ok("PATCH", "/api/v1/functions/circulation", {{"params", {{"mode", "always"}}}});
  s.world().fill(40.0, 1.4, 5.8);  // EC and pH on target: no round runs, or is cut, when the Stop comes
  REQUIRE(soon(s, [&] {
    const json ctl = c.state()["controllers"];
    return ctl["ec"]["state"] == "idle" && ctl["ph"]["state"] == "idle";
  }, 60 * 60 * 1000, 5000));
  c.ok("POST", "/api/v1/stop");
  // a person at the tank: below the refill start, and EC or pH off its target
  std::string ctl;
  SUBCASE("EC well below its target") {
    s.world().fill(25.0, 0.9, 5.9);
    ctl = "ec";
  }
  SUBCASE("pH well above its target") {
    s.world().fill(25.0, 1.4, 7.0);
    ctl = "ph";
  }
  s.step(10 * 60 * 1000, 5000);
  const size_t doses = findEvents(c, "dose").size();
  c.ok("POST", "/api/v1/resume");
  s.step(5000);
  REQUIRE(refillLine(c)["key"] == "refill.settle");  // the fill waits for the level, as after any Stop
  CHECK(c.state()["controllers"][ctl]["line"]["key"] == ctl + ".wait_refill");
  REQUIRE(soon(s, [&] { return inletOpen(c); }, 5 * 60 * 1000, 5000));
  CHECK(findEvents(c, "dose").size() == doses);  // nothing dosed before the fill opened
}

TEST_CASE("Scenario: a fill that falls due while EC settles a round waits for that round, and EC starts no next round before the fill (#289, RAT-055, RAT-057, SD-090)") {
  // Demo: EC target 1.4; at EC 0.9 a correction takes more than one round
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  demoAt(s, c, 40.0);
  c.ok("PATCH", "/api/v1/functions/circulation", {{"params", {{"mode", "always"}}}});
  s.world().fill(40.0, 0.9, 5.9);
  REQUIRE(soon(s, [&] { return c.state()["controllers"]["ec"]["line"]["key"] == "ec.settling"; }, 30 * 60 * 1000, 5000));
  const json effectBefore = c.state()["effects"]["ec"];
  const size_t doses = findEvents(c, "dose").size();
  // a fill falls due while the round settles (the start level raised above the level)
  c.ok("PATCH", "/api/v1/functions/refill", {{"params", {{"start_below_l", 45}, {"target_l", 48}}}});
  s.step(5000);
  CHECK(refillLine(c)["key"] == "refill.dosing");  // the round is not cut: the fill waits for it
  REQUIRE(soon(s, [&] { return inletOpen(c); }, 30 * 60 * 1000, 5000));
  CHECK(findEvents(c, "dose").size() == doses);  // no next round between the settled one and the fill
  CHECK(c.state()["effects"]["ec"] != effectBefore);  // the settled round was learned from (RAT-057)
}

namespace {
// Demo at 40 L with the refill off, the circulation pump always on and a watering pump on a plug, 11 min
// after binding it: from then a round has not watered (SD-103)
struct Watered {
  std::filesystem::path dir;
  std::unique_ptr<sim::Simulation> sp;
  std::unique_ptr<Client> cp;
  std::string plug;
  Watered() {
    dir = std::filesystem::temp_directory_path() / ("gc-test-watered-" + std::to_string(std::random_device{}()));
    std::filesystem::remove_all(dir);
    auto o = test::opts("demo");
    o.dataDir = dir.string();
    sp = std::make_unique<sim::Simulation>(o);
    cp = std::make_unique<Client>(Client{*sp});
    demoAt(*sp, *cp, 40.0);
    cp->ok("PATCH", "/api/v1/functions/refill", {{"enabled", false}});
    cp->ok("PATCH", "/api/v1/functions/circulation", {{"params", {{"mode", "always"}}}});
    plug = irrigationPlug(*sp, *cp);
    sp->step(11 * 60 * 1000, 5000);
  }
  ~Watered() {
    cp.reset();
    sp.reset();
    std::filesystem::remove_all(dir);
  }
  sim::Simulation& s() { return *sp; }
  Client& c() { return *cp; }
  json line(const char* fn) { return c().state()["controllers"][fn]["line"]["key"]; }
  json effect(const char* kind) { return c().state()["effects"][kind]; }
  int count(const char* type, const char* title) {
    int n = 0;
    for (const auto& e : findEvents(c(), type)) n += e["title"]["key"] == title ? 1 : 0;
    return n;
  }
  // Pressed at the plug: the hub reads it as on, then the gateway cuts it during the mixing time (SD-093)
  void press() {
    const int cuts = count("block", "ev.irrigation.dose");
    {
      std::lock_guard<std::recursive_mutex> l(s().mutex());
      auto& o = s().world().netPlug(plug)->outlets[0];
      o.on = true;
      o.onSince = s().world().now();  // as its button does, so the plug's own auto-off counts from now
      ++o.switchOns;
    }
    s().step(5000, 5000);
    REQUIRE(count("block", "ev.irrigation.dose") == cuts + 1);  // the hub read it as on and cut it
  }
  void pump(bool on) { c().ok("POST", "/api/v1/roles/zone.irrigation_pump/switch", {{"on", on}}); }
  void control(const char* fn, bool on) { c().ok("PATCH", std::string("/api/v1/functions/") + fn, {{"enabled", on}}); }
  // The water changed with control off, by hand in a minute of maintenance mode so no jump locks the
  // probes, then on once the readings have followed (the twin's pH has a dead time)
  void water(const char* fn, double ec, double ph) {
    control(fn, false);
    c().ok("POST", "/api/v1/maintenance", {{"minutes", 1}});
    s().world().fill(40.0, ec, ph);
    s().step(10 * 60 * 1000, 5000);
  }
};
}  // namespace

TEST_CASE("Scenario: an EC or pH round that watering overlaps learns nothing: the pump on, its state not known, stopped or the hub started less than 10 min before (#301, SD-103, RAT-057)") {
  // A flood draws tank water out during the settle and the drain-back brings the table's water back, so
  // what the round sees is not the dose's own effect.
  Watered w;
  auto& s = w.s();
  auto& c = w.c();

  SUBCASE("EC") {
    w.control("ph_control", false);
    w.water("ec_control", 0.9, 5.9);  // the demo's EC target is 1.4
    SUBCASE("without watering the round learns") {
      w.control("ec_control", true);
      REQUIRE(soon(s, [&] { return w.line("ec") == "ec.settling"; }, 5 * 60 * 1000, 5000));
      const json before = w.effect("ec");
      CHECK(soon(s, [&] { return w.effect("ec") != before; }, 7 * 60 * 1000, 5000));
    }
    SUBCASE("the pump read as on during the settle") {
      w.control("ec_control", true);
      REQUIRE(soon(s, [&] { return w.line("ec") == "ec.settling"; }, 5 * 60 * 1000, 5000));
      const json before = w.effect("ec");
      w.press();
      REQUIRE(soon(s, [&] { return w.line("ec") != "ec.settling"; }, 6 * 60 * 1000, 5000));
      s.step(10000, 5000);
      CHECK(c.state()["controllers"]["ec"]["state"] != "blocked");  // judged, not reset
      CHECK(w.effect("ec") == before);
    }
    SUBCASE("the pump's state not known during the settle") {
      w.control("ec_control", true);
      REQUIRE(soon(s, [&] { return w.line("ec") == "ec.settling"; }, 5 * 60 * 1000, 5000));
      const json before = w.effect("ec");
      s.control("fault", {{"device", w.plug}, {"fault", "offline"}});
      REQUIRE(soon(s, [&] { return w.line("ec") != "ec.settling"; }, 6 * 60 * 1000, 5000));
      s.step(10000, 5000);
      CHECK(w.effect("ec") == before);
    }
    SUBCASE("a round that starts 8 min after the pump stopped, and settles after the 10 min") {
      const json before = w.effect("ec");
      w.pump(true);
      s.step(30000, 5000);
      w.pump(false);
      s.step(8 * 60 * 1000, 5000);
      w.control("ec_control", true);
      REQUIRE(soon(s, [&] { return w.line("ec") == "ec.settling"; }, 3 * 60 * 1000, 5000));
      REQUIRE(soon(s, [&] { return w.line("ec") != "ec.settling"; }, 6 * 60 * 1000, 5000));
      s.step(10000, 5000);
      CHECK(c.state()["controllers"]["ec"]["state"] != "blocked");
      CHECK(w.effect("ec") == before);
    }
    SUBCASE("a round that starts 11 min after the pump stopped learns again") {
      const json before = w.effect("ec");
      w.pump(true);
      s.step(30000, 5000);
      w.pump(false);
      s.step(11 * 60 * 1000, 5000);
      w.control("ec_control", true);
      REQUIRE(soon(s, [&] { return w.line("ec") == "ec.settling"; }, 3 * 60 * 1000, 5000));
      CHECK(soon(s, [&] { return w.effect("ec") != before; }, 7 * 60 * 1000, 5000));
    }
    SUBCASE("the state not known, then back reading off: the 10 min count from then, as the plug may have run its own schedule") {
      const json before = w.effect("ec");
      s.control("fault", {{"device", w.plug}, {"fault", "offline"}});
      s.step(3 * 60 * 1000, 5000);
      s.control("fault", {{"device", w.plug}, {"fault", "none"}});
      REQUIRE(soon(s, [&] { return c.state()["outputs"]["zone.irrigation_pump"] == false; }, 60 * 1000, 5000));
      w.control("ec_control", true);
      REQUIRE(soon(s, [&] { return w.line("ec") == "ec.settling"; }, 3 * 60 * 1000, 5000));
      REQUIRE(soon(s, [&] { return w.line("ec") != "ec.settling"; }, 6 * 60 * 1000, 5000));
      s.step(10000, 5000);
      CHECK(w.effect("ec") == before);
    }
    SUBCASE("a restart: the 10 min count from the start, as it may have cut a flood (R6)") {
      const json before = w.effect("ec");
      s.reboot();
      Client again{s};
      again.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
      again.ok("PATCH", "/api/v1/functions/ec_control", {{"enabled", true}});
      REQUIRE(soon(s, [&] { return again.state()["controllers"]["ec"]["line"]["key"] == "ec.settling"; }, 3 * 60 * 1000, 5000));
      REQUIRE(soon(s, [&] { return again.state()["controllers"]["ec"]["line"]["key"] != "ec.settling"; }, 6 * 60 * 1000, 5000));
      s.step(10000, 5000);
      CHECK(again.state()["effects"]["ec"] == before);
    }
  }

  SUBCASE("pH") {
    w.control("ec_control", false);
    w.water("ph_control", 1.4, 6.6);  // the demo's pH target is 5.8
    w.control("ph_control", true);
    REQUIRE(soon(s, [&] { return w.line("ph") == "ph.settling"; }, 5 * 60 * 1000, 5000));
    const json before = w.effect("ph");
    SUBCASE("without watering the round learns") {
      CHECK(soon(s, [&] { return w.effect("ph") != before; }, 6 * 60 * 1000, 5000));
    }
    SUBCASE("the pump read as on during the settle") {
      w.press();
      REQUIRE(soon(s, [&] { return w.line("ph") != "ph.settling"; }, 6 * 60 * 1000, 5000));
      s.step(10000, 5000);
      CHECK(w.effect("ph") == before);
    }
  }
  SUBCASE("a mix to the target's first step, EC control's round 1 (SD-102 D7)") {
    w.control("ph_control", false);
    w.water("ec_control", 0.02, 6.5);  // fresh water
    bool pressed = false, dead = false;
    SUBCASE("without watering it learns") {}
    SUBCASE("the pump read as on while its first part doses: not learned") { pressed = true; }
    SUBCASE("products without effect, the pump read as on while its first part doses: still judged, EC control locks at once (D6)") {
      pressed = dead = true;
      std::lock_guard<std::recursive_mutex> l(s.mutex());
      for (const char* liquid : {"grow_a", "grow_b", "calmag"}) s.world().liquids[liquid].ecPerMlL = 0;
    }
    const json before = c.state()["effects"]["ecByRecipe"].value("veg", json());
    startMeasured(c);
    REQUIRE(soon(s, [&] { return c.state()["job"]["state"] != "measuring"; }, 30 * 60 * 1000, 5000));
    if (pressed) w.press();
    REQUIRE(soon(s, [&] { return c.state()["job"].is_null(); }, 2 * 3600 * 1000, 10000));
    CHECK(c.state()["lastJob"]["message"]["key"] == (dead ? "mix.done_handover_locked" : "mix.done_handover"));
    CHECK(c.state()["latches"].contains("ec.no_effect") == dead);
    s.step(30 * 1000, 5000);
    const json learned = c.state()["effects"]["ecByRecipe"].value("veg", json());
    CHECK((learned == before) == pressed);
  }
}

TEST_CASE("Scenario: \"no effect\" latches after two clean rounds without effect, or four that only watering made unclean (#301, SD-103, RAT-041, RAT-055)") {
  // Points: a clean round without effect 2, a watered one 1, the latch at 4; a judged round with an effect
  // resets the count; another disturbance (here the inlet opened by hand) neither counts nor resets. The
  // twin's products do nothing (an empty bottle, a blocked line, a probe in a cup read the same), except in
  // a round marked with an effect. Each plan: c clean, w watered, d disturbed, W watered with an effect.
  struct Plan {
    const char* name;
    const char* rounds;
    int latchAfter;  // 1-based
  };
  const Plan plans[] = {{"two clean rounds latch", "cc", 2},
                        {"watered rounds count half: three do not latch, the fourth does", "wwww", 4},
                        {"one clean and two watered rounds latch", "cww", 3},
                        {"a disturbed round neither counts nor resets", "cddc", 4},
                        // the rounds after it watered too, as a clean one would start only 10 min after the press
                        {"a round with an effect resets the count, also a watered one", "cWwwww", 6}};
  for (const std::string kind : {"ec", "ph"})
    for (const Plan& plan : plans)
      SUBCASE((kind + ": " + plan.name).c_str()) {
        Watered w;
        auto& s = w.s();
        auto& c = w.c();
        const bool ec = kind == "ec";
        const char* fn = ec ? "ec_control" : "ph_control";
        const std::string settling = kind + ".settling", latch = kind + ".no_effect";
        w.control(ec ? "ph_control" : "ec_control", false);
        w.water(fn, ec ? 0.5 : 1.4, ec ? 5.9 : 6.6);  // far from the targets (EC 1.4, pH 5.8): a round with an effect leaves a gap
        const auto effective = [&](bool on) {
          std::lock_guard<std::recursive_mutex> l(s.mutex());
          s.world().liquids["grow_a"].ecPerMlL = on ? 0.275 : 0;
          s.world().liquids["grow_b"].ecPerMlL = on ? 0.275 : 0;
          s.world().liquids["calmag"].ecPerMlL = on ? 0.217 : 0;
          s.world().liquids["ph_down"].phPerMlL = on ? -4.0 : 0;
        };
        const std::string rounds = plan.rounds;
        effective(rounds[0] == 'W');
        w.control(fn, true);
        for (size_t k = 0; k < rounds.size(); ++k) {
          CAPTURE(k);
          REQUIRE(soon(s, [&] { return w.line(kind.c_str()) == settling; }, 5 * 60 * 1000, 5000));
          if (k + 1 < rounds.size()) effective(rounds[k + 1] == 'W');  // for the next round's dose
          if (rounds[k] == 'w' || rounds[k] == 'W') w.press();
          if (rounds[k] == 'd') {
            c.ok("POST", "/api/v1/roles/tank.inlet/switch", {{"on", true}});
            s.step(10000, 5000);
            c.ok("POST", "/api/v1/roles/tank.inlet/switch", {{"on", false}});
          }
          REQUIRE(soon(s, [&] { return w.line(kind.c_str()) != settling; }, 7 * 60 * 1000, 5000));
          s.step(5000, 5000);
          CHECK(c.state()["latches"].contains(latch) == (k + 1 == static_cast<size_t>(plan.latchAfter)));
        }
      }
}

TEST_CASE("Scenario: a pH dose without a measurable fall, clean or watered, sets the learned pH effect to at least its start and never lowers it, so doses do not grow (#301, SD-103, RAT-041)") {
  Watered w;
  auto& s = w.s();
  SUBCASE("clean, after a high learned effect: it stays, the next dose does not grow") {
    w.control("ec_control", false);
    w.water("ph_control", 1.4, 6.6);
    const auto pHminus = [&](double phPerMlL) {
      std::lock_guard<std::recursive_mutex> l(s.mutex());
      s.world().liquids["ph_down"].phPerMlL = phPerMlL;
    };
    pHminus(-12.0);  // strong: a clean dose learns an effect well above the start of 5 pH per ml/L
    w.control("ph_control", true);
    REQUIRE(soon(s, [&] { return w.line("ph") == "ph.settling"; }, 5 * 60 * 1000, 5000));
    pHminus(-0.4);  // the next dose falls by less than 0.03 (air in the line, a sluggish probe)
    REQUIRE(soon(s, [&] { return w.line("ph") != "ph.settling"; }, 7 * 60 * 1000, 5000));
    s.step(5000, 5000);
    const json high = w.effect("ph");
    REQUIRE(high.is_number());
    REQUIRE(high.get<double>() > 6.0);
    REQUIRE(soon(s, [&] { return w.line("ph") == "ph.settling"; }, 5 * 60 * 1000, 5000));
    REQUIRE(soon(s, [&] { return w.line("ph") != "ph.settling"; }, 7 * 60 * 1000, 5000));
    s.step(5000, 5000);
    CHECK(w.effect("ph") == high);
  }
  SUBCASE("watered, after a low learned effect: it rises to the start") {
    w.control("ec_control", false);
    w.water("ph_control", 1.4, 6.6);
    const auto pHminus = [&](double phPerMlL) {
      std::lock_guard<std::recursive_mutex> l(s.mutex());
      s.world().liquids["ph_down"].phPerMlL = phPerMlL;
    };
    pHminus(-2.0);  // weak: a clean dose learns an effect below the start of 5 pH per ml/L, so the next doses grow
    w.control("ph_control", true);
    REQUIRE(soon(s, [&] { return w.line("ph") == "ph.settling"; }, 5 * 60 * 1000, 5000));
    pHminus(0);  // the next doses do nothing (an empty bottle)
    REQUIRE(soon(s, [&] { return w.line("ph") != "ph.settling"; }, 7 * 60 * 1000, 5000));
    s.step(5000, 5000);
    const json weak = w.effect("ph");
    REQUIRE(weak.is_number());
    REQUIRE(weak.get<double>() < 4.0);
    REQUIRE(soon(s, [&] { return w.line("ph") == "ph.settling"; }, 5 * 60 * 1000, 5000));
    w.press();
    REQUIRE(soon(s, [&] { return w.line("ph") != "ph.settling"; }, 7 * 60 * 1000, 5000));
    s.step(5000, 5000);
    CHECK(w.effect("ph") == 5.0);
  }
}

TEST_CASE("Scenario: the watering pump unbound lets learning's tail run out; bound to another plug, it starts again (#301, SD-103, RAT-057)") {
  Watered w;
  auto& s = w.s();
  auto& c = w.c();
  w.control("ph_control", false);
  w.water("ec_control", 0.9, 5.9);
  const json before = w.effect("ec");
  SUBCASE("unbound right after a run: a round within the 10 min learns nothing") {
    w.pump(true);
    s.step(30000, 5000);
    w.pump(false);
    c.ok("DELETE", "/api/v1/roles/zone.irrigation_pump");
  }
  SUBCASE("bound to another plug: the 10 min start again, as a restart may have cut a flood there") {
    const std::string other = irrigationPlug(s, c);
    REQUIRE(other != w.plug);
  }
  w.control("ec_control", true);
  REQUIRE(soon(s, [&] { return w.line("ec") == "ec.settling"; }, 3 * 60 * 1000, 5000));
  REQUIRE(soon(s, [&] { return w.line("ec") != "ec.settling"; }, 6 * 60 * 1000, 5000));
  s.step(10000, 5000);
  CHECK(c.state()["controllers"]["ec"]["state"] != "blocked");
  CHECK(w.effect("ec") == before);
}

TEST_CASE("Scenario: a clean EC or pH round learns over the volume the dose went into, the one when the round started (#301, SD-103, RAT-057)") {
  // The same round twice; in the second the level falls by 8 L during the settle (below the jump
  // threshold, no inlet, no watering). The dose mixed into the 40 L of the start: both learn the same.
  for (const std::string kind : {"ec", "ph"}) {
    CAPTURE(kind);
    json learned[2];
    for (int k = 0; k < 2; ++k) {
      sim::Simulation s(test::opts("demo"));
      Client c{s};
      demoAt(s, c, 40.0);
      c.ok("PATCH", "/api/v1/functions/refill", {{"enabled", false}});
      c.ok("PATCH", "/api/v1/functions/circulation", {{"params", {{"mode", "always"}}}});
      const bool ec = kind == "ec";
      c.ok("PATCH", std::string("/api/v1/functions/") + (ec ? "ph_control" : "ec_control"), {{"enabled", false}});
      const std::string fnPatch = std::string("/api/v1/functions/") + (ec ? "ec_control" : "ph_control");
      c.ok("PATCH", fnPatch, {{"enabled", false}});
      s.world().fill(40.0, ec ? 0.9 : 1.4, ec ? 5.9 : 6.6);
      s.step(10 * 60 * 1000, 5000);  // the readings have followed the new water (the twin's pH has a dead time)
      c.ok("PATCH", fnPatch, {{"enabled", true}});
      const std::string fn = ec ? "ec" : "ph", settling = ec ? "ec.settling" : "ph.settling";
      REQUIRE(soon(s, [&] { return c.state()["controllers"][fn]["line"]["key"] == settling; }, 30 * 60 * 1000, 5000));
      const json before = c.state()["effects"][kind];
      if (k == 1) s.world().tank.volumeL -= 8.0;
      REQUIRE(soon(s, [&] { return c.state()["effects"][kind] != before; }, 7 * 60 * 1000, 5000));
      learned[k] = c.state()["effects"][kind];
    }
    REQUIRE(learned[0].is_number());
    REQUIRE(learned[1].is_number());
    CHECK(learned[0] != (kind == "ec" ? json(nullptr) : json(5.0)));  // learned, not the start value
    CHECK(learned[1].get<double>() == doctest::Approx(learned[0].get<double>()).epsilon(0.02));
  }
}

TEST_CASE("Scenario: the refill switched off and on again waits for the level to settle before it fills (#289, RAT-038, SD-090)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  demoAt(s, c, 40.0);
  for (const char* fn : {"ec_control", "ph_control"}) c.ok("PATCH", std::string("/api/v1/functions/") + fn, {{"enabled", false}});  // no dose in between
  // a person switches the refill off, bails the tank and switches it on again (by less than a jump)
  c.ok("PATCH", "/api/v1/functions/refill", {{"enabled", false}});
  s.step(60000, 5000);
  s.world().fill(25.0, 1.3, 5.9);
  s.step(30000, 5000);
  c.ok("PATCH", "/api/v1/functions/refill", {{"enabled", true}});
  s.step(5000);
  CHECK_FALSE(inletOpen(c));
  CHECK(refillLine(c)["key"] == "refill.settle");  // as after a Stop: a person may be at the tank
  CHECK_FALSE(soon(s, [&] { return inletOpen(c); }, 2 * 60 * 1000, 5000));
  REQUIRE(soon(s, [&] { return inletOpen(c); }, 2 * 60 * 1000, 5000));
  CHECK(refillLine(c)["args"].value("litres", -1.0) == doctest::Approx(20.0).epsilon(0.05));  // planned from 25 L, to 45 L
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
  auto [code, refused] = c.call("POST", "/api/v1/dose", {{"canister", "part-a"}, {"ml", 2}});
  CHECK(code != 200);
  CHECK(refused["error"]["key"] == "job.start_failed");
  CHECK(refused["error"]["args"]["reason"]["key"] == "act.stopped");
  // a mix is refused too, with a message in the page's language (SD-045: "Stop", never "emergency stop")
  auto [mixCode, mix] = c.call("POST", "/api/v1/mix/start", {{"recipe", "veg"}, {"waterL", 20}});
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
    }, 30 * 60 * 1000));
    c.ok("POST", "/api/v1/dose", {{"canister", "part-a"}, {"ml", 5}});
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
  const double stockBefore = c.state()["stock"]["part-a"];  // a single run, booked only when it ends
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
      CHECK(e["text"]["args"]["name"] == "Part A");
      CHECK(e["text"]["args"]["reason"] == "bus read failed");
      CHECK(e["data"]["canister"] == "part-a");
    }
  CHECK(said);
  // the read keeps failing: the dropped dose is not read again, so no internal error follows,
  // nothing switches on, and nothing of it is booked later
  s.step(5000);
  CHECK(count("system", "ev.tick_fault") == 0);
  s.world().onRunStatus = nullptr;
  s.step(5000);
  CHECK_FALSE(s.world().circulating());
  CHECK(c.state()["stock"]["part-a"].get<double>() == doctest::Approx(stockBefore));
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
    const double stockBefore = c.state()["stock"]["part-a"];
    c.ok("POST", "/api/v1/stop");
    CHECK(c.state()["stock"]["part-a"].get<double>() < stockBefore);  // the cut part is booked (RAT-070)
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
    // then the fill that fell due meanwhile goes first, with no next round before it (SD-090) ...
    bool ecFirst = false;
    CHECK(until(s, [&] {
      ecFirst = ecFirst || purpose(c) == "ec";
      return c.state()["outputs"]["tank.inlet"] == true;
    }, 4 * 60 * 1000));
    CHECK_FALSE(ecFirst);
    // ... and EC decides again once the fill's rise is checked
    CHECK(until(s, [&] { return purpose(c) == "ec"; }, 30 * 60 * 1000));
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
    s.step(5000);
    // not held by the cut round, only by the reading's settling time after the refill was off (SD-090)
    CHECK(c.state()["controllers"]["refill"]["line"]["key"] == "refill.settle");
    CHECK(until(s, [&] { return c.state()["outputs"]["tank.inlet"] == true; }, 4 * 60 * 1000));
    CHECK(until(s, [&] { return s.world().circulating(); }, 2 * 60 * 1000));  // the level is back
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
  CHECK(m["key"] == "job.aborted_pair");  // the rest of Teil A and all of Teil B are missing (SD-057, RAT-054)
  CHECK(m["args"]["done"][0]["args"]["name"] == "Teil A");
  REQUIRE(m["args"]["partners"].size() == 2);
  CHECK(m["args"]["partners"][0]["args"]["name"] == "Teil A");
  CHECK(m["args"]["partners"][0]["args"]["ml"].get<double>() == doctest::Approx(40.0 - (before - after)).epsilon(0.05));
  CHECK(m["args"]["partners"][1]["args"]["name"] == "Teil B");
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
