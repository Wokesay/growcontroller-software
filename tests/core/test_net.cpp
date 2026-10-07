// SPDX-License-Identifier: AGPL-3.0-or-later
// Schaltbare Netzsteckdosen (Shelly im Simulator): Erkennen, Übernehmen,
// Schutzeinstellung im Gerät mit Rücklesen, Sicherheitsprofile im Gateway,
// Not-Halt und Stromausfall (Konzept PLANT_AUTOMATION.md §2–§3).
#include <doctest/doctest.h>

#include <cmath>

#include "client.hpp"
#include "gc/embedded.hpp"

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

void reboot(sim::Simulation& s, const json& b) {
  std::lock_guard<std::recursive_mutex> l(s.mutex());
  s.control("reboot", b);
}

bool hasEvent(Client& c, const std::string& title) {
  const json events = c.ok("GET", "/api/v1/events?limit=2000")["events"];
  for (const auto& e : events)
    if (e.value("title", std::string()) == title) return true;
  return false;
}

}  // namespace

TEST_CASE("Steckdose: übernehmen setzt „nach Stromausfall aus“, Umwälzpumpe schaltet über die Dose") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  auto id = addPlug(s, c, "shelly_plug", {{{"load", "circulation"}, {"watts", 18}}});
  CHECK(outlet(s, id, 0).powerOn == gc::PowerOn::Off);
  c.ok("PUT", "/api/v1/roles/tank.circulation", {{"device", id}, {"channel", 0}});
  CHECK(std::isnan(outlet(s, id, 0).autoOffS));  // Profil dauer: kein Auto-Off
  c.ok("POST", "/api/v1/roles/tank.circulation/test");
  CHECK(outlet(s, id, 0).on);
  CHECK(s.world().circulating());
  s.step(4000);
  CHECK_FALSE(outlet(s, id, 0).on);  // Testen: 3 s
}

TEST_CASE("Sockets: fans come back on after a power loss, every other role stays off (PD-050)") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  auto id = addPlug(s, c, "shelly_strip4",
                    {{{"load", "light"}, {"watts", 240}}, {{"load", "exhaust"}, {"watts", 35}}, {{"load", "fan"}, {"watts", 20}},
                     {{"load", "circulation"}, {"watts", 18}}});
  for (int ch = 0; ch < 4; ++ch) CHECK(outlet(s, id, ch).powerOn == gc::PowerOn::Off);  // accepted: every outlet off
  c.ok("PUT", "/api/v1/roles/zone.light", {{"device", id}, {"channel", 0}});
  c.ok("PUT", "/api/v1/roles/zone.exhaust", {{"device", id}, {"channel", 1}});
  c.ok("PUT", "/api/v1/roles/zone.circulation_fan", {{"device", id}, {"channel", 2}});
  c.ok("PUT", "/api/v1/roles/tank.circulation", {{"device", id}, {"channel", 3}});
  CHECK(outlet(s, id, 0).powerOn == gc::PowerOn::Off);
  CHECK(outlet(s, id, 1).powerOn == gc::PowerOn::On);
  CHECK(outlet(s, id, 2).powerOn == gc::PowerOn::On);
  CHECK(outlet(s, id, 3).powerOn == gc::PowerOn::Off);
  c.ok("POST", "/api/v1/roles/zone.light/switch", {{"on", true}});
  c.ok("POST", "/api/v1/roles/zone.circulation_fan/switch", {{"on", true}});
  c.ok("POST", "/api/v1/roles/tank.circulation/switch", {{"on", true}});
  CHECK_FALSE(outlet(s, id, 1).on);  // exhaust off before the outage
  {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    s.control("reboot", {{"outageMin", 5}});
  }
  s.step(5000);
  CHECK_FALSE(outlet(s, id, 0).on);  // light: off (R6)
  CHECK(outlet(s, id, 1).on);        // exhaust: on, although it was off before
  CHECK(outlet(s, id, 2).on);        // circulation fan: on, the hub leaves it on
  CHECK_FALSE(outlet(s, id, 3).on);  // circulation pump: off (R6)
  c.ok("POST", "/api/v1/auth/login", {{"password", "mein-passwort"}});
  c.ok("POST", "/api/v1/roles/zone.exhaust/switch", {{"on", false}});
  CHECK_FALSE(outlet(s, id, 1).on);
  c.ok("POST", "/api/v1/roles/zone.exhaust/switch", {{"on", true}});  // switching on reads the setting back: still "on"
  CHECK(outlet(s, id, 1).on);
}

