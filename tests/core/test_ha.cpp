// SPDX-License-Identifier: AGPL-3.0-or-later
// Read-only spike: Home Assistant as the device layer (docs/HOME_ASSISTANT.md).
#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <tuple>
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
  // A path in front of the API, as the Supervisor gives an add-on (http://supervisor/core/api/…)
  const std::pair<const char*, const char*> kept[] = {{"http://supervisor/core/", "http://supervisor/core"},
                                                      {"http://192.168.1.20:8123/core", "http://192.168.1.20:8123/core"},
                                                      {"http://ha/my-ha_1/Core", "http://ha/my-ha_1/Core"},
                                                      {"http://[::1]:8123/core/", "http://[::1]:8123/core"},
                                                      {"http://ha/core///", "http://ha/core"},
                                                      {"http://home_assistant:8123", "http://home_assistant:8123"}};
  for (const auto& [url, normal] : kept) {
    CAPTURE(url);
    CHECK(ha::parseMapping({{"url", url}}, err).url == normal);
    CHECK(err.empty());
  }
  std::string why;
  const auto a = ha::parseAddress("http://[::1]:8123/core", why);
  REQUIRE(a);
  CHECK(a->host == "::1");
  CHECK(a->port == 8123);
  CHECK(a->base == "/core");
  CHECK_FALSE(a->tls);
  CHECK(ha::parseAddress("https://ha", why)->port == 443);
  // The host part too: the token goes only to the host a person reads in the address
  for (const char* url : {"http://ha/../api", "http://ha/a?b=1", "http://ha/a#x", "http://ha/a b", "http://ha//core", "http://ha/a/./b",
                          "http://ha/%2e%2e", "http://ha/core/..", "http://ha/core.v1", "http://ha/c\xc3\xb6re", "http://ha/a//b",
                          "http://a:8123@b/core", "http://user@evil/core", "http://user:pw@ha/core", "http://ha?x/core", "http://ha#x/core",
                          "http://ha#@192.0.2.10/core", "http://ha:8123x", "http://:8123", "http://:8123/core", "http://ha\r\nX/core",
                          "http://[::1]x/core", "http://[]/core", "http:///core", "https:///core", "http://ha:0", "http://ha:65536",
                          "http://ha:123456", "http://ha /core", "HTTP://ha", "http://[cafe]/core", "http://[1234]/core"}) {
    CAPTURE(url);
    CHECK(refused({{"url", url}}));
  }
  // The API address as Home Assistant's docs give it, or a pasted endpoint: refused with a hint, not a 404 later
  for (const char* url : {"http://192.168.1.20:8123/api", "http://supervisor/core/api/", "http://ha:8123/api/states", "http://ha/API", "http://ha/api/x"}) {
    CAPTURE(url);
    ha::parseMapping({{"url", url}}, err);
    CHECK(err.find("/api") != std::string::npos);
  }
  CHECK(ha::parseMapping({{"url", "http://ha/myapi"}}, err).url == "http://ha/myapi");
  // Without entities the user picks them in the web app
  CHECK(ha::parseMapping({{"url", "http://ha:8123"}}, err).entities.empty());
  CHECK(err.empty());
  CHECK(refused({{"url", "http://ha:8123"}, {"entities", "sensor.grow_ph"}}));  // not a list
  for (const char* id : {"Sensor.Grow", "sensor", "../api/config", "sensor.a.b", "sensor.grow ph", "person.someone"})
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
  CHECK_FALSE(bus.select("sensor.phone_battery", "humidity", why));  // Home Assistant says it is a battery
  CHECK(why.key == "ha.mismatch");
  CHECK_FALSE(bus.select("sensor.nowhere", "humidity", why));  // not in Home Assistant
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
  CHECK(bus.candidatesJson()["truncated"] == json::array({"temperature"}));  // only the kind that was cut
}

TEST_CASE("Home Assistant: any numeric sensor can be picked from the other sensors, in a unit that fits (SD-036)") {
  json meter = described(state("sensor.meter_l", "1234", "L", iso(kNoonMs)), "", "Wasserzähler ohne Klasse");
  meter["attributes"]["state_class"] = "total_increasing";
  json volumeMeter = described(state("sensor.vol_total", "800", "L", iso(kNoonMs)), "volume", "Zähler");
  volumeMeter["attributes"]["state_class"] = "total";
  json states = json::array({
      described(state("sensor.tank_ph", "6.1", "", iso(kNoonMs)), "ph", "Tank pH"),             // a suggestion
      described(state("sensor.diy_rh", "58", "%", iso(kNoonMs)), "", "Selbstbau Feuchte"),       // no device class
      described(state("sensor.diy_co2", "720", "ppm", iso(kNoonMs)), "", "Selbstbau CO2"),
      described(state("sensor.diy_ph", "6.3", "", iso(kNoonMs)), "", "Eimer pH"),                // no unit: only pH
      described(state("sensor.diy_level", "33", "L", iso(kNoonMs)), "", "Tank Liter"),
      described(state("sensor.diy_off", "unavailable", "%", iso(kNoonMs)), "", "Selbstbau aus"),  // a unit, no value now
      described(state("sensor.phone_battery", "80", "%", iso(kNoonMs)), "battery", "Handy"),     // its unit fits, its class not
      described(state("sensor.rh_raw", "55", "", iso(kNoonMs)), "humidity", "Feuchte ohne Einheit"),  // the hub's class, no unit
      volumeMeter,                                                                                  // the hub's class, but a meter
      meter,                                                                                        // a litre counter is no level
      described(state("sensor.plug_power", "12.5", "W", iso(kNoonMs)), "power", "Steckdose"),    // nothing the hub reads: not kept
      described(state("sensor.watts", "3", "W", iso(kNoonMs)), "", "Watt ohne Klasse"),
      described(state("sensor.firmware", "1.2.3", "", iso(kNoonMs)), "", "Firmware"),            // no number
      described(state("sensor.last_boot", "2026-10-09T10:00:00+00:00", "", iso(kNoonMs)), "timestamp", "Start"),
      described(state("binary_sensor.door", "on", "", iso(kNoonMs)), "door", "Tür"),           // not a sensor
  });
  ha::HaBus bus;
  bus.updateAll(states, kNoonMs, 1000);
  // The suggestions stay what they were; the others come only when asked for
  CHECK(bus.candidates().size() == 1);
  const json brief = bus.candidatesJson();
  CHECK(brief["candidates"].size() == 1);
  CHECK(brief["otherFits"] == json{{"humidity", 2}, {"co2", 1}, {"ph", 1}, {"level", 1}});
  CHECK(brief["otherCount"] == 8);
  CHECK_FALSE(brief.contains("others"));
  std::map<std::string, json> other;
  const json full = bus.candidatesJson(true);  // kept: a loop over a temporary's part would dangle
  for (const auto& o : full["others"]) other[o["entity"].get<std::string>()] = o;
  // Only what someone could pick, or what needs saying why not: the web app learns no more of the house
  CHECK(other.size() == 8);
  for (const char* gone : {"sensor.plug_power", "sensor.watts", "sensor.meter_l", "sensor.firmware", "sensor.last_boot",
                           "binary_sensor.door", "sensor.tank_ph"})
    CHECK_MESSAGE(other.count(gone) == 0, gone);
  CHECK(other["sensor.diy_rh"]["measures"] == json::array({"humidity"}));
  CHECK(other["sensor.diy_rh"]["name"] == "Selbstbau Feuchte");
  CHECK(other["sensor.diy_rh"]["raw"] == 58);
  CHECK(other["sensor.diy_rh"]["problem"].is_null());
  CHECK(other["sensor.diy_co2"]["measures"] == json::array({"co2"}));
  CHECK(other["sensor.diy_ph"]["measures"] == json::array({"ph"}));
  CHECK(other["sensor.diy_level"]["measures"] == json::array({"level"}));
  CHECK(other["sensor.diy_off"]["measures"] == json::array({"humidity"}));
  CHECK(other["sensor.diy_off"]["raw"].is_null());  // no value, never 0 (R5)
  CHECK(other["sensor.phone_battery"]["measures"].empty());
  CHECK(other["sensor.phone_battery"]["problem"] == "device_class");
  CHECK(other["sensor.phone_battery"]["deviceClass"] == "battery");
  CHECK(other["sensor.phone_battery"]["raw"].is_null());  // what it measures stays in Home Assistant
  CHECK(other["sensor.rh_raw"]["problem"] == "unit_missing");  // the unit is the reason, not "something else"
  CHECK(other["sensor.rh_raw"]["raw"].is_null());
  CHECK(other["sensor.vol_total"]["problem"] == "meter");
  // Sorted by name, the entity ID breaking ties
  std::vector<std::string> names;
  for (const auto& o : full["others"]) names.push_back(o["name"]);
  CHECK(std::is_sorted(names.begin(), names.end()));
  // Picked for a measure its unit fits; refused for any other, with the reason
  gc::Msg why;
  CHECK_FALSE(bus.select("sensor.diy_rh", "ph", why));
  CHECK(why.key == "ha.mismatch");
  CHECK_FALSE(bus.select("sensor.phone_battery", "humidity", why));
  CHECK(why.key == "ha.mismatch");
  CHECK_FALSE(bus.select("sensor.plug_power", "humidity", why));  // not offered at all
  CHECK(why.key == "ha.unknown");
  CHECK_FALSE(bus.select("sensor.meter_l", "level", why));
  REQUIRE(bus.select("sensor.diy_rh", "humidity", why));
  CHECK(bus.devices().at(0).online);  // at once, from the round that listed it
  REQUIRE(bus.sample("ha.sensor.diy_rh", "measure.humidity"));
  CHECK(bus.sample("ha.sensor.diy_rh", "measure.humidity")->raw == doctest::Approx(58));
  const json picked = bus.candidatesJson(true);
  for (const auto& o : picked["others"]) CHECK(o["used"] == (o["entity"] == "sensor.diy_rh" ? json("humidity") : json(nullptr)));
  bus.updateAll(states, kNoonMs, 2000);  // read on like any pick
  CHECK(bus.devices().at(0).online);
  // Home Assistant not answering: no old value shows as current
  bus.lostAll();
  for (const auto& o : bus.others()) CHECK(std::isnan(o.raw));
  const json lost = bus.candidatesJson(true);
  for (const auto& o : lost["others"]) CHECK(o["raw"].is_null());
  // A large installation: past the limit the picked sensor and the ones that
  // can be picked stay, whatever Home Assistant lists first, and it says so
  json many = json::array();
  for (size_t i = 0; i < ha::HaBus::kMaxOthers + 20; ++i)
    many.push_back(described(state("sensor.b" + std::to_string(i), "80", "%", iso(kNoonMs)), "battery", "Akku"));
  many.push_back(described(state("sensor.diy_rh", "58", "%", iso(kNoonMs)), "", "Selbstbau Feuchte"));
  many.push_back(described(state("sensor.diy_co2", "720", "ppm", iso(kNoonMs)), "", "Selbstbau CO2"));
  many.push_back(described(state("sensor.tank_ph", "6.1", "", iso(kNoonMs)), "ph", "Tank pH"));
  bus.updateAll(many, kNoonMs, 3000);
  const json cut = bus.candidatesJson(true);
  CHECK(cut["others"].size() == ha::HaBus::kMaxOthers);
  CHECK(cut["truncated"] == json::array({"other"}));
  CHECK(cut["candidates"].size() == 1);  // the tank's pH is still suggested
  std::set<std::string> kept;
  for (const auto& o : cut["others"]) kept.insert(o["entity"]);
  CHECK(kept.count("sensor.diy_rh"));
  CHECK(kept.count("sensor.diy_co2"));
  CHECK(bus.devices().at(0).online);  // the pick reads on
  ha::HaBus fresh;
  fresh.updateAll(many, kNoonMs, 3000);
  CHECK(fresh.select("sensor.diy_co2", "co2", why));  // can be picked although listed last
}

