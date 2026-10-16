// SPDX-License-Identifier: AGPL-3.0-or-later
#include <doctest/doctest.h>

#include <cstdint>
#include <set>
#include <sstream>

#include "client.hpp"
#include "fakes.hpp"

using test::Client;

TEST_CASE("API: Info ist öffentlich, alles andere braucht eine Anmeldung") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  CHECK(c.call("GET", "/api/v1/info").first == 200);
  CHECK(c.call("GET", "/api/v1/info").second["hasPassword"] == false);
  CHECK(c.call("GET", "/api/v1/state").first == 401);
  CHECK(c.call("POST", "/api/v1/stop").first == 401);
  CHECK(c.call("POST", "/api/v1/auth/login", {{"password", "x"}}).first == 401);
}

TEST_CASE("API: Ersteinrichtung setzt das Passwort genau einmal") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  CHECK(c.call("POST", "/api/v1/auth/setup", {{"password", "kurz"}}).first == 422);
  CHECK(c.call("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}}).first == 200);
  CHECK_FALSE(c.token.empty());
  CHECK(c.call("GET", "/api/v1/state").first == 200);
  Client other{s};
  CHECK(other.call("POST", "/api/v1/auth/setup", {{"password", "uebernahme-123"}}).first == 409);
}

TEST_CASE("API: with a setup code armed, the first password needs it, and info says so (#79)") {
  sim::Simulation s(test::opts("neu"));
  std::string code;
  {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    code = s.hub().armSetupCode();
  }
  Client someone{s, {}, "192.168.1.66"};
  CHECK(someone.call("GET", "/api/v1/info").second["setupCode"] == true);
  auto refused = someone.call("POST", "/api/v1/auth/setup", {{"password", "uebernahme-123"}});
  CHECK(refused.first == 403);
  CHECK(refused.second["error"]["key"] == "auth.code");
  CHECK(someone.call("POST", "/api/v1/auth/setup?code=" + code, {{"password", "uebernahme-123"}}).first == 403);  // from the body only
  for (int i = 0; i < 48; ++i)
    CHECK(someone.call("POST", "/api/v1/auth/setup", {{"password", "uebernahme-123"}, {"code", "2222-2222-2222-2222"}}).first == 403);
  CHECK(someone.call("POST", "/api/v1/auth/setup", {{"password", "uebernahme-123"}, {"code", 42}}).first == 403);
  CHECK(someone.call("POST", "/api/v1/auth/setup", {{"password", "uebernahme-123"}, {"code", nullptr}}).first == 403);
  CHECK(someone.token.empty());
  Client owner{s, {}, "192.168.1.20"};
  CHECK(owner.call("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}, {"code", code}}).first == 200);  // no lockout in the way
  CHECK(owner.call("GET", "/api/v1/state").first == 200);
  CHECK(owner.call("GET", "/api/v1/info").second["setupCode"] == false);
  CHECK(someone.call("POST", "/api/v1/auth/setup", {{"password", "uebernahme-123"}, {"code", code}}).first == 409);  // used up
  // The owner learns of the tries: the first wrong code, not one entry per try, and the count at the setup
  const gc::json events = owner.ok("GET", "/api/v1/events?limit=2000&type=auth")["events"];
  int wrong = 0;
  bool summary = false;
  for (const auto& e : events) {
    const std::string title = e["title"].is_object() ? e["title"].value("key", std::string()) : "";
    wrong += title == "ev.auth.code_wrong";
    if (title == "ev.auth.password_set" && e["text"].is_object())
      summary = e["text"].value("key", std::string()) == "ev.auth.first_setup_wrong" && e["text"]["args"]["count"] == 52;
  }
  CHECK(wrong == 1);
  CHECK(summary);
  // The code is in none of what the hub gives out
  std::string bare;
  for (char ch : code) if (ch != '-') bare += ch;
  for (const char* path : {"/api/v1/info", "/api/v1/auth/session", "/api/v1/state", "/api/v1/config", "/api/v1/config/export", "/api/v1/diagnostics", "/api/v1/events?limit=2000"}) {
    const std::string out = owner.ok("GET", path).dump();
    CHECK_MESSAGE(out.find(bare) == std::string::npos, path);
    CHECK_MESSAGE(out.find(code) == std::string::npos, path);
  }
}

TEST_CASE("API: without an armed setup code, info asks for none (#79)") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  CHECK(c.call("GET", "/api/v1/info").second["setupCode"] == false);
}

TEST_CASE("API: Fehlversuche führen zur Sperre (429), Abmelden beendet die Sitzung") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  Client bad{s};
  int last = 0;
  for (int i = 0; i < 6; ++i) last = bad.call("POST", "/api/v1/auth/login", {{"password", "falsch"}}).first;
  CHECK(last == 429);
  c.ok("POST", "/api/v1/auth/logout");
  CHECK(c.call("GET", "/api/v1/state").first == 401);
}

TEST_CASE("API: a page that asks for its session gets its cookie renewed for another 12 hours; without a session nothing (SD-064)") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  REQUIRE_FALSE(c.token.empty());
  const auto session = [&](const std::string& token) {
    gc::ApiRequest r;
    r.method = "GET";
    r.path = "/api/v1/auth/session";
    r.token = token;
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    return s.api().handle(r);
  };
  const gc::ApiResponse res = session(c.token);
  CHECK(gc::json::parse(res.body)["authenticated"] == true);
  std::vector<std::string> cookies;
  for (const auto& [k, v] : res.headers)
    if (k == "Set-Cookie") cookies.push_back(v);
  CHECK(cookies == std::vector<std::string>{"gc_session=" + c.token + "; Path=/; HttpOnly; SameSite=Strict; Max-Age=43200"});
  for (const std::string& token : {std::string(""), std::string("nonsense")}) {
    const gc::ApiResponse none = session(token);
    CHECK(gc::json::parse(none.body)["authenticated"] == false);
    for (const auto& [k, v] : none.headers) CHECK(k != "Set-Cookie");
  }
}

TEST_CASE("API: someone guessing in the network does not lock out a client that signed in before (SD-051)") {
  sim::Simulation s(test::opts("neu"));
  Client owner{s, {}, "192.168.1.20"};
  owner.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});  // signed in by the setup
  owner.ok("POST", "/api/v1/auth/logout");
  Client bad{s, {}, "192.168.1.66"};
  for (int i = 0; i < 5; ++i) CHECK(bad.call("POST", "/api/v1/auth/login", {{"password", "falsch"}}).first == 401);
  CHECK(bad.call("POST", "/api/v1/auth/login", {{"password", "mein-passwort"}}).first == 429);
  Client other{s, {}, "192.168.1.77"};  // never signed in: shares the lockout
  CHECK(other.call("POST", "/api/v1/auth/login", {{"password", "mein-passwort"}}).first == 429);
  CHECK(owner.call("POST", "/api/v1/auth/login", {{"password", "mein-passwort"}}).first == 200);
  CHECK(owner.call("POST", "/api/v1/stop").first == 200);
  // A refused try during the lockout writes no event: only counted failures do (flash wear)
  auto failed = [&] {
    const gc::json events = owner.ok("GET", "/api/v1/events?limit=2000&type=auth")["events"];
    int n = 0;
    for (const auto& e : events)
      if (e["title"].is_object() && e["title"].value("key", std::string()) == "ev.auth.failed") ++n;
    return n;
  };
  const int before = failed();
  CHECK(before == 5);
  for (int i = 0; i < 10; ++i) CHECK(bad.call("POST", "/api/v1/auth/login", {{"password", "falsch"}}).first == 429);
  CHECK(failed() == before);
}

TEST_CASE("API: a locked client gets 429 when changing the password, too (SD-051)") {
  sim::Simulation s(test::opts("neu"));
  Client c{s, {}, "192.168.1.20"};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  for (int i = 0; i < 5; ++i) CHECK(c.call("PUT", "/api/v1/auth/password", {{"old", "falsch"}, {"new", "neues-passwort"}}).first == 422);
  CHECK(c.call("PUT", "/api/v1/auth/password", {{"old", "mein-passwort"}, {"new", "neues-passwort"}}).first == 429);
  // Each wrong old password is logged like a failed sign-in; the refused try is not
  const gc::json events = c.ok("GET", "/api/v1/events?limit=2000&type=auth")["events"];
  int failed = 0;
  for (const auto& e : events)
    if (e["title"].is_object() && e["title"].value("key", std::string()) == "ev.auth.failed") ++failed;
  CHECK(failed == 5);
  Client other{s, {}, "192.168.1.77"};  // the count was the caller's own, not the shared one
  CHECK(other.call("POST", "/api/v1/auth/login", {{"password", "mein-passwort"}}).first == 200);
  s.step(31000);
  c.ok("PUT", "/api/v1/auth/password", {{"old", "mein-passwort"}, {"new", "neues-passwort"}});
  Client guesser{s, {}, "192.168.1.66"};
  for (int i = 0; i < 5; ++i) guesser.call("POST", "/api/v1/auth/login", {{"password", "falsch"}});
  CHECK(c.call("POST", "/api/v1/auth/login", {{"password", "neues-passwort"}}).first == 200);  // the caller kept its count
  CHECK(other.call("POST", "/api/v1/auth/login", {{"password", "neues-passwort"}}).first == 429);
}

TEST_CASE("API: after a restart every device shares the count until it signs in again (SD-051)") {
  sim::Simulation s(test::opts("neu"));
  Client phone{s, {}, "192.168.1.20"};
  phone.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  s.reboot();  // the known devices are kept in memory only
  Client guesser{s, {}, "192.168.1.66"};
  for (int i = 0; i < 5; ++i) guesser.call("POST", "/api/v1/auth/login", {{"password", "falsch"}});
  Client again{s, {}, "192.168.1.20"};
  CHECK(again.call("POST", "/api/v1/auth/login", {{"password", "mein-passwort"}}).first == 429);
}