TEST_CASE("Sockets: a socket that loses its fan role goes back to off after a power loss (PD-050)") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  auto id = addPlug(s, c, "shelly_strip4", json::array());
  c.ok("PUT", "/api/v1/roles/zone.exhaust", {{"device", id}, {"channel", 1}});
  CHECK(outlet(s, id, 1).powerOn == gc::PowerOn::On);
  c.ok("PUT", "/api/v1/roles/zone.exhaust", {{"device", id}, {"channel", 2}});  // moved
  CHECK(outlet(s, id, 1).powerOn == gc::PowerOn::Off);
  CHECK(outlet(s, id, 2).powerOn == gc::PowerOn::On);
  c.ok("PUT", "/api/v1/roles/zone.light", {{"device", id}, {"channel", 1}});  // former fan socket, new role
  CHECK(outlet(s, id, 1).powerOn == gc::PowerOn::Off);
  c.ok("DELETE", "/api/v1/roles/zone.exhaust");
  CHECK(outlet(s, id, 2).powerOn == gc::PowerOn::Off);
}

TEST_CASE("Catalog: only the fans may come back on after a power loss") {
  auto j = json::parse(gc::embedded::kCatalogJson);
  j["roles"]["zone.humidifier"]["afterPowerLoss"] = "on";  // pulse role
  CHECK_THROWS(gc::Catalog::fromJson(j));
  j = json::parse(gc::embedded::kCatalogJson);
  j["roles"]["tank.circulation"]["afterPowerLoss"] = "on";  // continuous, but not a fan
  CHECK_THROWS(gc::Catalog::fromJson(j));
  j = json::parse(gc::embedded::kCatalogJson);
  j["roles"]["zone.exhaust"]["maxOnS"] = 3600;  // a maximum run time would go unenforced after a power loss
  CHECK_THROWS(gc::Catalog::fromJson(j));
  j = json::parse(gc::embedded::kCatalogJson);
  j["roles"]["zone.exhaust"]["afterPowerLoss"] = "maybe";
  CHECK_THROWS(gc::Catalog::fromJson(j));
}

TEST_CASE("Sockets: removing the device or importing a configuration releases fan sockets (PD-050)") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  auto id = addPlug(s, c, "shelly_strip4", json::array());
  c.ok("PUT", "/api/v1/roles/zone.exhaust", {{"device", id}, {"channel", 1}});
  c.ok("PUT", "/api/v1/roles/zone.circulation_fan", {{"device", id}, {"channel", 2}});
  json cfg = c.ok("GET", "/api/v1/config");
  bool edited = false;
  for (auto& z : cfg["zones"]) {
    if (!z["roles"].contains("zone.exhaust")) continue;
    z["roles"]["zone.exhaust"]["channel"] = 3;  // moved
    z["roles"].erase("zone.circulation_fan");   // dropped
    edited = true;
  }
  REQUIRE(edited);
  c.ok("POST", "/api/v1/config/import", cfg);
  CHECK(outlet(s, id, 1).powerOn == gc::PowerOn::Off);
  CHECK(outlet(s, id, 2).powerOn == gc::PowerOn::Off);
  CHECK(outlet(s, id, 3).powerOn == gc::PowerOn::On);  // the import writes the setting of the new fan socket
  c.ok("DELETE", "/api/v1/devices/" + id);
  CHECK(outlet(s, id, 3).powerOn == gc::PowerOn::Off);
}

TEST_CASE("Sockets: a fan socket that keeps \"on after power loss\" is reported") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  auto id = addPlug(s, c, "shelly_strip4", json::array());
  c.ok("PUT", "/api/v1/roles/zone.exhaust", {{"device", id}, {"channel", 1}});
  c.ok("PUT", "/api/v1/roles/zone.circulation_fan", {{"device", id}, {"channel", 2}});
  fault(s, id, "ignore");  // reports success, stores nothing
  c.ok("DELETE", "/api/v1/roles/zone.exhaust");
  CHECK(outlet(s, id, 1).powerOn == gc::PowerOn::On);
  CHECK(hasEvent(c, "Abluft: Dose bleibt „nach Stromausfall an“"));
  fault(s, id, "offline");
  s.step(2000);
  c.ok("DELETE", "/api/v1/roles/zone.circulation_fan");
  CHECK(outlet(s, id, 2).powerOn == gc::PowerOn::On);
  CHECK(hasEvent(c, "Umluft: Dose bleibt „nach Stromausfall an“"));
}

