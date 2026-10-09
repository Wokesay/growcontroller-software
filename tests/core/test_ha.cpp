// SPDX-License-Identifier: AGPL-3.0-or-later
// Read-only spike: Home Assistant as the device layer (docs/HOME_ASSISTANT.md).
#include <doctest/doctest.h>

#include <atomic>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include <httplib.h>

#include "fakes.hpp"
#include "gc/hub.hpp"
#include "gc/api.hpp"
#include "ha_assign.hpp"
#include "ha_bus.hpp"
#include "ha_client.hpp"

using gc::json;

namespace {

void pseudoRandom(std::uint8_t* p, size_t n) {
  for (size_t i = 0; i < n; ++i) p[i] = static_cast<std::uint8_t>(i * 7 + 1);
}

constexpr std::int64_t kNoonMs = 1791547200000;  // 2026-10-09T12:00:00Z

json state(const std::string& entity, const std::string& value, const std::string& unit, const std::string& reported) {
  return {{"entity_id", entity},
          {"state", value},
          {"attributes", {{"unit_of_measurement", unit}}},
          {"last_updated", "2026-10-09T10:00:00+00:00"},
          {"last_reported", reported}};
}

// The same with Home Assistant's device class and friendly name
json described(json s, const std::string& deviceClass, const std::string& name) {
  if (!deviceClass.empty()) s["attributes"]["device_class"] = deviceClass;
  s["attributes"]["friendly_name"] = name;
  return s;
}

// "2026-10-09T12:00:05.000Z" for kNoonMs + 5000; times from noon on only
std::string iso(std::int64_t ms) {
  REQUIRE(ms >= kNoonMs);
  REQUIRE(ms < kNoonMs + 12 * 3600 * 1000);
  const std::int64_t s = ms / 1000 - kNoonMs / 1000;
  char buf[40];
  std::snprintf(buf, sizeof buf, "2026-10-09T%02lld:%02lld:%02lld.%03lldZ", static_cast<long long>(12 + s / 3600),
                static_cast<long long>(s / 60 % 60), static_cast<long long>(s % 60), static_cast<long long>(ms % 1000));
  return buf;
}

// A stand-in Home Assistant on a free local port; stopped and joined in any case.
struct FakeHa {
  httplib::Server svr;
  std::thread t;
  int port = 0;
  std::mutex m;
  std::vector<std::string> requests;  // "GET /api/states/…"
  std::atomic<int> count{0};
  explicit FakeHa(std::function<void(const httplib::Request&, httplib::Response&)> handler) {
    // Recorded before routing, so a request with any method is seen
    svr.set_pre_routing_handler([this](const httplib::Request& req, httplib::Response&) {
      {
        std::lock_guard<std::mutex> l(m);
        requests.push_back(req.method + " " + req.path);
      }
      ++count;
      return httplib::Server::HandlerResponse::Unhandled;
    });
    svr.Get(R"(/.*)", handler);
    port = svr.bind_to_any_port("127.0.0.1");
    t = std::thread([this] { svr.listen_after_bind(); });
    svr.wait_until_ready();
  }
  ~FakeHa() { stop(); }
  void stop() {
    svr.stop();
    if (t.joinable()) t.join();
  }
  std::string url() const { return "http://127.0.0.1:" + std::to_string(port); }
};

}  // namespace

TEST_CASE("Home Assistant: timestamps and HTTP dates") {
  CHECK(ha::parseTimestampMs("1970-01-01T00:00:00+00:00") == 0);
  CHECK(ha::parseTimestampMs("2026-10-09T12:00:00Z") == kNoonMs);
  CHECK(ha::parseTimestampMs("2026-10-09T12:00:00.123456+00:00") == kNoonMs + 123);
  CHECK(ha::parseTimestampMs("2026-10-09T14:00:00+02:00") == kNoonMs);
  CHECK(ha::parseTimestampMs(iso(kNoonMs + 65432)) == kNoonMs + 65432);
  for (const char* bad : {"", "2026-13-09T12:00:00Z", "2026-02-30T12:00:00Z", "2026-10-09T12:00:00.Z", "2026-10-09T12:00:00+0200"})
    CHECK_FALSE(ha::parseTimestampMs(bad).has_value());
  CHECK(ha::parseHttpDateMs("Fri, 09 Oct 2026 12:00:00 GMT") == kNoonMs);
  CHECK_FALSE(ha::parseHttpDateMs("Fri, 09 Okt 2026 12:00:00 GMT").has_value());
  CHECK_FALSE(ha::parseHttpDateMs("").has_value());
}