TEST_CASE("API: before the first password, a sign-in try is not logged (SD-051)") {
  sim::Simulation s(test::opts("neu"));
  Client c{s, {}, "192.168.1.66"};
  for (int i = 0; i < 20; ++i) CHECK(c.call("POST", "/api/v1/auth/login", {{"password", "falsch"}}).first == 401);
  Client owner{s, {}, "192.168.1.20"};
  owner.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  const gc::json events = owner.ok("GET", "/api/v1/events?limit=2000&type=auth")["events"];
  for (const auto& e : events) {
    const bool failed = e["title"].is_object() && e["title"].value("key", std::string()) == "ev.auth.failed";
    CHECK_FALSE(failed);
  }
}

TEST_CASE("API: a phone that stays signed in keeps its own lockout count while other devices sign in (SD-051)") {
  sim::Simulation s(test::opts("neu"));
  Client phone{s, {}, "192.168.1.20"};
  phone.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  for (int i = 1; i <= 16; ++i) {  // 16 other devices sign in, one a second; the phone keeps using the app
    s.step(1000);
    Client other{s, {}, "192.168.1." + std::to_string(100 + i)};
    other.ok("POST", "/api/v1/auth/login", {{"password", "mein-passwort"}});
    phone.ok("GET", "/api/v1/state");
  }
  Client bad{s, {}, "192.168.1.66"};
  for (int i = 0; i < 5; ++i) bad.call("POST", "/api/v1/auth/login", {{"password", "falsch"}});
  CHECK(bad.call("POST", "/api/v1/auth/login", {{"password", "mein-passwort"}}).first == 429);
  phone.ok("POST", "/api/v1/auth/logout");
  CHECK(phone.call("POST", "/api/v1/auth/login", {{"password", "mein-passwort"}}).first == 200);  // the first other device dropped out instead
  Client first{s, {}, "192.168.1.101"};
  CHECK(first.call("POST", "/api/v1/auth/login", {{"password", "mein-passwort"}}).first == 429);
}

TEST_CASE("API: ungültiges JSON, unbekannter Pfad, Validierung") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  gc::ApiRequest r;
  r.method = "POST";
  r.path = "/api/v1/tank";
  r.body = "{kaputt";
  r.token = c.token;
  CHECK(s.api().handle(r).status == 400);
  CHECK(c.call("GET", "/api/v1/gibtsnicht").first == 404);
  auto [st, j] = c.call("PUT", "/api/v1/tank", {{"capacityL", -5}});
  CHECK(st == 422);
  CHECK(j["errors"].size() >= 1);
}

TEST_CASE("API: Zustand hat die Felder, die die Web-App liest (Vertrag)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  s.step(10000);
  auto st = c.state();
  for (const char* k : {"now", "ports", "devices", "readings", "tank", "controllers", "outputs", "job", "watchdog",
                        "functions", "latches", "stock", "grow", "setupDone", "stopped"})
    CHECK_MESSAGE(st.contains(k), k);
  CHECK(st["ports"].size() == 8);
  CHECK(st["controllers"].contains("ph"));
  CHECK(st["watchdog"].contains("stale"));
  auto cfg = c.ok("GET", "/api/v1/config");
  CHECK(cfg.dump().find("hash") == std::string::npos);  // keine Geheimnisse in der Konfiguration
  auto h = c.ok("GET", "/api/v1/history?series=tank.ph,tank.ec&points=50");
  CHECK(h["series"].size() == 2);
  auto d = c.ok("GET", "/api/v1/diagnostics");
  CHECK(d["reportId"].get<std::string>().rfind("GC-", 0) == 0);
  CHECK(d.dump().find("salt") == std::string::npos);
}

TEST_CASE("API: the CSV export follows the language, gaps stay empty (RAT-016, SD-083)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  s.step(120000);  // two minutes of readings
  auto csv = [&](const std::string& q, const std::string& series = "tank.ph,zone.co2") {
    gc::ApiRequest r;
    r.method = "GET";
    r.path = "/api/v1/export.csv";
    r.query = {{"series", series}};
    if (!q.empty()) r.query["lang"] = q;
    r.token = c.token;
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    return s.api().handle(r);
  };
  auto header = [](const gc::ApiResponse& r) {
    for (const auto& [k, v] : r.headers)
      if (k == "Content-Disposition") return v;
    return std::string();
  };
  // A line with a pH value and no CO2 (the demo has no CO2 sensor): the gap stays an empty field
  auto row = [](const std::string& body) {
    std::istringstream in(body);
    std::string line, last;
    std::getline(in, line);
    while (std::getline(in, line))
      if (!line.empty()) last = line;
    return last;
  };
  const auto en = csv("en");
  CHECK(en.status == 200);
  CHECK(en.body.rfind("time_utc,tank.ph,zone.co2\n", 0) == 0);
  const std::string enRow = row(en.body);
  REQUIRE_FALSE(enRow.empty());
  CHECK(enRow.find(';') == std::string::npos);
  CHECK(enRow.find('.') != std::string::npos);  // decimal point
  CHECK(enRow.back() == ',');                    // CO2 left empty, never 0
  CHECK(header(en).find("growcontroller-history.csv") != std::string::npos);
  const auto de = csv("de");
  CHECK(de.body.rfind("zeit_utc;tank.ph;zone.co2\n", 0) == 0);
  const std::string deRow = row(de.body);
  REQUIRE_FALSE(deRow.empty());
  CHECK(deRow.find(',') != std::string::npos);  // decimal comma
  CHECK(deRow.find('.') == std::string::npos);
  CHECK(deRow.back() == ';');
  CHECK(header(de).find("growcontroller-verlauf.csv") != std::string::npos);
  // Without a language the hub's own decides; the demo's hub speaks German
  CHECK(csv("").body.rfind("zeit_utc;", 0) == 0);
  CHECK(csv("xx").body.rfind("zeit_utc;", 0) == 0);  // an unknown one counts as none
  c.ok("PUT", "/api/v1/system", {{"language", "en"}});
  CHECK(csv("").body.rfind("time_utc,", 0) == 0);
  // Only a series id heads a column: anything else is refused, nothing is written
  for (const char* bad : {"=1+1", "tank.ph\n=2", "tank.ph;x", "Tank.PH", "-1", "@a", "\"q\"", "tank ph"}) {
    const auto r = csv("en", bad);
    CHECK_MESSAGE(r.status == 422, bad);
    CHECK_MESSAGE(gc::json::parse(r.body)["error"]["key"] == "history.series_id", bad);
  }
  CHECK(csv("en", "tank.ph,zone.vpd,tank.water_temp").status == 200);
  CHECK(csv("en", "tank.ph,,zone.co2").status == 422);  // an empty id between two
  CHECK(csv("en", "tank.ph,").status == 422);           // and after the last
  CHECK(csv("en", ",tank.ph").status == 422);
  CHECK(csv("en", std::string(64, 'a')).status == 200);
  CHECK(csv("en", std::string(65, 'a')).status == 422);
  CHECK(csv("de").body.rfind("zeit_utc;", 0) == 0);  // the page's language beats the hub's
}

TEST_CASE("API: history and CSV name at most 16 series, none twice, each a series id (SD-083)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  auto list = [](int n) {
    std::string out;
    for (int i = 0; i < n; ++i) out += (i ? "," : "") + std::string("s") + std::to_string(i);
    return out;
  };
  for (const char* route : {"/api/v1/history?series=", "/api/v1/export.csv?lang=en&series="}) {
    CAPTURE(route);
    auto [ok, okBody] = c.call("GET", route + list(16));
    CHECK(ok == 200);
    auto [many, manyBody] = c.call("GET", route + list(17));
    CHECK(many == 422);
    CHECK(manyBody["error"]["key"] == "history.series_many");
    CHECK(manyBody["error"]["args"]["max"] == 16);
    auto [twice, twiceBody] = c.call("GET", route + std::string("tank.ph,tank.ec,tank.ph"));
    CHECK(twice == 422);
    CHECK(twiceBody["error"]["key"] == "history.series_twice");
    CHECK(twiceBody["error"]["args"]["id"] == "tank.ph");
    for (const char* bad : {"tank.ph,", ",tank.ph", "tank.ph,,tank.ec", "Tank.PH", "=1+1"}) {
      auto [st, body] = c.call("GET", route + std::string(bad));
      CHECK_MESSAGE(st == 422, bad);
      CHECK_MESSAGE(body["error"]["key"] == "history.series_id", bad);
    }
  }
  CHECK(c.ok("GET", "/api/v1/history?series=tank.ph,zone.vpd")["series"].size() == 2);
  CHECK(c.ok("GET", "/api/v1/history?series=")["series"].empty());  // none named, none read
}

