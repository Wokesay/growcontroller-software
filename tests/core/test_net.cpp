// Schaltbare Netzsteckdosen (Shelly im Simulator): Erkennen, Übernehmen,
// Schutzeinstellung im Gerät mit Rücklesen, Sicherheitsprofile im Gateway,
// Not-Halt und Stromausfall (Konzept PFLANZENAUTOMATISIERUNG §2–§3).
#include <doctest/doctest.h>

#include <cmath>

#include "client.hpp"

using gc::json;
using test::Client;

namespace {

template <typename F>
bool until(sim::Simulation& s, F cond, gc::Ms maxMs, gc::Ms step = 1000) {
  for (gc::Ms t = 0; t < maxMs; t += step) {
    if (cond()) return true;
    s.step(step);
  }
  return cond();
}

// Legt eine Dose oder Leiste im Simulator an, wartet, bis der Hub sie sieht,
// und übernimmt sie.
std::string addPlug(sim::Simulation& s, Client& c, const std::string& cls, const json& loads) {
  json r;
  {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    r = s.control("net_add", {{"class", cls}, {"loads", loads}});
  }
  std::string id = r["id"];
  REQUIRE(until(s, [&] {
    auto st = c.state();
    for (const auto& d : st["devices"])
      if (d["id"] == id && d["online"] == true) return true;
    return false;
  }, 10000));
  c.ok("POST", "/api/v1/devices/" + id + "/accept", {{"name", ""}});
  return id;
}

sim::NetOutlet& outlet(sim::Simulation& s, const std::string& id, int ch) { return s.world().netPlug(id)->outlets[static_cast<size_t>(ch)]; }

void fault(sim::Simulation& s, const std::string& id, const std::string& f) {
  std::lock_guard<std::recursive_mutex> l(s.mutex());
  s.control("fault", {{"device", id}, {"fault", f}});
}

}  // namespace

TEST_CASE("Steckdose: übernehmen setzt „nach Stromausfall aus“, Umwälzpumpe schaltet über die Dose") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  auto id = addPlug(s, c, "shelly_plug", {{{"load", "circulation"}, {"watts", 18}}});
  CHECK(outlet(s, id, 0).initialOff);
  c.ok("PUT", "/api/v1/roles/tank.circulation", {{"device", id}, {"channel", 0}});
  CHECK(std::isnan(outlet(s, id, 0).autoOffS));  // Profil dauer: kein Auto-Off
  c.ok("POST", "/api/v1/roles/tank.circulation/test");
  CHECK(outlet(s, id, 0).on);
  CHECK(s.world().circulating());
  s.step(4000);
  CHECK_FALSE(outlet(s, id, 0).on);  // Testen: 3 s
}

TEST_CASE("Steckdose: Handbetrieb und Leistung im Zustand") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  auto id = addPlug(s, c, "shelly_plug", {{{"load", "light"}, {"watts", 240}}});
  c.ok("PUT", "/api/v1/roles/zone.light", {{"device", id}, {"channel", 0}});
  c.ok("POST", "/api/v1/roles/zone.light/switch", {{"on", true}});
  s.step(2000);
  auto st = c.state();
  bool seen = false;
  for (const auto& d : st["devices"])
    if (d["id"] == id) seen = d["info"]["outlets"][0]["powerW"] == 240.0;
  CHECK(seen);
  CHECK(st["outputs"]["zone.light"] == true);
  CHECK(c.call("POST", "/api/v1/roles/zone.light/switch", {{"on", "ja"}}).first == 422);
  CHECK(c.call("POST", "/api/v1/roles/tank.ph/switch", {{"on", true}}).first == 404);
}

TEST_CASE("Steckdose: Profil puls setzt Auto-Off im Gerät, ohne bestätigtes Rücklesen keine Zuordnung") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  auto id = addPlug(s, c, "shelly_strip4", json::array());
  c.ok("PUT", "/api/v1/roles/zone.humidifier", {{"device", id}, {"channel", 0}});
  CHECK(outlet(s, id, 0).autoOffS == doctest::Approx(360));  // 5 min · 1,11 → 6 min
  fault(s, id, "ignore");  // Gerät meldet Erfolg, speichert aber nicht
  auto [st1, e1] = c.call("PUT", "/api/v1/roles/zone.irrigation_pump", {{"device", id}, {"channel", 1}});
  CHECK(st1 == 502);
  CHECK(e1["error"]["key"] == "role.net.verify");
  fault(s, id, "readonly");
  auto [st2, e2] = c.call("PUT", "/api/v1/roles/zone.irrigation_pump", {{"device", id}, {"channel", 1}});
  CHECK(st2 == 502);
  CHECK(e2["error"]["key"] == "role.net.write");
  CHECK_FALSE(c.ok("GET", "/api/v1/config")["zones"][0]["roles"].contains("zone.irrigation_pump"));
  // Eine Dose nur für eine Rolle
  fault(s, id, "none");
  CHECK(c.call("PUT", "/api/v1/roles/zone.light", {{"device", id}, {"channel", 0}}).first == 422);
}