TEST_CASE("Home Assistant: the mapping is checked before anything is read") {
  std::string err;
  auto m = ha::parseMapping({{"url", "http://ha:8123/"}, {"entities", {{{"entity", "sensor.grow_ph"}, {"measures", "ph"}}}}}, err);
  CHECK(err.empty());
  CHECK(m.url == "http://ha:8123");
  REQUIRE(m.entities.size() == 1);
  CHECK(m.entities[0].entityId == "sensor.grow_ph");
  auto refused = [&](const json& j) {
    ha::parseMapping(j, err);
    return !err.empty();
  };
  const json one = {{{"entity", "sensor.grow_ph"}, {"measures", "ph"}}};
  CHECK(refused({{"entities", one}}));                                       // no address
  CHECK(refused({{"url", "ws://ha:8123"}, {"entities", one}}));              // not http(s)
  CHECK(refused({{"url", "http://ha:8123/prefix"}, {"entities", one}}));     // a path would be dropped
  // Without entities the user picks them in the web app
  CHECK(ha::parseMapping({{"url", "http://ha:8123"}}, err).entities.empty());
  CHECK(err.empty());
  CHECK(refused({{"url", "http://ha:8123"}, {"entities", "sensor.grow_ph"}}));  // not a list
  for (const char* id : {"Sensor.Grow", "sensor", "../api/config", "sensor.a.b", "sensor.grow ph"})
    CHECK(refused({{"url", "http://ha:8123"}, {"entities", {{{"entity", id}, {"measures", "ph"}}}}}));
  CHECK(refused({{"url", "http://ha:8123"}, {"entities", {{{"entity", "sensor.x"}, {"measures", "voltage"}}}}}));
  CHECK(refused({{"url", "http://ha:8123"}, {"entities", {one[0], one[0]}}}));  // listed twice
}

TEST_CASE("Home Assistant: the hub finds the sensors it can use, drops the rest, and uses only what is picked") {
  const json states = json::array({
      described(state("sensor.tank_ph", "6.1", "", iso(kNoonMs)), "ph", "Tank pH"),
      described(state("sensor.tank_ec", "1450", "µS/cm", iso(kNoonMs)), "", "Tank EC"),
      described(state("sensor.tent_temp", "70", "°F", iso(kNoonMs)), "temperature", "Zelt"),
      described(state("sensor.tank_temp", "unavailable", "°C", iso(kNoonMs)), "temperature", "Tank Wasser"),
      described(state("sensor.tent_rh", "55", "%", iso(kNoonMs)), "humidity", "Zelt Feuchte"),
      described(state("sensor.tent_co2", "800", "ppm", iso(kNoonMs)), "carbon_dioxide", "Zelt CO2"),
      described(state("sensor.tank_volume", "40", "L", iso(kNoonMs)), "volume_storage", "Tank Inhalt"),
      // %, ppm and L also stand for other things: only with the device class
      described(state("sensor.phone_battery", "80", "%", iso(kNoonMs)), "battery", "Handy"),
      described(state("sensor.voc", "300", "ppm", iso(kNoonMs)), "volatile_organic_compounds_parts", "VOC"),
      described(state("sensor.water_used", "40", "L", iso(kNoonMs)), "water", "Wasserzähler"),
      [] {  // a volume that only adds up is no level
        json s = described(state("sensor.water_meter", "1234", "L", iso(kNoonMs)), "volume", "Zähler");
        s["attributes"]["state_class"] = "total_increasing";
        return s;
      }(),
      // Not a sensor, or not an entity ID the hub accepts
      described(state("switch.light", "on", "", iso(kNoonMs)), "", "Licht"),
      {{"entity_id", "person.someone"}, {"state", "home"}, {"attributes", {{"latitude", 52.5}}}},
      described(state("sensor.Bad Name", "6.0", "pH", iso(kNoonMs)), "ph", "x"),
      "not a state",
  });
  ha::HaBus bus;
  bus.updateAll(states, kNoonMs, 1000);
  std::map<std::string, ha::Candidate> found;
  for (const auto& c : bus.candidates()) found[c.entityId] = c;
  CHECK(found.size() == 7);
  CHECK(found["sensor.tank_ph"].kind == "ph");
  CHECK(found["sensor.tank_ph"].name == "Tank pH");
  CHECK(found["sensor.tank_ec"].kind == "ec");
  CHECK(found["sensor.tank_ec"].value == doctest::Approx(1.45));  // in the hub's unit
  CHECK(found["sensor.tent_temp"].kind == "temperature");
  CHECK(found["sensor.tent_temp"].value == doctest::Approx(21.11).epsilon(0.001));
  CHECK(std::isnan(found["sensor.tank_temp"].value));  // unavailable: no value, never 0 (R5)
  CHECK(found["sensor.tent_rh"].kind == "humidity");
  CHECK(found["sensor.tent_co2"].kind == "co2");
  CHECK(found["sensor.tank_volume"].kind == "level");
  CHECK(bus.devices().empty());  // nothing is a device before it is picked
  // Picking: a picked sensor is online at once, from the round that offered it
  gc::Msg why;
  REQUIRE(bus.select("sensor.tent_temp", "air_temp", why));
  CHECK(bus.devices().at(0).online);
  CHECK_FALSE(bus.select("sensor.tent_temp", "water_temp", why));  // one sensor, one measure
  CHECK(why.key == "ha.used");
  CHECK(why.text.find("sensor.") == std::string::npos);  // the reason in words, not in IDs
  CHECK_FALSE(bus.select("sensor.tank_ph", "ec", why));  // pH cannot serve as EC
  CHECK(why.key == "ha.mismatch");
  CHECK_FALSE(bus.select("sensor.phone_battery", "humidity", why));  // no candidate
  CHECK(why.key == "ha.unknown");
  CHECK_FALSE(bus.select("person.someone", "ph", why));
  CHECK_FALSE(bus.select("sensor.tank_ph", "voltage", why));
  CHECK(bus.select("sensor.tank_ph", "ph", why));
  CHECK(bus.select("sensor.tank_ph", "ph", why));  // the same again is fine
  CHECK(bus.selectedMeasure("sensor.tank_ph") == "ph");
  CHECK(bus.selectedMeasure("sensor.tank_ec").empty());
  bus.updateAll(states, kNoonMs, 2000);
  const auto devs = bus.devices();
  REQUIRE(devs.size() == 2);
  CHECK(devs[0].id == "ha.sensor.tent_temp");
  CHECK(devs[0].cls == "ha_air_temp");
  CHECK(devs[0].online);
  CHECK(devs[0].info["name"] == "Zelt");
  REQUIRE(bus.sample("ha.sensor.tent_temp", "measure.air_temp"));
  CHECK(bus.sample("ha.sensor.tent_temp", "measure.air_temp")->raw == doctest::Approx(21.11).epsilon(0.001));
  const json list = bus.candidatesJson()["candidates"];
  for (const auto& c : list) {
    if (c["entity"] == "sensor.tent_temp") {
      CHECK(c["used"] == "air_temp");
      CHECK(c["measures"] == json::array({"water_temp", "air_temp"}));
    }
    if (c["entity"] == "sensor.tank_temp") CHECK(c["value"].is_null());
    if (c["entity"] == "sensor.tank_ec") CHECK(c["used"].is_null());
  }
  // The picks can be written as the mapping file writes entities, and read back
  std::string err;
  CHECK(ha::parseEntities(ha::entitiesJson(bus.entities()), err).size() == 2);
  CHECK(err.empty());
  // Home Assistant not answering: the candidates' old values are not shown as current
  bus.lostAll();
  for (const auto& c : bus.candidates()) CHECK(std::isnan(c.value));
  bus.updateAll(states, kNoonMs, 2500);
  // A picked sensor that disappears from Home Assistant goes offline and says why
  bus.updateAll(json::array(), kNoonMs, 3000);
  CHECK_FALSE(bus.devices()[0].online);
  CHECK(bus.fault("sensor.tent_temp") == "not in Home Assistant");
  // A huge installation does not grow the list without end, and 400 device
  // temperatures cannot crowd out the tank's pH listed after them
  json many = json::array();
  for (int i = 0; i < 400; ++i) many.push_back(described(state("sensor.t" + std::to_string(i), "20", "°C", iso(kNoonMs)), "temperature", "t"));
  many.push_back(described(state("sensor.tank_ph", "6.1", "", iso(kNoonMs)), "ph", "Tank pH"));
  bus.updateAll(many, kNoonMs, 4000);
  CHECK(bus.candidates().size() == ha::HaBus::kMaxPerKind + 1);
  CHECK(bus.candidates().back().entityId == "sensor.tank_ph");
  CHECK(bus.candidatesJson()["truncated"] == true);
}

