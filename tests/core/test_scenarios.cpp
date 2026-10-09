// SPDX-License-Identifier: AGPL-3.0-or-later
// Scenario tests against the digital twin: sequences end to end through the
// API, as the web app uses it. The expectations come from the domain rules
// (docs/INVARIANTS.md, docs/RATIONALE.md).
#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>

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
  CHECK(dry["text"]["key"] == "ev.circ.dry.low");
  c.ok("POST", "/api/v1/latches/circulation.dry/ack");
  const json released = findEvents(c, "block").at(0);  // named as on the Release button (SD-032)
  CHECK(released["title"]["key"] == "ev.latch_released");
  CHECK(released["text"]["key"] == "latch.circulation.dry");
  s.step(5000);
  st = c.state();
  CHECK(st["outputs"]["tank.circulation"] == false);  // Einschaltsperre bleibt, solange der Pegel fehlt
  CHECK_FALSE(st["latches"].contains("circulation.dry"));
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
  s.step(5 * 60 * 1000);
  CHECK(c.state()["outputs"]["tank.inlet"] == false);
  c.ok("POST", "/api/v1/latches/inlet.fault/ack");
  CHECK(findEvents(c, "block").at(0)["text"]["key"] == "latch.inlet.fault");  // named as on the Release button
}

TEST_CASE("Szenario: Not-Halt stoppt alles und sperrt Automatik bis Fortsetzen") {
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
  c.ok("POST", "/api/v1/resume");
  CHECK(c.state()["stopped"] == false);
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
  st.write("config.json", "{}");
  CHECK_NOTHROW(st.flush());
  CHECK(st.read("config.json") == std::optional<std::string>("{}"));  // im Speicher weiter
  st.write("config.json", "{\"a\":1}");
  CHECK_NOTHROW(st.flush());
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

TEST_CASE("Messages: pump calibration and emergency stop name their result and cause (SD-032)") {
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
