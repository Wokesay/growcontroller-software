// SPDX-License-Identifier: AGPL-3.0-or-later
// Read-only spike: Home Assistant as the device layer (docs/HOME_ASSISTANT.md).
#include <doctest/doctest.h>

#include <thread>

#include <httplib.h>

#include "fakes.hpp"
#include "gc/hub.hpp"
#include "ha_bus.hpp"
#include "ha_client.hpp"

using gc::json;

namespace {

void zeros(std::uint8_t* p, size_t n) {
  for (size_t i = 0; i < n; ++i) p[i] = static_cast<std::uint8_t>(i * 7 + 1);
}

json state(const std::string& entity, const std::string& value, const std::string& unit, const std::string& reported) {
  return {{"entity_id", entity},
          {"state", value},
          {"attributes", {{"unit_of_measurement", unit}}},
          {"last_updated", "2026-10-09T10:00:00+00:00"},
          {"last_reported", reported}};
}

constexpr std::int64_t kNoonMs = 1791547200000;  // 2026-10-09T12:00:00Z

}  // namespace

TEST_CASE("Home Assistant: timestamps with fraction and offset") {
  CHECK(ha::parseTimestampMs("1970-01-01T00:00:00+00:00") == 0);
  CHECK(ha::parseTimestampMs("2026-10-09T12:00:00Z") == kNoonMs);
  CHECK(ha::parseTimestampMs("2026-10-09T12:00:00.123456+00:00") == kNoonMs + 123);
  CHECK(ha::parseTimestampMs("2026-10-09T14:00:00+02:00") == kNoonMs);
  CHECK(ha::parseTimestampMs("") == -1);
  CHECK(ha::parseTimestampMs("2026-13-09T12:00:00Z") == -1);
  CHECK(ha::parseTimestampMs("2026-10-09T12:00:00+0200") == -1);
}

TEST_CASE("Home Assistant: the mapping names an address and what each entity measures") {
  std::string err;
  auto m = ha::parseMapping({{"url", "http://ha:8123"}, {"entities", {{{"entity", "sensor.grow_ph"}, {"measures", "ph"}}}}}, err);
  CHECK(err.empty());
  REQUIRE(m.entities.size() == 1);
  CHECK(m.entities[0].entityId == "sensor.grow_ph");
  ha::parseMapping({{"url", "http://ha:8123"}, {"entities", {{{"entity", "sensor.x"}, {"measures", "voltage"}}}}}, err);
  CHECK_FALSE(err.empty());
  ha::parseMapping({{"entities", json::array()}}, err);
  CHECK_FALSE(err.empty());
}

TEST_CASE("Home Assistant: states become samples in the hub's units; missing stays missing (R5)") {
  ha::HaBus bus({{"sensor.ph", "ph"}, {"sensor.ec", "ec"}, {"sensor.temp", "water_temp"}, {"sensor.level", "level"}});
  const gc::Ms now = 600000;
  bus.update(state("sensor.ph", "6.2", "", "2026-10-09T11:59:50+00:00"), kNoonMs, now);
  bus.update(state("sensor.ec", "1450", "µS/cm", "2026-10-09T12:00:00+00:00"), kNoonMs, now);
  bus.update(state("sensor.temp", "68", "°F", "2026-10-09T12:00:00+00:00"), kNoonMs, now);
  bus.update(state("sensor.level", "40", "%", "2026-10-09T12:00:00+00:00"), kNoonMs, now);
  auto ph = bus.sample("ha.sensor.ph", "measure.ph");
  REQUIRE(ph);
  CHECK(ph->raw == doctest::Approx(6.2));
  CHECK(ph->ts == now - 10000);  // reported 10 s ago
  CHECK(bus.sample("ha.sensor.ec", "measure.ec")->raw == doctest::Approx(1.45));
  CHECK(bus.sample("ha.sensor.temp", "measure.water_temp")->raw == doctest::Approx(20.0));
  CHECK_FALSE(bus.sample("ha.sensor.level", "measure.level"));  // % needs the tank's shape: not yet
  CHECK_FALSE(bus.sample("ha.sensor.ph", "measure.ec"));
  auto devs = bus.devices();
  REQUIRE(devs.size() == 4);
  CHECK(devs[0].cls == "ha_ph");
  CHECK(devs[0].online);
  CHECK(devs[3].fault == "unit % not supported");
  // "unknown" is no value, "unavailable" is offline; neither becomes 0
  bus.update(state("sensor.ph", "unknown", "", "2026-10-09T12:00:00+00:00"), kNoonMs, now);
  CHECK_FALSE(bus.sample("ha.sensor.ph", "measure.ph"));
  CHECK(bus.devices()[0].online);
  bus.update(state("sensor.ph", "unavailable", "", "2026-10-09T12:00:00+00:00"), kNoonMs, now);
  CHECK_FALSE(bus.devices()[0].online);
  bus.lost("sensor.ec");
  CHECK_FALSE(bus.devices()[1].online);
  CHECK_FALSE(bus.sample("ha.sensor.ec", "measure.ec"));
}