TEST_CASE("Not-Halt: survives a power loss, fans stay off until resume (PD-076)") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  auto id = addPlug(s, c, "shelly_strip4", {{{"load", "light"}, {"watts", 240}}, {{"load", "exhaust"}, {"watts", 35}}});
  c.ok("PUT", "/api/v1/roles/zone.light", {{"device", id}, {"channel", 0}});
  c.ok("PUT", "/api/v1/roles/zone.exhaust", {{"device", id}, {"channel", 1}});
  c.ok("POST", "/api/v1/roles/zone.exhaust/switch", {{"on", true}});
  c.ok("POST", "/api/v1/stop");
  CHECK_FALSE(outlet(s, id, 1).on);
  CHECK(outlet(s, id, 1).powerOn == gc::PowerOn::Off);  // during the stop the fan socket stays off after a power loss
  reboot(s, {{"outageMin", 5}});
  s.step(5000);
  CHECK_FALSE(outlet(s, id, 0).on);
  CHECK_FALSE(outlet(s, id, 1).on);
  c.ok("POST", "/api/v1/auth/login", {{"password", "mein-passwort"}});
  CHECK(c.state()["stopped"] == true);
  CHECK(hasEvent(c, "Not-Halt besteht weiter"));
  s.step(60000);
  CHECK_FALSE(outlet(s, id, 1).on);  // the hub does not switch it on either
  c.ok("POST", "/api/v1/resume");
  CHECK(c.state()["stopped"] == false);
  CHECK(outlet(s, id, 1).powerOn == gc::PowerOn::On);  // back to the fan setting
}

TEST_CASE("Internal error: everything goes off except the fans (PD-077)") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  auto id = addPlug(s, c, "shelly_strip4",
                    {{{"load", "light"}, {"watts", 240}}, {{"load", "exhaust"}, {"watts", 35}}, {{"load", "fan"}, {"watts", 20}},
                     {{"load", "circulation"}, {"watts", 18}}});
  c.ok("PUT", "/api/v1/roles/zone.light", {{"device", id}, {"channel", 0}});
  c.ok("PUT", "/api/v1/roles/zone.exhaust", {{"device", id}, {"channel", 1}});
  c.ok("PUT", "/api/v1/roles/zone.circulation_fan", {{"device", id}, {"channel", 2}});
  c.ok("PUT", "/api/v1/roles/tank.circulation", {{"device", id}, {"channel", 3}});
  for (const char* role : {"zone.light", "zone.exhaust", "zone.circulation_fan", "tank.circulation"})
    c.ok("POST", std::string("/api/v1/roles/") + role + "/switch", {{"on", true}});
  fault(s, id, "crash");
  s.step(2000);
  CHECK_FALSE(outlet(s, id, 0).on);  // light off
  CHECK(outlet(s, id, 1).on);        // exhaust keeps running
  CHECK(outlet(s, id, 2).on);        // circulation fan keeps running
  CHECK_FALSE(outlet(s, id, 3).on);  // circulation pump off
  CHECK(hasEvent(c, "Interner Fehler – alles aus außer den Lüftern"));
  fault(s, id, "none");
  s.step(2000);
  CHECK(hasEvent(c, "Steuerung läuft wieder"));
}

