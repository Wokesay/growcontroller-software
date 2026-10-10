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
  CHECK(written < 400000);  // rewriting the whole history every 10 min wrote about 55 MB
}

TEST_CASE("Storage: history and events come back after a hard stop, from the snapshot and the journal (#68)") {
  TempDir dir;
  const gc::Epoch start = 1790000000;
  sim::Simulation s(onCard(dir, start));
  test::Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  for (int i = 0; i < 1800; ++i) s.step(1000);  // half an hour after the snapshot at the start
  c.ok("POST", "/api/v1/stop");
  const gc::Epoch end = s.hub().now();
  const std::string range = "&from=" + std::to_string(start) + "&to=" + std::to_string(end - 30);
  const json before = c.ok("GET", "/api/v1/history?series=tank.ph,tank.ec" + range);
  const std::string week = "&from=" + std::to_string(start - 8 * 86400) + "&to=" + std::to_string(end - 30);
  const json coarse = c.ok("GET", "/api/v1/history?points=4000&series=tank.ph" + week);  // older than a week: the 15 min tier
  const json events = c.ok("GET", "/api/v1/events?limit=5");
  // A hard stop: the first simulation never saves again; a second one starts from what is on disk
  sim::Simulation again(onCard(dir, end + 1));
  test::Client c2{again};
  c2.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  // Up to 30 s before the end: the newest slot takes the samples after the restart
  const json after = c2.ok("GET", "/api/v1/history?series=tank.ph,tank.ec" + range);
  REQUIRE(before["series"].size() == 2);
  CHECK(before["series"][0]["t"].size() > 100);  // half an hour in 10 s steps
  CHECK(after["series"] == before["series"]);
  CHECK(coarse["series"][0]["stepS"] == 900);
  json avg = coarse["series"][0]["avg"], avgAfter = c2.ok("GET", "/api/v1/history?points=4000&series=tank.ph" + week)["series"][0]["avg"];
  avg.erase(avg.size() - 1);  // the open slot takes the samples after the restart
  avgAfter.erase(avgAfter.size() - 1);
  CHECK(avgAfter == avg);
  const json kept = c2.ok("GET", "/api/v1/events?limit=50&to=4000000000");
  bool stopKept = false;
  for (const auto& e : kept["events"])
    if (e["id"] == events["events"][0]["id"]) stopKept = e["title"]["key"] == "ev.stop";
  CHECK(stopKept);
}

TEST_CASE("Storage: a day and more in one go, with its snapshot, comes back whole (#68)") {
  TempDir dir;
  const gc::Epoch start = 1790000000;
  sim::Simulation s(onCard(dir, start));
  test::Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  s.fastForward(25);  // writes are held in one go, the daily snapshot falls inside
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});  // the session ended meanwhile
  const gc::Epoch end = s.hub().now();
  const std::string range = "&from=" + std::to_string(end - 6 * 3600) + "&to=" + std::to_string(end - 900);
  const json before = c.ok("GET", "/api/v1/history?series=tank.ph" + range);
  const json events = c.ok("GET", "/api/v1/events?limit=1");
  sim::Simulation again(onCard(dir, end + 1));
  test::Client c2{again};
  c2.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  CHECK(before["series"][0]["t"].size() > 100);
  CHECK(c2.ok("GET", "/api/v1/history?series=tank.ph" + range)["series"] == before["series"]);
  const json kept = c2.ok("GET", "/api/v1/events?limit=200&to=4000000000");
  bool found = false;
  for (const auto& e : kept["events"]) found = found || e["id"] == events["events"][0]["id"];
  CHECK(found);
}

TEST_CASE("Storage: failed logins cannot grow the event journal without limit (#68)") {
  TempDir dir;
  sim::Simulation s(onCard(dir));
  test::Client c{s};
  for (int i = 0; i < 12000; ++i) c.call("POST", "/api/v1/auth/login", {{"password", "falsch"}});  // locked after five: cheap
  size_t lines = 0;
  for (char ch : onDisk(dir.path / "events.log")) lines += ch == '\n';
  CHECK(lines <= 5000);
  const json snapshot = json::parse(onDisk(dir.path / "events.json"), nullptr, false);
  CHECK(snapshot["events"].size() <= 5000);
}

