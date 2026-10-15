// SPDX-License-Identifier: AGPL-3.0-or-later
// Storage on an SD card (#68): what the user changes is on disk when the call
// returns; an idle hub writes little; history and events come back after a
// restart from a daily snapshot plus an append-only journal.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <vector>

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
  CHECK(s.storage().persistent());  // named as its data folder (#213)
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

TEST_CASE("Storage: the event journal does not grow without limit; failed logins write only what they count (#68, SD-051)") {
  TempDir dir;
  sim::Simulation s(onCard(dir));
  test::Client c{s};
  for (int i = 0; i < 12000; ++i) c.call("POST", "/api/v1/auth/login", {{"password", "falsch"}});  // locked after five: cheap
  s.step(16 * 60 * 1000);  // past the longest wait
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  const json auth = c.ok("GET", "/api/v1/events?limit=2000&type=auth")["events"];
  int failed = 0;
  for (const auto& e : auth) failed += e["title"].is_object() && e["title"].value("key", std::string()) == "ev.auth.failed";
  CHECK(failed == 5);
  for (int i = 0; i < 2550; ++i) {  // 5100 events, past the journal's 5000
    c.ok("POST", "/api/v1/stop");
    c.ok("POST", "/api/v1/resume");
  }
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

TEST_CASE("Storage: a first password that cannot be saved leaves the setup code valid (#79)") {
  TempDir dir;
  sim::Options o = test::opts("neu");
  o.dataDir = dir.path.string();
  sim::Simulation s(o);
  std::string code;
  {
    std::lock_guard<std::recursive_mutex> l(s.mutex());
    code = s.hub().armSetupCode();
  }
  test::Client c{s};
  fs::create_directories(dir.path / "auth.json.tmp");  // the password file cannot be written
  CHECK(c.call("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}, {"code", code}}).first == 500);
  CHECK(c.ok("GET", "/api/v1/info")["setupCode"] == true);  // nothing set, the code not used up
  fs::remove_all(dir.path / "auth.json.tmp");
  CHECK(c.call("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}, {"code", code}}).first == 200);
  std::string bare;
  for (char ch : code) if (ch != '-') bare += ch;
  for (const auto& f : fs::directory_iterator(dir.path))
    if (f.is_regular_file()) CHECK_MESSAGE(onDisk(f.path()).find(bare) == std::string::npos, f.path().string());
}

TEST_CASE("Storage: a snapshot that cannot be written leaves its journal alone (#68)") {
  TempDir dir;
  sim::Simulation s(onCard(dir));
  test::Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  fs::create_directories(dir.path / "events.json.tmp");  // no snapshot of the events can be written
  for (int i = 0; i < 3000; ++i) {  // two events each
    c.ok("POST", "/api/v1/stop");
    c.ok("POST", "/api/v1/resume");
  }
  size_t lines = 0;
  for (char ch : onDisk(dir.path / "events.log")) lines += ch == '\n';
  CHECK(lines >= 6000);  // every event is still on disk, in the journal
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
    CHECK_FALSE(s.storage().persistent());  // not named as a data folder (#213)
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

TEST_CASE("Storage: after a full card, a clean shutdown right after space returns keeps the history (#68)") {
  TempDir dir;
  const gc::Epoch start = 1790000000;
  json before;
  gc::Epoch end = 0;
  {
    sim::Simulation s(onCard(dir, start));
    test::Client c{s};
    c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
    // Neither the history journal nor its snapshot can be written for five minutes
    fs::remove(dir.path / "history.log");
    fs::create_directories(dir.path / "blocked");
    fs::create_directory_symlink(dir.path / "blocked", dir.path / "history.log");
    fs::create_directories(dir.path / "history.bin.tmp");
    for (int i = 0; i < 320; ++i) s.step(1000);  // retried after 10, 30, 70, 150 and 310 s; the next one is minutes away
    fs::remove_all(dir.path / "history.bin.tmp");  // space again
    for (int i = 0; i < 20; ++i) s.step(1000);
    end = s.hub().now();
    before = c.ok("GET", "/api/v1/history?series=tank.ph&from=" + std::to_string(start) + "&to=" + std::to_string(end - 30));
  }  // a clean shutdown, before the next retry was due
  sim::Simulation again(onCard(dir, end + 1));
  test::Client c2{again};
  c2.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  CHECK(before["series"][0]["t"].size() > 30);
  CHECK(c2.ok("GET", "/api/v1/history?series=tank.ph&from=" + std::to_string(start) + "&to=" + std::to_string(end - 30))["series"] == before["series"]);
}

TEST_CASE("Storage: a data folder it cannot read runs the simulator in memory only and is left alone (SD-074)") {
  // The status of the folder or of its config.json cannot be read (here a
  // link to itself; also a folder it may not enter): it may hold data, so
  // nothing is read, written or removed there.
  for (const bool folder : {true, false}) {
    CAPTURE(folder);
    TempDir dir;
    // the folder itself is the link, or a folder it could write holds a config.json it cannot read
    const fs::path link = dir.path / (folder ? "loop" : "config.json");
    fs::create_symlink(link.filename(), link);
    sim::Options o = test::opts("demo");
    o.dataDir = (folder ? link : dir.path).string();
    std::unique_ptr<sim::Simulation> made;
    REQUIRE_NOTHROW(made = std::make_unique<sim::Simulation>(o));
    sim::Simulation& s = *made;
    CHECK(s.dataDir().empty());
    test::Client c{s};
    c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
    s.fastForward(1);
    s.reboot();
    test::Client after{s};
    after.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
    CHECK(after.ok("GET", "/api/v1/history?series=tank.ph")["series"][0]["t"].size() > 100);
    s.loadScenario("neu");
    made.reset();
    std::vector<std::string> left;
    for (const auto& f : fs::directory_iterator(dir.path)) left.push_back(f.path().filename().string());
    CHECK(left == std::vector<std::string>{link.filename().string()});
    CHECK(fs::is_symlink(link));
  }
}

TEST_CASE("Storage: a hub file the simulator cannot read runs it in memory only, and every file stays as it was (SD-074)") {
  // E.g. root's files after a start with sudo, in a folder it can read: a
  // start would boot without them and later save over them.
  TempDir dir;
  { sim::Simulation first(onCard(dir)); }  // a finished run leaves its files
  const fs::path locked = dir.path / "state.json";
  REQUIRE(fs::exists(locked));
  const auto files = [&] {
    std::map<std::string, std::string> all;
    for (const auto& f : fs::directory_iterator(dir.path)) all[f.path().filename().string()] = onDisk(f.path());
    return all;
  };
  const auto before = files();
  fs::permissions(locked, fs::perms::none);
  if (std::ifstream(locked)) {
    fs::permissions(locked, fs::perms::owner_read | fs::perms::owner_write);
    MESSAGE("not checked: this process reads any file (root or equivalent)");
    return;
  }
  fs::permissions(locked, fs::perms::owner_read | fs::perms::owner_write);
  // Each file the hub or the simulator reads, one at a time (one the run
  // did not leave is put there): a name the check leaves out fails here.
  for (const char* name : {"config.json", "auth.json", "state.json", "job.json", "events.json", "events.log",
                           "history.bin", "history.log", "world.json"}) {
    const std::string shown = name;  // the name, not its address
    CAPTURE(shown);
    const fs::path p = dir.path / name;
    const bool placed = !fs::exists(p);
    if (placed) std::ofstream(p) << "{}";
    const auto with = files();
    fs::permissions(p, fs::perms::none);
    CHECK(sim::Simulation(onCard(dir, 1790100000)).dataDir().empty());
    fs::permissions(p, fs::perms::owner_read | fs::perms::owner_write);
    CHECK(files() == with);
    if (placed) fs::remove(p);
  }
  REQUIRE(files() == before);
  // the whole way once: a restart, a scenario change, the end
  fs::permissions(locked, fs::perms::none);
  std::unique_ptr<sim::Simulation> made;
  REQUIRE_NOTHROW(made = std::make_unique<sim::Simulation>(onCard(dir, 1790100000)));
  sim::Simulation& s = *made;
  CHECK(s.dataDir().empty());
  test::Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  s.fastForward(1);
  s.reboot();
  s.loadScenario("neu");
  made.reset();
  fs::permissions(locked, fs::perms::owner_read | fs::perms::owner_write);
  CHECK(files() == before);
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
    void (*handler)(int) = nullptr;
    Limit() {
      handler = std::signal(SIGXFSZ, SIG_IGN);
      getrlimit(RLIMIT_FSIZE, &before);
      rlimit small = before;
      small.rlim_cur = 1 << 20;
      setrlimit(RLIMIT_FSIZE, &small);
    }
    ~Limit() {
      setrlimit(RLIMIT_FSIZE, &before);
      std::signal(SIGXFSZ, handler);
    }
  } limit;
  for (int i = 0; i < 30; ++i) s.step(1000);
  CHECK_FALSE(fs::exists(dir.path / "history.bin.tmp"));  // a failed snapshot does not keep the space
  c.ok("POST", "/api/v1/stop");
  CHECK(gc::jbool(json::parse(onDisk(dir.path / "state.json"), nullptr, false), "stopped", false));
}
#endif

TEST_CASE("Storage: a removed bottle's stock leaves the card at once; a new bottle of the same name starts full (#152)") {
  TempDir dir;
  sim::Simulation s(onCard(dir));
  test::Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  s.step(61000);  // just after a regular save of the runtime state
  REQUIRE(c.state()["stock"]["ph"].get<double>() < 500);
  c.ok("DELETE", "/api/v1/canisters/ph");
  s.step(2000);
  // A hard stop: the first simulation never saves again; a second one starts from what is on disk
  sim::Simulation again(onCard(dir, s.hub().now() + 1));
  test::Client c2{again};
  c2.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  const std::string id = c2.ok("POST", "/api/v1/canisters", {{"name", "pH−"}, {"kind", "ph_down"}, {"capacityMl", 500}})["id"];
  REQUIRE(id == "ph");
  CHECK(c2.state()["stock"]["ph"].get<double>() == doctest::Approx(500));
}

TEST_CASE("Storage: an import drops the stock of the bottles it leaves out; a bottle created later never takes over a stock left behind (#161)") {
  TempDir dir;
  sim::Simulation s(onCard(dir));
  test::Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  c.ok("POST", "/api/v1/canisters", {{"id", "ph"}, {"stockMl", 123}});
  json file = c.ok("GET", "/api/v1/config/export");
  json kept = json::array();
  for (const auto& k : file["canisters"])
    if (k["id"] != "ph") kept.push_back(k);
  file["canisters"] = kept;
  // An import is refused while a job or a dose runs
  for (int i = 0; i < 600 && !(c.state()["job"].is_null() && c.state()["dosing"].is_null()); ++i) s.step(5000);
  c.ok("POST", "/api/v1/config/import", file);
  s.step(2000);
  CHECK_FALSE(json::parse(onDisk(dir.path / "state.json"), nullptr, false)["stockMl"].contains("ph"));
  const std::string id = c.ok("POST", "/api/v1/canisters", {{"name", "pH−"}, {"kind", "ph_down"}, {"capacityMl", 500}})["id"];
  REQUIRE(id == "ph");
  CHECK(c.state()["stock"]["ph"].get<double>() == doctest::Approx(500));
  // A stock left behind on the card (by an older version): a new bottle of that name still starts full,
  // or unknown without a size
  c.ok("DELETE", "/api/v1/canisters/ph");
  s.step(2000);
  json state = json::parse(onDisk(dir.path / "state.json"), nullptr, false);
  state["stockMl"]["ph"] = 123;
  state["stockMl"]["neu"] = 77;
  state["stockMl"]["minus"] = -5;
  {
    std::ofstream out(dir.path / "state.json", std::ios::trunc);
    out << state.dump();
  }
  sim::Simulation again(onCard(dir, s.hub().now() + 1));
  test::Client c2{again};
  c2.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  REQUIRE(c2.ok("POST", "/api/v1/canisters", {{"name", "pH−"}, {"kind", "ph_down"}, {"capacityMl", 500}})["id"] == "ph");
  CHECK(c2.state()["stock"]["ph"].get<double>() == doctest::Approx(500));
  REQUIRE(c2.ok("POST", "/api/v1/canisters", {{"name", "Neu"}, {"kind", "nutrient"}, {"capacityMl", nullptr}})["id"] == "neu");
  CHECK(c2.state()["stock"]["neu"].is_null());
  // A stock below zero left behind does not let a new bottle be entered below zero (SD-066)
  auto [code, body] = c2.call("POST", "/api/v1/canisters", {{"name", "Minus"}, {"kind", "nutrient"}, {"stockMl", -5}});
  CHECK(code == 422);
  CHECK(body["error"]["key"] == "canister.stock");
  CHECK_FALSE(c2.state()["stock"].contains("minus"));
  REQUIRE(c2.ok("POST", "/api/v1/canisters", {{"name", "Minus"}, {"kind", "nutrient"}, {"stockMl", 0}})["id"] == "minus");
  CHECK(c2.state()["stock"]["minus"].get<double>() == doctest::Approx(0));
}

namespace {
sim::Options onFreshCard(const TempDir& d, gc::Epoch start = 1790000000) {
  sim::Options o = test::opts("neu");
  o.dataDir = d.path.string();
  o.startEpoch = start;
  return o;
}
json withoutBottle(json file, const std::string& id) {
  json kept = json::array();
  for (const auto& k : file["canisters"])
    if (k["id"] != id) kept.push_back(k);
  file["canisters"] = kept;
  return file;
}
}  // namespace

TEST_CASE("Storage: an import keeps the stock of the bottles it keeps (#161)") {
  TempDir dir;
  sim::Simulation s(onFreshCard(dir));
  test::Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  const std::string one = c.ok("POST", "/api/v1/canisters", {{"name", "Eins"}, {"kind", "nutrient"}, {"capacityMl", 1000}, {"stockMl", 123}})["id"];
  const std::string two = c.ok("POST", "/api/v1/canisters", {{"name", "Zwei"}, {"kind", "nutrient"}, {"capacityMl", 1000}, {"stockMl", 45}})["id"];
  c.ok("POST", "/api/v1/config/import", withoutBottle(c.ok("GET", "/api/v1/config/export"), two));
  s.step(2000);
  const json state = json::parse(onDisk(dir.path / "state.json"), nullptr, false);
  CHECK(c.state()["stock"][one].get<double>() == doctest::Approx(123));
  CHECK(state["stockMl"][one].get<double>() == doctest::Approx(123));
  CHECK_FALSE(state["stockMl"].contains(two));
}

TEST_CASE("Storage: an import refused while a dose runs drops no stock (#161)") {
  TempDir dir;
  sim::Simulation s(onCard(dir));
  test::Client c{s};
  c.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
  for (int i = 0; i < 600 && !(c.state()["job"].is_null() && c.state()["dosing"].is_null()); ++i) s.step(5000);
  const json file = withoutBottle(c.ok("GET", "/api/v1/config/export"), "ph");
  const double ph = c.state()["stock"]["ph"].get<double>();
  c.ok("POST", "/api/v1/dose", {{"canister", "teil-a"}, {"ml", 3}});
  CHECK(c.call("POST", "/api/v1/config/import", file).first == 409);
  CHECK(c.state()["stock"]["ph"].get<double>() == doctest::Approx(ph));
  s.step(2000);
  CHECK(json::parse(onDisk(dir.path / "state.json"), nullptr, false)["stockMl"].contains("ph"));
}

TEST_CASE("Storage: a bottle new to the hub that comes with an import does not take over a stock left behind (#161)") {
  TempDir dir;
  sim::Simulation s(onFreshCard(dir));
  test::Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  const std::string one = c.ok("POST", "/api/v1/canisters", {{"name", "Eins"}, {"kind", "nutrient"}, {"capacityMl", 1000}, {"stockMl", 123}})["id"];
  const json file = c.ok("GET", "/api/v1/config/export");
  c.ok("DELETE", "/api/v1/canisters/" + one);
  s.step(2000);
  json state = json::parse(onDisk(dir.path / "state.json"), nullptr, false);
  state["stockMl"][one] = 77;  // left behind by an older version
  {
    std::ofstream out(dir.path / "state.json", std::ios::trunc);
    out << state.dump();
  }
  sim::Simulation again(onFreshCard(dir, s.hub().now() + 1));
  test::Client c2{again};
  c2.ok("POST", "/api/v1/auth/login", {{"password", "mein-passwort"}});
  c2.ok("POST", "/api/v1/config/import", file);
  CHECK(c2.state()["stock"][one].is_null());
}

TEST_CASE("Storage: after an import onto a fresh hub every stock is unknown and stays so through edits and a restart until entered (RAT-015, #184)") {
  json file;
  {
    sim::Simulation demo(test::opts("demo"));
    test::Client d{demo};
    d.ok("POST", "/api/v1/auth/login", {{"password", "demo-passwort"}});
    file = d.ok("GET", "/api/v1/config/export");
  }
  TempDir dir;
  sim::Simulation s(onFreshCard(dir));
  test::Client c{s};
  c.ok("POST", "/api/v1/auth/setup", {{"password", "mein-passwort"}});
  c.ok("POST", "/api/v1/config/import", file);
  const json stock = c.state()["stock"];
  REQUIRE(stock.contains("teil-a"));
  for (const auto& [id, ml] : stock.items()) {
    CAPTURE(id);
    CHECK(ml.is_null());
  }
  c.ok("POST", "/api/v1/canisters", {{"id", "teil-a"}, {"name", "Teil A neu"}});
  CHECK(c.state()["stock"]["teil-a"].is_null());
  s.step(2000);
  CHECK_FALSE(json::parse(onDisk(dir.path / "state.json"), nullptr, false)["stockMl"].contains("teil-a"));
  // Through a restart; then a bottle change gives it a number that stays
  sim::Simulation again(onFreshCard(dir, s.hub().now() + 1));
  test::Client c2{again};
  c2.ok("POST", "/api/v1/auth/login", {{"password", "mein-passwort"}});
  CHECK(c2.state()["stock"]["teil-a"].is_null());
  c2.ok("POST", "/api/v1/canisters", {{"id", "teil-a"}, {"color", "#336699"}});
  CHECK(c2.state()["stock"]["teil-a"].is_null());
  double full = 0;
  const json cfg = c2.ok("GET", "/api/v1/config");
  for (const auto& k : cfg["canisters"])
    if (k["id"] == "teil-a") full = k["capacityMl"].get<double>();
  REQUIRE(full > 0);
  c2.ok("POST", "/api/v1/canisters/teil-a/stock", {{"ml", full}});
  again.step(2000);
  sim::Simulation third(onFreshCard(dir, again.hub().now() + 1));
  test::Client c3{third};
  c3.ok("POST", "/api/v1/auth/login", {{"password", "mein-passwort"}});
  CHECK(c3.state()["stock"]["teil-a"].get<double>() == doctest::Approx(full));
}
