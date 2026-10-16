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
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
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
#include "gc/messages.hpp"
#include "ha_assign.hpp"
#include "ha_bus.hpp"
#include "ha_client.hpp"
#include "scenario.hpp"  // sim::FileStorage, sim::randomBytes

namespace {

std::atomic<bool> g_running{true};
void onSignal(int) { g_running = false; }

// An address of this computer in the form the server reports a peer's:
// 127.x.x.x, written as short as it goes. The Ingress listener trusts its
// one peer to name Home Assistant's user, so only the Supervisor or this
// computer (the tests) can be that peer (SD-077).
bool loopbackV4(const std::string& a) {
  unsigned n[4];
  char rest = 0;
  if (std::sscanf(a.c_str(), "%3u.%3u.%3u.%3u%c", &n[0], &n[1], &n[2], &n[3], &rest) != 4) return false;
  if (n[0] != 127 || n[1] > 255 || n[2] > 255 || n[3] > 255) return false;
  return a == std::to_string(n[0]) + "." + std::to_string(n[1]) + "." + std::to_string(n[2]) + "." + std::to_string(n[3]);
}

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

// The hub's session: a bearer token, or the cookie gc_session by its exact name
std::string tokenOf(const httplib::Request& req) {
  return gc::sessionToken(req.get_header_value("Authorization"), req.get_header_value("Cookie"));
}

// The two ways into the Home Assistant app (SD-077): Ingress, where Home
// Assistant signed the user in and the Supervisor forwards the request, and
// the direct port, with the hub's own password. Elsewhere only Direct.
enum class Way { Direct, Ingress };

// The Supervisor, the only peer the Ingress listener answers
constexpr const char* kSupervisor = "172.30.32.2";

bool sameLetters(const std::string& a, const char* b) {
  const size_t n = std::strlen(b);
  if (a.size() != n) return false;
  for (size_t i = 0; i < n; ++i)
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
  return true;
}
// The one value of a header sent exactly once in exactly this spelling; a
// copy spelt otherwise, or a second one, gives none: the Supervisor replaces
// only its own spelling of what a browser sends (SD-077).
std::optional<std::string> soleHeader(const httplib::Request& req, const char* name) {
  std::optional<std::string> value;
  int copies = 0;
  bool exact = false;
  for (const auto& [k, v] : req.headers) {
    if (!sameLetters(k, name)) continue;
    ++copies;
    exact = k == name;
    value = v;
  }
  if (copies != 1 || !exact) return std::nullopt;
  return value;
}
// Whether a cleaned name shows anything: spaces of any width, the blank
// Hangul fillers and the blank Braille pattern count as nothing.
bool visible(const std::string& s) {
  for (size_t i = 0; i < s.size();) {
    const auto c = static_cast<unsigned char>(s[i]);
    const size_t n = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;  // well-formed after ha::printable
    char32_t cp = n == 1 ? c : c & (0xff >> (n + 1));
    for (size_t k = 1; k < n && i + k < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3f);
    i += n;
    const bool blank = cp == 0x20 || cp == 0xa0 || cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200a) || cp == 0x202f || cp == 0x205f ||
                       cp == 0x3000 || cp == 0x3164 || cp == 0x115f || cp == 0x1160 || cp == 0x2800 || cp == 0xffa0;
    if (!blank) return true;
  }
  return false;
}

// The user the Supervisor signed in: its ID of 32 lowercase hex digits and
// a name to show; nothing without a valid ID.
std::optional<gc::PlatformUser> ingressUser(const httplib::Request& req) {
  const auto id = soleHeader(req, "X-Remote-User-Id");
  if (!id || id->size() != 32 || !std::all_of(id->begin(), id->end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) || (c >= 'a' && c <= 'f'); }))
    return std::nullopt;
  std::string name;
  for (const char* header : {"X-Remote-User-Display-Name", "X-Remote-User-Name"}) {
    if (const auto v = soleHeader(req, header)) name = ha::printable(*v, 64);
    if (visible(name)) break;
    name.clear();  // nothing to read: the next, or one made from the ID
  }
  if (name.empty()) name = "Home Assistant " + id->substr(0, 8);
  return gc::PlatformUser{*id, name};
}

