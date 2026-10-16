// SPDX-License-Identifier: AGPL-3.0-or-later
// Read-only spike: the hub's core on a computer next to Home Assistant
// (docs/HOME_ASSISTANT.md). It reads the sensors picked in the web app, runs
// them through the sensor truth and serves the web app; it switches nothing.
// The HTTP part follows the simulator's server (sim/main.cpp).
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include <httplib.h>

#ifdef __linux__
#include <grp.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "gc/api.hpp"
#include "gc/embedded.hpp"
#include "gc/hub.hpp"
#include "ha_assign.hpp"
#include "ha_bus.hpp"
#include "ha_client.hpp"
#include "scenario.hpp"  // sim::FileStorage, sim::randomBytes

namespace {

std::atomic<bool> g_running{true};
void onSignal(int) { g_running = false; }

#ifdef __linux__
// As a Home Assistant app the server is started as root (#80). Before it
// opens its port it gives the data folder to an account of its own and
// becomes that account for good: no root, no capabilities, no way back,
// so a flaw in the web server cannot reach the other apps' traffic on Home
// Assistant's network. Port 8099 needs no privilege. "" or why it failed.
constexpr uid_t kAppUid = 65532;
constexpr gid_t kAppGid = 65532;
std::string dropRoot(std::string data) {
  namespace fs = std::filesystem;
  const auto sys = [](int e) { return std::error_code(e, std::generic_category()).message(); };
  while (data.size() > 1 && data.back() == '/') data.pop_back();  // "link/" would be followed
  std::error_code ec;
  if (fs::is_symlink(fs::symlink_status(data, ec))) return data + " is a link";  // what it points to is not ours
  if (fs::create_directories(data, ec) && !ec) {
    std::error_code ignored;  // a new folder is the owner's only, as the store makes it
    fs::permissions(data, fs::perms::owner_all, ignored);
  }
  if (ec) return "cannot create " + data + ": " + ec.message();
  // The folder and what is in it (the store keeps no subfolders), including
  // files a version before #80 or a restore from a backup left as root; a
  // link is changed itself, never what it points to
  if (lchown(data.c_str(), kAppUid, kAppGid) != 0) {
    const int e = errno;
    return "cannot hand " + data + " to its own account: " + sys(e);
  }
  for (fs::directory_iterator it(data, ec), end; !ec && it != end; it.increment(ec))
    if (lchown(it->path().c_str(), kAppUid, kAppGid) != 0) {
      const int e = errno;
      return "cannot hand " + it->path().string() + " to its own account: " + sys(e);
    }
  if (ec) return "cannot read " + data + ": " + ec.message();
  if (setgroups(0, nullptr) != 0 || setgid(kAppGid) != 0 || setuid(kAppUid) != 0) {
    const int e = errno;
    return "cannot switch to its own account: " + sys(e);
  }
  if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
    const int e = errno;
    return "cannot block new rights (no_new_privs): " + sys(e);
  }
  if (setuid(0) == 0 || geteuid() != kAppUid || getegid() != kAppGid) return "it could still become root again";
  // Now its owner: the folder is private again, whatever an earlier run left
  if (chmod(data.c_str(), 0700) != 0) {
    const int e = errno;
    return "cannot make " + data + " private: " + sys(e);
  }
  return {};
}
#endif

// Wall time from the computer. Assumption: its operating system keeps the
// time synced (NTP), so the time counts as secured (PD-073).
class HostClock : public gc::IClock {
 public:
  gc::Ms nowMs() const override {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_).count();
  }
  gc::Epoch epoch() const override {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  }
  bool secured() const override { return true; }

 private:
  std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

std::string tokenOf(const httplib::Request& req) {
  auto auth = req.get_header_value("Authorization");
  if (auth.rfind("Bearer ", 0) == 0) return auth.substr(7);
  auto cookie = req.get_header_value("Cookie");
  auto pos = cookie.find("gc_session=");
  if (pos == std::string::npos) return {};
  auto end = cookie.find(';', pos);
  return cookie.substr(pos + 11, end == std::string::npos ? std::string::npos : end - pos - 11);
}

gc::ApiRequest toApi(const httplib::Request& req) {
  gc::ApiRequest r;
  r.method = req.method;
  r.path = req.path;
  for (const auto& [k, v] : req.params) r.query[k] = v;
  r.body = req.body;
  r.token = tokenOf(req);
  r.client = gc::clientKey(req.remote_addr);  // the socket's peer; never X-Forwarded-For (SD-051)
  return r;
}