TEST_CASE("Home Assistant: the spike switches nothing") {
  ha::HaBus bus({{"sensor.ph", "ph"}});
  std::string err;
  CHECK_FALSE(bus.setSwitch("ha.sensor.ph", 0, true, err));
  CHECK_FALSE(err.empty());
  CHECK_FALSE(bus.startRun("ha.sensor.ph", 1000, "job", err));
  CHECK_FALSE(bus.writePumpCalibration("ha.sensor.ph", 50, err));
}

TEST_CASE("Home Assistant: a mapped pH sensor shows in the hub as a calibrated reading") {
  const gc::Catalog cat = ha::catalog();
  ha::HaBus bus({{"sensor.grow_ph", "ph"}});
  gc::MemoryStorage store;
  test::Clock clk;
  gc::Hub hub(cat, bus, store, clk, zeros);
  hub.boot();
  const std::int64_t nowEpochMs = static_cast<std::int64_t>(clk.epoch()) * 1000;
  bus.update(state("sensor.grow_ph", "6.1", "", "2026-01-01T00:00:00+00:00"), nowEpochMs, clk.ms);  // old report
  auto fresh = state("sensor.grow_ph", "6.1", "", "");
  fresh["last_updated"] = "";
  for (int i = 0; i < 5; ++i) {
    bus.update(fresh, nowEpochMs + clk.ms, clk.ms);  // no timestamp: seen now
    clk.ms += 1000;
    hub.tick();
  }
  REQUIRE(hub.acceptDevice("ha.sensor.grow_ph", "").status == 200);
  auto bound = hub.config().binding("tank.ph");
  if (!bound) REQUIRE(hub.bindRole("tank.ph", "ha.sensor.grow_ph", 0).status == 200);
  for (int i = 0; i < 3; ++i) {
    bus.update(fresh, nowEpochMs + clk.ms, clk.ms);
    clk.ms += 1000;
    hub.tick();
  }
  const json r = hub.state()["readings"]["tank.ph"];
  CHECK(r["quality"] == "ok");  // calibrated in Home Assistant, not "uncalibrated"
  CHECK(r["value"].get<double>() == doctest::Approx(6.1));
  bus.lost("sensor.grow_ph");
  clk.ms += 1000;
  hub.tick();
  CHECK(hub.state()["readings"]["tank.ph"]["value"].is_null());  // never 0 (R5)
}

TEST_CASE("Home Assistant: the poller reads entities with the token and reports a rejected token") {
  httplib::Server ha;
  ha.Get(R"(/api/states/(.+))", [](const httplib::Request& req, httplib::Response& res) {
    if (req.get_header_value("Authorization") != "Bearer secret") {
      res.status = 401;
      return;
    }
    if (req.matches[1] != "sensor.grow_ph") {
      res.status = 404;
      return;
    }
    res.set_content(state("sensor.grow_ph", "5.9", "", "2026-10-09T12:00:00+00:00").dump(), "application/json");
  });
  const int port = ha.bind_to_any_port("127.0.0.1");
  REQUIRE(port > 0);
  std::thread t([&] { ha.listen_after_bind(); });
  ha.wait_until_ready();
  const std::string url = "http://127.0.0.1:" + std::to_string(port);
  {
    ha::HaBus bus({{"sensor.grow_ph", "ph"}, {"sensor.gone", "ec"}});
    ha::Poller p(bus, url, "secret", [] { return gc::Ms{0}; });
    const std::string problem = p.pollOnce();
    CHECK(problem.find("sensor.gone") != std::string::npos);
    CHECK(problem.find("secret") == std::string::npos);
    CHECK(bus.sample("ha.sensor.grow_ph", "measure.ph")->raw == doctest::Approx(5.9));
    CHECK_FALSE(bus.devices()[1].online);
  }
  {
    ha::HaBus bus({{"sensor.grow_ph", "ph"}});
    ha::Poller p(bus, url, "wrong", [] { return gc::Ms{0}; });
    CHECK(p.pollOnce() == "Home Assistant rejected the token");
    CHECK_FALSE(bus.devices()[0].online);
  }
  ha.stop();
  t.join();
  {
    ha::HaBus bus({{"sensor.grow_ph", "ph"}});
    ha::Poller p(bus, url, "secret", [] { return gc::Ms{0}; });
    CHECK(p.pollOnce().find("not reachable") != std::string::npos);
  }
}