TEST_CASE("API: where one key had several texts, each refusal has its own key (SD-083)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  s.step(5000);
  auto key = [&](const std::string& method, const std::string& path, const gc::json& body) {
    auto [st, res] = c.call(method, path, body);
    return std::make_pair(st, res["error"]["key"].is_string() ? res["error"]["key"].get<std::string>() : res.dump());
  };
  auto probe = [&](const char* dev, const char* kind, const char* action, gc::json more = gc::json::object()) {
    more["device"] = dev;
    more["kind"] = kind;
    more["action"] = action;
    return key("POST", "/api/v1/probe", more);
  };
  // pH: a point needs its value, a value a buffer can be, and two points before saving
  c.ok("POST", "/api/v1/probe", {{"device", "PHEC-3F2A91"}, {"kind", "ph"}, {"action", "start"}});
  CHECK(probe("PHEC-3F2A91", "ph", "point") == std::make_pair(422, std::string("probe.reference")));
  CHECK(probe("PHEC-3F2A91", "ph", "point", {{"reference", 99}}) == std::make_pair(422, std::string("probe.reference_range")));
  CHECK(probe("PHEC-3F2A91", "ph", "commit") == std::make_pair(422, std::string("probe.points_two")));
  c.ok("POST", "/api/v1/probe", {{"device", "PHEC-3F2A91"}, {"kind", "ph"}, {"action", "cancel"}});
  // EC: one point in the reference solution before saving
  c.ok("POST", "/api/v1/probe", {{"device", "PHEC-3F2A91"}, {"kind", "ec"}, {"action", "start"}});
  CHECK(probe("PHEC-3F2A91", "ec", "commit") == std::make_pair(422, std::string("probe.points_one")));
  c.ok("POST", "/api/v1/probe", {{"device", "PHEC-3F2A91"}, {"kind", "ec"}, {"action", "cancel"}});
  // A tank curve: two points at least, and litres that rise with the sensor value
  c.ok("POST", "/api/v1/probe", {{"device", "LVL-77B210"}, {"kind", "tank_curve"}, {"action", "start"}});
  s.world().fill(20, 1.3, 5.9);
  s.step(5000);
  c.ok("POST", "/api/v1/probe", {{"device", "LVL-77B210"}, {"kind", "tank_curve"}, {"action", "point"}, {"reference", 20}});
  CHECK(probe("LVL-77B210", "tank_curve", "commit") == std::make_pair(422, std::string("curve.two")));
  s.world().fill(40, 1.3, 5.9);
  s.step(5000);
  c.ok("POST", "/api/v1/probe", {{"device", "LVL-77B210"}, {"kind", "tank_curve"}, {"action", "point"}, {"reference", 10}});
  CHECK(probe("LVL-77B210", "tank_curve", "commit") == std::make_pair(422, std::string("curve.falls")));
  c.ok("POST", "/api/v1/probe", {{"device", "LVL-77B210"}, {"kind", "tank_curve"}, {"action", "cancel"}});
  // An imported calibration: the reason as a nested message; one the device's class does not offer
  gc::json cfg = c.ok("GET", "/api/v1/config/export");
  cfg["calibrations"]["PHEC-3F2A91"]["ph"] = {{"points", "kaputt"}};
  cfg["calibrations"]["LVL-77B210"]["ec"] = {{"factor", 1.0}};
  auto [ist, ibody] = c.call("POST", "/api/v1/config/import", cfg);
  CHECK(ist == 422);
  bool reason = false, offered = false;
  for (const auto& e : ibody["errors"]) {
    reason = reason || (e["key"] == "cfg.calibration" && e["args"]["reason"]["key"] == "calib.ph" && e["args"]["device"] == "PHEC-3F2A91");
    offered = offered || (e["key"] == "cfg.calibration.offered" && e["args"]["device"] == "LVL-77B210");
  }
  CHECK_MESSAGE(reason, ibody.dump());
  CHECK_MESSAGE(offered, ibody.dump());
  // Adding a device the hub does not see
  CHECK(key("POST", "/api/v1/devices/NOPE-1/accept", {{"name", ""}}) == std::make_pair(404, std::string("device.not_detected")));
}

TEST_CASE("API: a refused import names at most 50 findings and counts the rest; a newer schema says so (SD-083)") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  gc::json cfg = c.ok("GET", "/api/v1/config/export");
  for (int i = 0; i < 60; ++i) cfg["devices"].push_back({{"id", "X" + std::to_string(i)}, {"class", "unknown_class"}, {"name", "x"}});
  auto [st, body] = c.call("POST", "/api/v1/config/import", cfg);
  CHECK(st == 422);
  CHECK(body["errors"].size() == 50);
  CHECK(body["more"].get<int>() >= 10);
  CHECK(body["error"]["key"] == body["errors"][0]["key"]);
  gc::json few = c.ok("GET", "/api/v1/config/export");
  for (int i = 0; i < 3; ++i) few["devices"].push_back({{"id", "Y" + std::to_string(i)}, {"class", "unknown_class"}, {"name", "y"}});
  auto [fst, fbody] = c.call("POST", "/api/v1/config/import", few);
  CHECK(fst == 422);
  CHECK(fbody["errors"].size() == 3);
  CHECK_FALSE(fbody.contains("more"));  // only when there are more than 50
  for (const gc::json& v : {gc::json(99), gc::json(4294967298ULL), gc::json(99.0), gc::json(3.0e9), gc::json(1e300)}) {
    CAPTURE(v.dump());
    gc::json newer = c.ok("GET", "/api/v1/config/export");
    newer["schemaVersion"] = v;
    auto [nst, nbody] = c.call("POST", "/api/v1/config/import", newer);
    CHECK(nst == 422);
    CHECK(nbody["error"]["key"] == "config.newer");
  }
}

TEST_CASE("API: an imported configuration without a language is German; the settings file has an English name (SD-083)") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  CHECK(c.ok("GET", "/api/v1/config")["system"]["language"] == "en");
  gc::json cfg = c.ok("GET", "/api/v1/config/export");
  cfg["system"].erase("language");  // a file saved before the hub wrote one
  c.ok("POST", "/api/v1/config/import", cfg);
  CHECK(c.ok("GET", "/api/v1/config")["system"]["language"] == "de");
  CHECK(c.ok("GET", "/api/v1/info")["language"] == "de");
  gc::ApiRequest r;
  r.method = "GET";
  r.path = "/api/v1/config/export";
  r.token = c.token;
  gc::ApiResponse res;
  {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    res = s.api().handle(r);
  }
  bool named = false;
  for (const auto& [k, v] : res.headers) named = named || (k == "Content-Disposition" && v.find("growcontroller-config.json") != std::string::npos);
  CHECK(named);
}

TEST_CASE("API: Herkunftsprüfung gegen DNS-Rebinding und CSRF") {
  CHECK(gc::hostAllowed("127.0.0.1:8080"));
  CHECK(gc::hostAllowed("localhost:8080"));
  CHECK(gc::hostAllowed("192.168.1.20"));
  CHECK(gc::hostAllowed("growcontroller.local"));
  CHECK(gc::hostAllowed("[::1]:8080"));
  CHECK(gc::hostAllowed("mein-hub", "mein-hub"));
  CHECK(gc::hostAllowed("hub.fritz.box"));
  CHECK(gc::hostAllowed("hub.home.arpa:80"));
  CHECK_FALSE(gc::hostAllowed("angreifer.example:8080"));
  CHECK_FALSE(gc::hostAllowed("fritz.box.angreifer.example"));
  CHECK_FALSE(gc::hostAllowed("127.0.0.1.angreifer.example"));
  // Lesen immer, Schreiben nur von derselben Herkunft
  CHECK(gc::writeAllowed("GET", "cross-site", "https://angreifer.example", "192.168.1.20"));
  CHECK(gc::writeAllowed("POST", "same-origin", "http://192.168.1.20", "192.168.1.20"));
  CHECK_FALSE(gc::writeAllowed("POST", "cross-site", "https://angreifer.example", "192.168.1.20"));
  CHECK_FALSE(gc::writeAllowed("POST", "same-site", "http://192.168.1.21", "192.168.1.20"));
  CHECK_FALSE(gc::writeAllowed("POST", "", "https://angreifer.example", "192.168.1.20"));
  CHECK(gc::writeAllowed("POST", "", "http://192.168.1.20", "192.168.1.20"));
  CHECK(gc::writeAllowed("POST", "", "", "192.168.1.20"));  // kein Browser (curl, Integrationen)
}

TEST_CASE("API: the session token comes from a bearer header or the cookie named exactly gc_session") {
  CHECK(gc::sessionToken("Bearer abc", "gc_session=def") == "abc");
  CHECK(gc::sessionToken("", "gc_session=abc") == "abc");
  CHECK(gc::sessionToken("", "theme=dark; gc_session=abc; lang=de") == "abc");
  CHECK(gc::sessionToken("", "theme=dark;gc_session=abc") == "abc");
  CHECK(gc::sessionToken("", "") == "");
  CHECK(gc::sessionToken("Basic abc", "") == "");
  // Only that name counts: not a name that ends in it, nor a value that holds it
  CHECK(gc::sessionToken("", "xgc_session=other") == "");
  CHECK(gc::sessionToken("", "xgc_session=other; gc_session=abc") == "abc");
  CHECK(gc::sessionToken("", "note=gc_session=other") == "");
  CHECK(gc::sessionToken("", "note=gc_session=other; gc_session=abc") == "abc");
  // More than one gc_session: neither counts, as the order says nothing about which is ours
  CHECK(gc::sessionToken("", "gc_session=abc; gc_session=def") == "");
  CHECK(gc::sessionToken("", "gc_session=; gc_session=abc") == "");
  CHECK(gc::sessionToken("", "gc_session=") == "");
  // A bearer header comes first, also an empty one; its scheme is matched as written
  CHECK(gc::sessionToken("Bearer ", "gc_session=abc") == "");
  CHECK(gc::sessionToken("bearer abc", "gc_session=def") == "def");
  // The name matches whole and in its case
  CHECK(gc::sessionToken("", "GC_SESSION=abc") == "");
  CHECK(gc::sessionToken("", "gc_session") == "");
  CHECK(gc::sessionToken("", "gc_session; theme=a") == "");
  CHECK(gc::sessionToken("", "=abc; gc_session=def") == "def");
  // Spaces, tabs and empty pairs around it
  CHECK(gc::sessionToken("", " gc_session = abc ") == "abc");
  CHECK(gc::sessionToken("", "\tgc_session=abc") == "abc");
  CHECK(gc::sessionToken("", ";;gc_session=abc;;") == "abc");
  // The value as sent: not unquoted, an '=' in it kept
  CHECK(gc::sessionToken("", "gc_session=\"abc\"") == "\"abc\"");
  CHECK(gc::sessionToken("", "gc_session=a=b") == "a=b");
  // A long header of separators only
  CHECK(gc::sessionToken("", std::string(8192, ';')) == "");
  CHECK(gc::sessionToken("", std::string(8192, '=')) == "");
}