TEST_CASE("Home Assistant: only units the hub converts exactly give a value; nothing is guessed (RAT-006, RAT-015)") {
  auto withClass = [](json s, const std::string& stateClass) {
    s["attributes"]["state_class"] = stateClass;
    return s;
  };
  const json states = json::array({
      described(state("sensor.tank_tds", "700", "ppm", iso(kNoonMs)), "conductivity", "Tank TDS"),  // a factor would be a guess
      described(state("sensor.tds_plain", "700", "ppm", iso(kNoonMs)), "", "TDS"),                  // ppm alone: not offered
      described(state("sensor.ec_msm", "145", "mS/m", iso(kNoonMs)), "conductivity", "EC mS/m"),
      described(state("sensor.ec_greek", "1450", "\xce\xbcS/cm", iso(kNoonMs)), "conductivity", "EC"),  // μ U+03BC, as Home Assistant writes it
      described(state("sensor.ec_none", "1.4", "", iso(kNoonMs)), "conductivity", "EC ohne Einheit"),
      described(state("sensor.water_k", "293.15", "K", iso(kNoonMs)), "temperature", "Wasser K"),
      withClass(described(state("sensor.tank_total", "40", "L", iso(kNoonMs)), "volume", "Summe"), "total"),
      described(state("sensor.tank_gal", "10", "gal", iso(kNoonMs)), "volume_storage", "Tank gal"),
      // A soil sensor reports EC too; it is offered (the name tells it apart) and stays display only (RAT-025)
      described(state("sensor.plant_conductivity", "350", "\xce\xbcS/cm", iso(kNoonMs)), "conductivity", "Pflanze Leitfähigkeit"),
  });
  ha::HaBus bus;
  bus.updateAll(states, kNoonMs, 1000);
  std::map<std::string, ha::Candidate> found;
  for (const auto& c : bus.candidates()) found[c.entityId] = c;
  CHECK_FALSE(found.count("sensor.tds_plain"));
  CHECK_FALSE(found.count("sensor.tank_total"));
  CHECK_FALSE(found.count("sensor.tank_gal"));
  CHECK(std::isnan(found["sensor.tank_tds"].value));
  CHECK(found["sensor.tank_tds"].problem == "unit_unsupported");
  CHECK(found["sensor.tank_tds"].unit == "ppm");
  CHECK(found["sensor.tank_tds"].raw == doctest::Approx(700));
  CHECK(std::isnan(found["sensor.ec_msm"].value));
  CHECK(found["sensor.ec_greek"].value == doctest::Approx(1.45));
  CHECK(found["sensor.ec_none"].problem == "unit_missing");
  CHECK(found["sensor.water_k"].problem == "unit_unsupported");
  CHECK(found["sensor.plant_conductivity"].value == doctest::Approx(0.35));
  const json listed = bus.candidatesJson()["candidates"];
  for (const auto& c : listed)
    if (c["entity"] == "sensor.tank_tds") {
      CHECK(c["problem"] == "unit_unsupported");
      CHECK(c["value"].is_null());
    }
  // Picked anyway, TDS gives no value, never one in mS/cm
  gc::Msg why;
  REQUIRE(bus.select("sensor.tank_tds", "ec", why));
  CHECK_FALSE(bus.sample("ha.sensor.tank_tds", "measure.ec"));
  CHECK(bus.fault("sensor.tank_tds") == "unit ppm not supported");
}

