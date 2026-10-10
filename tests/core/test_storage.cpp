// SPDX-License-Identifier: AGPL-3.0-or-later
// Storage on an SD card (#68): what the user changes is on disk when the call
// returns; an idle hub writes little; history and events come back after a
// restart from a daily snapshot plus an append-only journal.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

#include "client.hpp"

namespace fs = std::filesystem;
using gc::json;

namespace {

struct TempDir {
  fs::path path;
  TempDir() {
    std::random_device rd;
    path = fs::temp_directory_path() / ("gc-storage-" + std::to_string(rd()) + std::to_string(rd()));
    fs::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

std::string onDisk(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

sim::Options onCard(const TempDir& d, gc::Epoch start = 1790000000) {
  sim::Options o = test::opts("demo");
  o.dataDir = d.path.string();
  o.startEpoch = start;
  return o;
}

}  // namespace

TEST_CASE("Storage: password, configuration and STOP are on disk when the call returns (#68)") {
  TempDir dir;
  sim::Simulation s(onCard(dir));
  test::Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  const std::string authBefore = onDisk(dir.path / "auth.json");
  c.ok("PATCH", "/api/v1/devices/PHEC-3F2A91", {{"name", "Tank probe renamed"}});
  CHECK(onDisk(dir.path / "config.json").find("Tank probe renamed") != std::string::npos);
  c.ok("POST", "/api/v1/stop");
  const json state = json::parse(onDisk(dir.path / "state.json"), nullptr, false);
  CHECK(state.is_object());
  CHECK(gc::jbool(state, "stopped", false));
  CHECK((onDisk(dir.path / "events.json") + onDisk(dir.path / "events.log")).find("\"ev.stop\"") != std::string::npos);
  c.ok("PUT", "/api/v1/auth/password", {{"old", "demo-passwort"}, {"new", "ein-neues-passwort"}});
  CHECK(onDisk(dir.path / "auth.json") != authBefore);
}

TEST_CASE("Storage: an idle hub writes little to the card (#68)") {
  TempDir dir;
  sim::Simulation s(onCard(dir));
  s.step(10 * 60 * 1000);  // past the start, where the hub writes everything once
  const auto before = s.storage().bytesWritten();
  for (int i = 0; i < 2 * 3600; ++i) s.step(1000);
  const auto written = s.storage().bytesWritten() - before;
  MESSAGE("bytes written in two idle hours: " << written);
  CHECK(written < 2000000);  // rewriting the whole history every 10 min wrote about 55 MB
}

TEST_CASE("Storage: history and events come back after a restart, from the snapshot and the journal (#68)") {
  TempDir dir;
  const gc::Epoch start = 1790000000;
  json before, events;
  gc::Epoch end = 0;
  {
    sim::Simulation s(onCard(dir, start));
    test::Client c{s};
    c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
    for (int i = 0; i < 1800; ++i) s.step(1000);  // half an hour after the snapshot at the start
    c.ok("POST", "/api/v1/stop");
    end = s.hub().now();
    before = c.ok("GET", "/api/v1/history?series=tank.ph,tank.ec&from=" + std::to_string(start) + "&to=" + std::to_string(end - 30));
    events = c.ok("GET", "/api/v1/events?limit=5");
  }
  sim::Simulation s(onCard(dir, end + 1));
  test::Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  // Up to 30 s before the end: the newest slot takes the samples after the restart
  const json after = c.ok("GET", "/api/v1/history?series=tank.ph,tank.ec&from=" + std::to_string(start) + "&to=" + std::to_string(end - 30));
  REQUIRE(before["series"].size() == 2);
  CHECK(before["series"][0]["t"].size() > 100);  // half an hour in 10 s steps
  CHECK(after["series"] == before["series"]);
  const json again = c.ok("GET", "/api/v1/events?limit=50");
  bool stopKept = false;
  for (const auto& e : again["events"])
    if (e["id"] == events["events"][0]["id"]) stopKept = e["title"]["key"] == "ev.stop";
  CHECK(stopKept);
}

TEST_CASE("Storage: a new scenario starts without the old journals (#68)") {
  TempDir dir;
  sim::Simulation s(onCard(dir));
  test::Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  c.ok("POST", "/api/v1/stop");
  s.loadScenario("neu");
  test::Client fresh{s};
  fresh.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  bool oldStop = false;
  const json events = fresh.ok("GET", "/api/v1/events?limit=200&to=4000000000");  // also events stamped later than the new clock
  for (const auto& e : events["events"]) oldStop = oldStop || e["title"]["key"] == "ev.stop";
  CHECK_FALSE(oldStop);
  CHECK(fresh.ok("GET", "/api/v1/history?series=tank.ph")["series"][0]["t"].empty());
}