TEST_CASE("Restart without a power loss: fans keep their state, everything else goes off (R6, PD-050)") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  auto id = addPlug(s, c, "shelly_strip4",
                    {{{"load", "light"}, {"watts", 240}}, {{"load", "exhaust"}, {"watts", 35}}, {{"load", "fan"}, {"watts", 20}},
                     {{"load", "circulation"}, {"watts", 18}}});
  c.ok("PUT", "/api/v1/roles/zone.light", {{"device", id}, {"channel", 0}});
  c.ok("PUT", "/api/v1/roles/zone.exhaust", {{"device", id}, {"channel", 1}});
  c.ok("PUT", "/api/v1/roles/zone.circulation_fan", {{"device", id}, {"channel", 2}});
  c.ok("PUT", "/api/v1/roles/tank.circulation", {{"device", id}, {"channel", 3}});
  for (const char* role : {"zone.light", "zone.exhaust", "tank.circulation"})
    c.ok("POST", std::string("/api/v1/roles/") + role + "/switch", {{"on", true}});
  reboot(s, {{"outageMin", 0}, {"mainsLost", false}});  // only the hub restarts, the sockets keep power
  s.step(5000);
  CHECK_FALSE(outlet(s, id, 0).on);  // light off (R6)
  CHECK(outlet(s, id, 1).on);        // exhaust stays on
  CHECK_FALSE(outlet(s, id, 2).on);  // circulation fan stays off, as before
  CHECK_FALSE(outlet(s, id, 3).on);  // circulation pump off (R6)
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

TEST_CASE("Gießpumpe: Füllstand wird im Lauf ungültig → aus, einmal gemeldet") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  auto id = addPlug(s, c, "shelly_plug", json::array());
  c.ok("PUT", "/api/v1/roles/zone.irrigation_pump", {{"device", id}, {"channel", 0}});
  c.ok("POST", "/api/v1/roles/zone.irrigation_pump/switch", {{"on", true}});
  fault(s, "LVL-77B210", "offline");
  REQUIRE(until(s, [&] { return !outlet(s, id, 0).on; }, 300000));
  s.step(30000);
  auto ev = c.ok("GET", "/api/v1/events?limit=200");
  int n = 0;
  for (const auto& e : ev["events"]) n += e["title"] == "Gießpumpe aus: Trockenlaufschutz";
  CHECK(n == 1);
}

namespace {

int countEvents(Client& c, const std::string& title) {
  auto ev = c.ok("GET", "/api/v1/events?limit=200");
  int n = 0;
  for (const auto& e : ev["events"]) n += e["title"] == title;
  return n;
}

// Schutzabschaltung an einer Dose, die Schaltbefehle ablehnt: einmal „Aus
// nicht bestätigt“, keine Erfolgsmeldung, kein Eintrag je Takt; die Dose
// bleibt an. Danach Störung weg: Hub schaltet aus und meldet es einmal.
void checkCutNotConfirmed(sim::Simulation& s, Client& c, const std::string& id, const std::string& label,
                          const std::string& okTitle, gc::Ms settle = 60000) {
  REQUIRE(until(s, [&] { return countEvents(c, label + ": Aus nicht bestätigt") > 0; }, 400000));
  s.step(settle);
  CHECK(outlet(s, id, 0).on);
  CHECK(countEvents(c, label + ": Aus nicht bestätigt") == 1);
  CHECK(countEvents(c, okTitle) == 0);
  fault(s, id, "none");
  REQUIRE(until(s, [&] { return !outlet(s, id, 0).on; }, 10000));
  s.step(5000);
  CHECK(countEvents(c, label + ": Aus bestätigt") == 1);
}

}  // namespace

TEST_CASE("Gießpumpe: Ausschalten scheitert → einmal „Aus nicht bestätigt“, nicht „aus“") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  auto id = addPlug(s, c, "shelly_plug", json::array());
  c.ok("PUT", "/api/v1/roles/zone.irrigation_pump", {{"device", id}, {"channel", 0}});
  c.ok("POST", "/api/v1/roles/zone.irrigation_pump/switch", {{"on", true}});
  fault(s, id, "stuck");  // Schaltbefehle scheitern, die Dose bleibt an
  fault(s, "LVL-77B210", "offline");
  checkCutNotConfirmed(s, c, id, "Gießpumpe", "Gießpumpe aus: Trockenlaufschutz");
}

TEST_CASE("Umwälzpumpe an der Dose: Ausschalten scheitert → einmal „Aus nicht bestätigt“") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  auto id = addPlug(s, c, "shelly_plug", {{{"load", "circulation"}, {"watts", 18}}});
  c.ok("PUT", "/api/v1/roles/tank.circulation", {{"device", id}, {"channel", 0}});
  c.ok("POST", "/api/v1/roles/tank.circulation/switch", {{"on", true}});
  fault(s, id, "stuck");
  fault(s, "LVL-77B210", "offline");
  checkCutNotConfirmed(s, c, id, "Umwälzpumpe", "Umwälzpumpe aus");
}