TEST_CASE("Home Assistant: an answer keeps only what the hub reads, however large or nested") {
  json big = json::array();
  json one = described(state("sensor.tank_ph", "6.1", "", iso(kNoonMs)), "ph", "Tank\xe2\x80\xae pH\xe2\x80\x8b");  // bidi override, zero width
  one["attributes"]["entity_picture"] = std::string(100000, 'x');
  one["attributes"]["options"] = json::array({1, 2, 3});
  one["context"] = {{"id", "abc"}, {"user_id", "someone"}};
  big.push_back(one);
  big.push_back({{"entity_id", "person.someone"}, {"state", "home"}, {"attributes", {{"latitude", 52.5}, {"friendly_name", "Someone"}}}});
  big.push_back({{"entity_id", "sensor." + std::string(300, 'a')}, {"state", "1"}, {"attributes", {{"device_class", "ph"}}}});  // too long an ID
  std::string nested = "[";
  for (int i = 0; i < 10000; ++i) nested += "[";
  for (int i = 0; i < 10000; ++i) nested += "]";
  nested += "]";
  std::string body = big.dump();
  body.insert(body.size() - 1, "," + nested);
  const json kept = ha::parseStates(body);
  REQUIRE(kept.is_array());
  const json& ph = kept.at(0);
  CHECK_FALSE(ph.contains("context"));
  CHECK_FALSE(ph["attributes"].contains("entity_picture"));
  CHECK_FALSE(ph["attributes"].contains("options"));
  CHECK(ph["attributes"]["device_class"] == "ph");
  CHECK(kept.dump().size() < 2000);  // nothing large or nested survives
  ha::HaBus bus;
  bus.updateAll(kept, kNoonMs, 1000);
  REQUIRE(bus.candidates().size() == 1);
  CHECK(bus.candidates()[0].name == "Tank pH");  // invisible format characters gone
  CHECK(ha::parseStates("not json").is_discarded());
}