TEST_CASE("API: Import prüft Grenzen und Kalibrierungen, nicht während eines Auftrags (R7)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  gc::json cfg = c.ok("GET", "/api/v1/config/export");
  if (cfg.contains("config")) cfg = cfg["config"];
  gc::json bad = cfg;
  bad["limits"]["maxRunS"] = 600;
  CHECK(c.call("POST", "/api/v1/config/import", bad).first == 422);
  bad = cfg;
  bad["grow"]["phases"][0]["params"]["ph_tolerance"] = 0;
  CHECK(c.call("POST", "/api/v1/config/import", bad).first == 422);
  bad = cfg;
  bad["calibrations"]["PHEC-3F2A91"]["ph"] = {{"points", "kaputt"}};
  CHECK(c.call("POST", "/api/v1/config/import", bad).first == 422);
  // Falsche Typen bringen den Server nicht zum Absturz
  CHECK(c.call("POST", "/api/v1/mix/plan", {{"recipe", 5}, {"waterL", "zehn"}, {"confirmRepeat", "ja"}}).first < 500);
  CHECK(c.call("PUT", "/api/v1/roles/tank.ph", {{"device", "PHEC-3F2A91"}, {"channel", 99}}).first == 422);
  CHECK(c.call("POST", "/api/v1/grow/start", {{"phases", "viele"}}).first >= 400);
  CHECK(c.call("GET", "/api/v1/state").first == 200);
  // Während einer Handgabe kein Import
  for (int i = 0; i < 600; ++i) {
    auto now = c.state();
    if (now["job"].is_null() && now["dosing"].is_null()) break;
    s.step(5000);
  }
  // Die Sperre gegen eine zweite Einrichtung kommt nie aus einer Datei
  gc::json old = cfg;
  old["system"]["passwordSet"] = false;
  c.ok("POST", "/api/v1/config/import", old);
  CHECK(c.ok("GET", "/api/v1/config")["system"]["passwordSet"] == true);
  c.ok("POST", "/api/v1/dose", {{"canister", "teil-a"}, {"ml", 3}});
  auto [busy, busyBody] = c.call("POST", "/api/v1/config/import", cfg);
  CHECK(busy == 409);
  CHECK(busyBody["error"]["key"] == "config.busy");
}

TEST_CASE("API: Passwort verloren → keine Ersteinrichtung über das Netz (EN 18031 AUM)") {
  gc::Catalog cat = gc::Catalog::builtin();
  gc::MemoryStorage store;
  test::FakeBus bus;
  test::Clock clk;
  auto rng = [](std::uint8_t* p, size_t n) {
    static std::uint8_t x = 7;
    for (size_t i = 0; i < n; ++i) p[i] = x += 31;
  };
  auto setup = [&](gc::Hub& h) {
    gc::Api api(h, clk);
    gc::ApiRequest r;
    r.method = "POST";
    r.path = "/api/v1/auth/setup";
    r.body = R"({"password":"mein-passwort"})";
    return api.handle(r).status;
  };
  {
    gc::Hub h(cat, bus, store, clk, rng);
    h.boot();
    CHECK(setup(h) == 200);
    h.flush();
  }
  store.write("auth.json", "{}");  // Datei beschädigt oder verloren
  gc::Hub h(cat, bus, store, clk, rng);
  h.boot();
  CHECK(h.credentialsLost());
  CHECK(h.armSetupCode().empty());  // no code for a setup that cannot happen (#79)
  CHECK(setup(h) == 423);
}

TEST_CASE("Demo: jede Phase verweist auf ein vorhandenes Rezept") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  auto cfg = c.ok("GET", "/api/v1/config");
  std::set<std::string> ids;
  for (const auto& r : cfg["recipes"]) ids.insert(r["id"].get<std::string>());
  REQUIRE(cfg["grow"]["phases"].size() >= 2);
  for (const auto& ph : cfg["grow"]["phases"]) {
    INFO(ph["name"].get<std::string>());
    CHECK(ids.count(ph["params"]["recipe"].get<std::string>()) == 1);
  }
}

TEST_CASE("Kurznamen: Umlaute werden umschrieben") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  auto j = c.ok("POST", "/api/v1/canisters", {{"name", "Blüte Größe Ä"}, {"kind", "nutrient"}, {"pump", ""}});
  CHECK(j["id"] == "bluete-groesse-ae");
}

TEST_CASE("Vorlagen: Zuordnung über Rollen, sonst über Namen, sonst Liste der Fehlenden") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  // Demo hat Teil A, Teil B, CalMag → die Grundvorlage passt über die Namen
  auto j = c.ok("POST", "/api/v1/recipes/template", {{"id", "two_part_basic"}});
  auto cfg = c.ok("GET", "/api/v1/config");
  const gc::json* made = nullptr;
  for (const auto& r : cfg["recipes"])
    if (r["id"] == j["id"]) made = &r;
  REQUIRE(made);
  CHECK((*made)["steps"].size() == 3);
  // Andere Namen: ohne Zuordnung 422 mit den fehlenden Rollen, mit Zuordnung angelegt
  auto [st, err] = c.call("POST", "/api/v1/recipes/template", {{"id", "athena_blended_veg"}});
  CHECK(st == 422);
  CHECK(err["missing"].size() == 3);
  c.ok("POST", "/api/v1/recipes/template", {{"id", "athena_blended_veg"}, {"map", {{"a", "teil-a"}, {"b", "teil-b"}, {"calmag", "calmag"}}}});
  auto [nf, nfBody] = c.call("POST", "/api/v1/recipes/template", {{"id", "gibtsnicht"}});
  CHECK(nf == 404);
  CHECK(nfBody["error"]["key"] == "recipe.template.unknown");
}

TEST_CASE("Recipes: a template brings its chart EC and pH range; null clears them, a wrong range is refused (SD-057)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  auto recipe = [&](const gc::json& id) {
    const auto cfg = c.ok("GET", "/api/v1/config");
    for (const auto& r : cfg["recipes"])
      if (r["id"] == id) return r;
    return gc::json();
  };
  const gc::json id = c.ok("POST", "/api/v1/recipes/template",
                           {{"id", "athena_blended_veg"}, {"map", {{"a", "teil-a"}, {"b", "teil-b"}, {"calmag", "calmag"}}}})["id"];
  auto veg = recipe(id);
  CHECK(veg["refEc"].get<double>() == doctest::Approx(2.1));
  REQUIRE(veg["ph"].size() == 2);
  CHECK(veg["ph"][0].get<double>() == doctest::Approx(5.8));
  CHECK(veg["ph"][1].get<double>() == doctest::Approx(6.2));
  // A save without the keys keeps them; null clears them, never to 0 (R5)
  c.ok("POST", "/api/v1/recipes", {{"id", id}, {"name", "Veg"}});
  CHECK(recipe(id)["refEc"].get<double>() == doctest::Approx(2.1));
  CHECK(recipe(id)["ph"][1].get<double>() == doctest::Approx(6.2));
  c.ok("POST", "/api/v1/recipes", {{"id", id}, {"refEc", nullptr}, {"ph", nullptr}});
  CHECK(recipe(id)["refEc"].is_null());
  CHECK(recipe(id)["ph"].is_null());
  c.ok("POST", "/api/v1/recipes", {{"id", id}, {"refEc", 2.0}, {"ph", {5.7, 6.1}}});
  CHECK(recipe(id)["refEc"].get<double>() == doctest::Approx(2.0));
  CHECK(recipe(id)["ph"][1].get<double>() == doctest::Approx(6.1));
  // Refused with the reason, and the recipe stays as it was
  auto [st1, e1] = c.call("POST", "/api/v1/recipes", {{"id", id}, {"ph", {6.2, 5.8}}});
  CHECK(st1 == 422);
  CHECK(e1.dump().find("cfg.recipe.ph_band") != std::string::npos);
  auto [st2, e2] = c.call("POST", "/api/v1/recipes", {{"id", id}, {"ph", "5.8"}});
  CHECK(st2 == 422);
  CHECK(e2["error"]["key"] == "recipe.ph");
  auto [st3, e3] = c.call("POST", "/api/v1/recipes", {{"id", id}, {"refEc", "2,1"}});
  CHECK(st3 == 422);
  CHECK(e3["error"]["key"] == "recipe.ref_ec");
  auto [st4, e4] = c.call("POST", "/api/v1/recipes", {{"id", id}, {"refEc", 0.4}});  // 0.06 per ml/L over 6.8 ml/L: a typo?
  CHECK(st4 == 422);
  CHECK(e4.dump().find("cfg.recipe.ref_far") != std::string::npos);
  // The error itself is the first finding, key and arguments included, so a German page translates it
  CHECK(e4["error"] == e4["errors"][0]);
  CHECK(e4["error"]["key"] == "cfg.recipe.ref_far");
  for (const gc::json& half : {gc::json::array({5.8}), gc::json::array({nullptr, 6.2})}) {
    auto [st, e] = c.call("POST", "/api/v1/recipes", {{"id", id}, {"ph", half}});
    CHECK(st == 422);
    CHECK(e["error"]["key"] == "recipe.ph");
  }
  auto [st5, e5] = c.call("POST", "/api/v1/recipes", {{"id", id}, {"refEc", 5.0}});  // above the target's range
  CHECK(st5 == 422);
  CHECK(e5.dump().find("cfg.recipe.ref_range") != std::string::npos);
  CHECK(recipe(id)["refEc"].get<double>() == doctest::Approx(2.0));
  CHECK(recipe(id)["ph"][0].get<double>() == doctest::Approx(5.7));
  // A new recipe keeps both; the flowering template brings its own
  const gc::json made = c.ok("POST", "/api/v1/recipes",
                             {{"name", "Neu"}, {"steps", {{{"canister", "teil-a"}, {"mlPerL", 2.0}}, {{"canister", "teil-b"}, {"mlPerL", 2.0}}}},
                              {"refEc", 1.2}, {"ph", {5.8, 6.0}}})["id"];
  CHECK(recipe(made)["refEc"].get<double>() == doctest::Approx(1.2));
  CHECK(recipe(made)["ph"][1].get<double>() == doctest::Approx(6.0));
  const gc::json bloom = c.ok("POST", "/api/v1/recipes/template",
                              {{"id", "athena_blended_bloom"}, {"map", {{"a", "teil-a"}, {"b", "teil-b"}, {"calmag", "calmag"}}}})["id"];
  CHECK(recipe(bloom)["refEc"].get<double>() == doctest::Approx(2.3));
  // An import is checked the same way and changes nothing when refused
  gc::json cfg = c.ok("GET", "/api/v1/config");
  for (auto& r : cfg["recipes"])
    if (r["id"] == id) r["refEc"] = 0.4;
  CHECK(c.call("POST", "/api/v1/config/import", cfg).first == 422);
  CHECK(recipe(id)["refEc"].get<double>() == doctest::Approx(2.0));
}

