#include <doctest/doctest.h>

#include "client.hpp"

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