std::string readFile(const std::string& path) {
  std::ifstream f(path);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

void usage() {
  std::cout << "growcontroller on Home Assistant (read-only spike) " << gc::embedded::kVersion << "\n"
            << "  --config FILE       {\"url\": \"http://192.168.1.20:8123\"} (an IP, not .local; as an app use --app);\n"
            << "                      sensors are picked in the web app\n"
            << "  --token-file FILE   long-lived access token (or the environment variable GC_HA_TOKEN)\n"
            << "  --data DIR          where the hub keeps its files (growcontroller-ha-data)\n"
            << "  --port N            HTTP port (8090)\n"
            << "  --host ADDR         address (127.0.0.1)\n"
            << "  --web DIR           built web app (web/dist)\n"
            << "  --every S           seconds between two reads (5)\n"
            << "  --app               as a Home Assistant app (SD-034): Home Assistant at http://supervisor/core with the\n"
            << "                      Supervisor's token (SUPERVISOR_TOKEN), data in /data/hub, the web app from /web,\n"
            << "                      reachable in the network on port 8099 (--data, --host, --web, --port still apply);\n"
            << "                      --config and --token-file are refused; started as root, it hands the --data\n"
            << "                      folder to user 65532 and switches to that user for good (#80)\n"
            << "Until a password is set, the start prints a setup code that the first password needs (#79).\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::string config, tokenFile, data, host, web;
  int port = 0;
  int everyS = 5;
  bool app = false;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) {
        usage();
        std::exit(2);
      }
      return argv[++i];
    };
    auto number = [&]() {
      const std::string v = next();
      char* end = nullptr;
      const long n = std::strtol(v.c_str(), &end, 10);
      if (v.empty() || *end != '\0' || n <= 0 || n > 65535) {
        std::cerr << a << " must be a whole number from 1 to 65535, not \"" << v << "\"\n";
        std::exit(2);
      }
      return static_cast<int>(n);
    };
    if (a == "--config") config = next();
    else if (a == "--token-file") tokenFile = next();
    else if (a == "--data") data = next();
    else if (a == "--port") port = number();
    else if (a == "--host") host = next();
    else if (a == "--web") web = next();
    else if (a == "--every") everyS = number();
    else if (a == "--app") app = true;
    else if (a == "--help" || a == "-h") {
      usage();
      return 0;
    } else {
      std::cerr << "Unknown option " << a << "\n";
      usage();
      return 2;
    }
  }
  // Defaults. As a Home Assistant app (SD-034) the address and the token are
  // fixed below, so the Supervisor's token never goes anywhere but the Supervisor.
  if (data.empty()) data = app ? "/data/hub" : "growcontroller-ha-data";
  if (host.empty()) host = app ? "0.0.0.0" : "127.0.0.1";
  if (web.empty()) web = app ? "/web" : "web/dist";
  if (port == 0) port = app ? 8099 : 8090;
  if (app && (!config.empty() || !tokenFile.empty())) {
    std::cerr << "--app takes Home Assistant's address and token from the Supervisor; leave out --config and --token-file\n";
    return 2;
  }
  if (config.empty() && !app) {
    usage();
    return 2;
  }
  std::error_code configErr;  // a file it may not read is an error to name, not a crash at the start (#213)
  if (!app && !std::filesystem::exists(config, configErr)) {
    if (configErr)
      std::cerr << config << ": cannot be read (" << configErr.message() << ")\n";
    else
      std::cerr << config << ": file not found\n";
    return 2;
  }
  if (!app) {
    errno = 0;
    std::ifstream probe(config);  // there but not opened (no permission): say so, not "no JSON object"
    const int err = errno;
    if (!probe) {
      std::cerr << config << ": cannot be read (" << (err ? std::generic_category().message(err) : "cannot be opened") << ")\n";
      return 2;
    }
  }
  std::string err;
  const auto mapping = ha::parseMapping(app ? gc::json{{"url", "http://supervisor/core"}} : gc::json::parse(readFile(config), nullptr, false), err);
  if (!err.empty()) {
    std::cerr << (app ? "--app" : config) << ": " << err << "\n";
    return 2;
  }
  const char* tokenVar = app ? "SUPERVISOR_TOKEN" : "GC_HA_TOKEN";
  std::string token = tokenFile.empty() ? (std::getenv(tokenVar) ? std::getenv(tokenVar) : "") : readFile(tokenFile);
  // Not passed on to anything this process starts. The Supervisor also sets
  // the same token under its old name.
  std::vector<const char*> clear = {tokenVar};
  if (app) clear.push_back("HASSIO_TOKEN");
  for (const char* var : clear) {
#ifdef _WIN32
    _putenv_s(var, "");
#else
    unsetenv(var);
#endif
  }
  while (!token.empty() && (token.back() == '\n' || token.back() == '\r' || token.back() == ' ')) token.pop_back();
  if (token.empty()) {
    std::cerr << (app ? "No SUPERVISOR_TOKEN: start this as a Home Assistant app with homeassistant_api\n" : "No token: --token-file FILE or GC_HA_TOKEN\n");
    return 2;
  }

  // What to do when the app cannot start: never reinstall, that deletes its data
  const char* const startAgain =
      "Your data is kept. Start the app again. If this comes back, restore the backup from before the update "
      "and report this: https://github.com/Wokesay/growcontroller-software/issues\n";
