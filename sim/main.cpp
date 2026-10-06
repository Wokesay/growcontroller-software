// Host-Server des Simulators: liefert die Web-App aus, bindet die REST-API
// des Kerns an HTTP und schickt den Live-Zustand per Server-Sent Events.
// Auf dem Hub übernimmt esp_http_server diese Rolle (firmware/README.md).
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

#include <httplib.h>

#include "gc/embedded.hpp"
#include "scenario.hpp"

namespace {

std::atomic<bool> g_running{true};

void onSignal(int) { g_running = false; }

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

void usage() {
  std::cout << "growcontroller Simulator " << gc::embedded::kVersion << "\n"
            << "  --port N            HTTP-Port (8080)\n"
            << "  --host ADR          Adresse (127.0.0.1)\n"
            << "  --scenario NAME     demo | neu | stufe1 (demo)\n"
            << "  --data DIR          Daten dauerhaft ablegen (sonst nur im Speicher)\n"
            << "  --web DIR           gebaute Web-App (web/dist)\n"
            << "  --speed X           Zeitraffer (1)\n"
            << "  --password PW       Passwort für das Demo-Szenario\n"
            << "  --prefill H         Stunden Verlauf vorrechnen (48)\n"
            << "  --allow-reset       Szenario ohne Anmeldung wechseln (nur für automatische Tests)\n";
}

}  // namespace

