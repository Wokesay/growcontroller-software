// SPDX-License-Identifier: AGPL-3.0-or-later
// Read-only spike: the hub's core on a computer next to Home Assistant
// (docs/HOME_ASSISTANT.md). It reads the sensors picked in the web app, runs
// them through the sensor truth and serves the web app; it switches nothing.
// The HTTP part follows the simulator's server (sim/main.cpp).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>

#include <httplib.h>

#include "gc/api.hpp"
#include "gc/embedded.hpp"
#include "gc/hub.hpp"
#include "ha_assign.hpp"
#include "ha_bus.hpp"
#include "ha_client.hpp"
#include "scenario.hpp"  // sim::FileStorage

namespace {

std::atomic<bool> g_running{true};
void onSignal(int) { g_running = false; }

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

void randomBytes(std::uint8_t* p, size_t n) {
  static std::random_device rd;
  for (size_t i = 0; i < n; ++i) p[i] = static_cast<std::uint8_t>(rd());
}

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
            << "                      --config and --token-file are refused\n";
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
        std::cerr << a << ": not a number: " << v << "\n";
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
  if (!app && !std::filesystem::exists(config)) {
    std::cerr << config << ": file not found\n";
    return 2;
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
  for (const char* var : {tokenVar, app ? "HASSIO_TOKEN" : tokenVar}) {
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

  const gc::Catalog cat = ha::catalog();
  HostClock clock;
  sim::FileStorage store(data);
  if (!store.ready()) {  // a password set here must survive a restart (#68)
    std::cerr << "The data folder " << data << " cannot be written. "
              << (app ? "Restart the app; if it stays, reinstall it.\n" : "Choose a writable one with --data.\n");
    return 2;
  }
  ha::HaBus bus(mapping.entities);
  gc::Hub hub(cat, bus, store, clock, randomBytes);
  hub.setPlatform({{"kind", "home-assistant"}, {"simulated", false}, {"readOnly", true}});
  hub.boot();
  ha::adoptFromConfig(hub, bus);  // the sensors picked in the web app
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
  svr.Get(R"(/api/v1/.*)", apiHandler);
  svr.Post(R"(/api/v1/.*)", apiHandler);
  svr.Put(R"(/api/v1/.*)", apiHandler);
  svr.Patch(R"(/api/v1/.*)", apiHandler);
  svr.Delete(R"(/api/v1/.*)", apiHandler);
  const bool haveWeb = std::filesystem::exists(std::filesystem::path(web) / "index.html");
  if (haveWeb) svr.set_mount_point("/", web);
  svr.Get(R"(/(?!api/).*)", [&](const httplib::Request&, httplib::Response& res) {
    if (!haveWeb) {
      res.set_content("Web app not built: cd web && npm ci && npm run build", "text/plain; charset=utf-8");
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
      hub.tick();
    }
    svr.stop();
  });

  std::cout << "growcontroller on Home Assistant (read-only) " << gc::embedded::kVersion << ": http://" << host << ":" << port
            << "\nReading " << bus.entities().size() << " picked sensors from " << mapping.url << " every " << everyS << " s\n"
            << std::flush;
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