TEST_CASE("Gateway: Befeuchter und Entfeuchter nie zugleich, Kompressor-Pause, Höchstlaufzeit") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  auto id = addPlug(s, c, "shelly_strip4", json::array());
  c.ok("PUT", "/api/v1/roles/zone.humidifier", {{"device", id}, {"channel", 0}});
  c.ok("PUT", "/api/v1/roles/zone.dehumidifier", {{"device", id}, {"channel", 1}});
  c.ok("POST", "/api/v1/roles/zone.humidifier/switch", {{"on", true}});
  auto [st, e] = c.call("POST", "/api/v1/roles/zone.dehumidifier/switch", {{"on", true}});
  CHECK(st == 409);
  CHECK(e["error"]["key"] == "act.climate.pair");
  // Höchstlaufzeit 5 min: der Hub schaltet aus, bevor das Gerät (6 min) es tut
  s.step(305 * 1000);
  CHECK_FALSE(outlet(s, id, 0).on);
  // Entfeuchter: an, aus, sofort wieder an → Mindestpause
  c.ok("POST", "/api/v1/roles/zone.dehumidifier/switch", {{"on", true}});
  c.ok("POST", "/api/v1/roles/zone.dehumidifier/switch", {{"on", false}});
  auto [st2, e2] = c.call("POST", "/api/v1/roles/zone.dehumidifier/switch", {{"on", true}});
  CHECK(st2 == 409);
  CHECK(e2["error"]["key"] == "act.compressor.pause");
  s.step(301 * 1000);
  c.ok("POST", "/api/v1/roles/zone.dehumidifier/switch", {{"on", true}});
}

TEST_CASE("Steckdose: Auto-Off im Gerät greift, wenn der Hub ausfällt") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  auto id = addPlug(s, c, "shelly_plug", json::array());
  c.ok("PUT", "/api/v1/roles/zone.irrigation_pump", {{"device", id}, {"channel", 0}});
  CHECK(outlet(s, id, 0).autoOffS == doctest::Approx(720));  // 10 min · 1,11 → 12 min
  // Jemand schaltet am Gerät selbst ein; der Hub weiß davon nichts
  outlet(s, id, 0).on = true;
  outlet(s, id, 0).onSince = s.world().now();
  s.world().advance(s.world().now() + 721 * 1000);
  CHECK_FALSE(outlet(s, id, 0).on);
}

TEST_CASE("Heizung: keine Heizrolle, bis die Notabschaltung nach RAT-060 steht") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  auto id = addPlug(s, c, "shelly_plug", json::array());
  for (const char* role : {"tank.heater", "zone.heater"})
    CHECK(c.call("PUT", std::string("/api/v1/roles/") + role, {{"device", id}, {"channel", 0}}).first == 422);
}

TEST_CASE("Gateway: unbekannter Zustand des Gegengeräts sperrt (R5)") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  auto a = addPlug(s, c, "shelly_plug", json::array());
  auto b = addPlug(s, c, "shelly_plug", json::array());
  c.ok("PUT", "/api/v1/roles/zone.humidifier", {{"device", a}, {"channel", 0}});
  c.ok("PUT", "/api/v1/roles/zone.dehumidifier", {{"device", b}, {"channel", 0}});
  fault(s, b, "offline");  // Entfeuchter vielleicht noch an
  s.step(2000);
  auto [st, e] = c.call("POST", "/api/v1/roles/zone.humidifier/switch", {{"on", true}});
  CHECK(st == 409);
  CHECK(e["error"]["key"] == "act.climate.pair_unknown");
}

TEST_CASE("Gateway: Kompressor-Pause gilt auch nach Not-Halt") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  auto id = addPlug(s, c, "shelly_plug", json::array());
  c.ok("PUT", "/api/v1/roles/zone.dehumidifier", {{"device", id}, {"channel", 0}});
  c.ok("POST", "/api/v1/roles/zone.dehumidifier/switch", {{"on", true}});
  c.ok("POST", "/api/v1/stop");
  c.ok("POST", "/api/v1/resume");
  auto [st, e] = c.call("POST", "/api/v1/roles/zone.dehumidifier/switch", {{"on", true}});
  CHECK(st == 409);
  CHECK(e["error"]["key"] == "act.compressor.pause");
  // Auch nach einem Neustart des Hubs
  s.step(301 * 1000);
  c.ok("POST", "/api/v1/roles/zone.dehumidifier/switch", {{"on", true}});
  {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    s.control("reboot", json::object());
  }
  c.ok("POST", "/api/v1/auth/login", {{"password", "mein-passwort"}});
  auto [st2, e2] = c.call("POST", "/api/v1/roles/zone.dehumidifier/switch", {{"on", true}});
  CHECK(st2 == 409);
  CHECK(e2["error"]["key"] == "act.compressor.pause");
}

TEST_CASE("Gateway: Schutzeinstellung im Gerät verloren → kein Einschalten") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  auto id = addPlug(s, c, "shelly_plug", json::array());
  c.ok("PUT", "/api/v1/roles/zone.humidifier", {{"device", id}, {"channel", 0}});
  outlet(s, id, 0).autoOffS = gc::kNaN;  // z. B. Werksreset in der Shelly-App
  auto [st, e] = c.call("POST", "/api/v1/roles/zone.humidifier/switch", {{"on", true}});
  CHECK(st == 409);
  CHECK(e["error"]["key"] == "act.net.safety");
  CHECK_FALSE(outlet(s, id, 0).on);
}