TEST_CASE("Recipes: a fixed part is refused for a pair and as anything but yes or no; fixing one scales the learned effect up (SD-057, RAT-085)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  auto steps = [](bool calmagFixed, const gc::json& aFixed) {
    return gc::json::array({{{"canister", "teil-a"}, {"mlPerL", 2.0}, {"fixed", aFixed}},
                            {{"canister", "teil-b"}, {"mlPerL", 2.0}},
                            {{"canister", "calmag"}, {"mlPerL", 0.6}, {"fixed", calmagFixed}}});
  };
  auto [st1, e1] = c.call("POST", "/api/v1/recipes", {{"id", "wachstum"}, {"steps", steps(false, true)}});
  CHECK(st1 == 422);
  CHECK(e1.dump().find("cfg.recipe.fixed_pair") != std::string::npos);
  for (const gc::json& wrong : {gc::json("true"), gc::json(1)}) {
    auto [st, e] = c.call("POST", "/api/v1/recipes", {{"id", "wachstum"}, {"steps", steps(false, wrong)}});
    CHECK(st == 422);
    CHECK(e["error"]["key"] == "recipe.fixed");
  }
  // EC control's recipe: CalMag (0.6 of 4.6 ml/L) fixed leaves the learned effect on 4.0 ml/L, so it grows
  // by 4.6 / 4.0 and the next round doses less; unticking it again keeps the larger effect
  for (int h = 0; h < 12 && !c.state()["effects"]["ec"].is_number(); ++h) s.fastForward(1);
  REQUIRE(c.state()["effects"]["ec"].is_number());
  const double learned = c.state()["effects"]["ec"].get<double>();
  c.ok("POST", "/api/v1/recipes", {{"id", "wachstum"}, {"steps", steps(true, false)}});
  CHECK(c.state()["effects"]["ec"].get<double>() == doctest::Approx(learned * 4.6 / 4.0));
  c.ok("POST", "/api/v1/recipes", {{"id", "wachstum"}, {"steps", steps(false, false)}});
  CHECK(c.state()["effects"]["ec"].get<double>() == doctest::Approx(learned * 4.6 / 4.0));
  auto effect = [&] { return c.state()["effects"]["ec"].get<double>(); };
  auto idOf = [&](const std::string& name) {
    const gc::json cfg = c.ok("GET", "/api/v1/config");
    for (const auto& r : cfg["recipes"])
      if (r["name"] == name) return r["id"].get<std::string>();
    return std::string();
  };
  // A recipe EC control does not use leaves its effect alone
  double e = effect();
  c.ok("POST", "/api/v1/recipes",
       {{"id", idOf("Blüte")},
        {"steps", {{{"canister", "teil-a"}, {"mlPerL", 2.6}}, {{"canister", "teil-b"}, {"mlPerL", 2.6}}, {{"canister", "calmag"}, {"mlPerL", 0.4}, {"fixed", true}}}}});
  CHECK(effect() == doctest::Approx(e));
  // Ticked while a round settles: that round was planned on the old parts and learns nothing
  s.world().fill(40, 0.6, 5.9);
  auto line = [&] { return c.state()["controllers"]["ec"]["line"]["key"].get<std::string>(); };
  for (int i = 0; i < 2 * 720 && line() != "ec.settling"; ++i) s.step(5000);
  REQUIRE(line() == "ec.settling");
  e = effect();
  c.ok("POST", "/api/v1/recipes", {{"id", "wachstum"}, {"steps", steps(true, false)}});
  const double scaled = e * 4.6 / 4.0;
  CHECK(effect() == doctest::Approx(scaled));
  for (int i = 0; i < 720 && line() == "ec.settling"; ++i) s.step(5000);
  CHECK(line() != "ec.settling");
  CHECK(effect() == doctest::Approx(scaled));
  // EC control set to a recipe that was deleted uses the first one, and so does the scaling
  c.ok("POST", "/api/v1/grow/complete", gc::json::object());  // the phase's recipe would win (R4)
  const gc::json gone = c.ok("POST", "/api/v1/recipes", {{"name", "Weg"}, {"steps", {{{"canister", "teil-a"}, {"mlPerL", 1.0}}, {{"canister", "teil-b"}, {"mlPerL", 1.0}}}}})["id"];
  c.ok("PATCH", "/api/v1/functions/ec_control", {{"params", {{"recipe", gone}}}});
  CHECK(c.call("DELETE", "/api/v1/recipes/" + gone.get<std::string>()).first == 200);
  c.ok("POST", "/api/v1/recipes", {{"id", "wachstum"}, {"steps", steps(false, false)}});
  e = effect();
  c.ok("POST", "/api/v1/recipes", {{"id", "wachstum"}, {"steps", steps(true, false)}});
  CHECK(effect() == doctest::Approx(e * 4.6 / 4.0));
}

TEST_CASE("Templates: the recipe is stored in the language of the request, by default the system language (#32)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  auto recipe = [&](const gc::json& id) {
    const auto cfg = c.ok("GET", "/api/v1/config");
    for (const auto& r : cfg["recipes"])
      if (r["id"] == id) return r;
    return gc::json();
  };
  c.ok("PUT", "/api/v1/system", {{"language", "en"}});
  auto veg = recipe(c.ok("POST", "/api/v1/recipes/template",
                         {{"id", "athena_blended_veg"}, {"map", {{"a", "teil-a"}, {"b", "teil-b"}, {"calmag", "calmag"}}}})["id"]);
  CHECK(veg["name"] == "Vegetative wk 1–4 (per Athena A01.004)");
  CHECK(veg["note"].get<std::string>().find("Manufacturer data, not binding") != std::string::npos);
  // The request names the language of the page, which may differ from the hub's
  auto basic = recipe(c.ok("POST", "/api/v1/recipes/template", {{"id", "two_part_basic"}, {"lang", "de"}})["id"]);
  CHECK(basic["name"] == "Zweikomponenten-Dünger");
  auto [st, err] = c.call("POST", "/api/v1/recipes/template", {{"id", "two_part_basic"}, {"lang", "fr"}});
  CHECK(st == 422);
  CHECK(err["error"]["key"] == "recipe.template.lang");
}

TEST_CASE("Templates: missing parts are named in the language of the request; English part names match too (#32)") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  auto [st, err] = c.call("POST", "/api/v1/recipes/template", {{"id", "two_part_basic"}, {"lang", "en"}});
  CHECK(st == 422);
  CHECK(err["missing"] == gc::json::array({"Part B", "Part A", "CalMag"}));
  // English canister names match the template's English part names
  for (const char* n : {"Part A", "Part B", "CalMag"}) c.ok("POST", "/api/v1/canisters", {{"name", n}, {"kind", "nutrient"}, {"pump", ""}});
  auto j = c.ok("POST", "/api/v1/recipes/template", {{"id", "two_part_basic"}, {"lang", "en"}});
  const auto cfg = c.ok("GET", "/api/v1/config");
  REQUIRE(cfg["recipes"].size() == 1);
  CHECK(cfg["recipes"][0]["id"] == j["id"]);
  CHECK(cfg["recipes"][0]["steps"].size() == 3);
}

TEST_CASE("Templates: an unknown system language in an imported configuration falls back to English (#32, SD-014)") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  for (const char* n : {"Part A", "Part B", "CalMag"}) c.ok("POST", "/api/v1/canisters", {{"name", n}, {"kind", "nutrient"}, {"pump", ""}});
  gc::json cfg = c.ok("GET", "/api/v1/config/export");
  if (cfg.contains("config")) cfg = cfg["config"];
  cfg["system"]["language"] = "xx";
  // The import does not check the language yet; if it does one day, this
  // case has to expect its 422 instead.
  c.ok("POST", "/api/v1/config/import", cfg);
  auto j = c.ok("POST", "/api/v1/recipes/template", {{"id", "two_part_basic"}});
  const auto after = c.ok("GET", "/api/v1/config");
  REQUIRE(after["recipes"].size() == 1);
  CHECK(after["recipes"][0]["id"] == j["id"]);
  CHECK(after["recipes"][0]["name"] == "Two-part nutrient");
}