TEST_CASE("Home Assistant: a sensor is picked for a role in one step, and taken away again without leftovers") {
  const json states = json::array({
      described(state("sensor.tank_ph", "6.1", "", iso(kNoonMs)), "ph", "Tank pH"),
      described(state("sensor.tent_temp", "24.5", "°C", iso(kNoonMs)), "temperature", "Zelt Temperatur"),
      described(state("sensor.room_temp", "21.0", "°C", iso(kNoonMs)), "temperature", "Wohnzimmer"),
  });
  const gc::Catalog cat = ha::catalog();
  ha::HaBus bus;
  gc::MemoryStorage store;
  test::Clock clk;
  gc::Hub hub(cat, bus, store, clk, pseudoRandom);
  hub.boot();
  bus.updateAll(states, kNoonMs + clk.ms, clk.ms);
  int ticks = 0;
  auto letHubSee = [&] {  // what the server's loop does meanwhile
    ++ticks;
    clk.ms += 200;
    bus.updateAll(states, kNoonMs + clk.ms, clk.ms);
    hub.tick();
  };
  // Picked: selected, accepted under its Home Assistant name, bound
  REQUIRE(ha::assign(hub, bus, "tank.ph", "sensor.tank_ph", letHubSee).status == 200);
  CHECK(ticks >= 1);
  REQUIRE(hub.config().device("ha.sensor.tank_ph"));
  CHECK(hub.config().device("ha.sensor.tank_ph")->name == "Tank pH");
  REQUIRE(hub.config().binding("tank.ph"));
  CHECK(hub.config().binding("tank.ph")->device == "ha.sensor.tank_ph");
  letHubSee();
  CHECK(hub.state()["readings"]["tank.ph"]["value"].get<double>() == doctest::Approx(6.1));
  // A temperature serves the role it was picked for, and only that one
  REQUIRE(ha::assign(hub, bus, "zone.air_temp", "sensor.tent_temp", letHubSee).status == 200);
  CHECK(hub.config().device("ha.sensor.tent_temp")->cls == "ha_air_temp");
  const auto twice = ha::assign(hub, bus, "tank.water_temp", "sensor.tent_temp", letHubSee);
  CHECK(twice.status == 422);
  CHECK(twice.body["error"]["key"] == "ha.used");
  CHECK_FALSE(hub.config().binding("tank.water_temp"));
  // Another sensor for the same role: the first one is gone, not left as a device
  REQUIRE(ha::assign(hub, bus, "zone.air_temp", "sensor.room_temp", letHubSee).status == 200);
  CHECK_FALSE(hub.config().device("ha.sensor.tent_temp"));
  CHECK(hub.config().binding("zone.air_temp")->device == "ha.sensor.room_temp");
  CHECK(ha::assign(hub, bus, "tank.water_temp", "sensor.tent_temp", letHubSee).status == 200);  // free again
  // Taken away: unbound, removed, no longer read
  REQUIRE(ha::assign(hub, bus, "tank.ph", "", letHubSee).status == 200);
  CHECK_FALSE(hub.config().binding("tank.ph"));
  CHECK_FALSE(hub.config().device("ha.sensor.tank_ph"));
  for (const auto& e : bus.entities()) CHECK(e.entityId != "sensor.tank_ph");
  letHubSee();  // the hub no longer lists it
  // Refused without leftovers: no measuring role, no candidate, the hub never sees it
  CHECK(ha::assign(hub, bus, "tank.circulation", "sensor.tank_ph", letHubSee).body["error"]["key"] == "ha.role");
  CHECK(ha::assign(hub, bus, "tank.ph", "sensor.unknown", letHubSee).body["error"]["key"] == "ha.unknown");
  CHECK(ha::assign(hub, bus, "tank.ph", "../etc", letHubSee).status == 422);
  const auto unseen = ha::assign(hub, bus, "tank.ph", "sensor.tank_ph", [] {});  // no tick: never appears
  REQUIRE(unseen.status == 504);
  CHECK(unseen.body["error"]["key"] == "ha.timeout");
  CHECK_FALSE(hub.config().device("ha.sensor.tank_ph"));
  for (const auto& e : bus.entities()) CHECK(e.entityId != "sensor.tank_ph");
  // A failed change keeps the sensor the role had
  REQUIRE(hub.config().binding("zone.air_temp")->device == "ha.sensor.room_temp");
  letHubSee();
  CHECK(ha::assign(hub, bus, "zone.air_temp", "sensor.tank_ph", [] {}).status == 422);  // pH is no air temperature
  REQUIRE(ha::assign(hub, bus, "tank.ph", "sensor.tank_ph", letHubSee).status == 200);
  REQUIRE(ha::assign(hub, bus, "tank.ph", "", letHubSee).status == 200);
  letHubSee();
  json more = states;
  more.push_back(described(state("sensor.box_temp", "26.0", "°C", iso(kNoonMs)), "temperature", "Box"));
  bus.updateAll(more, kNoonMs + clk.ms, clk.ms);
  CHECK(ha::assign(hub, bus, "zone.air_temp", "sensor.box_temp", [] {}).status == 504);
  CHECK(hub.config().binding("zone.air_temp")->device == "ha.sensor.room_temp");
  CHECK(bus.selectedMeasure("sensor.room_temp") == "air_temp");
  // The picks live in the configuration: after a restart the bus reads them again
  ha::HaBus after;
  gc::Hub again(cat, after, store, clk, pseudoRandom);
  again.boot();
  ha::adoptFromConfig(again, after);
  CHECK(after.selectedMeasure("sensor.room_temp") == "air_temp");
  CHECK(after.selectedMeasure("sensor.tent_temp") == "water_temp");
  CHECK(after.selectedMeasure("sensor.tank_ph").empty());
  // Bound in the configuration but not read (lost on the way): picking it again reads it again
  ha::HaBus forgetful;
  gc::Hub third(cat, forgetful, store, clk, pseudoRandom);
  third.boot();
  forgetful.updateAll(states, kNoonMs + clk.ms, clk.ms);
  REQUIRE(ha::assign(third, forgetful, "zone.air_temp", "sensor.room_temp", [] {}).status == 200);
  CHECK(forgetful.selectedMeasure("sensor.room_temp") == "air_temp");
}

TEST_CASE("Home Assistant: the server's routes need a signed-in session") {
  const gc::Catalog cat = ha::catalog();
  ha::HaBus bus;
  gc::MemoryStorage store;
  test::Clock clk;
  gc::Hub hub(cat, bus, store, clk, pseudoRandom);
  hub.boot();
  gc::Api api(hub, clk);
  bus.updateAll(json::array({described(state("sensor.tank_ph", "6.1", "", iso(kNoonMs)), "ph", "Tank pH")}), kNoonMs, clk.ms);
  gc::ApiRequest anon;
  anon.body = json{{"role", "tank.ph"}, {"entity", "sensor.tank_ph"}}.dump();
  CHECK(ha::candidatesRoute(api, bus, anon).status == 401);
  CHECK(ha::assignRoute(api, hub, bus, anon, [] {}).status == 401);
  CHECK(bus.entities().empty());
  // Signed in
  gc::ApiRequest setup;
  setup.method = "POST";
  setup.path = "/api/v1/auth/setup";
  setup.body = json{{"password", "ein-gutes-passwort"}}.dump();
  const gc::ApiResponse res = api.handle(setup);
  REQUIRE(res.status == 200);
  gc::ApiRequest signedIn = anon;
  for (const auto& [k, v] : res.headers)
    if (k == "Set-Cookie" && v.rfind("gc_session=", 0) == 0) signedIn.token = v.substr(11, v.find(';') - 11);
  REQUIRE_FALSE(signedIn.token.empty());
  const auto listed = ha::candidatesRoute(api, bus, signedIn);
  CHECK(listed.status == 200);
  CHECK(listed.body["connection"] == "starting");
  CHECK(listed.body["candidates"].size() == 1);
  CHECK(ha::assignRoute(api, hub, bus, signedIn, [&] {
          clk.ms += 200;
          hub.tick();
        }).status == 200);
  CHECK(hub.config().binding("tank.ph"));
  gc::ApiRequest junk = signedIn;
  junk.body = "[1,2";
  CHECK(ha::assignRoute(api, hub, bus, junk, [] {}).status == 422);
}