TEST_CASE("Home Assistant: only units the hub converts exactly give a value; nothing is guessed (RAT-006, RAT-015)") {
  auto withClass = [](json s, const std::string& stateClass) {
    s["attributes"]["state_class"] = stateClass;
    return s;
  };
  const json states = json::array({
      described(state("sensor.tank_tds", "700", "ppm", iso(kNoonMs)), "conductivity", "Tank TDS"),  // a factor would be a guess
      described(state("sensor.tds_plain", "700", "ppm", iso(kNoonMs)), "", "TDS"),                  // ppm alone: not suggested (as an other it fits only CO2, SD-036)
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
  std::string nested = "[";  // 20 levels: within the limit, read past and dropped
  for (int i = 0; i < 20; ++i) nested += "[";
  for (int i = 0; i < 20; ++i) nested += "]";
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
  CHECK(kept.size() == 1);            // only sensors: the person and the over-long ID are gone
  ha::HaBus bus;
  bus.updateAll(kept, kNoonMs, 1000);
  REQUIRE(bus.candidates().size() == 1);
  CHECK(bus.candidates()[0].name == "Tank pH");  // invisible format characters gone
  CHECK(ha::parseStates("not json").is_discarded());
  CHECK(ha::parseStates("{}").is_discarded());  // not a list: a problem, not "no sensors"
  CHECK(ha::parseStates("5").is_discarded());
  CHECK(ha::parseStates("[]").is_array());
  // A huge kept text is cut while reading
  json huge = json::array({described(state("sensor.tank_ph", "6.1", "", iso(kNoonMs)), "ph", std::string(1000000, 'n'))});
  CHECK(ha::parseStates(huge.dump()).dump().size() < 2000);
  // Only a name is cut; any other over-long field is dropped, so a cut never makes it valid or changes its meaning
  json longId = json::array({state("sensor." + std::string(248, 'a') + "\u00e9", "6.1", "", iso(kNoonMs))});
  CHECK(longId[0]["entity_id"].get<std::string>().size() == 257);
  CHECK(ha::parseStates(longId.dump()).empty());
  json longState = json::array({described(state("sensor.tank_ph", std::string(256, '6') + "x", "", iso(kNoonMs)), "ph", "pH")});
  const json keptState = ha::parseStates(longState.dump());
  REQUIRE(keptState.size() == 1);
  CHECK_FALSE(keptState[0].contains("state"));
  // Too deep a nesting stops the read instead of building it
  CHECK(ha::parseStates("[" + std::string(100, '[') + std::string(100, ']') + "]").is_discarded());
  // Many tiny states cost time in proportion to their number, not its square
  std::string tiny = "[";
  for (int i = 0; i < 200000; ++i) tiny += i ? ",{}" : "{}";
  tiny += "]";
  const auto t0 = std::chrono::steady_clock::now();
  CHECK(ha::parseStates(tiny).empty());
  CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(5));
  // Invisible characters beyond the format ones are gone too
  bus.updateAll(json::array({described(state("sensor.tank_ph", "6.1", "", iso(kNoonMs)), "ph",
                                       "Tank\xc2\xad pH\xf3\xa0\x80\x81")}),
                kNoonMs, 2000);
  CHECK(bus.candidates()[0].name == "Tank pH");
}

TEST_CASE("Home Assistant: a sensor is picked for a role in one step, and taken away again without leftovers") {
  const json states = json::array({
      described(state("sensor.tank_ph", "6.1", "", iso(kNoonMs)), "ph", "Tank pH"),
      described(state("sensor.tent_temp", "24.5", "°C", iso(kNoonMs)), "temperature", "Zelt Temperatur"),
      described(state("sensor.room_temp", "21.0", "°C", iso(kNoonMs)), "temperature", "Wohnzimmer"),
      described(state("sensor.diy_rh", "58", "%", iso(kNoonMs)), "", "Selbstbau Feuchte"),  // an other sensor (SD-036)
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
  // An other sensor is picked the same way, under its Home Assistant name, for what its unit fits
  REQUIRE(ha::assign(hub, bus, "zone.humidity", "sensor.diy_rh", letHubSee).status == 200);
  CHECK(hub.config().device("ha.sensor.diy_rh")->name == "Selbstbau Feuchte");
  CHECK(hub.config().device("ha.sensor.diy_rh")->cls == "ha_humidity");
  letHubSee();
  CHECK(hub.state()["readings"]["zone.humidity"]["value"].get<double>() == doctest::Approx(58));
  CHECK(ha::assign(hub, bus, "zone.co2", "sensor.diy_rh", letHubSee).body["error"]["key"] == "ha.used");
  REQUIRE(ha::assign(hub, bus, "zone.humidity", "", letHubSee).status == 200);
  CHECK(ha::assign(hub, bus, "zone.co2", "sensor.diy_rh", letHubSee).body["error"]["key"] == "ha.mismatch");  // % is no CO2
  CHECK_FALSE(hub.config().device("ha.sensor.diy_rh"));
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
  REQUIRE(ha::assign(hub, bus, "zone.humidity", "sensor.diy_rh", letHubSee).status == 200);  // an other sensor (SD-036)
  // The picks live in the configuration: after a restart the bus reads them again
  ha::HaBus after;
  gc::Hub again(cat, after, store, clk, pseudoRandom);
  again.boot();
  ha::adoptFromConfig(again, after);
  CHECK(after.selectedMeasure("sensor.room_temp") == "air_temp");
  CHECK(after.selectedMeasure("sensor.diy_rh") == "humidity");
  after.updateAll(states, kNoonMs + clk.ms, clk.ms);
  REQUIRE(after.sample("ha.sensor.diy_rh", "measure.humidity"));
  CHECK(after.sample("ha.sensor.diy_rh", "measure.humidity")->raw == doctest::Approx(58));
  CHECK(after.selectedMeasure("sensor.tent_temp") == "water_temp");
  CHECK(after.selectedMeasure("sensor.tank_ph").empty());
  // The configuration wins over the mapping file at start
  ha::HaBus listed({{"sensor.room_temp", "water_temp"}});
  gc::Hub fourth(cat, listed, store, clk, pseudoRandom);
  fourth.boot();
  ha::adoptFromConfig(fourth, listed);
  CHECK(listed.selectedMeasure("sensor.room_temp") == "air_temp");
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
  CHECK_FALSE(listed.body.contains("others"));  // the other sensors only when asked for (SD-036)
  gc::ApiRequest withOthers = signedIn;
  withOthers.query["others"] = "1";
  CHECK(ha::candidatesRoute(api, bus, withOthers).body["others"].is_array());
  CHECK(ha::assignRoute(api, hub, bus, signedIn, [&] {
          clk.ms += 200;
          hub.tick();
        }).status == 200);
  CHECK(hub.config().binding("tank.ph"));
  gc::ApiRequest junk = signedIn;
  junk.body = "[1,2";
  CHECK(ha::assignRoute(api, hub, bus, junk, [] {}).status == 400);
  junk.body = json{{"role", "tank.ph"}}.dump();  // no entity: nothing is taken away
  CHECK(ha::assignRoute(api, hub, bus, junk, [] {}).status == 400);
  CHECK(hub.config().binding("tank.ph"));
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
  // with the last report while its own time runs on. It reported every second, so its limit is the
  // shortest for a sensor on its own schedule, 5 min: valid until then, stale after it (RAT-023, SD-035).
  tick(15, true);
  const std::int64_t reportedAt = kNoonMs + clk.ms;
  auto sameReport = [&] {
    auto s = state("sensor.grow_ph", "6.1", "", "");
    s["last_reported"] = s["last_updated"] = iso(reportedAt);
    bus.update("sensor.grow_ph", s, kNoonMs + clk.ms, clk.ms);
  };
  for (int i = 0; i < 299; ++i) {
    sameReport();
    clk.ms += 1000;
    hub.tick();
  }
  CHECK(hub.state()["readings"]["tank.ph"]["reason"]["key"] == "truth.external");  // 299 s: not stale yet
  for (int i = 0; i < 3; ++i) {
    sameReport();
    clk.ms += 1000;
    hub.tick();
  }
  CHECK(hub.state()["readings"]["tank.ph"]["quality"] == "stale");  // 302 s
  bus.lost("sensor.grow_ph");
  clk.ms += 1000;
  hub.tick();
  CHECK(hub.state()["readings"]["tank.ph"]["value"].is_null());  // never 0 (R5)
}

TEST_CASE("Home Assistant: a sensor that reports every few minutes stays valid, and pH is in the history (SD-035, SD-039)") {
  const gc::Catalog cat = ha::catalog();
  for (const char* m : {"ph", "ec", "water_temp", "level", "air_temp", "humidity", "co2"}) {
    REQUIRE(cat.deviceClass(std::string("ha_") + m));
    CHECK(cat.deviceClass(std::string("ha_") + m)->ownReportSchedule);
  }
  CHECK_FALSE(cat.deviceClass("head_ph_ec")->ownReportSchedule);  // the hub's own probes keep the catalog's limit
  ha::HaBus bus({{"sensor.grow_ph", "ph"}, {"sensor.grow_temp", "water_temp"}});
  gc::MemoryStorage store;
  test::Clock clk;
  clk.start = kNoonMs / 1000;
  gc::Hub hub(cat, bus, store, clk, pseudoRandom);
  hub.boot();
  // The sensors report every 3 min; Home Assistant is asked every 5 s and
  // answers with the last report in between.
  std::int64_t reportedAt = kNoonMs;
  double ph = 6.0;
  auto answer = [&] {
    for (const auto& [entity, value, unit] : {std::tuple<std::string, std::string, std::string>{"sensor.grow_ph", std::to_string(ph), ""},
                                              {"sensor.grow_temp", "21.5", "°C"}}) {
      auto s = state(entity, value, unit, "");
      s["last_reported"] = s["last_updated"] = iso(reportedAt);
      bus.update(entity, s, kNoonMs + clk.ms, clk.ms);
    }
  };
  auto run = [&](int minutes) {
    for (int i = 0; i < minutes * 12; ++i) {
      if (kNoonMs + clk.ms - reportedAt >= 3 * 60 * 1000) {
        reportedAt = kNoonMs + clk.ms;
        ph += 0.01;
      }
      answer();
      clk.ms += 5000;
      hub.tick();
    }
  };
  run(1);
  REQUIRE(hub.acceptDevice("ha.sensor.grow_ph", "").status == 200);
  REQUIRE(hub.acceptDevice("ha.sensor.grow_temp", "").status == 200);
  run(20);
  // 3 min after the last report: valid, not "last value 3 min ago"
  json st = hub.state();
  CHECK(st["readings"]["tank.water_temp"]["quality"] == "ok");
  CHECK(st["readings"]["tank.ph"]["reason"]["key"] == "truth.external");
  // Calibrated in Home Assistant, so no value for control, but its curve is
  // recorded: shown values belong in the history (SD-039)
  CHECK_FALSE(st["readings"]["tank.ph"]["usable"].get<bool>());
  const json h = hub.history("tank.ph", clk.epoch() - 600, clk.epoch(), 0);
  size_t values = 0;
  for (const auto& v : h["avg"])
    if (v.is_number()) {
      ++values;
      CHECK(v.get<double>() > 6.0);
      CHECK(v.get<double>() < 6.2);
    }
  CHECK(values > 0);
  CHECK(hub.exportCsv({"tank.ph"}, clk.epoch() - 600, clk.epoch()).find(";6") != std::string::npos);  // and exported
  // What fails a check is a gap, never a value (R5). Silent: valid for three
  // of its usual gaps (9 min), then stale.
  auto silent = [&](int minutes) {
    for (int i = 0; i < minutes * 12; ++i) {
      answer();
      clk.ms += 5000;
      hub.tick();
    }
  };
  auto historyEmpty = [&](gc::Epoch seconds) {
    const json h = hub.history("tank.ph", clk.epoch() - seconds, clk.epoch(), 0);
    for (const auto& v : h["avg"])
      if (!v.is_null()) return false;
    return true;
  };
  silent(5);  // 8 min since the last report
  CHECK(hub.state()["readings"]["tank.ph"]["reason"]["key"] == "truth.external");
  silent(2);
  CHECK(hub.state()["readings"]["tank.ph"]["quality"] == "stale");
  CHECK(historyEmpty(50));
  // Reporting again, then an implausible pH
  run(6);
  REQUIRE(hub.state()["readings"]["tank.ph"]["reason"]["key"] == "truth.external");
  ph = 9.5;
  run(6);
  CHECK(hub.state()["readings"]["tank.ph"]["quality"] == "implausible");
  CHECK(historyEmpty(300));
}

TEST_CASE("Home Assistant: an address with a path is read below it, as in an add-on") {
  FakeHa ha([&](const httplib::Request& req, httplib::Response& res) {
    if (req.path != "/core/api/states") {
      res.status = 404;
      return;
    }
    res.set_content(json::array({state("sensor.grow_ph", "5.9", "", "2026-10-09T12:00:00+00:00")}).dump(), "application/json");
  });
  REQUIRE(ha.port > 0);
  ha::HaBus bus({{"sensor.grow_ph", "ph"}});
  ha::Poller p(bus, ha.url() + "/core/", "secret", [] { return gc::Ms{900000}; });
  CHECK(p.pollOnce().empty());
  CHECK(bus.candidatesJson()["connection"] == "ok");
  // A path Home Assistant does not know: the answer names it
  ha::HaBus lost({{"sensor.grow_ph", "ph"}});
  ha::Poller wrong(lost, ha.url() + "/lovelace", "secret", [] { return gc::Ms{0}; });
  CHECK(wrong.pollOnce() == "Home Assistant answered HTTP 404 for /lovelace/api/states; check the path in \"url\"");
  // An address the mapping would refuse is not used, whatever passes it in
  ha::HaBus other({{"sensor.grow_ph", "ph"}});
  ha::Poller bad(other, "http://evil@127.0.0.1:" + std::to_string(ha.port) + "/core", "secret", [] { return gc::Ms{0}; });
  CHECK(bad.pollOnce() == "the Home Assistant address is not valid");
  CHECK(other.candidatesJson()["connection"] == "unreachable");
  ha.stop();
  REQUIRE(ha.requests.size() == 2);  // the bad address sent nothing
  CHECK(ha.requests[0] == "GET /core/api/states");
  CHECK(ha.requests[1] == "GET /lovelace/api/states");
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

// ---------- Raw values calibrated in the app (SD-038, SD-041)

namespace {
// A pH probe's voltage at a given pH: its offset at pH 7 and its sensitivity in mV/pH (rising
// voltage for falling pH, as a glass electrode behaves)
double electrodeMv(double ph, double offsetMv, double mvPerPh) { return offsetMv + (7.0 - ph) * mvPerPh; }
std::string fmt(double v) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.3f", v);
  return buf;
}
}  // namespace

TEST_CASE("Home Assistant: a pH probe's voltage is a raw value; the picker suggests it, other voltages only on request (SD-038, SD-041)") {
  ha::HaBus bus;
  bus.updateAll(json::array({
                    described(state("sensor.tank_ph_mv", "-10", "mV", iso(kNoonMs)), "voltage", "Tank pH Spannung"),
                    described(state("sensor.tank_ph", "6.1", "", iso(kNoonMs)), "ph", "Tank pH"),
                    described(state("sensor.probe_ph", "0.012", "V", iso(kNoonMs)), "ph", "Sonde"),  // Home Assistant calls it pH, the unit says raw
                    described(state("sensor.ups_voltage", "230", "V", iso(kNoonMs)), "voltage", "USV Netzspannung"),
                    described(state("sensor.phone_battery", "3.9", "V", iso(kNoonMs)), "", "Handy Akku"),
                }),
                kNoonMs, 1000);
  auto suggestion = [&](const std::string& id) -> std::optional<ha::Candidate> {
    for (const auto& c : bus.candidates())
      if (c.entityId == id) return c;
    return std::nullopt;
  };
  auto other = [&](const std::string& id) -> std::optional<ha::Candidate> {
    for (const auto& c : bus.others())
      if (c.entityId == id) return c;
    return std::nullopt;
  };
  // A voltage named pH, or one Home Assistant calls pH: suggested for pH, as a raw signal without a value of its own
  for (const char* id : {"sensor.tank_ph_mv", "sensor.probe_ph"}) {
    CAPTURE(id);
    const auto c = suggestion(id);
    REQUIRE(c);
    CHECK(c->kind == "ph");
    CHECK(c->signal);
    CHECK(c->problem.empty());
    CHECK(std::isnan(c->value));  // no pH before the app has calibrated it
  }
  CHECK(suggestion("sensor.tank_ph_mv")->raw == doctest::Approx(-10));
  CHECK(suggestion("sensor.tank_ph_mv")->unit == "mV");
  CHECK_FALSE(suggestion("sensor.tank_ph")->signal);  // a pH is calibrated in Home Assistant
  // Any other voltage a front end can deliver is never suggested, but can be picked as a pH signal
  // from the other sensors; the mains is not passed on at all
  CHECK_FALSE(other("sensor.ups_voltage"));
  for (const char* id : {"sensor.phone_battery"}) {
    CAPTURE(id);
    CHECK_FALSE(suggestion(id));
    const auto o = other(id);
    REQUIRE(o);
    CHECK(o->signal);
    CHECK(o->measures == std::vector<std::string>{"ph"});
    CHECK(o->problem.empty());
  }
  const json j = bus.candidatesJson(true);
  bool seen = false;
  for (const auto& c : j["candidates"])
    if (c["entity"] == "sensor.tank_ph_mv") {
      seen = true;
      CHECK(c["signal"] == true);
      CHECK(c["value"].is_null());
      CHECK(c["raw"].get<double>() == doctest::Approx(-10));
    } else if (c["entity"] == "sensor.tank_ph") {
      CHECK_FALSE(c.contains("signal"));  // only where it is one
    }
  CHECK(seen);
  // Picked, it is a device of its own class, with the calibration offered in the app
  gc::Msg why;
  REQUIRE(bus.select("sensor.tank_ph_mv", "ph", why));
  CHECK(bus.devices()[0].cls == "ha_ph_raw");
  CHECK_FALSE(bus.select("sensor.phone_battery", "water_temp", why));  // a voltage is no temperature
  CHECK(why.key == "ha.mismatch");
}

TEST_CASE("Home Assistant: a pH voltage is calibrated in the app with two buffers, then valid; it switches nothing (SD-038, RAT-025, RAT-006)") {
  const gc::Catalog cat = ha::catalog();
  const gc::DeviceClassDef* dc = cat.deviceClass("ha_ph_raw");
  REQUIRE(dc);
  CHECK(dc->calibrations == std::vector<std::string>{"ph"});
  CHECK_FALSE(dc->externalCalibration);  // set in code: the hub checks it itself
  CHECK(dc->ownReportSchedule);
  CHECK(cat.deviceClass("ha_ph")->externalCalibration);  // a pH from Home Assistant stays display only
  ha::HaBus bus;
  gc::MemoryStorage store;
  test::Clock clk;
  gc::Hub hub(cat, bus, store, clk, pseudoRandom);
  hub.boot();
  const double offset = -10, slope = 57;  // a healthy probe: 96 % of Nernst
  double mv = electrodeMv(6.1, offset, slope);
  std::int64_t reportedAt = kNoonMs;
  auto states = [&] {
    return json::array({described(state("sensor.tank_ph_mv", fmt(mv), "mV", iso(reportedAt)), "voltage", "Tank pH Spannung")});
  };
  auto report = [&](double newMv) {  // a new report from Home Assistant
    mv = newMv;
    reportedAt = kNoonMs + clk.ms;
  };
  auto tick = [&] {
    clk.ms += 1000;
    bus.updateAll(states(), kNoonMs + clk.ms, clk.ms);
    hub.tick();
  };
  bus.updateAll(states(), kNoonMs + clk.ms, clk.ms);
  REQUIRE(ha::assign(hub, bus, "tank.ph", "sensor.tank_ph_mv", tick).status == 200);
  REQUIRE(hub.config().device("ha.sensor.tank_ph_mv"));
  CHECK(hub.config().device("ha.sensor.tank_ph_mv")->cls == "ha_ph_raw");
  report(mv);
  tick();
  json ph = hub.state()["readings"]["tank.ph"];
  CHECK(ph["quality"] == "uncalibrated");  // waits for the app's calibration, not "calibrated elsewhere"
  CHECK(ph["reason"]["key"] == "truth.uncalibrated");
  // Shown as the pH an ideal probe would read (Nernst, 59.16 mV/pH at 25 °C), marked uncalibrated
  CHECK(ph["value"].get<double>() == doctest::Approx(7.0 - mv / ha::kNernstMvPerPh).epsilon(1e-6));
  // The calibration: a point takes the first report after it was asked for, not the last one before
  auto probe = [&](const std::string& action, double ref = gc::kNaN) {
    json j = {{"device", "ha.sensor.tank_ph_mv"}, {"kind", "ph"}, {"action", action}};
    if (std::isfinite(ref)) j["reference"] = ref;
    return hub.probeCalibration(j);
  };
  REQUIRE(probe("start").status == 200);
  auto session = [&] { return hub.state()["probeCalibration"]["ha.sensor.tank_ph_mv:ph"]; };
  // The probe goes into the pH 7 buffer; the last report is still from the tank
  auto asked = probe("point", 7.0);
  REQUIRE(asked.status == 200);
  CHECK(asked.body["pending"]["reference"] == 7.0);
  tick();
  tick();
  tick();  // Home Assistant sends the same report again: it is not taken
  CHECK(session()["points"].empty());
  CHECK(session().contains("pending"));
  report(electrodeMv(7.0, offset, slope));
  tick();
  REQUIRE(session()["points"].size() == 1);
  CHECK_FALSE(session().contains("pending"));
  CHECK(session()["points"][0][0].get<double>() == doctest::Approx(7.0 - offset / ha::kNernstMvPerPh).epsilon(1e-6));
  CHECK(session()["points"][0][1] == 7.0);
  REQUIRE(probe("point", 4.0).status == 200);
  tick();
  tick();
  tick();
  report(electrodeMv(4.0, offset, slope));
  tick();
  REQUIRE(session()["points"].size() == 2);
  const auto done = probe("commit");
  REQUIRE(done.status == 200);
  // In the tank again: a valid pH, usable, with nothing switched
  report(electrodeMv(6.1, offset, slope));
  tick();
  ph = hub.state()["readings"]["tank.ph"];
  CHECK(ph["quality"] == "ok");
  CHECK(ph["usable"] == true);
  CHECK(ph["value"].get<double>() == doctest::Approx(6.1).epsilon(1e-4));
  std::string err;
  CHECK_FALSE(bus.setSwitch("ha.sensor.tank_ph_mv", 0, true, err));
  CHECK_FALSE(bus.startRun("ha.sensor.tank_ph_mv", 1000, "job", err));
  // A point that gets no new report within the sensor's limit is refused, and says so
  REQUIRE(probe("start").status == 200);
  REQUIRE(probe("point", 7.0).status == 200);
  for (int i = 0; i < 11 * 60; ++i) tick();  // the same report for 11 min
  CHECK(session()["points"].empty());
  CHECK_FALSE(session().contains("pending"));
  CHECK(session()["failed"]["key"] == "probe.no_raw");
  REQUIRE(probe("cancel").status == 200);
}

TEST_CASE("Home Assistant: the slope band for a pH voltage is 80–125 % of Nernst; an amplified board and swapped buffers are refused (SD-038, RAT-025)") {
  // Two buffers, pH 7 and pH 4, as the bus hands them to the hub; offset 0
  auto calibrated = [](double ph7Mv, double ph4Mv) {
    const json data = {{"points", json::array({json::array({7.0 - ph7Mv / ha::kNernstMvPerPh, 7.0}),
                                               json::array({7.0 - ph4Mv / ha::kNernstMvPerPh, 4.0})})}};
    return gc::SensorTruth::calibrate("measure.ph", 7.0, &data).has_value();
  };
  auto bySlope = [&](double mvPerPh) { return calibrated(electrodeMv(7, 0, mvPerPh), electrodeMv(4, 0, mvPerPh)); };
  CHECK_FALSE(bySlope(47.2));  // below 80 % (47.3 mV/pH)
  CHECK(bySlope(47.5));
  CHECK(bySlope(59.16));
  CHECK(bySlope(73.8));
  CHECK_FALSE(bySlope(74.1));  // above 125 % (74.0 mV/pH)
  CHECK_FALSE(bySlope(-57));   // falling instead of rising: swapped buffers or an inverting amplifier
  CHECK_FALSE(calibrated(1500, 2032.44));  // a board with gain 3 (1500 mV at pH 7): needs a rule of its own first
  CHECK(calibrated(1650, 1650 + 3 * 59.16));  // a front end without gain but with an offset: the offset does not matter
  CHECK_FALSE(calibrated(0, 25));            // 25 mV apart: less than 0.5 on the raw scale (29.6 mV), whatever the slope
}

TEST_CASE("Home Assistant: a pH voltage in V reads as in mV; after a unit change it has no value (SD-041, RAT-006)") {
  ha::HaBus bus({{"sensor.ph_mv", "ph", true}, {"sensor.ph_v", "ph", true}, {"sensor.grow_ph", "ph"}});
  bus.update("sensor.ph_mv", state("sensor.ph_mv", "161", "mV", iso(kNoonMs)), kNoonMs, 1000);
  bus.update("sensor.ph_v", state("sensor.ph_v", "0.161", "V", iso(kNoonMs)), kNoonMs, 1000);
  const auto mv = bus.sample("ha.sensor.ph_mv", "measure.ph");
  const auto v = bus.sample("ha.sensor.ph_v", "measure.ph");
  REQUIRE(mv);
  REQUIRE(v);
  CHECK(mv->raw == doctest::Approx(v->raw));
  CHECK(mv->raw == doctest::Approx(7.0 - 161 / 59.16).epsilon(1e-9));
  // The node now reports pH (calibrated there): not the old calibration on a new meaning
  bus.update("sensor.ph_mv", state("sensor.ph_mv", "6.2", "pH", iso(kNoonMs + 1000)), kNoonMs + 1000, 2000);
  CHECK_FALSE(bus.sample("ha.sensor.ph_mv", "measure.ph"));
  CHECK(bus.fault("sensor.ph_mv") == "unit pH is no raw signal");
  bus.update("sensor.ph_mv", state("sensor.ph_mv", "161", "", iso(kNoonMs + 2000)), kNoonMs + 2000, 3000);
  CHECK_FALSE(bus.sample("ha.sensor.ph_mv", "measure.ph"));
  CHECK(bus.fault("sensor.ph_mv") == "unit missing");
  // And a pH calibrated in Home Assistant that turns into a voltage is no pH either
  bus.update("sensor.grow_ph", state("sensor.grow_ph", "161", "mV", iso(kNoonMs)), kNoonMs, 1000);
  CHECK_FALSE(bus.sample("ha.sensor.grow_ph", "measure.ph"));
}

TEST_CASE("Home Assistant: a pH signal goes into the mapping file and comes back from the configuration (SD-041)") {
  std::string err;
  const auto list = ha::parseEntities(json::array({{{"entity", "sensor.ph_mv"}, {"measures", "ph"}, {"signal", true}},
                                                   {{"entity", "sensor.grow_ph"}, {"measures", "ph"}}}),
                                      err);
  REQUIRE(err.empty());
  REQUIRE(list.size() == 2);
  CHECK(list[0].signal);
  CHECK_FALSE(list[1].signal);
  CHECK(ha::entitiesJson(list) == json::array({{{"entity", "sensor.ph_mv"}, {"measures", "ph"}, {"signal", true}},
                                               {{"entity", "sensor.grow_ph"}, {"measures", "ph"}}}));
  // Only pH can be a raw signal so far
  ha::parseEntities(json::array({{{"entity", "sensor.t"}, {"measures", "water_temp"}, {"signal", true}}}), err);
  CHECK_FALSE(err.empty());
  ha::parseEntities(json::array({{{"entity", "sensor.t"}, {"measures", "ph"}, {"signal", "yes"}}}), err);
  CHECK_FALSE(err.empty());
  // A picked signal is read again as one after a restart
  const gc::Catalog cat = ha::catalog();
  ha::HaBus bus;
  gc::MemoryStorage store;
  test::Clock clk;
  {
    gc::Hub hub(cat, bus, store, clk, pseudoRandom);
    hub.boot();
    bus.adopt({"sensor.ph_mv", "ph", true});
    bus.update("sensor.ph_mv", state("sensor.ph_mv", "0", "mV", iso(kNoonMs)), kNoonMs, clk.ms);
    clk.ms += 1000;
    hub.tick();
    REQUIRE(hub.acceptDevice("ha.sensor.ph_mv", "Tank pH").status == 200);
    CHECK(hub.config().device("ha.sensor.ph_mv")->cls == "ha_ph_raw");
  }
  ha::HaBus again;
  gc::Hub hub(cat, again, store, clk, pseudoRandom);
  hub.boot();
  ha::adoptFromConfig(hub, again);
  REQUIRE(again.entities().size() == 1);
  CHECK(again.entities()[0].entityId == "sensor.ph_mv");
  CHECK(again.entities()[0].measures == "ph");
  CHECK(again.entities()[0].signal);
}

TEST_CASE("Home Assistant: an import cannot make a pH calibrated in Home Assistant a raw value, nor give a class a calibration it does not offer (SD-041, RAT-025)") {
  const gc::Catalog cat = ha::catalog();
  ha::HaBus bus({{"sensor.grow_ph", "ph"}});
  gc::MemoryStorage store;
  test::Clock clk;
  gc::Hub hub(cat, bus, store, clk, pseudoRandom);
  hub.boot();
  std::int64_t reportedAt = kNoonMs;
  auto tick = [&] {
    clk.ms += 1000;
    reportedAt = kNoonMs + clk.ms;
    bus.update("sensor.grow_ph", state("sensor.grow_ph", "6.1", "", iso(reportedAt)), kNoonMs + clk.ms, clk.ms);
    hub.tick();
  };
  tick();
  REQUIRE(hub.acceptDevice("ha.sensor.grow_ph", "").status == 200);
  tick();
  REQUIRE(hub.state()["readings"]["tank.ph"]["reason"]["key"] == "truth.external");
  // The import says the same sensor is a raw signal, with a calibration that passes the check on its own
  json cfg = hub.state()["config"].is_null() ? json() : json();
  cfg = gc::json(hub.config());
  for (auto& d : cfg["devices"])
    if (d["id"] == "ha.sensor.grow_ph") d["class"] = "ha_ph_raw";
  cfg["calibrations"]["ha.sensor.grow_ph"]["ph"] = {{"points", json::array({json::array({4.0, 4.0}), json::array({7.0, 7.0})})}, {"at", 0}};
  REQUIRE(hub.importConfig(cfg).status == 200);
  tick();
  json ph = hub.state()["readings"]["tank.ph"];
  CHECK(ph["usable"] == false);  // the bus still reads a pH calibrated in Home Assistant: the classes disagree
  CHECK(ph["value"].is_null());
  CHECK(ph["reason"]["key"] == "truth.class_mismatch");
  // Nor does the app offer its calibration for it
  CHECK(hub.probeCalibration({{"device", "ha.sensor.grow_ph"}, {"kind", "ph"}, {"action", "start"}}).body["error"]["key"] == "probe.not_offered");
  // A calibration left over in an older backup for a device that is not in it is left out, not refused
  cfg = gc::json(hub.config());
  cfg["calibrations"]["head-gone"]["ph"] = {{"points", json::array({json::array({4.0, 4.0}), json::array({7.0, 7.0})})}, {"at", 0}};
  for (auto& d : cfg["devices"])
    if (d["id"] == "ha.sensor.grow_ph") d["class"] = "ha_ph";
  cfg["calibrations"].erase("ha.sensor.grow_ph");
  REQUIRE(hub.importConfig(cfg).status == 200);
  CHECK_FALSE(hub.config().calibrations.count("head-gone"));
  // A calibration for a class that offers none is refused on import
  cfg = gc::json(hub.config());
  for (auto& d : cfg["devices"])
    if (d["id"] == "ha.sensor.grow_ph") d["class"] = "ha_ph";
  cfg["calibrations"]["ha.sensor.grow_ph"]["ph"] = {{"points", json::array({json::array({4.0, 4.0}), json::array({7.0, 7.0})})}, {"at", 0}};
  CHECK(hub.importConfig(cfg).status == 422);
}

TEST_CASE("Home Assistant: an abandoned calibration stops explaining jumps after 30 min but keeps control waiting; a point is retaken, not added (SD-041, RAT-042)") {
  const gc::Catalog cat = ha::catalog();
  ha::HaBus bus({{"sensor.ph_mv", "ph", true}});
  gc::MemoryStorage store;
  test::Clock clk;
  gc::Hub hub(cat, bus, store, clk, pseudoRandom);
  hub.boot();
  double mv = 0;
  auto tick = [&] {
    clk.ms += 1000;
    const double noise = (clk.ms / 1000) % 2 ? 0.1 : 0.0;  // a live probe's raw signal moves a little
    bus.update("sensor.ph_mv", state("sensor.ph_mv", fmt(mv + noise), "mV", iso(kNoonMs + clk.ms)), kNoonMs + clk.ms, clk.ms);
    hub.tick();
  };
  tick();
  REQUIRE(hub.acceptDevice("ha.sensor.ph_mv", "").status == 200);
  json file = gc::json(hub.config());  // calibrated as an ideal probe, so jumps are judged
  file["calibrations"]["ha.sensor.ph_mv"]["ph"] = {{"points", json::array({json::array({7.0, 7.0}), json::array({4.0, 4.0})})}, {"at", 0}};
  REQUIRE(hub.importConfig(file).status == 200);
  auto probe = [&](const std::string& action, double ref = gc::kNaN) {
    json j = {{"device", "ha.sensor.ph_mv"}, {"kind", "ph"}, {"action", action}};
    if (std::isfinite(ref)) j["reference"] = ref;
    return hub.probeCalibration(j);
  };
  auto session = [&] { return hub.state()["probeCalibration"]["ha.sensor.ph_mv:ph"]; };
  REQUIRE(probe("start").status == 200);
  // pH 7 taken twice (the probe was not settled the first time): one point, the later one
  REQUIRE(probe("point", 7.0).status == 200);
  for (int i = 0; i < 3; ++i) tick();  // reports every second; taken once clearly after the request
  mv = 3;
  REQUIRE(probe("point", 7.0).status == 200);
  for (int i = 0; i < 3; ++i) tick();
  REQUIRE(session()["points"].size() == 1);
  CHECK(session()["points"][0][0].get<double>() == doctest::Approx(7.0 - 3 / ha::kNernstMvPerPh).epsilon(0.002 / 7));  // within the 0.1 mV noise
  // A reference outside what a buffer can be is refused
  CHECK(probe("point", 15.0).status == 422);
  CHECK(probe("point", -1.0).status == 422);
  // While it runs, a jump is taken as the calibration
  mv += 2 * ha::kNernstMvPerPh;  // 2 pH at once
  for (int i = 0; i < 5; ++i) tick();
  CHECK(hub.state()["readings"]["tank.ph"]["quality"] == "ok");
  // The window was lost without cancelling (a closed tab): after 30 min without a step the session
  // stays, so pH and EC control keep waiting (the probe may still be in a buffer), but no longer
  // explains jumps once its last 10 min of explanation have run out
  for (int i = 0; i < 29 * 60; ++i) tick();
  CHECK_FALSE(session().contains("idle"));
  for (int i = 0; i < 2 * 60; ++i) tick();
  REQUIRE_FALSE(session().is_null());
  CHECK(session()["idle"] == true);
  for (int i = 0; i < 11 * 60; ++i) tick();
  mv -= 2 * ha::kNernstMvPerPh;
  for (int i = 0; i < 5; ++i) tick();
  CHECK(hub.state()["readings"]["tank.ph"]["quality"] == "jump");
  REQUIRE_FALSE(session().is_null());  // still there until saved or cancelled
  REQUIRE(probe("cancel").status == 200);
  // A session ends with its device too
  REQUIRE(probe("start").status == 200);
  REQUIRE(hub.removeDevice("ha.sensor.ph_mv").status == 200);
  CHECK(hub.state()["probeCalibration"].empty());
}

TEST_CASE("Home Assistant: which voltages are suggested for pH, and which are not passed on at all (SD-041)") {
  ha::HaBus bus;
  bus.updateAll(json::array({
                    described(state("sensor.probe_a", "12", "mV", iso(kNoonMs)), "", "pH Sonde"),            // the name says pH
                    described(state("sensor.tank_ph1", "0.4", "V", iso(kNoonMs)), "voltage", "Tank 1"),      // ph1
                    described(state("sensor.ph_orp", "250", "mV", iso(kNoonMs)), "voltage", "pH ORP Board"),  // a redox probe
                    described(state("sensor.ph_battery", "3.7", "V", iso(kNoonMs)), "battery", "pH Battery"), // not a voltage class
                    described(state("sensor.ph_mains", "231", "V", iso(kNoonMs)), "voltage", "Phase pH"),    // above 5 V
                    described(state("sensor.ups_voltage", "230", "V", iso(kNoonMs)), "voltage", "USV"),
                    described(state("sensor.esp_vcc", "3.3", "V", iso(kNoonMs)), "voltage", "ESP Versorgung"),
                    described(state("sensor.probe_b", "unknown", "mV", iso(kNoonMs)), "ph", "Sonde B"),     // no value yet: still a signal
                    described(state("sensor.probe_c", "6000", "mV", iso(kNoonMs)), "ph", "Sonde C"),        // 6 V: neither signal nor pH
                }),
                kNoonMs, 1000);
  std::set<std::string> suggested, others;
  for (const auto& c : bus.candidates()) suggested.insert(c.entityId);
  for (const auto& c : bus.others()) others.insert(c.entityId);
  CHECK(suggested == std::set<std::string>{"sensor.probe_a", "sensor.tank_ph1", "sensor.probe_b"});
  for (const auto& c : bus.candidates())
    if (c.entityId == "sensor.probe_b") CHECK(c.signal);
  CHECK_FALSE(others.count("sensor.probe_c"));
  CHECK(others.count("sensor.ph_orp"));      // can still be picked
  CHECK(others.count("sensor.esp_vcc"));     // 3.3 V could be a front end
  CHECK_FALSE(others.count("sensor.ups_voltage"));  // 230 V is never a pH signal and is not passed on
  CHECK_FALSE(others.count("sensor.ph_mains"));
  // An other voltage picked for pH is a signal too
  gc::Msg why;
  REQUIRE(bus.select("sensor.esp_vcc", "ph", why));
  CHECK(bus.selected("sensor.esp_vcc")->signal);
  CHECK(bus.devices()[0].cls == "ha_ph_raw");
}

TEST_CASE("Home Assistant: a calibration point waits as long as the sensor's own limit, takes no report from before the request, and fails when the sensor goes away (SD-041, RAT-006)") {
  const gc::Catalog cat = ha::catalog();
  ha::HaBus bus({{"sensor.ph_mv", "ph", true}});
  gc::MemoryStorage store;
  test::Clock clk;
  gc::Hub hub(cat, bus, store, clk, pseudoRandom);
  hub.boot();
  double mv = 0;
  std::int64_t reportedAt = kNoonMs;
  std::int64_t haLag = 0;  // Home Assistant's Date header is in whole seconds: its time can read up to 999 ms early
  auto poll = [&] {
    bus.update("sensor.ph_mv", state("sensor.ph_mv", fmt(mv), "mV", iso(reportedAt)), kNoonMs + clk.ms - haLag, clk.ms);
  };
  auto run = [&](int seconds, int reportEvery) {
    for (int i = 0; i < seconds; ++i) {
      clk.ms += 1000;
      if (reportEvery > 0 && (clk.ms / 1000) % reportEvery == 0) {
        mv += 0.3;
        reportedAt = kNoonMs + clk.ms;
      }
      poll();
      hub.tick();
    }
  };
  run(1, 0);
  REQUIRE(hub.acceptDevice("ha.sensor.ph_mv", "").status == 200);
  run(10 * 60, 120);  // learns: a report every 2 min, so its limit is 6 min
  auto probe = [&](const std::string& action, double ref = gc::kNaN) {
    json j = {{"device", "ha.sensor.ph_mv"}, {"kind", "ph"}, {"action", action}};
    if (std::isfinite(ref)) j["reference"] = ref;
    return hub.probeCalibration(j);
  };
  auto session = [&] { return hub.state()["probeCalibration"]["ha.sensor.ph_mv:ph"]; };
  REQUIRE(probe("start").status == 200);
  // No new report: still waiting just before the limit, failed just after it
  REQUIRE(probe("point", 7.0).status == 200);
  run(6 * 60 - 5, 0);
  CHECK(session().contains("pending"));
  run(10, 0);
  CHECK_FALSE(session().contains("pending"));
  CHECK(session()["failed"]["key"] == "probe.no_raw");
  // A report made half a second before the request, first seen after it, is not taken, also
  // when Home Assistant's clock in the answer reads late
  run(30, 0);
  haLag = 999;
  REQUIRE(probe("point", 7.0).status == 200);
  reportedAt = kNoonMs + clk.ms - 500;
  run(3, 0);
  CHECK(session()["points"].empty());
  CHECK(session().contains("pending"));
  // A commit while a point waits is refused; the waiting point stays
  const auto early = probe("commit");
  CHECK(early.status == 409);
  CHECK(early.body["error"]["key"] == "probe.pending");
  CHECK(session().contains("pending"));
  // The sensor goes away during the wait (Home Assistant restarting): the point fails at once,
  // so a value restored after the restart cannot become it
  bus.lostAll();
  clk.ms += 1000;
  hub.tick();
  CHECK_FALSE(session().contains("pending"));
  CHECK(session()["failed"]["key"] == "probe.no_raw");
  // Cancelled while waiting: no session, and a later report adds nothing
  run(5, 0);
  REQUIRE(probe("point", 7.0).status == 200);
  REQUIRE(probe("cancel").status == 200);
  reportedAt = kNoonMs + clk.ms + 5000;
  run(10, 0);
  CHECK(session().is_null());
}

TEST_CASE("Home Assistant: a raw pH from Home Assistant gets the standstill check by its reports (SD-041, RAT-023, RAT-059)") {
  const gc::Catalog cat = ha::catalog();
  ha::HaBus bus({{"sensor.ph_mv", "ph", true}});
  gc::MemoryStorage store;
  test::Clock clk;
  gc::Hub hub(cat, bus, store, clk, pseudoRandom);
  hub.boot();
  double mv = 50;
  std::int64_t reportedAt = kNoonMs;
  auto run = [&](int seconds, int reportEvery, double change) {
    for (int i = 0; i < seconds; ++i) {
      clk.ms += 1000;
      if (reportEvery > 0 && (clk.ms / 1000) % reportEvery == 0) {
        mv += change;
        reportedAt = kNoonMs + clk.ms;
      }
      bus.update("sensor.ph_mv", state("sensor.ph_mv", fmt(mv), "mV", iso(reportedAt)), kNoonMs + clk.ms, clk.ms);
      hub.tick();
    }
  };
  run(1, 0, 0);
  REQUIRE(hub.acceptDevice("ha.sensor.ph_mv", "").status == 200);
  // Calibrated as an ideal probe, through the import a backup would take
  json file = gc::json(hub.config());
  file["calibrations"]["ha.sensor.ph_mv"]["ph"] = {{"points", json::array({json::array({7.0, 7.0}), json::array({4.0, 4.0})})}, {"at", 0}};
  REQUIRE(hub.importConfig(file).status == 200);
  // A probe that reports every 20 min, a little different each time, is not frozen between reports:
  // 19 min after its last report, where the hub's own clock would already say frozen
  run(90 * 60, 20 * 60, 0.4);
  run(9 * 60, 0, 0.0);
  CHECK(hub.state()["readings"]["tank.ph"]["quality"] == "ok");
  // One that keeps reporting the same voltage every minute is frozen once 15 min of its reports are the same
  run(60, 60, 0.4);  // one more change
  run(14 * 60, 60, 0.0);
  CHECK(hub.state()["readings"]["tank.ph"]["quality"] == "ok");
  run(2 * 60, 60, 0.0);
  CHECK(hub.state()["readings"]["tank.ph"]["quality"] == "frozen");
}

TEST_CASE("Home Assistant: a refused pH calibration says what is wrong, in mV per pH step (SD-041, RAT-025)") {
  const gc::Catalog cat = ha::catalog();
  ha::HaBus bus({{"sensor.ph_mv", "ph", true}});
  gc::MemoryStorage store;
  test::Clock clk;
  gc::Hub hub(cat, bus, store, clk, pseudoRandom);
  hub.boot();
  double mv = 0;
  auto tick = [&] {
    clk.ms += 1000;
    bus.update("sensor.ph_mv", state("sensor.ph_mv", fmt(mv), "mV", iso(kNoonMs + clk.ms)), kNoonMs + clk.ms, clk.ms);
    hub.tick();
  };
  tick();
  REQUIRE(hub.acceptDevice("ha.sensor.ph_mv", "").status == 200);
  auto calibrate = [&](double ph7Mv, double ph4Mv) {
    auto probe = [&](const std::string& action, double ref = gc::kNaN) {
      json j = {{"device", "ha.sensor.ph_mv"}, {"kind", "ph"}, {"action", action}};
      if (std::isfinite(ref)) j["reference"] = ref;
      return hub.probeCalibration(j);
    };
    REQUIRE(probe("start").status == 200);
    for (auto [ref, v] : {std::pair{7.0, ph7Mv}, std::pair{4.0, ph4Mv}}) {
      REQUIRE(probe("point", ref).status == 200);
      mv = v;
      for (int i = 0; i < 4; ++i) tick();
    }
    return probe("commit");
  };
  auto swapped = calibrate(177.5, 0);
  CHECK(swapped.body["error"]["key"] == "probe.swapped");
  auto amplified = calibrate(1500, 2032.44);  // a board with gain 3
  CHECK(amplified.body["error"]["key"] == "probe.too_steep");
  CHECK(amplified.body["error"]["args"]["mv"].get<double>() == doctest::Approx(177.5).epsilon(0.001));
  auto worn = calibrate(0, 3 * 40);
  CHECK(worn.body["error"]["key"] == "probe.too_flat");
  CHECK(worn.body["error"]["args"]["mv"].get<double>() == doctest::Approx(40).epsilon(0.001));
  auto still = calibrate(0, 3);  // 3 mV between pH 7 and pH 4: not the probe's signal, or not in the buffer
  CHECK(still.body["error"]["key"] == "probe.no_move");
}

TEST_CASE("Home Assistant: the configuration decides whether a sensor is read as a signal, over the mapping file (SD-041)") {
  const gc::Catalog cat = ha::catalog();
  gc::MemoryStorage store;
  test::Clock clk;
  {
    ha::HaBus bus;
    gc::Hub hub(cat, bus, store, clk, pseudoRandom);
    hub.boot();
    bus.adopt({"sensor.ph_mv", "ph", true});
    bus.update("sensor.ph_mv", state("sensor.ph_mv", "0", "mV", iso(kNoonMs)), kNoonMs, clk.ms);
    clk.ms += 1000;
    hub.tick();
    REQUIRE(hub.acceptDevice("ha.sensor.ph_mv", "").status == 200);
  }
  // The mapping file lists the pH sensor as no signal: the configuration wins
  ha::HaBus bus({{"sensor.ph_mv", "ph", false}});
  gc::Hub hub(cat, bus, store, clk, pseudoRandom);
  hub.boot();
  ha::adoptFromConfig(hub, bus);
  REQUIRE(bus.selected("sensor.ph_mv"));
  CHECK(bus.selected("sensor.ph_mv")->signal);
}

TEST_CASE("Home Assistant: picked from the list as a raw value, an entity the mapping file lists otherwise is read as picked (SD-041)") {
  const gc::Catalog cat = ha::catalog();
  ha::HaBus bus({{"sensor.tank_ph_mv", "ph", false}});  // the file says: a pH calibrated in Home Assistant
  gc::MemoryStorage store;
  test::Clock clk;
  gc::Hub hub(cat, bus, store, clk, pseudoRandom);
  hub.boot();
  const json states = json::array({described(state("sensor.tank_ph_mv", "-10", "mV", iso(kNoonMs)), "voltage", "Tank pH Spannung")});
  auto tick = [&] {
    clk.ms += 1000;
    bus.updateAll(states, kNoonMs + clk.ms, clk.ms);
    hub.tick();
  };
  tick();
  REQUIRE(ha::assign(hub, bus, "tank.ph", "sensor.tank_ph_mv", tick).status == 200);
  REQUIRE(hub.config().device("ha.sensor.tank_ph_mv"));
  CHECK(hub.config().device("ha.sensor.tank_ph_mv")->cls == "ha_ph_raw");
  CHECK(bus.selected("sensor.tank_ph_mv")->signal);
}

// ---- Sign of life (SD-042) ----

namespace {

// A sign-of-life text as an ESPHome device sends it, reported at reportedMs
json lifeText(const std::string& text, std::int64_t reportedMs, const std::string& entity = "sensor.hub_life") {
  return {{"entity_id", entity},
          {"state", text},
          {"attributes", {{"friendly_name", "Hub sign of life"}}},
          {"last_updated", iso(reportedMs)},
          {"last_reported", iso(reportedMs)}};
}

// A timestamp sensor that states when a sensor last read
json lifeStamp(const std::string& stamp, const std::string& entity = "sensor.water_read") {
  return {{"entity_id", entity},
          {"state", stamp},
          {"attributes", {{"device_class", "timestamp"}, {"friendly_name", "Water temperature read"}}},
          {"last_updated", "2026-10-09T10:00:00+00:00"}};
}

// A Home Assistant with a calm water temperature (its value reported once,
// at noon, then dropped by Home Assistant as unchanged) and a pH, and a hub
// that reads them; the test sets what Home Assistant answers each second.
struct LifeRig {
  gc::Catalog cat = ha::catalog();
  ha::HaBus bus{{{"sensor.water", "water_temp"}, {"sensor.ph_mv", "ph", true}}};
  gc::MemoryStorage store;
  test::Clock clk;
  gc::Hub hub{cat, bus, store, clk, pseudoRandom};
  std::function<json()> extra = [] { return json::array(); };  // the sign of life, as the test wants it
  double mv = 10;
  bool phGone = false;  // Home Assistant has the pH as "unavailable" this round
  std::string water = "21.5";
  std::int64_t waterAt = kNoonMs, phAt = kNoonMs;  // when Home Assistant last got each value
  LifeRig() {
    clk.ms = 100000;  // Home Assistant has run for a while: its values date from before the hub started
    hub.boot();
    step(1);
    REQUIRE(hub.acceptDevice("ha.sensor.water", "").status == 200);
    REQUIRE(hub.acceptDevice("ha.sensor.ph_mv", "").status == 200);
  }
  json states() {
    json all = json::array({state("sensor.water", water, "°C", iso(waterAt)), state("sensor.ph_mv", phGone ? "unavailable" : std::to_string(mv), "mV", iso(phAt))});
    for (const auto& s : extra()) all.push_back(s);
    return all;
  }
  void step(int seconds) {
    for (int i = 0; i < seconds; ++i) {
      clk.ms += 1000;
      bus.updateAll(states(), kNoonMs + clk.ms, clk.ms);
      std::lock_guard<std::recursive_mutex> l(hub.mutex());
      ha::syncLife(hub, bus);
      hub.tick();
    }
  }
  json reading(const std::string& role = "tank.water_temp") { return hub.state()["readings"][role]; }
};

}  // namespace

TEST_CASE("Home Assistant: a sign-of-life text is read only in its own format (SD-042)") {
  auto ok = ha::parseLifeText("n=4711|wt=2|ph=-|s_1=0");
  REQUIRE(ok);
  REQUIRE(ok->size() == 3);
  CHECK((*ok)[0] == std::make_pair(std::string("wt"), std::optional<int>(2)));
  CHECK_FALSE((*ok)[1].second.has_value());  // "-": no measurement since the device started
  CHECK((*ok)[2].second == 0);
  for (const char* bad : {"", "wt=2", "n=1", "n=x|wt=2", "n=1|wt=2|wt=3", "n=1|WT=2", "n=1|wt=-1", "n=1|wt=2.5", "n=1|wt=", "n=1|wt",
                          "n=1|n=2", "n=1|wt=2|", "n=1|a_very_long_channel_name=2", "n=1|wt=99999999"})
    CHECK_MESSAGE(!ha::parseLifeText(bad), bad);
  std::string many = "n=1";
  for (int i = 0; i < 65; ++i) many += "|c" + std::to_string(i) + "=1";
  CHECK_FALSE(ha::parseLifeText(many));
  CHECK(ha::validLifeChannel("wt"));
  CHECK_FALSE(ha::validLifeChannel(""));
  CHECK_FALSE(ha::validLifeChannel("w.t"));
}

TEST_CASE("Home Assistant: with a sign of life a calm sensor is fresh by its last measurement, stale 60 s after it (SD-042, RAT-027)") {
  LifeRig rig;
  // The device measures the water temperature every 5 s and sends its sign of life every 15 s
  std::int64_t measuredMs = kNoonMs, textMs = kNoonMs;
  int counter = 0;
  bool measuring = true, sending = true;
  rig.extra = [&] {
    const std::int64_t haNow = kNoonMs + rig.clk.ms;
    if (measuring && haNow - measuredMs >= 5000) measuredMs = haNow;
    if (sending && haNow - textMs >= 15000) {
      textMs = haNow;
      ++counter;
    }
    const std::int64_t age = (textMs - measuredMs) / 1000;
    return json::array({lifeText("n=" + std::to_string(counter) + "|wt=" + std::to_string(std::max<std::int64_t>(0, age)) + "|ph=1", textMs)});
  };
  rig.step(20);
  // Without a sign of life the age counts from the last change (SD-035)
  CHECK(rig.reading()["ageOf"] == "change");
  // The sign of life is listed with its channels
  const json listed = rig.bus.candidatesJson(false, true);
  REQUIRE(listed["lives"].size() == 1);
  CHECK(listed["lives"][0]["entity"] == "sensor.hub_life");
  CHECK(listed["lives"][0]["kind"] == "text");
  CHECK(listed["lives"][0]["channels"][0]["code"] == "wt");
  REQUIRE(ha::setLife(rig.hub, rig.bus, "tank.water_temp", "sensor.hub_life", "wt").status == 200);
  rig.step(1);
  // Unchanged for 20 min in Home Assistant, but measured: valid, and its age is the measurement's
  rig.step(20 * 60);
  json r = rig.reading();
  CHECK(r["quality"] == "ok");
  CHECK(r["ageOf"] == "measurement");
  CHECK(r["ageMs"].get<std::int64_t>() <= 21000);
  const json picks = rig.bus.candidatesJson(true);  // kept: a loop over a temporary's part would dangle
  for (const auto& c : picks["candidates"]) CHECK(c["entity"] != "sensor.hub_life");  // a sign of life is no sensor to pick
  for (const auto& c : picks["others"]) CHECK(c["entity"] != "sensor.hub_life");
  // The channel falls silent while the text goes on: valid up to 60 s after its last measurement, then stale
  measuring = false;
  const std::int64_t last = measuredMs;
  while (kNoonMs + rig.clk.ms - last < 60000) {
    rig.step(1);
    // a new text may date the measurement only to whole seconds
    if (kNoonMs + rig.clk.ms - last <= 59000) CHECK(rig.reading()["quality"] == "ok");
  }
  rig.step(16);  // the next text shows it
  r = rig.reading();
  CHECK(r["quality"] == "stale");
  CHECK(r["reason"]["key"] == "truth.life_stale");
  CHECK(r["reason"]["args"]["limitS"] == 60);
  CHECK_FALSE(r["usable"].get<bool>());
  // Measuring again: valid at once
  measuring = true;
  rig.step(20);
  CHECK(rig.reading()["quality"] == "ok");
  // The device sends nothing at all: every sensor with this sign of life says so, by one cause
  REQUIRE(ha::setLife(rig.hub, rig.bus, "tank.ph", "sensor.hub_life", "ph").status == 200);
  rig.step(1);
  sending = false;
  rig.step(62);
  CHECK(rig.reading()["reason"]["key"] == "truth.life_silent");
  CHECK(rig.reading("tank.ph")["reason"]["key"] == "truth.life_silent");
  CHECK(rig.reading("tank.ph")["quality"] == "stale");
  // Without the sign of life again, the learned limit applies and nothing is learned from before
  sending = true;
  REQUIRE(ha::setLife(rig.hub, rig.bus, "tank.water_temp", "", "").status == 200);
  rig.step(1);
  CHECK(rig.reading()["ageOf"] == "change");
  CHECK(rig.hub.config().device("ha.sensor.water")->life.empty());
}

TEST_CASE("Home Assistant: the sign of life adds the text's age; 60 s is still valid, above is stale (SD-042)") {
  LifeRig rig;
  std::string text = "n=1|wt=40";
  const std::int64_t textMs = kNoonMs + rig.clk.ms + 1000;
  rig.extra = [&] { return json::array({lifeText(text, textMs)}); };
  rig.step(2);  // the text arrives with the first step, now is a second later
  REQUIRE(ha::setLife(rig.hub, rig.bus, "tank.water_temp", "sensor.hub_life", "wt").status == 200);
  // 40 s at the text, 20 s since: 60 s
  rig.step(19);
  json r = rig.reading();
  CHECK(r["ageMs"].get<std::int64_t>() == 60000);
  CHECK(r["quality"] == "ok");
  rig.step(1);
  CHECK(rig.reading()["quality"] == "stale");
  CHECK(rig.reading()["reason"]["key"] == "truth.life_stale");
}

TEST_CASE("Home Assistant: a sign of life that cannot say when the sensor measured makes it stale, never looser (SD-042, RAT-006)") {
  LifeRig rig;
  std::string text = "n=1|wt=3";
  std::string life = "ok";
  rig.extra = [&] {
    const std::int64_t now = kNoonMs + rig.clk.ms;
    if (life == "gone") return json::array();
    json t = lifeText(text, now);
    if (life == "unavailable") t["state"] = "unavailable";
    return json::array({t});
  };
  rig.step(2);
  REQUIRE(ha::setLife(rig.hub, rig.bus, "tank.water_temp", "sensor.hub_life", "wt").status == 200);
  rig.step(2);
  REQUIRE(rig.reading()["quality"] == "ok");
  auto reason = [&] {
    rig.step(2);
    return rig.reading()["reason"]["key"].get<std::string>();
  };
  text = "n=2|ph=3";  // the channel is missing
  CHECK(reason() == "truth.life_no_channel");
  CHECK(rig.reading()["reason"]["args"]["channel"] == "wt");
  CHECK(rig.reading()["quality"] == "stale");
  text = "this is no sign of life";
  CHECK(reason() == "truth.life_unreadable");
  text = "n=3|wt=3";
  life = "unavailable";
  CHECK(reason() == "truth.life_unavailable");
  life = "gone";  // Home Assistant no longer lists it
  CHECK(reason() == "truth.life_unavailable");
  CHECK_FALSE(rig.reading()["usable"].get<bool>());
  life = "ok";
  CHECK(reason().rfind("truth.life", 0) != 0);
  CHECK(rig.reading()["quality"] == "ok");
}

TEST_CASE("Home Assistant: a channel without a measurement since the device started has no value, and is stale after 60 s (SD-042)") {
  LifeRig rig;
  std::string wt = "3";
  int counter = 0;
  rig.extra = [&] { return json::array({lifeText("n=" + std::to_string(++counter) + "|wt=" + wt, kNoonMs + rig.clk.ms)}); };
  rig.step(2);
  REQUIRE(ha::setLife(rig.hub, rig.bus, "tank.water_temp", "sensor.hub_life", "wt").status == 200);
  rig.step(2);
  REQUIRE(rig.reading()["quality"] == "ok");
  // The device restarted: Home Assistant still holds the old value, which is no value now
  wt = "-";
  rig.step(30);
  json r = rig.reading();
  CHECK(r["quality"] == "no_data");
  CHECK(r["reason"]["key"] == "truth.life_waiting");
  CHECK(r["value"].is_null());
  rig.step(32);
  r = rig.reading();
  CHECK(r["quality"] == "stale");
  CHECK(r["reason"]["key"] == "truth.life_never");
  wt = "1";
  rig.step(2);
  CHECK(rig.reading()["quality"] == "ok");
}

TEST_CASE("Home Assistant: a timestamp sensor gives the last measurement; a time ahead of Home Assistant's is refused (SD-042)") {
  LifeRig rig;
  std::int64_t readMs = kNoonMs;
  std::string stamp;
  rig.extra = [&] { return json::array({lifeStamp(stamp.empty() ? iso(readMs) : stamp)}); };
  rig.step(30);
  const json listed = rig.bus.candidatesJson(false, true);
  REQUIRE(listed["lives"].size() == 1);
  CHECK(listed["lives"][0]["kind"] == "timestamp");
  // A timestamp sensor has no channel
  CHECK(ha::setLife(rig.hub, rig.bus, "tank.water_temp", "sensor.water_read", "wt").status == 422);
  REQUIRE(ha::setLife(rig.hub, rig.bus, "tank.water_temp", "sensor.water_read", "").status == 200);
  readMs = kNoonMs + rig.clk.ms - 20000;
  rig.step(1);
  json r = rig.reading();
  CHECK(r["quality"] == "ok");
  CHECK(r["ageOf"] == "measurement");
  CHECK(r["ageMs"].get<std::int64_t>() >= 20000);
  CHECK(r["ageMs"].get<std::int64_t>() <= 21000);
  rig.step(41);
  CHECK(rig.reading()["quality"] == "stale");
  // Up to 2 s ahead is rounding and counts as now; more is no last measurement
  readMs = kNoonMs + rig.clk.ms + 2000;
  rig.step(1);
  CHECK(rig.reading()["quality"] == "ok");
  readMs = kNoonMs + rig.clk.ms + 5000;
  rig.step(1);
  CHECK(rig.reading()["reason"]["key"] == "truth.life_future");
  stamp = "unknown";
  rig.step(1);
  CHECK(rig.reading()["reason"]["key"] == "truth.life_unavailable");
  stamp = "yesterday";
  rig.step(1);
  CHECK(rig.reading()["reason"]["key"] == "truth.life_unreadable");
}

TEST_CASE("Home Assistant: a sign of life is set only from what Home Assistant offers, stored in the configuration and checked on import (SD-042, R7)") {
  LifeRig rig;
  rig.extra = [&] { return json::array({lifeText("n=1|wt=3|ph=2", kNoonMs + rig.clk.ms), lifeStamp(iso(kNoonMs))}); };
  rig.step(2);
  using ha::setLife;
  CHECK(setLife(rig.hub, rig.bus, "tank.water_temp", "sensor.not_listed", "wt").body["error"]["key"] == "ha.life_unknown");
  CHECK(setLife(rig.hub, rig.bus, "tank.water_temp", "sensor.hub_life", "xx").body["error"]["key"] == "ha.life_channel");
  CHECK(setLife(rig.hub, rig.bus, "tank.water_temp", "sensor.hub_life", "").body["error"]["key"] == "ha.life_channel");
  CHECK(setLife(rig.hub, rig.bus, "tank.water_temp", "sensor.water", "").body["error"]["key"] == "ha.life_unknown");  // a sensor is no sign of life
  CHECK(setLife(rig.hub, rig.bus, "zone.co2", "sensor.hub_life", "wt").body["error"]["key"] == "ha.life_role");  // no sensor from Home Assistant there
  CHECK(setLife(rig.hub, rig.bus, "tank.level_low", "sensor.hub_life", "wt").status == 422);
  REQUIRE(setLife(rig.hub, rig.bus, "tank.water_temp", "sensor.hub_life", "wt").status == 200);
  // Stored with the device and in a backup
  const gc::DeviceCfg* d = rig.hub.config().device("ha.sensor.water");
  REQUIRE(d);
  CHECK(d->life == "sensor.hub_life");
  CHECK(d->lifeChannel == "wt");
  json file = gc::json(rig.hub.config());
  bool found = false;
  for (const auto& dj : file["devices"])
    if (dj["id"] == "ha.sensor.water") found = dj["life"] == json{{"source", "sensor.hub_life"}, {"channel", "wt"}};
  CHECK(found);
  // An import changes it, and the bus reads the new one with the next tick
  for (auto& dj : file["devices"])
    if (dj["id"] == "ha.sensor.water") dj["life"]["channel"] = "ph";
  REQUIRE(rig.hub.importConfig(file).status == 200);
  rig.step(1);
  CHECK(rig.bus.selected("sensor.water")->lifeChannel == "ph");
  // Only a sensor on its own schedule has a sign of life, and only in the platform's own form
  for (auto [id, life] : {std::pair<std::string, json>{"ha.sensor.water", {{"source", "sensor.Hub"}, {"channel", "wt"}}},
                          {"ha.sensor.water", {{"source", ""}, {"channel", "wt"}}},
                          {"ha.sensor.water", {{"source", "sensor.hub_life"}, {"channel", "w.t"}}}}) {
    json bad = file;
    for (auto& dj : bad["devices"])
      if (dj["id"] == id) dj["life"] = life;
    CHECK(rig.hub.importConfig(bad).status == 422);
  }
  gc::Config c = rig.hub.config();
  c.devices.push_back({"HEAD-1", "head_ph_ec", "pH/EC", "sensor.hub_life", "wt"});
  bool refused = false;
  for (const auto& e : gc::validateConfig(c, rig.cat)) refused = refused || e.key == "cfg.device.life";
  CHECK(refused);
}

TEST_CASE("Home Assistant: with a sign of life a stuck pH signal is frozen by its measurements, and a calibration point takes a new measurement (SD-042, RAT-023)") {
  LifeRig rig;
  int counter = 0;
  rig.extra = [&] { return json::array({lifeText("n=" + std::to_string(++counter) + "|ph=1|wt=1", kNoonMs + rig.clk.ms)}); };
  rig.step(2);
  REQUIRE(ha::setLife(rig.hub, rig.bus, "tank.ph", "sensor.hub_life", "ph").status == 200);
  // Calibrated as an ideal probe
  json file = gc::json(rig.hub.config());
  file["calibrations"]["ha.sensor.ph_mv"]["ph"] = {{"points", json::array({json::array({7.0, 7.0}), json::array({4.0, 4.0})})}, {"at", 0}};
  REQUIRE(rig.hub.importConfig(file).status == 200);
  rig.step(2);
  REQUIRE(rig.reading("tank.ph")["quality"] == "ok");
  // Home Assistant drops the unchanged reports of a stuck probe; the sign of life shows it still measures the same
  rig.step(14 * 60);
  CHECK(rig.reading("tank.ph")["quality"] == "ok");
  rig.step(2 * 60);
  CHECK(rig.reading("tank.ph")["quality"] == "frozen");
}

TEST_CASE("Home Assistant: with a sign of life a stuck pH stays frozen through short drop-outs; only switching the sign of life starts its clock over (SD-042, RAT-023)") {
  LifeRig rig;
  int counter = 0;
  rig.extra = [&] { return json::array({lifeText("n=" + std::to_string(++counter) + "|ph=1|wt=1", kNoonMs + rig.clk.ms)}); };
  rig.step(2);
  REQUIRE(ha::setLife(rig.hub, rig.bus, "tank.ph", "sensor.hub_life", "ph").status == 200);
  json file = gc::json(rig.hub.config());
  file["calibrations"]["ha.sensor.ph_mv"]["ph"] = {{"points", json::array({json::array({7.0, 7.0}), json::array({4.0, 4.0})})}, {"at", 0}};
  REQUIRE(rig.hub.importConfig(file).status == 200);
  rig.step(2);
  REQUIRE(rig.reading("tank.ph")["quality"] == "ok");
  // A flaky network: the pH is "unavailable" for one round after 5 min, and a whole poll is lost after 10 min
  rig.step(5 * 60);
  rig.phGone = true;
  rig.step(1);
  CHECK(rig.reading("tank.ph")["quality"] == "offline");
  rig.phGone = false;
  rig.step(5 * 60);
  rig.bus.lostAll();
  {
    std::lock_guard<std::recursive_mutex> l(rig.hub.mutex());
    rig.hub.tick();
  }
  CHECK(rig.reading("tank.ph")["quality"] == "offline");
  // The value has stood still for 16 min of measurements: frozen, the drop-outs started nothing over
  rig.step(6 * 60);
  CHECK(rig.reading("tank.ph")["quality"] == "frozen");
  // Switching the sign of life off changes the clock: it starts over
  REQUIRE(ha::setLife(rig.hub, rig.bus, "tank.ph", "", "").status == 200);
  rig.step(2);
  CHECK(rig.reading("tank.ph")["quality"] != "frozen");
}

TEST_CASE("Home Assistant: with a sign of life a calibration point takes the first measurement after the request and fails when the device falls silent (SD-042, RAT-006)") {
  LifeRig rig;
  int counter = 0;
  bool sending = true;
  std::int64_t textMs = kNoonMs;
  rig.extra = [&] {
    if (sending) {
      textMs = kNoonMs + rig.clk.ms;
      ++counter;
    }
    return json::array({lifeText("n=" + std::to_string(counter) + "|ph=0", textMs)});
  };
  rig.step(2);
  REQUIRE(ha::setLife(rig.hub, rig.bus, "tank.ph", "sensor.hub_life", "ph").status == 200);
  rig.step(2);
  auto probe = [&](const std::string& action, double ref = gc::kNaN) {
    json j = {{"device", "ha.sensor.ph_mv"}, {"kind", "ph"}, {"action", action}};
    if (std::isfinite(ref)) j["reference"] = ref;
    return rig.hub.probeCalibration(j);
  };
  auto session = [&] { return rig.hub.state()["probeCalibration"]["ha.sensor.ph_mv:ph"]; };
  REQUIRE(probe("start").status == 200);
  REQUIRE(probe("point", 7.0).status == 200);
  // No new report in Home Assistant (the voltage did not change), but a measurement after the request
  rig.step(4);
  CHECK_FALSE(session().contains("pending"));
  CHECK(session()["points"].size() == 1);
  // The device falls silent during the next point: it fails, it does not wait the learned limit
  REQUIRE(probe("point", 4.0).status == 200);
  sending = false;
  rig.step(62);
  CHECK_FALSE(session().contains("pending"));
  CHECK(session()["failed"]["key"] == "probe.no_raw");
}

TEST_CASE("Home Assistant: a sign of life lives in the hub's configuration, not in the mapping file (SD-042)") {
  std::string err;
  auto list = ha::parseEntities(json::array({{{"entity", "sensor.water"}, {"measures", "water_temp"}, {"life", {{"entity", "sensor.hub_life"}, {"channel", "wt"}}}}}), err);
  REQUIRE(err.empty());
  REQUIRE(list.size() == 1);
  CHECK(list[0].life.empty());  // set in the web app, where Home Assistant's offer is checked
  list[0].life = "sensor.hub_life";
  CHECK_FALSE(ha::entitiesJson(list)[0].contains("life"));
}

TEST_CASE("Home Assistant: a sign of life that dates the sensor's measurement before the sensor's own report does not fit it (SD-042)") {
  LifeRig rig;
  // A channel of another device: it measured 40 s ago, the sensor reported a new value 5 s ago
  int counter = 0;
  rig.extra = [&] { return json::array({lifeText("n=" + std::to_string(++counter) + "|wt=40|zz=1", kNoonMs + rig.clk.ms)}); };
  rig.step(2);
  REQUIRE(ha::setLife(rig.hub, rig.bus, "tank.water_temp", "sensor.hub_life", "wt").status == 200);
  rig.water = "22.0";
  rig.waterAt = kNoonMs + rig.clk.ms + 1000;  // reported now
  rig.step(5);
  json r = rig.reading();
  CHECK(r["quality"] == "stale");
  CHECK(r["reason"]["key"] == "truth.life_mismatch");
  // The right channel measured with that report: it fits
  REQUIRE(ha::setLife(rig.hub, rig.bus, "tank.water_temp", "sensor.hub_life", "zz").status == 200);
  rig.step(1);
  CHECK(rig.reading()["quality"] == "ok");
}

TEST_CASE("Home Assistant: the same sign-of-life text dated again is no new send; a channel waits only while it shows \"-\" in a row (SD-042)") {
  LifeRig rig;
  std::string text = "n=7|wt=1";
  std::int64_t sentMs = kNoonMs + rig.clk.ms + 1000;
  rig.extra = [&] { return json::array({lifeText(text, sentMs)}); };
  rig.step(2);
  REQUIRE(ha::setLife(rig.hub, rig.bus, "tank.water_temp", "sensor.hub_life", "wt").status == 200);
  // Home Assistant dates the same text again (restored, retained, a state dump on reconnect): it keeps its first time
  for (int i = 0; i < 70; ++i) {
    sentMs = kNoonMs + rig.clk.ms + 1000;
    rig.step(1);
  }
  CHECK(rig.reading()["reason"]["key"] == "truth.life_silent");
  // Also after a round with the text "unavailable", and after a lost poll
  text = "unavailable";
  rig.step(1);
  text = "n=7|wt=1";
  sentMs = kNoonMs + rig.clk.ms + 1000;
  rig.step(1);
  CHECK(rig.reading()["reason"]["key"] == "truth.life_silent");
  rig.bus.lostAll();
  sentMs = kNoonMs + rig.clk.ms + 1000;
  rig.step(1);
  CHECK(rig.reading()["reason"]["key"] == "truth.life_silent");
  // "-" waits in a row: a channel that leaves the text and comes back as "-" starts over
  int n = 10;
  auto send = [&](const std::string& rest, int seconds) {
    for (int i = 0; i < seconds; ++i) {
      text = "n=" + std::to_string(++n) + rest;
      sentMs = kNoonMs + rig.clk.ms + 1000;
      rig.step(1);
    }
  };
  send("|wt=-", 40);
  CHECK(rig.reading()["reason"]["key"] == "truth.life_waiting");
  send("|ph=1", 2);  // wt is gone from the text
  CHECK(rig.reading()["reason"]["key"] == "truth.life_no_channel");
  send("|wt=-", 30);  // back as "-": 30 s in a row, not 72
  CHECK(rig.reading()["reason"]["key"] == "truth.life_waiting");
  // A counter of many digits (a running millisecond count) is fine
  text = "n=12345678901234|wt=2";
  sentMs = kNoonMs + rig.clk.ms + 1000;
  rig.step(1);
  CHECK(rig.reading()["quality"] == "ok");
}

TEST_CASE("Home Assistant: a timestamp sensor first seen ahead of Home Assistant's clock gets its real age once it is behind (SD-042, R5)") {
  LifeRig rig;
  const std::int64_t stampMs = kNoonMs + rig.clk.ms + 6000;  // 5 s ahead when first seen
  rig.extra = [&] { return json::array({lifeStamp(iso(stampMs))}); };
  rig.step(1);
  REQUIRE(ha::setLife(rig.hub, rig.bus, "tank.water_temp", "sensor.water_read", "").status == 200);
  rig.step(1);
  CHECK(rig.reading()["reason"]["key"] == "truth.life_future");
  rig.step(10);  // now 6 s behind; taken as now while within 2 s ahead, so it may read up to 2 s older, never younger
  json r = rig.reading();
  CHECK(r["quality"] == "ok");
  CHECK(r["ageMs"].get<std::int64_t>() >= 6000);
  CHECK(r["ageMs"].get<std::int64_t>() <= 9000);
}

TEST_CASE("Home Assistant: signs of life are listed only when asked for, with today's ages, per kind and cut at 100 each (SD-042)") {
  ha::HaBus bus({{"sensor.water", "water_temp"}});
  json states = json::array({state("sensor.water", "21.5", "°C", iso(kNoonMs))});
  states.push_back(lifeText("n=1|wt=5|ph=-", kNoonMs + 10000));
  for (int i = 0; i < 120; ++i) states.push_back(lifeStamp(iso(kNoonMs), "sensor.stamp_" + std::to_string(i)));
  bus.updateAll(states, kNoonMs + 20000, 20000);
  const json brief = bus.candidatesJson();
  CHECK_FALSE(brief.contains("lives"));
  CHECK(brief["lifeCount"] == 101);
  const json full = bus.candidatesJson(false, true);
  REQUIRE(full["lives"].size() == 101);  // the text is not crowded out by the timestamp sensors
  CHECK(std::find(full["truncated"].begin(), full["truncated"].end(), "lives") != full["truncated"].end());
  for (const auto& l : full["lives"])
    if (l["entity"] == "sensor.hub_life") {
      CHECK(l["channels"][0]["ageS"] == 15);  // 5 s when sent, sent 10 s ago
      CHECK(l["channels"][1]["ageS"].is_null());
    }
  // A time ahead shows no age, so nothing invites picking it
  bus.updateAll(json::array({lifeStamp(iso(kNoonMs + 60000), "sensor.next_run")}), kNoonMs + 20000, 21000);
  CHECK(bus.candidatesJson(false, true)["lives"][0]["ageS"].is_null());
  // A sign-of-life text of 255 characters is read, one of 256 is none
  std::string channels;
  for (int i = 10; i <= 30; ++i) channels += "|c" + std::to_string(i) + "=1234567";
  REQUIRE(("n=1" + channels).size() == 255);
  CHECK(ha::parseLifeText("n=1" + channels));
  CHECK_FALSE(ha::parseLifeText("n=12" + channels));
}

TEST_CASE("Home Assistant: with a sign of life a live pH that moves a little stays valid; a calibration point needs a measurement 3 s after the request (SD-042, RAT-023)") {
  LifeRig rig;
  int counter = 0;
  int phAge = 1;
  rig.extra = [&] { return json::array({lifeText("n=" + std::to_string(++counter) + "|ph=" + std::to_string(phAge), kNoonMs + rig.clk.ms)}); };
  rig.step(2);
  REQUIRE(ha::setLife(rig.hub, rig.bus, "tank.ph", "sensor.hub_life", "ph").status == 200);
  json file = gc::json(rig.hub.config());
  file["calibrations"]["ha.sensor.ph_mv"]["ph"] = {{"points", json::array({json::array({7.0, 7.0}), json::array({4.0, 4.0})})}, {"at", 0}};
  REQUIRE(rig.hub.importConfig(file).status == 200);
  // 0.1 mV every 10 min: a living probe in calm water, not frozen
  for (int k = 0; k < 4; ++k) {
    rig.mv += 0.1;
    rig.phAt = kNoonMs + rig.clk.ms + 1000;
    rig.step(10 * 60);
    CHECK(rig.reading("tank.ph")["quality"] == "ok");
  }
  // A measurement 2 s after the request is not taken; one 4 s after is
  auto probe = [&](const std::string& action, double ref = gc::kNaN) {
    json j = {{"device", "ha.sensor.ph_mv"}, {"kind", "ph"}, {"action", action}};
    if (std::isfinite(ref)) j["reference"] = ref;
    return rig.hub.probeCalibration(j);
  };
  auto session = [&] { return rig.hub.state()["probeCalibration"]["ha.sensor.ph_mv:ph"]; };
  REQUIRE(probe("start").status == 200);
  REQUIRE(probe("point", 7.0).status == 200);
  phAge = 0;  // measured right at each send, every second: 1 s after the request
  rig.step(1);
  phAge = 30;  // then silent: its last measurement stays at the request
  rig.step(1);
  CHECK(session().contains("pending"));
  // The channel stays silent while the text goes on: the point fails within the limit
  for (int i = 0; i < 62; ++i) {
    ++phAge;
    rig.step(1);
  }
  CHECK_FALSE(session().contains("pending"));
  CHECK(session()["failed"]["key"] == "probe.no_raw");
}

TEST_CASE("Devices: after every device was offline at once, each known device logs that it is back; the first devices after a start do not") {
  // With Home Assistant as the device layer this is the normal case: when
  // Home Assistant restarts, every device stops answering at once.
  gc::Catalog cat = gc::Catalog::builtin();
  gc::MemoryStorage store;
  test::FakeBus bus;
  test::Clock clk;
  bus.head("PHEC-1");
  bus.head("PHEC-2");
  auto tick = [&](gc::Hub& h) {
    clk.ms += 1000;
    h.tick();
  };
  {
    gc::Hub first(cat, bus, store, clk, pseudoRandom);
    first.boot();
    tick(first);
    REQUIRE(first.acceptDevice("PHEC-1", "").status == 200);
    REQUIRE(first.acceptDevice("PHEC-2", "").status == 200);
    first.flush();
  }
  // The devices of the events with this title, in the order they were logged
  auto devices = [&](gc::Hub& h, const std::string& key) {
    std::vector<std::string> ids;
    const json events = h.events(0, 9999999999, "device", 2000)["events"];
    for (const auto& e : events)
      if (e["title"]["key"] == key) ids.push_back(e["data"].value("device", std::string()));
    std::sort(ids.begin(), ids.end());
    return ids;
  };
  const std::vector<std::string> both = {"PHEC-1", "PHEC-2"};
  auto all = [&](bool online) {
    for (auto& d : bus.devs) d.online = online;
  };
  all(false);  // Home Assistant answers only after the start
  gc::Hub hub(cat, bus, store, clk, pseudoRandom);
  hub.boot();
  tick(hub);
  all(true);
  tick(hub);
  CHECK(devices(hub, "ev.device.back").empty());  // a start is not a return
  CHECK(devices(hub, "ev.device.lost").empty());
  all(false);
  tick(hub);
  CHECK(devices(hub, "ev.device.lost") == both);
  all(true);
  tick(hub);
  CHECK(devices(hub, "ev.device.back") == both);  // one per known device
  all(false);
  tick(hub);
  all(true);
  tick(hub);
  CHECK(devices(hub, "ev.device.lost").size() == 4);
  CHECK(devices(hub, "ev.device.back").size() == 4);
  // One device alone, as before
  bus.devs[0].online = false;
  tick(hub);
  bus.devs[0].online = true;
  tick(hub);
  CHECK(devices(hub, "ev.device.lost").size() == 5);
  CHECK(devices(hub, "ev.device.back") == std::vector<std::string>{"PHEC-1", "PHEC-1", "PHEC-1", "PHEC-2", "PHEC-2"});
}

TEST_CASE("Devices: a known device that answers after the first ones at a start logs that it is back") {
  // As before #69: only the first devices after a start are not "back". A
  // pump plugged in later still gets its "still on the bottle?" hint.
  gc::Catalog cat = gc::Catalog::builtin();
  gc::MemoryStorage store;
  test::FakeBus bus;
  test::Clock clk;
  bus.head("PHEC-1");
  bus.head("PHEC-2");
  auto tick = [&](gc::Hub& h) {
    clk.ms += 1000;
    h.tick();
  };
  {
    gc::Hub first(cat, bus, store, clk, pseudoRandom);
    first.boot();
    tick(first);
    REQUIRE(first.acceptDevice("PHEC-1", "").status == 200);
    REQUIRE(first.acceptDevice("PHEC-2", "").status == 200);
    first.flush();
  }
  for (auto& d : bus.devs) d.online = false;
  gc::Hub hub(cat, bus, store, clk, pseudoRandom);
  hub.boot();
  auto back = [&] {
    std::vector<std::string> ids;
    const json events = hub.events(0, 9999999999, "device", 2000)["events"];
    for (const auto& e : events)
      if (e["title"]["key"] == "ev.device.back") ids.push_back(e["data"].value("device", std::string()));
    return ids;
  };
  tick(hub);
  bus.devs[0].online = true;  // Home Assistant's integrations reconnect one after another
  tick(hub);
  CHECK(back().empty());
  bus.devs[1].online = true;
  tick(hub);
  CHECK(back() == std::vector<std::string>{"PHEC-2"});
}