TEST_CASE("Storage: setup writes the password and its lock marker at once, or says it could not (#68)") {
  TempDir dir;
  sim::Options o = test::opts("neu");
  o.dataDir = dir.path.string();
  sim::Simulation s(o);
  test::Client c{s};
  // The password file cannot be written (a folder where its temporary file goes, like a full card)
  fs::create_directories(dir.path / "auth.json.tmp");
  auto [st, body] = c.call("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  CHECK(st == 500);
  CHECK(body["error"]["key"] == "store.failed");
  CHECK(c.ok("GET", "/api/v1/auth/session")["hasPassword"] == false);  // nothing set: setup stays possible
  bool alarm = false;
  const json logged = s.hub().events(0, 4000000000, "", 50);
  for (const auto& e : logged["events"]) alarm = alarm || e["title"]["key"] == "ev.storage_failed";
  CHECK(alarm);
  // Space again: setup works and both files are on disk without a flush
  fs::remove_all(dir.path / "auth.json.tmp");
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  CHECK(!json::parse(onDisk(dir.path / "auth.json"), nullptr, false)["hash"].get<std::string>().empty());
  CHECK(json::parse(onDisk(dir.path / "config.json"), nullptr, false)["system"]["passwordSet"] == true);
  // A failed write is tried again: a STOP after the card had a problem is on disk
  fs::create_directories(dir.path / "state.json.tmp");
  c.ok("POST", "/api/v1/stop");
  CHECK_FALSE(gc::jbool(json::parse(onDisk(dir.path / "state.json"), nullptr, false), "stopped", false));
  fs::remove_all(dir.path / "state.json.tmp");
  c.ok("POST", "/api/v1/stop");
  CHECK(gc::jbool(json::parse(onDisk(dir.path / "state.json"), nullptr, false), "stopped", false));
}

TEST_CASE("Storage: a snapshot that cannot be written leaves its journal alone (#68)") {
  TempDir dir;
  sim::Simulation s(onCard(dir));
  test::Client c{s};
  fs::create_directories(dir.path / "events.json.tmp");  // no snapshot of the events can be written
  for (int i = 0; i < 6000; ++i) c.call("POST", "/api/v1/auth/login", {{"password", "falsch"}});
  size_t lines = 0;
  for (char ch : onDisk(dir.path / "events.log")) lines += ch == '\n';
  CHECK(lines >= 6000);  // every failed login is still on disk, in the journal
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

namespace {
size_t countKey(const json& events, const std::string& key) {
  size_t n = 0;
  for (const auto& e : events["events"]) n += e["title"]["key"] == key ? 1 : 0;
  return n;
}
}  // namespace

TEST_CASE("Storage: one file that cannot be written raises one alarm, cleared only once it is written (#68)") {
  TempDir dir;
  sim::Simulation s(onCard(dir));
  test::Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  fs::create_directories(dir.path / "config.json.tmp");  // the configuration cannot be written, everything else can
  c.ok("PATCH", "/api/v1/devices/PHEC-3F2A91", {{"name", "Tank probe renamed"}});
  for (int i = 0; i < 120; ++i) s.step(1000);  // state and journals are written meanwhile
  json logged = s.hub().events(0, 4000000000, "", 500);
  CHECK(countKey(logged, "ev.storage_failed") == 1);
  CHECK(countKey(logged, "ev.storage_ok") == 0);
  // Space again: the change made meanwhile is written without a second change
  fs::remove_all(dir.path / "config.json.tmp");
  for (int i = 0; i < 30; ++i) s.step(1000);
  logged = s.hub().events(0, 4000000000, "", 500);
  CHECK(countKey(logged, "ev.storage_ok") == 1);
  CHECK(onDisk(dir.path / "config.json").find("Tank probe renamed") != std::string::npos);
  sim::Simulation again(onCard(dir, s.hub().now() + 1));  // a hard stop now keeps the name
  CHECK(again.hub().config().device("PHEC-3F2A91")->name == "Tank probe renamed");
}

TEST_CASE("Storage: a journal append that fails is repaired by a snapshot, nothing after it is lost (#68)") {
  TempDir dir;
  const gc::Epoch start = 1790000000;
  sim::Simulation s(onCard(dir, start));
  test::Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  // The history journal cannot be appended to for a while (a link to a folder stands in for a full card)
  fs::remove(dir.path / "history.log");
  fs::create_directories(dir.path / "blocked");
  fs::create_directory_symlink(dir.path / "blocked", dir.path / "history.log");
  for (int i = 0; i < 120; ++i) s.step(1000);
  CHECK_FALSE(fs::is_symlink(dir.path / "history.log"));  // replaced by the snapshot the hub retried
  for (int i = 0; i < 600; ++i) s.step(1000);
  const gc::Epoch end = s.hub().now();
  const std::string range = "&from=" + std::to_string(start) + "&to=" + std::to_string(end - 30);
  const json before = c.ok("GET", "/api/v1/history?series=tank.ph" + range);
  sim::Simulation again(onCard(dir, end + 1));
  test::Client c2{again};
  c2.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  CHECK(before["series"][0]["t"].size() > 60);
  CHECK(c2.ok("GET", "/api/v1/history?series=tank.ph" + range)["series"] == before["series"]);
}

TEST_CASE("Storage: a password change that cannot be saved keeps the old password and the session (#68)") {
  TempDir dir;
  sim::Simulation s(onCard(dir));
  test::Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  fs::create_directories(dir.path / "auth.json.tmp");
  auto [st, body] = c.call("PUT", "/api/v1/auth/password", {{"old", "demo-passwort"}, {"new", "ein-neues-passwort"}});
  CHECK(st == 500);
  CHECK(body["error"]["key"] == "store.failed");
  CHECK(c.ok("GET", "/api/v1/auth/session")["authenticated"] == true);  // still signed in
  test::Client other{s};
  CHECK(other.call("POST", "/api/v1/auth/login", {{"password", "ein-neues-passwort"}}).first == 401);
  CHECK(other.call("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}}).first == 200);
}

TEST_CASE("Storage: in the simulator a snapshot that fails at the end of a step keeps its journal (#68)") {
  TempDir dir;
  sim::Simulation s(onCard(dir));
  test::Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  c.ok("POST", "/api/v1/stop");
  fs::create_directories(dir.path / "events.json.tmp");  // the daily snapshot of the events cannot be written
  s.fastForward(25);
  CHECK(onDisk(dir.path / "events.log").find("\"ev.stop\"") != std::string::npos);  // still in the journal
}

TEST_CASE("Storage: a simulator in memory only keeps everything through a fast-forward and a power cut (#68)") {
  for (const bool noFolder : {false, true}) {
    CAPTURE(noFolder);
    TempDir dir;
    sim::Options o = test::opts("demo");
    if (noFolder) {  // a folder under a file can never be created: memory only, said once
      { std::ofstream(dir.path / "file") << "x"; }
      o.dataDir = (dir.path / "file" / "data").string();
    }
    sim::Simulation s(o);
    test::Client c{s};
    c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
    s.fastForward(1);
    s.reboot();
    test::Client after{s};
    after.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
    CHECK(s.hub().config().device("PHEC-3F2A91") != nullptr);
    CHECK(after.ok("GET", "/api/v1/history?series=tank.ph")["series"][0]["t"].size() > 100);
  }
}

TEST_CASE("Storage: a first setup that failed does not keep the alarm once the card takes files again (#68)") {
  TempDir dir;
  sim::Options o = test::opts("neu");
  o.dataDir = dir.path.string();
  sim::Simulation s(o);
  test::Client c{s};
  fs::create_directories(dir.path / "auth.json.tmp");
  CHECK(c.call("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}}).first == 500);
  fs::remove_all(dir.path / "auth.json.tmp");
  for (int i = 0; i < 30; ++i) s.step(1000);
  CHECK(countKey(s.hub().events(0, 4000000000, "", 100), "ev.storage_ok") == 1);
}

#ifdef __linux__
#include <csignal>
#include <sys/resource.h>

TEST_CASE("Storage: a snapshot that does not fit leaves no temporary file, and a STOP still reaches the disk (#68)") {
  TempDir dir;
  sim::Simulation s(onCard(dir));
  test::Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  // The history journal fails, so the hub retries it through a snapshot that cannot fit
  fs::remove(dir.path / "history.log");
  fs::create_directories(dir.path / "blocked");
  fs::create_directory_symlink(dir.path / "blocked", dir.path / "history.log");
  struct Limit {  // no file over 1 MB, like a card with 1 MB left; the history snapshot is about 4.6 MB
    rlimit before{};
    Limit() {
      std::signal(SIGXFSZ, SIG_IGN);
      getrlimit(RLIMIT_FSIZE, &before);
      rlimit small = before;
      small.rlim_cur = 1 << 20;
      setrlimit(RLIMIT_FSIZE, &small);
    }
    ~Limit() { setrlimit(RLIMIT_FSIZE, &before); }
  } limit;
  for (int i = 0; i < 30; ++i) s.step(1000);
  CHECK_FALSE(fs::exists(dir.path / "history.bin.tmp"));  // a failed snapshot does not keep the space
  c.ok("POST", "/api/v1/stop");
  CHECK(gc::jbool(json::parse(onDisk(dir.path / "state.json"), nullptr, false), "stopped", false));
}
#endif