TEST_CASE("Home Assistant: states become samples in the hub's units; missing stays missing (R5)") {
  ha::HaBus bus({{"sensor.ph", "ph"}, {"sensor.ec", "ec"}, {"sensor.temp", "water_temp"}, {"sensor.level", "level"}, {"sensor.ec2", "ec"}});
  const gc::Ms now = 600000;
  bus.update("sensor.ph", state("sensor.ph", "6.2", "", "2026-10-09T11:59:50+00:00"), kNoonMs, now);
  bus.update("sensor.ec", state("sensor.ec", "1450", "µS/cm", "2026-10-09T12:00:00+00:00"), kNoonMs, now);
  bus.update("sensor.temp", state("sensor.temp", "68", "°F", "2026-10-09T12:00:00+00:00"), kNoonMs, now);
  bus.update("sensor.level", state("sensor.level", "40", "%", "2026-10-09T12:00:00+00:00"), kNoonMs, now);
  bus.update("sensor.ec2", state("sensor.ec2", "5", "", "2026-10-09T12:00:00+00:00"), kNoonMs, now);
  const auto ph = bus.sample("ha.sensor.ph", "measure.ph");
  REQUIRE(ph);
  CHECK(ph->raw == doctest::Approx(6.2));
  CHECK(ph->ts == now - 10000);  // reported 10 s ago in Home Assistant's time
  const auto ec = bus.sample("ha.sensor.ec", "measure.ec");
  REQUIRE(ec);
  CHECK(ec->raw == doctest::Approx(1.45));
  const auto temp = bus.sample("ha.sensor.temp", "measure.water_temp");
  REQUIRE(temp);
  CHECK(temp->raw == doctest::Approx(20.0));
  CHECK_FALSE(bus.sample("ha.sensor.level", "measure.level"));  // % needs the tank's shape: not yet
  CHECK(bus.fault("sensor.level") == "unit % not supported");
  // 5 µS/cm of reverse-osmosis water without a unit would read as 5 mS/cm (RAT-046)
  CHECK_FALSE(bus.sample("ha.sensor.ec2", "measure.ec"));
  CHECK(bus.fault("sensor.ec2") == "unit missing");
  CHECK_FALSE(bus.sample("ha.sensor.ph", "measure.ec"));
  auto devs = bus.devices();
  REQUIRE(devs.size() == 5);
  CHECK(devs[0].cls == "ha_ph");
  CHECK(devs[0].online);
  CHECK(devs[3].fault == "unit % not supported");
  // Home Assistant's text reaches terminal, state and diagnostics only printable and short
  bus.update("sensor.level", state("sensor.level", "40", "\x1b]0;x\x07" + std::string(100, 'L'), "2026-10-09T12:00:00+00:00"),
             kNoonMs, now);
  const std::string odd = bus.fault("sensor.level");
  CHECK(odd.find('\x1b') == std::string::npos);
  CHECK(odd.find('\x07') == std::string::npos);
  CHECK(odd.size() <= std::string("unit  not supported").size() + 32);
  bus.update("sensor.level", state("sensor.level", "40", "\xc2\x9b" "2J\x9b" "°x", "2026-10-09T12:00:00+00:00"), kNoonMs, now);
  CHECK(bus.fault("sensor.level") == "unit 2J°x not supported");  // C1 controls and stray bytes gone, ° kept
  // "unknown" is no value, "unavailable" is offline; neither becomes 0
  bus.update("sensor.ph", state("sensor.ph", "unknown", "", "2026-10-09T12:00:00+00:00"), kNoonMs, now);
  CHECK_FALSE(bus.sample("ha.sensor.ph", "measure.ph"));
  CHECK(bus.devices()[0].online);
  bus.update("sensor.ph", state("sensor.ph", "unavailable", "", "2026-10-09T12:00:00+00:00"), kNoonMs, now);
  CHECK_FALSE(bus.devices()[0].online);
  bus.lost("sensor.ec");
  CHECK_FALSE(bus.devices()[1].online);
  CHECK_FALSE(bus.sample("ha.sensor.ec", "measure.ec"));
}

TEST_CASE("Home Assistant: a report's time is never guessed") {
  ha::HaBus bus({{"sensor.ph", "ph"}});
  // No readable time: the hub cannot tell fresh from stuck, so no value (RAT-023)
  auto noTime = state("sensor.ph", "6.2", "", "");
  noTime["last_updated"] = "yesterday";
  bus.update("sensor.ph", noTime, kNoonMs, 1000);
  CHECK_FALSE(bus.sample("ha.sensor.ph", "measure.ph"));
  CHECK(bus.fault("sensor.ph") == "time of the report unreadable");
  // An answer naming another entity is not taken
  bus.update("sensor.ph", state("sensor.other", "6.2", "", iso(kNoonMs)), kNoonMs, 1000);
  CHECK_FALSE(bus.sample("ha.sensor.ph", "measure.ph"));
  // The same report keeps the time it got when first seen
  bus.update("sensor.ph", state("sensor.ph", "6.2", "", iso(kNoonMs)), kNoonMs + 2000, 5000);
  const auto first = bus.sample("ha.sensor.ph", "measure.ph");
  REQUIRE(first);
  CHECK(first->ts == 3000);
  // (Dated anew from each answer it would wander with the gap between the
  // two clocks, here 12000 - 7001 = 4999; the Date header's whole seconds
  // make that gap jump in practice.)
  bus.update("sensor.ph", state("sensor.ph", "6.2", "", iso(kNoonMs)), kNoonMs + 7001, 12000);
  const auto again = bus.sample("ha.sensor.ph", "measure.ph");
  REQUIRE(again);
  CHECK(again->ts == 3000);
  // A report dated after Home Assistant's "now" (clock step) counts as new, not as older
  bus.update("sensor.ph", state("sensor.ph", "6.3", "", iso(kNoonMs + 60000)), kNoonMs + 8000, 13000);
  const auto ahead = bus.sample("ha.sensor.ph", "measure.ph");
  REQUIRE(ahead);
  CHECK(ahead->ts == 13000);
}

