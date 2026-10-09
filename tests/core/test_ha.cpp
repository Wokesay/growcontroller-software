// SPDX-License-Identifier: AGPL-3.0-or-later
// Read-only spike: Home Assistant as the device layer (docs/HOME_ASSISTANT.md).
#include <doctest/doctest.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include <httplib.h>

#include "fakes.hpp"
#include "gc/hub.hpp"
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
  CHECK(refused({{"url", "http://ha:8123"}, {"entities", json::array()}}));  // nothing to read
  for (const char* id : {"Sensor.Grow", "sensor", "../api/config", "sensor.a.b", "sensor.grow ph"})
    CHECK(refused({{"url", "http://ha:8123"}, {"entities", {{{"entity", id}, {"measures", "ph"}}}}}));
  CHECK(refused({{"url", "http://ha:8123"}, {"entities", {{{"entity", "sensor.x"}, {"measures", "voltage"}}}}}));
  CHECK(refused({{"url", "http://ha:8123"}, {"entities", {one[0], one[0]}}}));  // listed twice
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
  bus.update("sensor.ph", state("sensor.ph", "6.2", "", iso(kNoonMs)), kNoonMs + 7001, 10001);
  const auto again = bus.sample("ha.sensor.ph", "measure.ph");
  REQUIRE(again);
  CHECK(again->ts == 3000);
  // A report dated after Home Assistant's "now" (clock step) counts as new, not as older
  bus.update("sensor.ph", state("sensor.ph", "6.3", "", iso(kNoonMs + 60000)), kNoonMs + 8000, 11000);
  const auto ahead = bus.sample("ha.sensor.ph", "measure.ph");
  REQUIRE(ahead);
  CHECK(ahead->ts == 11000);
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
  const std::int64_t epoch0 = static_cast<std::int64_t>(clk.epoch()) * 1000;
  auto report = [&](const std::string& entity, const std::string& value, const std::string& unit) {
    const std::int64_t haNow = epoch0 + clk.ms;
    auto s = state(entity, value, unit, "");
    s["last_reported"] = s["last_updated"] = iso(kNoonMs + (haNow - epoch0));
    bus.update(entity, s, kNoonMs + (haNow - epoch0), clk.ms);
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
  CHECK(hub.probeCalibration({{"device", "ha.sensor.grow_ph"}, {"kind", "ph"}, {"action", "start"}}).status == 422);
  bool explained = false;
  for (const auto& f : st["functions"])
    for (const auto& c : f["checks"])
      if (c["text"].get<std::string>().find("außerhalb des Hubs kalibriert") != std::string::npos) {
        explained = true;
        CHECK(c["fix"] == "");
      }
  CHECK(explained);
  // Not re-reported (ESPHome without force_update): one fresh report, then Home Assistant keeps
  // answering with it while its own time runs on. Valid until the 60 s limit, stale after it (RAT-023).
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
    if (req.matches.size() < 1 || req.path != "/api/states/sensor.grow_ph") {
      res.status = 404;
      return;
    }
    res.set_header("Date", "Fri, 09 Oct 2026 12:10:00 GMT");  // Home Assistant's own clock
    res.set_content(state("sensor.grow_ph", "5.9", "", "2026-10-09T12:00:00+00:00").dump(), "application/json");
  });
  REQUIRE(ha.port > 0);
  {
    ha::HaBus bus({{"sensor.grow_ph", "ph"}, {"sensor.gone", "ec"}});
    ha::Poller p(bus, ha.url(), "secret", [] { return gc::Ms{900000}; });
    const std::string problem = p.pollOnce();
    CHECK(problem.find("sensor.gone") != std::string::npos);
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
    CHECK(problem.find("wrong") == std::string::npos);
    CHECK(p.rejected());
    CHECK_FALSE(bus.devices()[0].online);
    // Running, it does not try a refused token again (Home Assistant bans after a few failed logins)
    const int before = ha.count;
    p.start(std::chrono::milliseconds(20));
    for (int i = 0; i < 500 && ha.count == before; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(ha.count == before + 1);                             // the first round ran
    std::this_thread::sleep_for(std::chrono::milliseconds(300));  // 15 more rounds at 20 ms if it went on
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
    CHECK(problem.find("secret") == std::string::npos);
    CHECK_FALSE(p.rejected());
  }
}

TEST_CASE("Home Assistant: the poller sends nothing but state reads, whatever the hub is asked to do") {
  FakeHa ha([](const httplib::Request& req, httplib::Response& res) {
    res.set_content(state(req.path.substr(12), "6.0", req.path.find("temp") != std::string::npos ? "°C" : "",
                          "2026-10-09T12:00:00+00:00")
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
  for (const auto& r : ha.requests) CHECK((r == "GET /api/states/sensor.grow_ph" || r == "GET /api/states/sensor.grow_temp"));
}
