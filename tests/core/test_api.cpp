// SPDX-License-Identifier: AGPL-3.0-or-later
#include <doctest/doctest.h>

#include <cstdint>
#include <set>

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
  CHECK(c.call("POST", "/api/v1/config/import", cfg).first == 409);
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
  CHECK(c.call("POST", "/api/v1/recipes/template", {{"id", "gibtsnicht"}}).first == 404);
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
  CHECK(c.call("POST", "/api/v1/recipes/template", {{"id", "two_part_basic"}, {"lang", "fr"}}).first == 422);
}

TEST_CASE("Templates: missing canisters are named and matched in the language of the request (#32)") {
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

TEST_CASE("System: Sprache nur de oder en") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  c.ok("PUT", "/api/v1/system", {{"language", "en"}});
  CHECK(c.ok("GET", "/api/v1/config")["system"]["language"] == "en");
  CHECK(c.call("PUT", "/api/v1/system", {{"language", "fr"}}).first == 422);
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