TEST_CASE("Home Assistant: the spike switches nothing") {
  ha::HaBus bus({{"sensor.ph", "ph"}});
  std::string err;
  CHECK_FALSE(bus.setSwitch("ha.sensor.ph", 0, true, err));
  CHECK_FALSE(err.empty());
  CHECK_FALSE(bus.startRun("ha.sensor.ph", 1000, "job", err));
  CHECK_FALSE(bus.writePumpCalibration("ha.sensor.ph", 50, err));
}

TEST_CASE("Home Assistant: pH is shown, but not used for control until the hub has checked it (RAT-025)") {
  const gc::Catalog cat = ha::catalog();
  ha::HaBus bus({{"sensor.grow_ph", "ph"}, {"sensor.grow_temp", "water_temp"}});
  gc::MemoryStorage store;
  test::Clock clk;
  gc::Hub hub(cat, bus, store, clk, pseudoRandom);
  hub.boot();
  auto report = [&](const std::string& entity, const std::string& value, const std::string& unit) {
    auto s = state(entity, value, unit, "");
    s["last_reported"] = s["last_updated"] = iso(kNoonMs + clk.ms);  // reported just now
    bus.update(entity, s, kNoonMs + clk.ms, clk.ms);
  };
  auto tick = [&](int seconds, bool fresh) {
    for (int i = 0; i < seconds; ++i) {
      if (fresh) {
        report("sensor.grow_ph", "6.1", "");
        report("sensor.grow_temp", "21.5", "°C");
      }
      clk.ms += 1000;
      hub.tick();
    }
  };
  tick(3, true);
  REQUIRE(hub.acceptDevice("ha.sensor.grow_ph", "").status == 200);
  REQUIRE(hub.acceptDevice("ha.sensor.grow_temp", "").status == 200);
  REQUIRE(hub.config().binding("tank.ph"));  // measuring roles assign themselves
  CHECK(hub.config().binding("tank.ph")->device == "ha.sensor.grow_ph");
  tick(3, true);
  json st = hub.state();
  const json ph = st["readings"]["tank.ph"];
  CHECK(ph["value"].get<double>() == doctest::Approx(6.1));  // shown
  CHECK(ph["quality"] == "uncalibrated");                    // but no value for control
  CHECK(ph["reason"]["key"] == "truth.external");
  CHECK(st["readings"]["tank.water_temp"]["quality"] == "ok");  // needs no calibration
  // The hub offers no calibration of its own for it, and says why instead of asking for one
  const auto refused = hub.probeCalibration({{"device", "ha.sensor.grow_ph"}, {"kind", "ph"}, {"action", "start"}});
  CHECK(refused.status == 422);
  CHECK(refused.body["error"]["key"] == "probe.not_offered");
  // Every function that reads pH says it once, whether it needs a calibration (pH control) or not
  // (tank monitoring), and in the language of the other lines
  std::map<std::string, int> explained;
  for (const auto& f : st["functions"])
    for (const auto& c : f["checks"]) {
      const std::string text = c["text"];
      if (text.find("außerhalb des Hubs kalibriert") != std::string::npos) {
        ++explained[f["id"].get<std::string>()];
        CHECK(c["fix"] == "");
      }
      CHECK(text.find("Calibrated outside") == std::string::npos);
    }
  CHECK(explained["ph_control"] == 1);
  CHECK(explained["monitor_tank"] == 1);
  // The sensor goes silent (or Home Assistant is older than 2024.4): Home Assistant keeps answering
  // with the last report while its own time runs on. Valid until the 60 s limit, stale after it (RAT-023).
  const std::int64_t reportedAt = kNoonMs + clk.ms;
  auto sameReport = [&] {
    auto s = state("sensor.grow_ph", "6.1", "", "");
    s["last_reported"] = s["last_updated"] = iso(reportedAt);
    bus.update("sensor.grow_ph", s, kNoonMs + clk.ms, clk.ms);
  };
  for (int i = 0; i < 59; ++i) {
    sameReport();
    clk.ms += 1000;
    hub.tick();
  }
  CHECK(hub.state()["readings"]["tank.ph"]["reason"]["key"] == "truth.external");  // 59 s: not stale yet
  for (int i = 0; i < 3; ++i) {
    sameReport();
    clk.ms += 1000;
    hub.tick();
  }
  CHECK(hub.state()["readings"]["tank.ph"]["quality"] == "stale");  // 62 s
  bus.lost("sensor.grow_ph");
  clk.ms += 1000;
  hub.tick();
  CHECK(hub.state()["readings"]["tank.ph"]["value"].is_null());  // never 0 (R5)
}