TEST_CASE("Templates: with both names present, the one in the language of the request wins (#32)") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  for (const char* n : {"Teil A", "Teil B", "Part A", "Part B", "CalMag"}) c.ok("POST", "/api/v1/canisters", {{"name", n}, {"kind", "nutrient"}, {"pump", ""}});
  auto steps = [&](const char* lang) {
    auto j = c.ok("POST", "/api/v1/recipes/template", {{"id", "two_part_basic"}, {"lang", lang}});
    const auto cfg = c.ok("GET", "/api/v1/config");
    std::set<std::string> used;
    for (const auto& r : cfg["recipes"])
      if (r["id"] == j["id"])
        for (const auto& st : r["steps"]) used.insert(st["canister"].get<std::string>());
    return used;
  };
  CHECK(steps("en") == std::set<std::string>{"part-a", "part-b", "calmag"});
  CHECK(steps("de") == std::set<std::string>{"teil-a", "teil-b", "calmag"});
}

TEST_CASE("System: Sprache nur de oder en") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  // A new hub speaks English, and says so before anyone signs in (SD-014, SD-083)
  CHECK(c.ok("GET", "/api/v1/info")["language"] == "en");
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  c.ok("PUT", "/api/v1/system", {{"language", "de"}});
  CHECK(c.ok("GET", "/api/v1/config")["system"]["language"] == "de");
  CHECK(Client{s}.ok("GET", "/api/v1/info")["language"] == "de");  // the sign-in page of a browser without a choice
  CHECK(c.call("PUT", "/api/v1/system", {{"language", "fr"}}).first == 422);
  CHECK(c.ok("GET", "/api/v1/config")["system"]["language"] == "de");
}

TEST_CASE("Vorlagen: ein Kanister für zwei Teile wird abgelehnt, Paar wird übernommen") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  for (const char* id : {"DB-7A31C0", "CAP-1F02A4", "CAP-1F02B7", "CAP-1F02C1"}) c.ok("POST", std::string("/api/v1/devices/") + id + "/accept", {{"name", ""}});
  auto a = c.ok("POST", "/api/v1/canisters", {{"name", "Eins"}, {"kind", "nutrient"}, {"pump", "CAP-1F02A4"}})["id"];
  auto b = c.ok("POST", "/api/v1/canisters", {{"name", "Zwei"}, {"kind", "nutrient"}, {"pump", "CAP-1F02B7"}})["id"];
  auto m = c.ok("POST", "/api/v1/canisters", {{"name", "Drei"}, {"kind", "nutrient"}, {"pump", "CAP-1F02C1"}})["id"];
  auto [st, err] = c.call("POST", "/api/v1/recipes/template", {{"id", "two_part_basic"}, {"map", {{"a", a}, {"b", a}, {"calmag", m}}}});
  CHECK(st == 422);
  CHECK(err["error"]["key"] == "recipe.template.twice");
  c.ok("POST", "/api/v1/recipes/template", {{"id", "two_part_basic"}, {"map", {{"a", a}, {"b", b}, {"calmag", m}}}});
  auto cfg = c.ok("GET", "/api/v1/config");
  for (const auto& k : cfg["canisters"]) {
    if (k["id"] == a || k["id"] == b) CHECK(k["pair"] == "AB");
    if (k["id"] == m) CHECK(k["pair"] == "");
  }
  // Über die API direkt: Rezept mit demselben Kanister zweimal
  CHECK(c.call("POST", "/api/v1/recipes", {{"name", "Doppelt"}, {"steps", {{{"canister", m}, {"mlPerL", 1}}, {{"canister", m}, {"mlPerL", 1}}}}}).first == 422);
}

TEST_CASE("API: a stock entered is never below zero (SD-066)") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  auto id = c.ok("POST", "/api/v1/canisters", {{"name", "Eins"}, {"kind", "nutrient"}, {"pump", ""}, {"capacityMl", 1000}, {"stockMl", 3}})["id"];
  auto [code, err] = c.call("POST", "/api/v1/canisters", {{"id", id}, {"name", "Eins"}, {"stockMl", -5}});
  CHECK(code == 422);
  CHECK(err["error"]["key"] == "canister.stock");
  CHECK(err["error"]["text"] == "The amount left can't be below 0 ml. Enter what is in the bottle.");
  CHECK(c.state()["stock"][id.get<std::string>()] == 3);
  CHECK(c.call("POST", "/api/v1/canisters/" + id.get<std::string>() + "/stock", {{"ml", -1}}).first == 422);
}

TEST_CASE("System: abgelehnte Änderung lässt alles unverändert") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  CHECK(c.call("PUT", "/api/v1/system", {{"name", "Neu"}, {"language", "fr"}}).first == 422);
  CHECK(c.ok("GET", "/api/v1/config")["system"]["name"] == "growcontroller");
}

TEST_CASE("Zone: Name und Art setzen, unbekannte Art abgelehnt") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  auto z = c.ok("GET", "/api/v1/config")["zones"];
  REQUIRE(z.size() == 1);
  CHECK(z[0]["kind"] == "room");
  c.ok("PUT", "/api/v1/zone", {{"name", "Gewächshaus Süd"}, {"kind", "greenhouse"}});
  z = c.ok("GET", "/api/v1/config")["zones"];
  CHECK(z[0]["name"] == "Gewächshaus Süd");
  CHECK(z[0]["kind"] == "greenhouse");
  CHECK(c.call("PUT", "/api/v1/zone", {{"kind", "keller"}}).first == 422);
  CHECK(c.ok("GET", "/api/v1/config")["zones"][0]["kind"] == "greenhouse");
}

TEST_CASE("Vorlagen: Paarname schon vergeben → freier Name, A/B skalieren gemeinsam") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  // Demo: Teil A/B tragen Paar AB. Zwei neue, ungepaarte Kanister auf freien Pumpen
  c.ok("DELETE", "/api/v1/canisters/ph");  // pH− räumt Pumpe 4
  auto a2 = c.ok("POST", "/api/v1/canisters", {{"name", "Bloom A"}, {"kind", "nutrient"}, {"pump", "CAP-1F02D9"}})["id"];
  auto b2 = c.ok("POST", "/api/v1/canisters", {{"name", "Bloom B"}, {"kind", "nutrient"}, {"pump", ""}})["id"];
  c.ok("POST", "/api/v1/recipes/template", {{"id", "athena_blended_bloom"}, {"map", {{"a", a2}, {"b", b2}, {"calmag", "calmag"}}}});
  auto cfg = c.ok("GET", "/api/v1/config");
  std::string pa, pb, pTeilA;
  for (const auto& k : cfg["canisters"]) {
    if (k["id"] == a2) pa = k["pair"];
    if (k["id"] == b2) pb = k["pair"];
    if (k["id"] == "teil-a") pTeilA = k["pair"];
  }
  CHECK(pTeilA == "AB");
  CHECK_FALSE(pa.empty());
  CHECK(pa == pb);
  CHECK(pa != "AB");
}

TEST_CASE("Namen: Kürzen auf 40 Zeichen zerschneidet keinen Umlaut") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  const std::string lang = std::string(39, 'x') + "ü" + "ende";  // „ü“ liegt auf der 40-Byte-Grenze
  c.ok("PUT", "/api/v1/zone", {{"name", lang}});
  c.ok("PUT", "/api/v1/system", {{"name", lang}});
  auto cfg = c.ok("GET", "/api/v1/config");  // muss sich weiter lesen lassen
  CHECK(cfg["zones"][0]["name"].get<std::string>().size() <= 40);
  CHECK(cfg["system"]["name"].get<std::string>() == std::string(39, 'x'));
}

TEST_CASE("System: what the app is to do per area is stored, only known areas and modes, and read back as saved (SD-037)") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  CHECK(c.ok("GET", "/api/v1/config")["system"]["control"] == gc::json::object());  // nothing chosen yet
  c.ok("PUT", "/api/v1/system", {{"control", {{"light", "outside"}, {"refill", "auto"}}}});
  CHECK(c.ok("GET", "/api/v1/config")["system"]["control"] == gc::json{{"light", "outside"}, {"refill", "auto"}});
  // A later choice adds to it; null takes one back
  c.ok("PUT", "/api/v1/system", {{"control", {{"light", nullptr}, {"climate", "outside"}}}});
  CHECK(c.ok("GET", "/api/v1/config")["system"]["control"] == gc::json{{"climate", "outside"}, {"refill", "auto"}});
  // Unknown areas and modes are refused, and a refusal changes nothing
  CHECK(c.call("PUT", "/api/v1/system", {{"control", {{"kitchen", "auto"}}}}).first == 422);
  CHECK(c.call("PUT", "/api/v1/system", {{"control", {{"light", "sometimes"}}}}).first == 422);
  CHECK(c.call("PUT", "/api/v1/system", {{"control", "auto"}}).first == 422);
  for (const gc::json& mode : {gc::json(1), gc::json(true), gc::json::array({"auto"}), gc::json{{"mode", "auto"}}})
    CHECK(c.call("PUT", "/api/v1/system", {{"control", {{"light", mode}}}}).first == 422);
  CHECK(c.call("PUT", "/api/v1/system", {{"control", {{"kitchen", nullptr}}}}).first == 422);
  // Keys come in order: the valid take-back of climate comes first and is undone with the refusal
  CHECK(c.call("PUT", "/api/v1/system", {{"control", {{"climate", nullptr}, {"light", "sometimes"}}}}).first == 422);
  c.ok("PUT", "/api/v1/system", {{"control", gc::json::object()}});  // nothing to change
  const auto [st, err] = c.call("PUT", "/api/v1/system", {{"name", "Neu"}, {"control", {{"light", "sometimes"}}}});
  CHECK(st == 422);
  CHECK(err["error"]["key"] == "system.control");
  gc::json cfg = c.ok("GET", "/api/v1/config");
  CHECK(cfg["system"]["name"] == "growcontroller");
  CHECK(cfg["system"]["control"] == gc::json{{"climate", "outside"}, {"refill", "auto"}});
  // Read back from a saved configuration: what is unknown there is left out, a configuration
  // written before this field has nothing chosen
  cfg["system"]["control"]["kitchen"] = "auto";
  cfg["system"]["control"]["light"] = "sometimes";
  const gc::Config back = gc::configFromJson(cfg);
  CHECK(back.system.control == std::map<std::string, std::string>{{"climate", "outside"}, {"refill", "auto"}});
  // A value that is no string is left out too, and does not make the whole file unreadable
  cfg["system"]["control"]["dosing"] = 1;
  cfg["system"]["control"]["circulation"] = nullptr;
  cfg["system"]["control"]["irrigation"] = {{"mode", "auto"}};
  CHECK_NOTHROW(gc::configFromJson(cfg));
  CHECK(gc::configFromJson(cfg).system.control == std::map<std::string, std::string>{{"climate", "outside"}, {"refill", "auto"}});
  for (const gc::json& v : {gc::json("auto"), gc::json::array()}) {
    gc::json odd = cfg;
    odd["system"]["control"] = v;
    CHECK(gc::configFromJson(odd).system.control.empty());
  }
  cfg["system"].erase("control");
  CHECK(gc::configFromJson(cfg).system.control.empty());
}