TEST_CASE("Zulauf an der Dose: Ausschalten scheitert → einmal „Aus nicht bestätigt“, Rastung bleibt") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  auto id = addPlug(s, c, "shelly_plug", json::array());
  c.ok("PUT", "/api/v1/roles/tank.inlet", {{"device", id}, {"channel", 0}});
  c.ok("POST", "/api/v1/roles/tank.inlet/switch", {{"on", true}});
  fault(s, id, "stuck");
  fault(s, "LVL-77B210", "offline");
  checkCutNotConfirmed(s, c, id, "Zulaufventil", "Zulauf-Notabschaltung");
  CHECK(c.state()["latches"].contains("inlet.fault"));
}

TEST_CASE("Höchstlaufzeit: Ausschalten scheitert → einmal „Aus nicht bestätigt“, Hub versucht es weiter") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  auto id = addPlug(s, c, "shelly_plug", json::array());
  c.ok("PUT", "/api/v1/roles/zone.humidifier", {{"device", id}, {"channel", 0}});
  c.ok("POST", "/api/v1/roles/zone.humidifier/switch", {{"on", true}});
  fault(s, id, "stuck");  // Höchstlaufzeit 5 min, Auto-Off im Gerät erst nach 6 min
  checkCutNotConfirmed(s, c, id, "Befeuchter", "Befeuchter aus: Höchstlaufzeit", 20000);
}

TEST_CASE("Höchstlaufzeit: Gerät schaltet selbst ab, Befehle weiter abgelehnt → keine Meldung je Takt") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  auto id = addPlug(s, c, "shelly_plug", json::array());
  c.ok("PUT", "/api/v1/roles/zone.humidifier", {{"device", id}, {"channel", 0}});
  c.ok("POST", "/api/v1/roles/zone.humidifier/switch", {{"on", true}});
  fault(s, id, "stuck");
  REQUIRE(until(s, [&] { return countEvents(c, "Befeuchter: Aus nicht bestätigt") > 0; }, 400000));
  s.step(180000);  // über das Auto-Off im Gerät (6 min) hinaus, Störung bleibt
  CHECK_FALSE(outlet(s, id, 0).on);
  CHECK(countEvents(c, "Befeuchter: Aus nicht bestätigt") == 1);
  CHECK(countEvents(c, "Befeuchter: Aus bestätigt") == 1);
}

TEST_CASE("Höchstlaufzeit: nach Umzuordnen keine falsche Entwarnung, neuer Ausgang bleibt unberührt") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  auto a = addPlug(s, c, "shelly_plug", json::array());
  auto b = addPlug(s, c, "shelly_plug", json::array());
  c.ok("PUT", "/api/v1/roles/zone.humidifier", {{"device", a}, {"channel", 0}});
  c.ok("POST", "/api/v1/roles/zone.humidifier/switch", {{"on", true}});
  fault(s, a, "stuck");
  REQUIRE(until(s, [&] { return countEvents(c, "Befeuchter: Aus nicht bestätigt") > 0; }, 400000));
  c.ok("PUT", "/api/v1/roles/zone.humidifier", {{"device", b}, {"channel", 0}});  // alter Ausgang klemmt weiter
  s.step(20000);
  CHECK(outlet(s, a, 0).on);
  CHECK_FALSE(outlet(s, b, 0).on);
  CHECK(countEvents(c, "Befeuchter: Aus bestätigt") == 0);
  CHECK(countEvents(c, "Befeuchter aus: Höchstlaufzeit") == 0);
}

TEST_CASE("Zwei Schutzgründe an einer klemmenden Dose: je Grund eine Meldung, kein Wechsel je Takt") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  auto id = addPlug(s, c, "shelly_plug", json::array());
  c.ok("PUT", "/api/v1/roles/zone.irrigation_pump", {{"device", id}, {"channel", 0}});
  c.ok("POST", "/api/v1/roles/zone.irrigation_pump/switch", {{"on", true}});
  fault(s, id, "stuck");
  fault(s, "LVL-77B210", "offline");  // Grund 1: Füllstand ungültig
  s.step(650000);                     // Grund 2: Höchstlaufzeit 10 min; Auto-Off im Gerät erst nach 12 min
  CHECK(outlet(s, id, 0).on);
  CHECK(countEvents(c, "Gießpumpe: Aus nicht bestätigt") == 2);
}