TEST_CASE("Home Assistant: the poller reads with the token, never shows it, and stops when refused") {
  std::atomic<int> refuse{0};
  FakeHa ha([&](const httplib::Request& req, httplib::Response& res) {
    if (refuse) {
      res.status = refuse.load();
      return;
    }
    if (req.get_header_value("Authorization") != "Bearer secret") {
      res.status = 401;
      return;
    }
    if (req.path != "/api/states") {
      res.status = 404;
      return;
    }
    res.set_header("Date", "Fri, 09 Oct 2026 12:10:00 GMT");  // Home Assistant's own clock
    res.set_content(json::array({state("sensor.grow_ph", "5.9", "", "2026-10-09T12:00:00+00:00")}).dump(), "application/json");
  });
  REQUIRE(ha.port > 0);
  {
    ha::HaBus bus({{"sensor.grow_ph", "ph"}, {"sensor.gone", "ec"}});
    ha::Poller p(bus, ha.url(), "secret", [] { return gc::Ms{900000}; });
    CHECK(bus.candidatesJson()["connection"] == "starting");
    const std::string problem = p.pollOnce();
    CHECK(bus.candidatesJson()["connection"] == "ok");
    CHECK(problem == "sensor.gone: not in Home Assistant");
    CHECK(problem.find("secret") == std::string::npos);
    const auto s = bus.sample("ha.sensor.grow_ph", "measure.ph");
    REQUIRE(s);
    CHECK(s->raw == doctest::Approx(5.9));
    CHECK(s->ts == 900000 - 600000);  // 10 min old by Home Assistant's clock, whatever ours says
    CHECK_FALSE(bus.devices()[1].online);
  }
  {
    ha::HaBus bus({{"sensor.grow_ph", "ph"}});
    ha::Poller p(bus, ha.url(), "wrong", [] { return gc::Ms{0}; });
    const std::string problem = p.pollOnce();
    CHECK(problem == "Home Assistant rejected the token");
    CHECK(bus.candidatesJson()["connection"] == "refused");
    CHECK(problem.find("wrong") == std::string::npos);
    CHECK(p.rejected());
    CHECK_FALSE(bus.devices()[0].online);
    // Running, it does not try a refused token again (Home Assistant bans after a few failed logins)
    const int before = ha.count;
    p.start(std::chrono::milliseconds(20));
    for (int i = 0; i < 500 && ha.count == before; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(ha.count == before + 1);                             // the first round ran
    std::this_thread::sleep_for(std::chrono::milliseconds(300));  // about 3 more rounds if it went on (it waits in 100 ms steps)
    p.stop();
    CHECK(ha.count == before + 1);
  }
  {
    refuse = 403;
    ha::HaBus bus({{"sensor.grow_ph", "ph"}});
    ha::Poller p(bus, ha.url(), "secret", [] { return gc::Ms{0}; });
    CHECK(p.pollOnce().find("403") != std::string::npos);
    CHECK(p.rejected());
    refuse = 0;
  }
  ha.stop();
  {
    ha::HaBus bus({{"sensor.grow_ph", "ph"}});
    ha::Poller p(bus, ha.url(), "secret", [] { return gc::Ms{0}; });
    const std::string problem = p.pollOnce();
    CHECK(problem.find("not reachable") != std::string::npos);
    CHECK(bus.candidatesJson()["connection"] == "unreachable");
    CHECK(problem.find("secret") == std::string::npos);
    CHECK_FALSE(p.rejected());
  }
}

TEST_CASE("Home Assistant: the poller sends nothing but state reads, whatever the hub is asked to do") {
  FakeHa ha([](const httplib::Request&, httplib::Response& res) {
    res.set_content(json::array({state("sensor.grow_ph", "6.0", "", "2026-10-09T12:00:00+00:00"),
                                 state("sensor.grow_temp", "21.0", "°C", "2026-10-09T12:00:00+00:00")})
                        .dump(),
                    "application/json");
  });
  const gc::Catalog cat = ha::catalog();
  ha::HaBus bus({{"sensor.grow_ph", "ph"}, {"sensor.grow_temp", "water_temp"}});
  gc::MemoryStorage store;
  test::Clock clk;
  gc::Hub hub(cat, bus, store, clk, pseudoRandom);
  hub.boot();
  ha::Poller p(bus, ha.url(), "secret", [&clk] { return clk.ms; });
  p.pollOnce();
  hub.tick();
  hub.acceptDevice("ha.sensor.grow_ph", "");
  hub.acceptDevice("ha.sensor.grow_temp", "");
  hub.switchRole("tank.circulation", true);
  hub.switchRole("tank.ph", true);
  hub.manualDose("ph-minus", 2);
  hub.probeCalibration({{"device", "ha.sensor.grow_ph"}, {"kind", "ph"}, {"action", "start"}});
  hub.stop(gc::Msg{"stop.app", "STOP button in the app", json::object()});
  hub.resume();
  p.pollOnce();
  hub.tick();
  std::lock_guard<std::mutex> l(ha.m);
  REQUIRE_FALSE(ha.requests.empty());
  for (const auto& r : ha.requests) CHECK(r == "GET /api/states");  // one read per round, nothing else
}