TEST_CASE("System: a time zone that is no zone name is refused when saved and imported; one saved before falls back when the file is read") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  for (const char* tz : {"Europe/Berlin", "America/Argentina/Buenos_Aires", "America/Port-au-Prince", "Etc/GMT+1", "Etc/GMT-14", "UTC", "EST5EDT"}) {
    CAPTURE(tz);
    c.ok("PUT", "/api/v1/system", {{"timezone", tz}});
    CHECK(c.ok("GET", "/api/v1/config")["system"]["timezone"] == tz);
  }
  c.ok("PUT", "/api/v1/system", {{"timezone", "Europe/Vienna"}});
  const std::string tooLong = "Europe/" + std::string(58, 'x');  // 65 characters
  for (const gc::json& tz : {gc::json(""), gc::json("Mars Olympus"), gc::json("../etc/passwd"), gc::json("Europe//Berlin"), gc::json("/Europe"),
                             gc::json("Europe/"), gc::json("Europe/Berlin\n"), gc::json("<b>Berlin</b>"), gc::json("1Europe"), gc::json("Europe/Zürich"), gc::json("+01:00"), gc::json(tooLong),
                             gc::json(42), gc::json(nullptr), gc::json::array({"Europe/Berlin"})}) {
    CAPTURE(tz.dump());
    // Refused with its reason, and a refusal changes nothing, not even the name sent along
    const auto [st, err] = c.call("PUT", "/api/v1/system", {{"name", "Neu"}, {"timezone", tz}});
    CHECK(st == 422);
    CHECK(err["error"]["key"] == "system.timezone");
    const gc::json sys = c.ok("GET", "/api/v1/config")["system"];
    CHECK(sys["timezone"] == "Europe/Vienna");
    CHECK(sys["name"] == "growcontroller");
  }
  CHECK(gc::isTimeZoneName("America/Argentina/ComodRivadavia"));
  CHECK(gc::isTimeZoneName("Europe/" + std::string(57, 'x')));  // 64 characters
  for (const char* tz : {"GMT0", "GMT-0", "W-SU", "Etc/UCT", "America/North_Dakota/New_Salem", "posixrules", "Factory"}) {
    CAPTURE(tz);
    CHECK(gc::isTimeZoneName(tz));
  }
  for (const std::string& tz : {std::string("Europe/../Berlin"), std::string("Europe/Berlin "), std::string("Europe/Berlin\0x", 15), std::string("Europe\\Berlin")}) {
    CAPTURE(tz);
    CHECK_FALSE(gc::isTimeZoneName(tz));
  }

  // A file chosen to import says so; the configuration stays as it was
  gc::json file = c.ok("GET", "/api/v1/config/export");
  file["system"]["name"] = "Aus der Datei";
  for (const gc::json& tz : {gc::json("Mars Olympus"), gc::json(42)}) {
    file["system"]["timezone"] = tz;
    const auto [st, err] = c.call("POST", "/api/v1/config/import", file);
    CHECK(st == 422);
    CHECK(err["error"]["key"] == "system.timezone");
  }
  CHECK(c.ok("GET", "/api/v1/config")["system"]["name"] == "growcontroller");
  file["system"]["timezone"] = "Asia/Tokyo";
  c.ok("POST", "/api/v1/config/import", file);
  CHECK(c.ok("GET", "/api/v1/config")["system"]["timezone"] == "Asia/Tokyo");

  // Read from a saved file (stored before the hub checked it): the default, and the file stays readable
  for (const gc::json& tz : {gc::json("Mars Olympus"), gc::json(42), gc::json(nullptr)}) {
    CAPTURE(tz.dump());
    file["system"]["timezone"] = tz;
    const gc::Config back = gc::configFromJson(file);
    CHECK(back.system.timezone == gc::SystemCfg{}.timezone);
    CHECK(back.system.name == "Aus der Datei");
  }
}

TEST_CASE("System: the choice per area goes along with an export and an import, unknown entries are left out (SD-037)") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  c.ok("PUT", "/api/v1/system", {{"control", {{"climate", "outside"}}}});
  gc::json file = c.ok("GET", "/api/v1/config/export");
  CHECK(file["system"]["control"] == gc::json{{"climate", "outside"}});
  file["system"]["control"] = {{"light", "auto"}, {"kitchen", "auto"}, {"dosing", "sometimes"}};
  c.ok("POST", "/api/v1/config/import", file);  // left out, not refused, as when the file is loaded
  CHECK(c.ok("GET", "/api/v1/config")["system"]["control"] == gc::json{{"light", "auto"}});
  // An import replaces: a file without the field has nothing chosen
  file["system"].erase("control");
  c.ok("POST", "/api/v1/config/import", file);
  CHECK(c.ok("GET", "/api/v1/config")["system"]["control"] == gc::json::object());
}

namespace {
gc::json canisterById(Client& c, const std::string& id) {
  const gc::json cfg = c.ok("GET", "/api/v1/config");
  for (const auto& k : cfg["canisters"])
    if (k["id"] == id) return k;
  return nullptr;
}
void deleteRecipes(Client& c) {
  const gc::json cfg = c.ok("GET", "/api/v1/config");
  for (const auto& r : cfg["recipes"]) c.ok("DELETE", "/api/v1/recipes/" + r["id"].get<std::string>());
}
}  // namespace

TEST_CASE("Bottles: a bottle a recipe uses is refused removal; configuration and stock stay as they were") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  const gc::json before = canisterById(c, "calmag");
  const gc::json stock = c.state()["stock"]["calmag"];
  auto [code, body] = c.call("DELETE", "/api/v1/canisters/calmag");
  CHECK(code == 409);
  CHECK(body["error"]["key"] == "canister.used");
  CHECK(canisterById(c, "calmag") == before);
  CHECK(c.state()["stock"]["calmag"] == stock);
}

TEST_CASE("Bottles: removing one bottle of a pair leaves its partner unpaired, so later saves still work (#147)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  deleteRecipes(c);
  const gc::json partner = canisterById(c, "teil-b");
  const gc::json stockB = c.state()["stock"]["teil-b"];
  REQUIRE(partner["pair"] == "AB");
  c.ok("DELETE", "/api/v1/canisters/teil-a");
  CHECK(canisterById(c, "teil-b")["pair"] == "");
  // Only the label goes: the partner keeps its pump and stock; the removed bottle's stock is gone
  CHECK(canisterById(c, "teil-b")["pump"] == partner["pump"]);
  CHECK(c.state()["stock"]["teil-b"] == stockB);
  CHECK_FALSE(c.state()["stock"].contains("teil-a"));
  // The event names the partner that is no longer paired
  const gc::json ev = c.ok("GET", "/api/v1/events?limit=5&type=config")["events"].at(0);
  CHECK(ev["title"]["key"] == "ev.cfg.canister_removed_pair");
  CHECK(ev["title"]["args"].value("partner", "") == "Teil B");
  // A save the hub checks as a whole still works
  c.ok("POST", "/api/v1/recipes", {{"name", "Nur B"}, {"steps", {{{"canister", "teil-b"}, {"mlPerL", 1.0}}}}});
}

TEST_CASE("Bottles: removing one bottle of a pair of three leaves the other two a pair (#147)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  deleteRecipes(c);
  c.ok("POST", "/api/v1/canisters", {{"id", "calmag"}, {"pair", "AB"}});
  c.ok("DELETE", "/api/v1/canisters/teil-a");
  CHECK(canisterById(c, "teil-b")["pair"] == "AB");
  CHECK(canisterById(c, "calmag")["pair"] == "AB");
  CHECK(c.ok("GET", "/api/v1/events?limit=5&type=config")["events"].at(0)["title"]["key"] == "ev.cfg.canister_removed");
}