#ifdef __linux__
  if (app && geteuid() == 0) {
    const std::string why = dropRoot(data);
    if (!why.empty()) {
      std::cerr << "growcontroller did not start: it could not give up its administrator rights (root). Detail: " << why << ".\n"
                << startAgain;
      return 2;
    }
  }
#endif
  const gc::Catalog cat = ha::catalog();
  HostClock clock;
  sim::FileStorage store(data);
  if (!store.ready()) {  // a password set here must survive a restart (#68)
    if (app)
      std::cerr << "growcontroller did not start: its data folder " << data << " cannot be written.\n" << startAgain;
    else
      std::cerr << "The data folder " << data << " cannot be written. Choose a writable one with --data.\n";
    return 2;
  }
  ha::HaBus bus(mapping.entities);
  gc::Hub hub(cat, bus, store, clock, sim::randomBytes);
  hub.setPlatform({{"kind", "home-assistant"}, {"simulated", false}, {"readOnly", true}});
  hub.boot();
  // Also here, where who runs it looks: the hub's alarm needs a sign-in (SD-081)
  for (const std::string& name : hub.unreadableFiles())
    std::cerr << (std::filesystem::path(data) / name).string() << ": cannot be read (" << store.readError(name).value_or("unknown")
              << "), so it is kept as it is and not written over. Check its owner and permissions, or the disk, then restart.\n";
  ha::adoptFromConfig(hub, bus);  // the sensors picked in the web app
  // The first password needs a code that only who reads this output sees: as
  // an app, Home Assistant's administrators in its log. Otherwise anyone in
  // the network could set it first (#79).
  const std::string setupCode = hub.armSetupCode();
  // A sign of life holds 60 s (SD-042): reading more seldom than every 10 s
  // would leave a healthy sensor read every 30 s too little margin
  if (everyS > 10)
    std::cerr << "--every " << everyS << ": a sensor with a sign of life may show as outdated; read every 10 s or more often\n";
  gc::Api api(hub, clock);
  ha::Poller poller(bus, mapping.url, token, [&clock] { return clock.nowMs(); });

  httplib::Server svr;
  // As the simulator's server: no shared port another program could bind too.
  svr.set_socket_options([](socket_t sock) {
#if defined(_WIN32)
    httplib::set_socket_opt(sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, 1);
#elif defined(__APPLE__)
    (void)sock;
#else
    httplib::set_socket_opt(sock, SOL_SOCKET, SO_REUSEADDR, 1);
#endif
  });
  if (!svr.bind_to_port(host, port)) {
    std::cerr << "Port " << port << " not available\n";
    return 1;
  }
  svr.set_default_headers({{"X-Content-Type-Options", "nosniff"},
                           {"X-Frame-Options", "DENY"},
                           {"Referrer-Policy", "no-referrer"},
                           {"Content-Security-Policy",
                            "default-src 'self'; img-src 'self' data:; style-src 'self' 'unsafe-inline'; "
                            "connect-src 'self'; frame-ancestors 'none'"}});
  svr.set_payload_max_length(1 << 20);
  svr.set_pre_routing_handler([&](const httplib::Request& req, httplib::Response& res) {
    const bool ok = gc::hostAllowed(req.get_header_value("Host"), host) &&
                    gc::writeAllowed(req.method, req.get_header_value("Sec-Fetch-Site"), req.get_header_value("Origin"),
                                     req.get_header_value("Host"));
    if (ok) return httplib::Server::HandlerResponse::Unhandled;
    res.status = 403;
    res.set_content(R"({"error":{"key":"api.origin","text":"Request from a foreign origin refused"}})", "application/json");
    return httplib::Server::HandlerResponse::Handled;
  });
  auto apiHandler = [&](const httplib::Request& req, httplib::Response& res) {
    gc::ApiResponse r;
    {
      std::lock_guard<std::recursive_mutex> l(hub.mutex());
      r = api.handle(toApi(req));
    }
    res.status = r.status;
    for (const auto& [k, v] : r.headers) res.set_header(k, v);
    res.set_header("Cache-Control", "no-store");
    res.set_content(r.body, r.contentType);
  };
  svr.Get("/api/v1/events/stream", [&](const httplib::Request& req, httplib::Response& res) {
    {
      std::lock_guard<std::recursive_mutex> l(hub.mutex());
      if (!api.authorized(toApi(req))) {
        res.status = 401;
        return;
      }
    }
    std::string tok = tokenOf(req);
    res.set_header("Cache-Control", "no-store");
    res.set_chunked_content_provider("text/event-stream", [&hub, &api, tok](size_t, httplib::DataSink& sink) {
      std::string msg;
      {
        std::lock_guard<std::recursive_mutex> l(hub.mutex());
        gc::ApiRequest r;
        r.token = tok;
        if (!api.authorized(r)) return false;
        msg = "event: state\ndata: " + hub.state().dump() + "\n\n";
      }
      if (!sink.write(msg.data(), msg.size())) return false;
      for (int i = 0; i < 10 && g_running; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
      return g_running.load();
    });
  });
  // Picking sensors from Home Assistant; registered before the hub's own API.
  auto reply = [](httplib::Response& res, const gc::Result& r) {
    res.status = r.status;
    res.set_header("Cache-Control", "no-store");
    res.set_content(r.body.dump(), "application/json");
  };
  svr.Get("/api/v1/ha/candidates", [&](const httplib::Request& req, httplib::Response& res) {
    reply(res, ha::candidatesRoute(api, bus, toApi(req)));
  });
  std::mutex assigning;  // one pick at a time, also from two browser tabs
  svr.Post("/api/v1/ha/assign", [&](const httplib::Request& req, httplib::Response& res) {
    const gc::ApiRequest areq = toApi(req);
    if (!api.authorized(areq)) return reply(res, gc::Result::fail(401, "auth.required", "Bitte anmelden"));  // before any lock or write
    std::lock_guard<std::mutex> one(assigning);
    // The hub's configuration is on disk when the pick returns (#68)
    gc::Result r = ha::assignRoute(api, hub, bus, areq, [] { std::this_thread::sleep_for(std::chrono::milliseconds(100)); });
    if (r.status == 200) r.body = {{"ok", true}};
    reply(res, r);
  });
  svr.Post("/api/v1/ha/life", [&](const httplib::Request& req, httplib::Response& res) {
    const gc::ApiRequest areq = toApi(req);
    if (!api.authorized(areq)) return reply(res, gc::Result::fail(401, "auth.required", "Bitte anmelden"));
    std::lock_guard<std::mutex> one(assigning);
    gc::Result r = ha::lifeRoute(api, hub, bus, areq);
    if (r.status == 200) r.body = {{"ok", true}};
    reply(res, r);
  });
  svr.Get(R"(/api/v1/.*)", apiHandler);
  svr.Post(R"(/api/v1/.*)", apiHandler);
  svr.Put(R"(/api/v1/.*)", apiHandler);
  svr.Patch(R"(/api/v1/.*)", apiHandler);
  svr.Delete(R"(/api/v1/.*)", apiHandler);
  std::error_code webErr;  // a folder it may not read is no web app, not a crash at the start (#80)
  const bool haveWeb = std::filesystem::exists(std::filesystem::path(web) / "index.html", webErr);
  if (webErr) std::cerr << "The web app in " << web << " cannot be read; growcontroller runs without it. Detail: " << webErr.message() << ".\n";
  if (haveWeb) svr.set_mount_point("/", web);
  svr.Get(R"(/(?!api/).*)", [&](const httplib::Request&, httplib::Response& res) {
    if (!haveWeb) {
      res.set_content(app ? "growcontroller's web app cannot be read. Start the app again; if this stays, report it: "
                            "https://github.com/Wokesay/growcontroller-software/issues"
                          : "Web app not built: cd web && npm ci && npm run build",
                      "text/plain; charset=utf-8");
      return;
    }
    res.set_content(readFile((std::filesystem::path(web) / "index.html").string()), "text/html; charset=utf-8");
  });

  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);
  poller.start(std::chrono::seconds(everyS));
  // The hub writes what it changes as it goes (#68): small files whole,
  // history and events appended, both whole once a day.
  std::thread loop([&] {
    while (g_running) {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      std::lock_guard<std::recursive_mutex> l(hub.mutex());
      ha::syncLife(hub, bus);  // an import may have changed a sign of life (SD-042)
      hub.tick();
    }
    svr.stop();
  });

  std::cout << "growcontroller on Home Assistant (read-only) " << gc::embedded::kVersion << ": http://" << host << ":" << port
            << "\nReading " << bus.entities().size() << " picked sensors from " << mapping.url << " every " << everyS << " s\n";
  if (!setupCode.empty())
    std::cout << "Setup code for the first password: " << setupCode << "\n"
              << "  Type it on growcontroller's page with your new password. Do not share it.\n"
              << "  It works once; every restart makes a new one.\n";
  std::cout << std::flush;
  svr.listen_after_bind();
  g_running = false;
  loop.join();
  {  // save first: stopping the poller can wait for a slow Home Assistant
    std::lock_guard<std::recursive_mutex> l(hub.mutex());
    hub.flush();
  }
  poller.stop();
  return 0;
}