gc::ApiRequest toApi(const httplib::Request& req, Way way) {
  gc::ApiRequest r;
  r.method = req.method;
  r.path = req.path;
  for (const auto& [k, v] : req.params) r.query[k] = v;
  r.body = req.body;
  if (way == Way::Ingress)
    r.user = ingressUser(req);  // never the hub's session there
  else
    r.token = tokenOf(req);  // never Home Assistant's user there
  r.client = gc::clientKey(req.remote_addr);  // the socket's peer; never X-Forwarded-For (SD-051)
  return r;
}

// Port 8099 without a password for direct access: where the app is now and
// how to get this address back, in German and English (the app's languages)
std::string directNotice() {
  const std::string source = "https://github.com/Wokesay/growcontroller-software/tree/v" + std::string(gc::embedded::kVersion);
  return "<!doctype html><html lang=\"de\"><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width\">"
         "<meta name=\"color-scheme\" content=\"light dark\"><title>growcontroller</title><h1>growcontroller</h1>"
         "<p><strong>Öffne growcontroller in Home Assistant:</strong> in der Seitenleiste oder unter Einstellungen › Apps › "
         "growcontroller › Weboberfläche öffnen.</p>"
         "<p>Der direkte Zugang über diese Adresse ist aus. Ein Administrator von Home Assistant schaltet ihn ein: in growcontroller "
         "unter Einstellungen › Zugang ein Passwort festlegen. Danach geht auch dieses Lesezeichen wieder.</p>"
         "<p lang=\"en\"><strong>Open growcontroller in Home Assistant:</strong> in the sidebar, or under Settings › Apps › "
         "growcontroller › Open web UI.</p>"
         "<p lang=\"en\">Direct access at this address is off. A Home Assistant administrator switches it on: in growcontroller, "
         "set a password under Settings › Access. Then this bookmark works again.</p>"
         "<p><a href=\"" + source + "\">Quellcode / Source code</a> (AGPL-3.0-or-later)</p></html>";
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
            << "  --app               as a Home Assistant app (SD-034, SD-077): Home Assistant at http://supervisor/core with the\n"
            << "                      Supervisor's token (SUPERVISOR_TOKEN), data in /data/hub, the web app from /web;\n"
            << "                      opened through Home Assistant's Ingress on --ingress-port, with Home Assistant's login,\n"
            << "                      and directly on --port (8099) only with a password for direct access set through\n"
            << "                      Ingress (--data, --host, --web, --port still apply); --config and --token-file are\n"
            << "                      refused; started as root, it hands the --data folder to user 65532 and switches to\n"
            << "                      that user for good (#80)\n"
            << "  --ingress-port N    with --app: the Ingress listener's port (8100)\n"
            << "  --ingress-from ADDR with --app: the only peer the Ingress listener answers (the Supervisor, 172.30.32.2; 127.x.x.x for tests)\n"
            << "Without --app, until a password is set, the start prints a setup code that the first password needs (#79).\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::string config, tokenFile, data, host, web;
  int port = 0;
  int ingressPort = 0;
  std::string ingressFrom;
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
    else if (a == "--ingress-port") ingressPort = number();
    else if (a == "--ingress-from") ingressFrom = next();
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
  if (ingressPort == 0) ingressPort = 8100;
  if (ingressFrom.empty()) ingressFrom = kSupervisor;
  if (!app && (ingressPort != 8100 || ingressFrom != kSupervisor)) {
    std::cerr << "--ingress-port and --ingress-from go with --app\n";
    return 2;
  }
  if (app && ingressPort == port) {
    std::cerr << "--ingress-port must differ from --port\n";
    return 2;
  }
  if (ingressFrom != kSupervisor && !loopbackV4(ingressFrom)) {
    std::cerr << "--ingress-from takes the Supervisor's address (172.30.32.2) or one of this computer's (127.x.x.x)\n";
    return 2;
  }
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
  gc::json platform = {{"kind", "home-assistant"}, {"simulated", false}, {"readOnly", true}};
  if (app) platform["signIn"] = "home-assistant";  // the web app's way in: Home Assistant's, through Ingress (SD-077)
  hub.setPlatform(platform);
  hub.boot();
  // Also here, where who runs it looks: the hub's alarm needs a sign-in (SD-081)
  for (const std::string& name : hub.unreadableFiles())
    std::cerr << (std::filesystem::path(data) / name).string() << ": cannot be read (" << store.readError(name).value_or("unknown")
              << "), so it is kept as it is and not written over. Check its owner and permissions, or the disk, then restart.\n";
  ha::adoptFromConfig(hub, bus);  // the sensors picked in the web app
  // Without --app the first password needs a code that only who reads this
  // output sees (#79). As an app there is no first password over the
  // network: Home Assistant signs users in through Ingress, and the
  // password for direct access is set there (SD-077).
  const std::string setupCode = app ? std::string() : hub.armSetupCode();
  // A sign of life holds 60 s (SD-042): reading more seldom than every 10 s
  // would leave a healthy sensor read every 30 s too little margin
  if (everyS > 10)
    std::cerr << "--every " << everyS << ": a sensor with a sign of life may show as outdated; read every 10 s or more often\n";
  gc::Api api(hub, clock);
  api.platformSignIn(app);
  if (app) api.cookiePath("/api/v1/");
  ha::Poller poller(bus, mapping.url, token, [&clock] { return clock.nowMs(); });

  std::error_code webErr;  // a folder it may not read is no web app, not a crash at the start (#80)
  const bool haveWeb = std::filesystem::exists(std::filesystem::path(web) / "index.html", webErr);
  if (webErr) std::cerr << "The web app in " << web << " cannot be read; growcontroller runs without it. Detail: " << webErr.message() << ".\n";

  const auto jsonError = [](httplib::Response& res, int status, const char* key, const char* text) {
    res.status = status;
    res.set_header("Cache-Control", "no-store");
    res.set_content(gc::json{{"error", {{"key", key}, {"text", text}}}}.dump(), "application/json");
  };
  std::mutex assigning;  // one pick at a time, also from two browser tabs
  // Both listeners serve the same hub; how a request is signed in is the
  // listener's, never the request's say (SD-077).
  const auto serve = [&](httplib::Server& svr, Way way) {
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
    if (way == Way::Ingress)  // shown in Home Assistant's frame, on its origin
      svr.set_default_headers({{"X-Content-Type-Options", "nosniff"},
                               {"X-Frame-Options", "SAMEORIGIN"},
                               {"Referrer-Policy", "no-referrer"},
                               {"Content-Security-Policy",
                                "default-src 'self'; img-src 'self' data:; style-src 'self' 'unsafe-inline'; connect-src 'self'; "
                                "object-src 'none'; base-uri 'none'; form-action 'self'; frame-ancestors 'self'"}});
    else
      svr.set_default_headers({{"X-Content-Type-Options", "nosniff"},
                               {"X-Frame-Options", "DENY"},
                               {"Referrer-Policy", "no-referrer"},
                               {"Content-Security-Policy",
                                "default-src 'self'; img-src 'self' data:; style-src 'self' 'unsafe-inline'; connect-src 'self'; "
                                "object-src 'none'; base-uri 'none'; form-action 'self'; frame-ancestors 'none'"}});
    svr.set_payload_max_length(1 << 20);
    svr.set_pre_routing_handler([&, way](const httplib::Request& req, httplib::Response& res) {
      const std::string hostHeader = req.get_header_value("Host");
      if (way == Way::Ingress) {
        // Only the Supervisor, by the socket's peer, never by a header. The
        // browser's host name may be any of Home Assistant's (a remote one
        // too); only its form is checked. Everything but /info, which the
        // Supervisor's watchdog reads, needs the user it signed in.
        const bool fromSupervisor = gc::clientKey(req.remote_addr) == ingressFrom;
        const bool hostForm = hostHeader.size() <= 255 &&
                              std::none_of(hostHeader.begin(), hostHeader.end(), [](char c) { return static_cast<unsigned char>(c) <= 0x20 || c == 0x7f; });
        if (!fromSupervisor || !hostForm) {
          jsonError(res, 403, "api.ingress", "Only Home Assistant's Supervisor reaches this port");
          return httplib::Server::HandlerResponse::Handled;
        }
        if (!(req.method == "GET" && req.path == "/api/v1/info") && !ingressUser(req)) {
          const gc::Msg m = gc::say("api.ingress_user");
          jsonError(res, 403, m.key.c_str(), m.text.c_str());
          return httplib::Server::HandlerResponse::Handled;
        }
        if (!gc::writeAllowed(req.method, req.get_header_value("Sec-Fetch-Site"), req.get_header_value("Origin"), hostHeader)) {
          jsonError(res, 403, "api.origin", "Request from a foreign origin refused");
          return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
      }
      if (!gc::hostAllowed(hostHeader, host) ||
          !gc::writeAllowed(req.method, req.get_header_value("Sec-Fetch-Site"), req.get_header_value("Origin"), hostHeader)) {
        jsonError(res, 403, "api.origin", "Request from a foreign origin refused");
        return httplib::Server::HandlerResponse::Handled;
      }
      if (app) {  // the direct port of the app: nothing but a notice until a password for it is set
        bool open;
        {
          std::lock_guard<std::recursive_mutex> l(hub.mutex());
          open = hub.auth().hasPassword();
        }
        if (!open) {
          if (req.path.rfind("/api/", 0) == 0) {
            const gc::Msg m = gc::say("api.ingress_only");
            jsonError(res, 404, m.key.c_str(), m.text.c_str());
          } else {
            res.status = 200;
            res.set_header("Cache-Control", "no-store");
            res.headers.erase("Content-Security-Policy");  // one policy, the notice's own
            res.set_header("Content-Security-Policy", "default-src 'none'; frame-ancestors 'none'");
            res.set_content(directNotice(), "text/html; charset=utf-8");
          }
          return httplib::Server::HandlerResponse::Handled;
        }
      }
      return httplib::Server::HandlerResponse::Unhandled;
    });
    const auto apiHandler = [&, way](const httplib::Request& req, httplib::Response& res) {
      gc::ApiResponse r;
      {
        std::lock_guard<std::recursive_mutex> l(hub.mutex());
        r = api.handle(toApi(req, way));
      }
      res.status = r.status;
      for (const auto& [k, v] : r.headers) res.set_header(k, v);
      res.set_header("Cache-Control", "no-store");
      res.set_content(r.body, r.contentType);
    };
    svr.Get("/api/v1/events/stream", [&, way](const httplib::Request& req, httplib::Response& res) {
      const gc::ApiRequest who = toApi(req, way);
      {
        std::lock_guard<std::recursive_mutex> l(hub.mutex());
        if (!api.authorized(who)) {
          jsonError(res, 401, "auth.required", "Bitte anmelden");
          return;
        }
      }
      gc::ApiRequest again;  // asked again before every message: a session that ends ends the stream
      again.token = who.token;
      again.user = who.user;
      res.set_header("Cache-Control", "no-store");
      // A stream ends after 15 min, so its reconnect passes Home Assistant's
      // sign-in again (SD-077); the web app reconnects by itself.
      auto messages = std::make_shared<int>(0);
      res.set_chunked_content_provider("text/event-stream", [&hub, &api, again, messages](size_t, httplib::DataSink& sink) {
        if (++*messages > 900) return false;
        std::string msg;
        {
          std::lock_guard<std::recursive_mutex> l(hub.mutex());
          if (!api.authorized(again)) return false;
          msg = "event: state\ndata: " + hub.state().dump() + "\n\n";
        }
        if (!sink.write(msg.data(), msg.size())) return false;
        for (int i = 0; i < 10 && g_running; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return g_running.load();
      });
    });
    // Picking sensors from Home Assistant; registered before the hub's own API.
    const auto reply = [](httplib::Response& res, const gc::Result& r) {
      res.status = r.status;
      res.set_header("Cache-Control", "no-store");
      res.set_content(r.body.dump(), "application/json");
    };
    svr.Get("/api/v1/ha/candidates", [&, way, reply](const httplib::Request& req, httplib::Response& res) {
      reply(res, ha::candidatesRoute(api, bus, toApi(req, way)));
    });
    svr.Post("/api/v1/ha/assign", [&, way, reply](const httplib::Request& req, httplib::Response& res) {
      const gc::ApiRequest areq = toApi(req, way);
      if (!api.authorized(areq)) return reply(res, gc::Result::fail(401, "auth.required", "Bitte anmelden"));  // before any lock or write
      std::lock_guard<std::mutex> one(assigning);
      // The hub's configuration is on disk when the pick returns (#68)
      gc::Result r = ha::assignRoute(api, hub, bus, areq, [] { std::this_thread::sleep_for(std::chrono::milliseconds(100)); });
      if (r.status == 200) r.body = {{"ok", true}};
      reply(res, r);
    });
    svr.Post("/api/v1/ha/life", [&, way, reply](const httplib::Request& req, httplib::Response& res) {
      const gc::ApiRequest areq = toApi(req, way);
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
  };

  httplib::Server svr;  // the direct port
  httplib::Server ingress;  // only as an app: Home Assistant's Ingress (SD-077)
  serve(svr, Way::Direct);
  if (!svr.bind_to_port(host, port)) {
    std::cerr << "Port " << port << " not available\n";
    return 1;
  }
  if (app) {
    serve(ingress, Way::Ingress);
    // Without its Ingress listener the app does not start: the direct port
    // never stands in for it (SD-077)
    if (!ingress.bind_to_port(host, ingressPort)) {
      std::cerr << "growcontroller did not start: Port " << ingressPort << " for Home Assistant's Ingress is not available.\n" << startAgain;
      return 1;
    }
  }

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
    svr.stop();  // also before it listens: then it does not start (httplib)
    if (app) ingress.stop();
  });
  std::atomic<bool> ingressLost{false};
  std::thread ingressLoop;
  if (app) ingressLoop = std::thread([&] {
    ingress.listen_after_bind();
    if (g_running.exchange(false)) ingressLost = true;  // without Ingress the app is no use: it ends
  });

  std::cout << "growcontroller on Home Assistant (read-only) " << gc::embedded::kVersion << ": http://" << host << ":" << port << "\n";
  if (app) {
    bool direct;
    {
      std::lock_guard<std::recursive_mutex> l(hub.mutex());
      direct = hub.auth().hasPassword();
    }
    std::cout << "Opens in Home Assistant's sidebar (Ingress, port " << ingressPort << ", only from " << ingressFrom << "); port " << port
              << (direct ? " asks for the password for direct access\n" : " shows only a notice until a password for direct access is set\n");
  }
  std::cout << "Reading " << bus.entities().size() << " picked sensors from " << mapping.url << " every " << everyS << " s\n";
  if (!setupCode.empty())
    std::cout << "Setup code for the first password: " << setupCode << "\n"
              << "  Type it on growcontroller's page with your new password. Do not share it.\n"
              << "  It works once; every restart makes a new one.\n";
  std::cout << std::flush;
  svr.listen_after_bind();
  g_running = false;
  loop.join();
  if (ingressLoop.joinable()) ingressLoop.join();
  {  // save first: stopping the poller can wait for a slow Home Assistant
    std::lock_guard<std::recursive_mutex> l(hub.mutex());
    hub.flush();
  }
  poller.stop();
  if (ingressLost) {
    std::cerr << "growcontroller stopped: Home Assistant's Ingress port " << ingressPort << " stopped answering.\n";
    return 1;
  }
  return 0;
}