TEST_CASE("Bottles: removing a bottle without a pair, or one whose label is already alone, unpairs nothing (#147)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  c.ok("DELETE", "/api/v1/canisters/ph");  // no pair: CalMag, also without one, is not anyone's partner
  CHECK(canisterById(c, "calmag")["pair"] == "");
  CHECK(c.ok("GET", "/api/v1/events?limit=5&type=config")["events"].at(0)["title"]["key"] == "ev.cfg.canister_removed");
  const std::string solo = c.ok("POST", "/api/v1/canisters", {{"name", "Solo"}, {"kind", "nutrient"}, {"pair", "XY"}})["id"];
  c.ok("DELETE", "/api/v1/canisters/" + solo);  // a label with no partner left
  CHECK(c.ok("GET", "/api/v1/events?limit=5&type=config")["events"].at(0)["title"]["key"] == "ev.cfg.canister_removed");
  CHECK(canisterById(c, "teil-a")["pair"] == "AB");
  CHECK(canisterById(c, "teil-b")["pair"] == "AB");
}

TEST_CASE("Bottles: editing one bottle out of a pair leaves its partner unpaired, and recipes still save (#152)") {
  for (const gc::json& edit : {gc::json{{"id", "teil-a"}, {"pair", ""}}, gc::json{{"id", "teil-a"}, {"pair", "XY"}}, gc::json{{"id", "teil-a"}, {"kind", "ph_down"}, {"pair", ""}}}) {
    CAPTURE(edit.dump());
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
    deleteRecipes(c);
    c.ok("POST", "/api/v1/canisters", edit);
    CHECK(canisterById(c, "teil-b")["pair"] == "");
    const gc::json ev = c.ok("GET", "/api/v1/events?limit=5&type=config")["events"].at(0);
    CHECK(ev["title"]["key"] == "ev.cfg.canister_unpairs");
    CHECK(ev["title"]["args"].value("partner", "") == "Teil B");
    // Moved into a new pair, Teil A is that pair's first bottle and may stand alone until its partner comes;
    // a recipe may hold it already. A pH bottle is in no recipe.
    gc::json steps = gc::json::array({gc::json{{"canister", "teil-b"}, {"mlPerL", 1.0}}});
    if (edit.value("kind", "nutrient") == "nutrient") steps.push_back({{"canister", "teil-a"}, {"mlPerL", 1.0}});
    c.ok("POST", "/api/v1/recipes", {{"name", "Weiter"}, {"steps", steps}});
  }
}

TEST_CASE("Bottles: an edit that would leave a recipe with half a pair is refused; a first partner may stand alone (#152)") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  // A bottle no recipe uses joins pair AB, while the recipes hold only Teil A and Teil B of it
  const std::string extra = c.ok("POST", "/api/v1/canisters", {{"name", "Bloom C"}, {"kind", "nutrient"}})["id"];
  const gc::json before = canisterById(c, extra);
  auto [code, body] = c.call("POST", "/api/v1/canisters", {{"id", extra}, {"pair", "AB"}});
  CHECK(code == 422);
  CHECK(body["errors"].at(0)["key"] == "cfg.recipe.pair");
  CHECK(canisterById(c, extra) == before);
  // CalMag is in both recipes: with it, pair AB stays whole in them
  c.ok("POST", "/api/v1/canisters", {{"id", "calmag"}, {"pair", "AB"}});
  c.ok("POST", "/api/v1/canisters", {{"id", "calmag"}, {"pair", ""}});
  // A new bottle with a new pair label may stand alone until its partner comes next; saved again, it keeps it
  const std::string bloom = c.ok("POST", "/api/v1/canisters", {{"name", "Bloom A"}, {"kind", "nutrient"}, {"pair", "CD"}})["id"];
  c.ok("POST", "/api/v1/canisters", {{"id", bloom}, {"name", "Bloom A1"}});
  CHECK(canisterById(c, bloom)["pair"] == "CD");
  CHECK(c.ok("GET", "/api/v1/events?limit=5&type=config")["events"].at(0)["title"]["key"] == "ev.cfg.canister");
  c.ok("POST", "/api/v1/canisters", {{"name", "Bloom B"}, {"kind", "nutrient"}, {"pair", "CD"}});
  c.ok("POST", "/api/v1/recipes", {{"name", "Weiter"}, {"steps", {{{"canister", "calmag"}, {"mlPerL", 1.0}}}}});
}

TEST_CASE("Bottles: two bottles a recipe holds one of pair up in either order, the recipe first (#152)") {
  for (const bool inRecipeFirst : {true, false}) {
    CAPTURE(inRecipeFirst);
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
    const std::string a = c.ok("POST", "/api/v1/canisters", {{"name", "Bloom A"}, {"kind", "nutrient"}})["id"];
    const std::string b = c.ok("POST", "/api/v1/canisters", {{"name", "Bloom B"}, {"kind", "nutrient"}})["id"];
    const std::string r = c.ok("POST", "/api/v1/recipes", {{"name", "Bloom"}, {"steps", {{{"canister", a}, {"mlPerL", 1.0}}}}})["id"];
    // The first label stands alone; the second would leave the recipe with half of pair CD
    c.ok("POST", "/api/v1/canisters", {{"id", inRecipeFirst ? a : b}, {"pair", "CD"}});
    auto [code, body] = c.call("POST", "/api/v1/canisters", {{"id", inRecipeFirst ? b : a}, {"pair", "CD"}});
    CHECK(code == 422);
    CHECK(body["errors"].at(0)["key"] == "cfg.recipe.pair");
    // The way out the refusal names: the recipe takes the other bottle first, then the pair is whole
    c.ok("POST", "/api/v1/recipes", {{"id", r}, {"steps", {{{"canister", a}, {"mlPerL", 1.0}}, {{"canister", b}, {"mlPerL", 1.0}}}}});
    c.ok("POST", "/api/v1/canisters", {{"id", inRecipeFirst ? b : a}, {"pair", "CD"}});
    CHECK(canisterById(c, a)["pair"] == "CD");
    CHECK(canisterById(c, b)["pair"] == "CD");
  }
}

TEST_CASE("Templates: a template pairs its parts while another bottle waits for its partner, and leaves no part alone with a label (#152)") {
  for (const bool partTaken : {false, true}) {
    CAPTURE(partTaken);
    sim::Simulation s(test::opts("demo"));
    Client c{s};
    c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
    deleteRecipes(c);
    c.ok("POST", "/api/v1/canisters", {{"id", "teil-a"}, {"pair", ""}});  // Teil B goes out of the pair with it
    const std::string solo = c.ok("POST", "/api/v1/canisters", {{"name", "Solo"}, {"kind", "nutrient"}, {"pair", "XY"}})["id"];
    if (partTaken)
      for (const char* id : {"teil-a", "calmag"}) c.ok("POST", "/api/v1/canisters", {{"id", id}, {"pair", "CM"}});
    c.ok("POST", "/api/v1/recipes/template", {{"id", "two_part_basic"}, {"map", {{"a", "teil-a"}, {"b", "teil-b"}, {"calmag", "calmag"}}}});
    // A part whose bottle has a pair already keeps it, and its partner gets no label of its own
    CHECK(canisterById(c, "teil-a")["pair"] == std::string(partTaken ? "CM" : "AB"));
    CHECK(canisterById(c, "teil-b")["pair"] == std::string(partTaken ? "" : "AB"));
    CHECK(canisterById(c, solo)["pair"] == "XY");
  }
}

TEST_CASE("Bottles: saving a bottle whose stock is unknown leaves it unknown, also when it first gets a size (RAT-015, #184)") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  const std::string id = c.ok("POST", "/api/v1/canisters", {{"name", "Booster"}, {"kind", "nutrient"}, {"capacityMl", nullptr}})["id"];
  CHECK(c.state()["stock"][id].is_null());
  // A size says how big the bottle is, not how full it is now
  c.ok("POST", "/api/v1/canisters", {{"id", id}, {"capacityMl", 1000}});
  CHECK(c.state()["stock"][id].is_null());
  for (const gc::json& edit : {gc::json{{"id", id}, {"name", "Booster neu"}}, gc::json{{"id", id}, {"color", "#336699"}}, gc::json{{"id", id}, {"pump", ""}},
                               gc::json{{"id", id}, {"stockMl", nullptr}}}) {
    CAPTURE(edit.dump());
    c.ok("POST", "/api/v1/canisters", edit);
    CHECK(c.state()["stock"][id].is_null());
  }
  // Only a bottle change or an amount entered gives it a number
  c.ok("POST", "/api/v1/canisters/" + id + "/stock", {{"ml", 1000}});
  CHECK(c.state()["stock"][id].get<double>() == doctest::Approx(1000));
  c.ok("POST", "/api/v1/canisters", {{"id", id}, {"stockMl", 400}});
  CHECK(c.state()["stock"][id].get<double>() == doctest::Approx(400));
  // A known stock stays through a rename and a new size; 0 is an amount entered, not unknown
  c.ok("POST", "/api/v1/canisters", {{"id", id}, {"name", "Booster"}, {"capacityMl", 2000}});
  CHECK(c.state()["stock"][id].get<double>() == doctest::Approx(400));
  c.ok("POST", "/api/v1/canisters", {{"id", id}, {"stockMl", 0}});
  CHECK(c.state()["stock"][id].get<double>() == doctest::Approx(0));
  // A new bottle whose amount is left empty on purpose stays unknown; one sent without the field at all
  // (as the setup does) is set up full
  const std::string blank = c.ok("POST", "/api/v1/canisters", {{"name", "Enzym"}, {"kind", "nutrient"}, {"capacityMl", 1000}, {"stockMl", nullptr}})["id"];
  CHECK(c.state()["stock"][blank].is_null());
  const std::string full = c.ok("POST", "/api/v1/canisters", {{"name", "Silizium"}, {"kind", "nutrient"}, {"capacityMl", 1000}})["id"];
  CHECK(c.state()["stock"][full].get<double>() == doctest::Approx(1000));
}