TEST_CASE("Umwälzpumpe: späterer Trockenlauf meldet neu, Rastung hält die Pumpe aus") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  auto id = addPlug(s, c, "shelly_plug", {{{"load", "circulation"}, {"watts", 18}}});
  c.ok("PUT", "/api/v1/roles/tank.circulation", {{"device", id}, {"channel", 0}});
  c.ok("POST", "/api/v1/roles/tank.circulation/switch", {{"on", true}});
  fault(s, id, "stuck");
  fault(s, "LVL-77B210", "offline");  // erst „Füllstand ungültig“
  REQUIRE(until(s, [&] { return countEvents(c, "Umwälzpumpe: Aus nicht bestätigt") == 1; }, 300000));
  fault(s, "LVL-77B210", "none");
  {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    s.control("water", {{"volumeL", 2.0}});  // dann echter Trockenlauf: neuer Grund, neue Meldung
  }
  REQUIRE(until(s, [&] { return countEvents(c, "Umwälzpumpe: Aus nicht bestätigt") == 2; }, 120000));
  CHECK(c.state()["latches"].contains("circulation.dry"));
  {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    s.control("water", {{"volumeL", 31.0}});  // Pegel wieder gut, Rastung nicht quittiert
  }
  s.step(30000);
  fault(s, id, "none");
  REQUIRE(until(s, [&] { return !outlet(s, id, 0).on; }, 10000));  // Hub hält die Rastung durch
  s.step(5000);
  CHECK(countEvents(c, "Umwälzpumpe: Aus nicht bestätigt") == 2);
  CHECK(countEvents(c, "Umwälzpumpe: Aus bestätigt") == 1);
}

TEST_CASE("Umzuordnen: alter Ausgang nicht erreichbar → „Aus nicht bestätigt“ im Protokoll") {
  sim::Simulation s(test::opts("neu"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  auto a = addPlug(s, c, "shelly_plug", json::array());
  auto b = addPlug(s, c, "shelly_plug", json::array());
  c.ok("PUT", "/api/v1/roles/zone.light", {{"device", a}, {"channel", 0}});
  c.ok("POST", "/api/v1/roles/zone.light/switch", {{"on", true}});
  fault(s, a, "offline");
  s.step(2000);
  c.ok("PUT", "/api/v1/roles/zone.light", {{"device", b}, {"channel", 0}});
  auto ev = c.ok("GET", "/api/v1/events?limit=50");
  bool seen = false;
  for (const auto& e : ev["events"]) seen = seen || e["title"] == "Licht: Aus nicht bestätigt";
  CHECK(seen);
}

TEST_CASE("Zulauf: Grund weg, Rastung steht, Ventil klemmt offen → Hub schaltet weiter aus") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  auto id = addPlug(s, c, "shelly_plug", json::array());
  c.ok("PUT", "/api/v1/roles/tank.inlet", {{"device", id}, {"channel", 0}});
  c.ok("POST", "/api/v1/roles/tank.inlet/switch", {{"on", true}});
  fault(s, id, "stuck");
  fault(s, "LVL-77B210", "offline");
  REQUIRE(until(s, [&] { return countEvents(c, "Zulaufventil: Aus nicht bestätigt") == 1; }, 300000));
  fault(s, "LVL-77B210", "none");  // Pegel wieder gültig, Rastung nicht quittiert
  s.step(30000);
  CHECK(outlet(s, id, 0).on);
  CHECK(countEvents(c, "Zulaufventil: Aus nicht bestätigt") == 1);  // Fortsetzung, keine neue Meldung
  fault(s, id, "none");
  REQUIRE(until(s, [&] { return !outlet(s, id, 0).on; }, 10000));
  s.step(2000);
  CHECK(countEvents(c, "Zulaufventil: Aus bestätigt") == 1);
}

TEST_CASE("Zulauf klemmt: nach Pegelausfall meldet die Notgrenze neu") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  auto id = addPlug(s, c, "shelly_plug", json::array());
  c.ok("PUT", "/api/v1/roles/tank.inlet", {{"device", id}, {"channel", 0}});
  c.ok("POST", "/api/v1/roles/tank.inlet/switch", {{"on", true}});
  fault(s, id, "stuck");
  fault(s, "LVL-77B210", "offline");
  REQUIRE(until(s, [&] { return countEvents(c, "Zulaufventil: Aus nicht bestätigt") == 1; }, 300000));
  fault(s, "LVL-77B210", "none");
  {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    s.control("water", {{"volumeL", 61.0}});  // über der Notgrenze (60 L)
  }
  REQUIRE(until(s, [&] { return countEvents(c, "Zulaufventil: Aus nicht bestätigt") == 2; }, 60000));
  auto ev = c.ok("GET", "/api/v1/events?limit=50");
  bool capacity = false;
  for (const auto& e : ev["events"])
    capacity = capacity || (e["title"] == "Zulaufventil: Aus nicht bestätigt" && e["text"].get<std::string>().find("Notgrenze") != std::string::npos);
  CHECK(capacity);
  // Rastungsgrund bleibt eingefroren (RAT-062)
  CHECK(c.state()["latches"]["inlet.fault"]["why"] == "Füllstand ungültig");
}