int main(int argc, char** argv) {
  sim::Options opts;
  int port = 8080;
  std::string host = "127.0.0.1";
  std::string web = "web/dist";
  double speed = 1.0;
  bool allowReset = false;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) {
        usage();
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "--port") port = std::stoi(next());
    else if (a == "--host") host = next();
    else if (a == "--scenario") opts.scenario = next();
    else if (a == "--data") opts.dataDir = next();
    else if (a == "--web") web = next();
    else if (a == "--speed") speed = std::stod(next());
    else if (a == "--password") opts.password = next();
    else if (a == "--prefill") opts.prefillHours = std::stod(next());
    else if (a == "--allow-reset") allowReset = true;
    else if (a == "--help" || a == "-h") {
      usage();
      return 0;
    } else {
      std::cerr << "Unbekannte Option " << a << "\n";
      usage();
      return 2;
    }
  }

  std::cout << "Starte Simulator (Szenario " << opts.scenario << ") …" << std::flush;
  sim::Simulation simulation(opts);
  simulation.speed = speed;
  std::cout << " fertig.\n";

  httplib::Server svr;
  svr.set_default_headers({{"X-Content-Type-Options", "nosniff"},
                           {"X-Frame-Options", "DENY"},
                           {"Referrer-Policy", "no-referrer"},
                           {"Content-Security-Policy",
                            "default-src 'self'; img-src 'self' data:; style-src 'self' 'unsafe-inline'; "
                            "connect-src 'self'; frame-ancestors 'none'"}});
  svr.set_payload_max_length(1 << 20);
  // Herkunft prüfen, bevor irgendein Handler läuft (DNS-Rebinding, CSRF).
  svr.set_pre_routing_handler([&](const httplib::Request& req, httplib::Response& res) {
    const bool ok = gc::hostAllowed(req.get_header_value("Host"), host) &&
                    gc::writeAllowed(req.method, req.get_header_value("Sec-Fetch-Site"),
                                     req.get_header_value("Origin"), req.get_header_value("Host"));
    if (ok) return httplib::Server::HandlerResponse::Unhandled;
    res.status = 403;
    res.set_content(R"({"error":{"key":"api.origin","text":"Anfrage von fremder Herkunft abgelehnt"}})",
                    "application/json");
    return httplib::Server::HandlerResponse::Handled;
  });

  auto apiHandler = [&](const httplib::Request& req, httplib::Response& res) {
    gc::ApiResponse r;
    {
      std::lock_guard<std::recursive_mutex> l(simulation.mutex());
      r = simulation.api().handle(toApi(req));
    }
    res.status = r.status;
    for (const auto& [k, v] : r.headers) res.set_header(k, v);
    res.set_header("Cache-Control", "no-store");
    res.set_content(r.body, r.contentType);
  };

  // Simulator-Steuerung (nur hier, nie auf dem Gerät)
  svr.Get("/api/v1/sim", [&](const httplib::Request& req, httplib::Response& res) {
    std::lock_guard<std::recursive_mutex> l(simulation.mutex());
    if (!simulation.api().authorized(toApi(req))) {
      res.status = 401;
      res.set_content(R"({"error":{"key":"auth.required","text":"Bitte anmelden"}})", "application/json");
      return;
    }
    res.set_content(simulation.simState().dump(), "application/json");
  });
  svr.Post(R"(/api/v1/sim/(\w+))", [&](const httplib::Request& req, httplib::Response& res) {
    std::lock_guard<std::recursive_mutex> l(simulation.mutex());
    std::string action = req.matches[1];
    // Szenario wechseln ohne Anmeldung nur mit --allow-reset (Playwright)
    const bool open = action == "scenario" && allowReset;
    if (!open && !simulation.api().authorized(toApi(req))) {
      res.status = 401;
      res.set_content(R"({"error":{"key":"auth.required","text":"Bitte anmelden"}})", "application/json");
      return;
    }
    auto body = gc::json::parse(req.body.empty() ? "{}" : req.body, nullptr, false);
    if (body.is_discarded()) body = gc::json::object();
    auto out = simulation.control(action, body);
    res.status = out.contains("error") ? 422 : 200;
    res.set_content(out.dump(), "application/json");
  });

  // Live-Kanal: Server-Sent Events, je Sekunde der Zustand
  svr.Get("/api/v1/events/stream", [&](const httplib::Request& req, httplib::Response& res) {
    {
      std::lock_guard<std::recursive_mutex> l(simulation.mutex());
      if (!simulation.api().authorized(toApi(req))) {
        res.status = 401;
        return;
      }
    }
    std::string token = tokenOf(req);
    res.set_header("Cache-Control", "no-store");
    res.set_chunked_content_provider("text/event-stream", [&simulation, token](size_t, httplib::DataSink& sink) {
      std::string msg;
      {
        std::lock_guard<std::recursive_mutex> l(simulation.mutex());
        gc::ApiRequest r;
        r.token = token;
        if (!simulation.api().authorized(r)) return false;
        gc::json st = simulation.hub().state();
        st["sim"] = {{"speed", simulation.speed}, {"scenario", simulation.scenario()}};
        msg = "event: state\ndata: " + st.dump() + "\n\n";
      }
      if (!sink.write(msg.data(), msg.size())) return false;
      for (int i = 0; i < 10 && g_running; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
      return g_running.load();
    });
  });

  svr.Get(R"(/api/v1/.*)", apiHandler);
  svr.Post(R"(/api/v1/.*)", apiHandler);
  svr.Put(R"(/api/v1/.*)", apiHandler);
  svr.Patch(R"(/api/v1/.*)", apiHandler);
  svr.Delete(R"(/api/v1/.*)", apiHandler);

  // Web-App mit Rückfall auf index.html (Single-Page-App)
  bool haveWeb = std::filesystem::exists(std::filesystem::path(web) / "index.html");
  if (haveWeb) svr.set_mount_point("/", web);
  svr.Get(R"(/(?!api/).*)", [&](const httplib::Request&, httplib::Response& res) {
    if (!haveWeb) {
      res.set_content("Web-App nicht gebaut: cd web && npm ci && npm run build", "text/plain; charset=utf-8");
      return;
    }
    std::ifstream f(std::filesystem::path(web) / "index.html");
    std::stringstream ss;
    ss << f.rdbuf();
    res.set_content(ss.str(), "text/html; charset=utf-8");
  });

  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);

  // Takt: alle 100 ms echte Zeit, Simulationszeit × Zeitraffer
  std::thread loop([&] {
    int flushCounter = 0;
    while (g_running) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      {
        std::lock_guard<std::recursive_mutex> l(simulation.mutex());
        simulation.step(static_cast<gc::Ms>(100 * simulation.speed));
      }
      if (++flushCounter >= 50) {
        flushCounter = 0;
        std::lock_guard<std::recursive_mutex> l(simulation.mutex());
        simulation.storage().write("world.json", simulation.world().save().dump());
        simulation.storage().flush();
      }
    }
    svr.stop();
  });

  std::cout << "growcontroller Simulator " << gc::embedded::kVersion << " auf http://" << host << ":" << port << "\n";
  if (opts.scenario == "demo" && !simulation.demoPassword().empty())
    std::cout << "Demo-Passwort: " << simulation.demoPassword() << " (nur Simulator)\n";
  if (!haveWeb) std::cout << "Hinweis: Web-App nicht gefunden unter " << web << "\n";
  if (!svr.listen(host, port)) {
    std::cerr << "Port " << port << " nicht verfügbar\n";
    g_running = false;
  }
  g_running = false;
  loop.join();
  return 0;
}