TEST_CASE("Gateway: Gießpumpe nur mit gültigem Füllstand, Befeuchter nur mit gültiger Feuchte") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  auto id = addPlug(s, c, "shelly_strip4", json::array());
  c.ok("PUT", "/api/v1/roles/zone.irrigation_pump", {{"device", id}, {"channel", 0}});
  auto [st, e] = c.call("POST", "/api/v1/roles/zone.irrigation_pump/switch", {{"on", true}});
  CHECK(st == 409);
  CHECK(e["error"]["key"] == "act.irrigation.level");
  // Feuchte zugeordnet, aber ohne gültigen Wert
  {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    s.control("plug", {{"port", 6}, {"class", "head_climate"}});
  }
  std::string clim;
  REQUIRE(until(s, [&] {
    auto st2 = c.state();
    for (const auto& d : st2["devices"])
      if (d["class"] == "head_climate" && d["online"] == true) clim = d["id"];
    return !clim.empty();
  }, 10000));
  c.ok("POST", "/api/v1/devices/" + clim + "/accept", {{"name", ""}});
  fault(s, clim, "offline");
  s.step(2000);
  c.ok("PUT", "/api/v1/roles/zone.humidifier", {{"device", id}, {"channel", 1}});
  auto [st3, e3] = c.call("POST", "/api/v1/roles/zone.humidifier/switch", {{"on", true}});
  CHECK(st3 == 409);
  CHECK(e3["error"]["key"] == "act.humidifier.rh");
}

TEST_CASE("Gateway: Umzuordnen und Entfernen schalten den alten Ausgang aus, Testen lässt Laufendes an") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  auto id = addPlug(s, c, "shelly_strip4", json::array());
  c.ok("PUT", "/api/v1/roles/zone.light", {{"device", id}, {"channel", 0}});
  c.ok("POST", "/api/v1/roles/zone.light/switch", {{"on", true}});
  c.ok("POST", "/api/v1/roles/zone.light/test");
  s.step(4000);
  CHECK(outlet(s, id, 0).on);  // war schon an, bleibt an
  c.ok("PUT", "/api/v1/roles/zone.light", {{"device", id}, {"channel", 1}});
  CHECK_FALSE(outlet(s, id, 0).on);
  c.ok("POST", "/api/v1/roles/zone.light/switch", {{"on", true}});
  CHECK(outlet(s, id, 1).on);
  c.ok("DELETE", "/api/v1/devices/" + id);
  CHECK_FALSE(outlet(s, id, 1).on);
}

TEST_CASE("Steckdose: Not-Halt und Stromausfall schalten aus, offline gibt Klartext") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  auto id = addPlug(s, c, "shelly_strip4", {{{"load", "light"}, {"watts", 240}}, {{"load", "exhaust"}, {"watts", 35}}});
  c.ok("PUT", "/api/v1/roles/zone.light", {{"device", id}, {"channel", 0}});
  c.ok("PUT", "/api/v1/roles/zone.exhaust", {{"device", id}, {"channel", 1}});
  c.ok("POST", "/api/v1/roles/zone.light/switch", {{"on", true}});
  c.ok("POST", "/api/v1/roles/zone.exhaust/switch", {{"on", true}});
  c.ok("POST", "/api/v1/stop");
  CHECK_FALSE(outlet(s, id, 0).on);
  CHECK_FALSE(outlet(s, id, 1).on);
  c.ok("POST", "/api/v1/resume");
  c.ok("POST", "/api/v1/roles/zone.light/switch", {{"on", true}});
  {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    s.control("reboot", json::object());
  }
  CHECK_FALSE(outlet(s, id, 0).on);  // nach Stromausfall aus (R6)
  c.ok("POST", "/api/v1/auth/login", {{"password", "mein-passwort"}});
  fault(s, id, "offline");
  s.step(2000);
  auto [st, e] = c.call("POST", "/api/v1/roles/zone.light/switch", {{"on", true}});
  CHECK(st == 409);
  CHECK(e["error"]["text"].get<std::string>().find("nicht erreichbar") != std::string::npos);
}

TEST_CASE("Gießpumpe: fällt der Füllstand im Lauf unter den Mindestfüllstand, geht sie aus") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  auto id = addPlug(s, c, "shelly_plug", json::array());
  c.ok("PUT", "/api/v1/roles/zone.irrigation_pump", {{"device", id}, {"channel", 0}});
  c.ok("POST", "/api/v1/roles/zone.irrigation_pump/switch", {{"on", true}});  // Demo: 31 L, Mindestfüllstand 3 L
  CHECK(outlet(s, id, 0).on);
  {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    s.control("water", {{"volumeL", 2.0}});
  }
  REQUIRE(until(s, [&] { return !outlet(s, id, 0).on; }, 60000));
}