TEST_CASE("Umwälzpumpe klemmt: neuer Trockenlauf nach Quittierung meldet neu") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  auto id = addPlug(s, c, "shelly_plug", {{{"load", "circulation"}, {"watts", 18}}});
  c.ok("PUT", "/api/v1/roles/tank.circulation", {{"device", id}, {"channel", 0}});
  c.ok("POST", "/api/v1/roles/tank.circulation/switch", {{"on", true}});
  fault(s, id, "stuck");
  {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    s.control("water", {{"volumeL", 2.0}});
  }
  REQUIRE(until(s, [&] { return countEvents(c, "Umwälzpumpe: Aus nicht bestätigt") == 1; }, 120000));
  c.ok("POST", "/api/v1/latches/circulation.dry/ack");  // quittiert, Pumpe klemmt weiter, Pegel noch zu tief
  REQUIRE(until(s, [&] { return countEvents(c, "Umwälzpumpe: Aus nicht bestätigt") == 2; }, 10000));
  CHECK(c.state()["latches"].contains("circulation.dry"));
}

TEST_CASE("Höchstlaufzeit: gleich wieder eingeschaltet → die nächste Abschaltung wird wieder gemeldet") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  auto id = addPlug(s, c, "shelly_plug", json::array());
  c.ok("PUT", "/api/v1/roles/zone.humidifier", {{"device", id}, {"channel", 0}});
  c.ok("POST", "/api/v1/roles/zone.humidifier/switch", {{"on", true}});
  // until prüft nach jedem Takt: Der Schnitt ist gemeldet, sein Abschluss im nächsten Takt steht noch aus.
  REQUIRE(until(s, [&] { return countEvents(c, "Befeuchter aus: Höchstlaufzeit") == 1; }, 400000));
  c.ok("POST", "/api/v1/roles/zone.humidifier/switch", {{"on", true}});
  REQUIRE(until(s, [&] { return countEvents(c, "Befeuchter aus: Höchstlaufzeit") == 2; }, 400000));
}

TEST_CASE("Lösen während „Aus nicht bestätigt“ → Meldung, nicht still") {
  sim::Simulation s(test::opts("demo"));
  Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  auto id = addPlug(s, c, "shelly_plug", json::array());
  c.ok("PUT", "/api/v1/roles/zone.humidifier", {{"device", id}, {"channel", 0}});
  c.ok("POST", "/api/v1/roles/zone.humidifier/switch", {{"on", true}});
  fault(s, id, "stuck");
  REQUIRE(until(s, [&] { return countEvents(c, "Befeuchter: Aus nicht bestätigt") == 1; }, 400000));
  c.ok("DELETE", "/api/v1/roles/zone.humidifier");
  s.step(5000);
  CHECK(countEvents(c, "Befeuchter: Aus nicht bestätigt") == 2);
  CHECK(countEvents(c, "Befeuchter: Aus bestätigt") == 0);
}
