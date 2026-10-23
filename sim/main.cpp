// SPDX-License-Identifier: AGPL-3.0-or-later
// Host-Server des Simulators: liefert die Web-App aus, bindet die REST-API
// des Kerns an HTTP und schickt den Live-Zustand per Server-Sent Events.
// Auf dem Hub übernimmt esp_http_server diese Rolle (firmware/README.md).
//
// Download-Fassung: Mit eingebetteter Web-App (GC_EMBEDDED_WEB) reicht eine
// Datei. Ohne Argumente gestartet (Doppelklick) läuft die Demo, die Daten
// liegen neben dem Programm, und der Browser öffnet sich.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

#include <httplib.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "gc/embedded.hpp"
#include "gc/messages.hpp"
#include "scenario.hpp"
#ifndef GC_EMBEDDED_WEB
#define GC_EMBEDDED_WEB 0
#endif
#if GC_EMBEDDED_WEB
#include "gc/web_assets.hpp"
#endif

namespace {

std::atomic<bool> g_running{true};

void onSignal(int) { g_running = false; }

std::string tokenOf(const httplib::Request& req) {
  return gc::sessionToken(req.get_header_value("Authorization"), req.get_header_value("Cookie"));
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

void usage() {
  std::cout << "growcontroller Simulator " << gc::embedded::kVersion << "\n"
            << "  --port N            HTTP port (8080)\n"
            << "  --host ADDR         address (127.0.0.1)\n"
            << "  --scenario NAME     demo | neu | stufe1 (demo)\n"
            << "  --data DIR          keep the data in this folder (else in memory only)\n"
            << "  --web DIR           built web app (web/dist)\n"
            << "  --speed X           time-lapse factor (1)\n"
            << "  --password PW       password for the demo scenario\n"
            << "  --prefill H         hours of history to compute ahead (48)\n"
            << "  --allow-reset       change the scenario without signing in (automated tests only)\n"
            << "  --open              open the browser\n"
            << "Without arguments: the demo, data next to the program, the browser opens.\n";
}

// The host of an address as this simulator writes it, http://<host>:<port>
// with nothing after: a name or IPv4 address of ASCII letters, digits, dots
// and dashes, or an IPv6 address in brackets. Empty for anything else: only
// such an address goes to a shell or to the console.
std::string hostOf(const std::string& url) {
  const std::string scheme = "http://";
  const size_t colon = url.rfind(':');
  if (url.compare(0, scheme.size(), scheme) != 0 || colon == std::string::npos || colon <= scheme.size()) return "";
  const std::string host = url.substr(scheme.size(), colon - scheme.size()), port = url.substr(colon + 1);
  auto digit = [](char c) { return c >= '0' && c <= '9'; };
  auto hex = [&](char c) { return digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); };
  auto nameChar = [&](char c) { return digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '-'; };
  if (port.empty() || port.size() > 5 || !std::all_of(port.begin(), port.end(), digit) || std::stoi(port) < 1 ||
      std::stoi(port) > 65535)
    return "";
  const bool v6 = host.size() > 2 && host.front() == '[' && host.back() == ']' &&
                  std::all_of(host.begin() + 1, host.end() - 1, [&](char c) { return hex(c) || c == ':'; });
  const bool name = !host.empty() && std::all_of(host.begin(), host.end(), nameChar);
  return v6 || name ? host : "";
}

// Opens the browser at the address (simulator only, never on the device) and
// returns once it has been asked. The address goes to a shell: only one that
// hostOf reads.
void openBrowserNow(const std::string& url) {
  if (hostOf(url).empty()) return;
#if defined(_WIN32)
  const std::string cmd = "start \"\" \"" + url + "\"";
#elif defined(__APPLE__)
  const std::string cmd = "open \"" + url + "\" >/dev/null 2>&1";
#else
  const std::string cmd = "xdg-open \"" + url + "\" >/dev/null 2>&1";
#endif
  int rc = std::system(cmd.c_str());
  (void)rc;
}

// The same in a moment, once the server listens
void openBrowser(const std::string& url) {
  std::thread([url] {
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    openBrowserNow(url);
  }).detach();
}

// One simulator per data folder (#311): two would load the same files and
// overwrite each other's. The first holds a lock on simulator.lock in the
// folder while it runs, which the system frees when the process ends, also
// after a crash, and writes its address into the file, so a second start can
// name it. The file stays: removing it at the end would let a start that has
// it open take a lock on a file no one else finds. A link under that name, or
// anything but a plain file with one name, is not followed, as FileStorage
// writes through none: it is left as it is. An address is taken only from a
// file of this user's. Without a lock (such a file, a file or folder it may
// not write, a file system without locks) the simulator still starts, and
// says so if it saves to the folder.
class FolderLock {
 public:
  enum class State { Held, Taken, None };

  explicit FolderLock(const std::filesystem::path& dir) : file_(dir / "simulator.lock") {
    std::error_code ec;
    // Created as FileStorage::ready creates it: for the owner only
    if (std::filesystem::create_directories(dir, ec) && !ec) {
#ifndef _WIN32
      std::error_code ignored;
      std::filesystem::permissions(dir, std::filesystem::perms::owner_all, ignored);
#endif
    }
    if (ec) {
      why_ = ec.message();
      return;
    }
#ifdef _WIN32
    // No sharing for writes: a second open for writing fails while this one holds it. The link
    // itself, not what it points to: refused below.
    h_ = CreateFileW(file_.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                     FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h_ != INVALID_HANDLE_VALUE) {
      created_ = GetLastError() != ERROR_ALREADY_EXISTS;
      if (!plain(h_)) {
        refuse();
        return;
      }
      state_ = State::Held;
      announce("");  // no address of an earlier run while this one starts
      return;
    }
    if (const DWORD err = GetLastError(); err != ERROR_SHARING_VIOLATION) {
      why_ = std::system_category().message(static_cast<int>(err));
      return;
    }
    state_ = State::Taken;
    HANDLE r = CreateFileW(file_.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (r == INVALID_HANDLE_VALUE) return;
    char buf[kMaxAddress + 1];
    DWORD n = 0;
    if (plain(r) && ReadFile(r, buf, sizeof buf, &n, nullptr) && n <= kMaxAddress) other_.assign(buf, n);
    CloseHandle(r);
#else
    // O_CLOEXEC: a browser started from here must not inherit the lock. O_NOFOLLOW: a link is not
    // followed; O_NONBLOCK: a planted pipe does not hold the start.
    // O_EXCL: also no link followed. Twice: a file removed between the two opens
    // (by a start that ran in memory only) is made anew.
    const int flags = O_RDWR | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK;
    for (int attempt = 0; attempt < 2 && fd_ < 0; ++attempt) {
      fd_ = ::open(file_.c_str(), flags | O_CREAT | O_EXCL, 0600);
      created_ = fd_ >= 0;
      if (fd_ < 0 && errno == EEXIST) fd_ = ::open(file_.c_str(), flags);
      if (fd_ >= 0 || errno != ENOENT) break;
    }
    if (fd_ < 0) {
      if (errno == ELOOP) refuse();
      else why_ = std::generic_category().message(errno);
      return;
    }
    struct stat st {};
    if (::fstat(fd_, &st) != 0 || !S_ISREG(st.st_mode) || st.st_nlink != 1) {
      refuse();
      return;
    }
    ::fchmod(fd_, S_IRUSR | S_IWUSR);  // only the owner writes the address in it
    if (::flock(fd_, LOCK_EX | LOCK_NB) == 0) {
      state_ = State::Held;
      announce("");  // no address of an earlier run while this one starts
      return;
    }
    if (errno == EWOULDBLOCK) {
      state_ = State::Taken;
      // Another user's file could name any page on this computer
      char buf[kMaxAddress + 1];
      const ssize_t n = st.st_uid == ::geteuid() ? ::pread(fd_, buf, sizeof buf, 0) : 0;
      if (n > 0 && n <= static_cast<ssize_t>(kMaxAddress)) other_.assign(buf, static_cast<size_t>(n));
    } else {
      why_ = std::generic_category().message(errno);
    }
    close();
#endif
  }
  ~FolderLock() { close(); }
  FolderLock(const FolderLock&) = delete;
  FolderLock& operator=(const FolderLock&) = delete;

  State state() const { return state_; }
  // Why there is no lock, if it is known
  const std::string& why() const { return why_; }
  // The address the simulator holding it wrote, if it is one: a whole line,
  // so a read while it writes is no address
  std::string other() const {
    if (other_.empty() || other_.back() != '\n') return "";
    const std::string url = other_.substr(0, other_.size() - 1);
    return hostOf(url).empty() ? std::string() : url;
  }

  // This simulator's address, for a second start
  void announce(const std::string& url) {
    const std::string line = url.empty() ? url : url + "\n";
#ifdef _WIN32
    if (h_ == INVALID_HANDLE_VALUE) return;
    DWORD n = 0;
    if (SetFilePointer(h_, 0, nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER && SetEndOfFile(h_))
      WriteFile(h_, line.data(), static_cast<DWORD>(line.size()), &n, nullptr);
#else
    if (fd_ < 0 || ::ftruncate(fd_, 0) != 0) return;
    const ssize_t n = ::pwrite(fd_, line.data(), line.size(), 0);
    (void)n;
#endif
  }

  // Memory only after all: a lock file this start made goes again (SD-074).
  // Removed while still held, so no start takes a lock on it meanwhile;
  // Windows removes no file that is open.
  void drop() {
    if (state_ != State::Held) return;
    std::error_code ec;
#ifdef _WIN32
    close();
    if (created_) std::filesystem::remove(file_, ec);
#else
    if (created_) std::filesystem::remove(file_, ec);
    close();
#endif
    state_ = State::None;
  }

 private:
  static constexpr size_t kMaxAddress = 256;

  void refuse() {
    why_ = "simulator.lock is a link or not a plain file, left as it is";
    close();
  }
#ifdef _WIN32
  static bool plain(HANDLE h) {
    BY_HANDLE_FILE_INFORMATION info;
    return GetFileInformationByHandle(h, &info) &&
           !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) && info.nNumberOfLinks == 1;
  }
#endif

  void close() {
#ifdef _WIN32
    if (h_ != INVALID_HANDLE_VALUE) CloseHandle(h_);
    h_ = INVALID_HANDLE_VALUE;
#else
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
#endif
  }

  std::filesystem::path file_;
  State state_ = State::None;
  bool created_ = false;  // this start made the file
  std::string other_, why_;
#ifdef _WIN32
  HANDLE h_ = INVALID_HANDLE_VALUE;
#else
  int fd_ = -1;
#endif
};

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);  // Umlaute in der Konsole
#endif
  sim::Options opts;
  int port = 8080;
  std::string host = "127.0.0.1";
  std::string web;
  double speed = 1.0;
  bool allowReset = false;
  bool open = false;
  if (argc == 1) {
    // Einfachstart (Doppelklick): Demo mit Daten neben dem Programm
    std::error_code ec;
    auto exe = std::filesystem::absolute(argv[0], ec);
    opts.scenario = "demo";
    opts.password = "demo-passwort";
    // Without the program's folder the working folder; without both, memory only (#213)
    std::error_code cwdErr;
    const std::filesystem::path base = ec ? std::filesystem::current_path(cwdErr) : exe.parent_path();
    if (cwdErr)
      std::cerr << "No folder for the data (" << cwdErr.message() << "). Running in memory only.\n";
    else
      opts.dataDir = (base / "growcontroller-daten").lexically_normal().string();
    open = true;
  }
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) {
        usage();
        std::exit(2);
      }
      return argv[++i];
    };
    // A value that is no number, or out of range, ends with a message (#213).
    // strtod and strtol read the C locale: nothing here sets another.
    auto number = [&](double lowest, double highest) {
      const std::string v = next();
      char* end = nullptr;
      const double n = std::strtod(v.c_str(), &end);
      if (v.empty() || *end != '\0' || !std::isfinite(n) || n < lowest || n > highest) {
        std::cerr << a << " must be a number from " << lowest << " to " << highest << ", not \"" << v << "\"\n";
        std::exit(2);
      }
      return n;
    };
    auto wholeNumber = [&](long lowest, long highest) {  // as gc_ha_server reads its numbers
      const std::string v = next();
      char* end = nullptr;
      const long n = std::strtol(v.c_str(), &end, 10);
      if (v.empty() || *end != '\0' || n < lowest || n > highest) {
        std::cerr << a << " must be a whole number from " << lowest << " to " << highest << ", not \"" << v << "\"\n";
        std::exit(2);
      }
      return static_cast<int>(n);
    };
    if (a == "--port") port = wholeNumber(1, 65535);
    else if (a == "--host") host = next();
    else if (a == "--scenario") opts.scenario = next();
    else if (a == "--data") opts.dataDir = next();
    else if (a == "--web") web = next();
    else if (a == "--speed") speed = number(0, sim::Simulation::kMaxSpeed);
    else if (a == "--password") opts.password = next();
    else if (a == "--prefill") opts.prefillHours = number(0, sim::Simulation::kMaxPrefillH);
    else if (a == "--allow-reset") allowReset = true;
    else if (a == "--open") open = true;
    else if (a == "--help" || a == "-h") {
      usage();
      return 0;
    } else {
      std::cerr << "Unknown option " << a << "\n";
      usage();
      return 2;
    }
  }

  // A second start on a data folder in use ends here, before it reads a
  // file, and names the first; started plainly or with --open it opens the
  // first one in the browser (#311). A folder it cannot read is left as it
  // is, without a lock: it runs in memory only (SD-074).
  std::optional<FolderLock> lock;
  if (!opts.dataDir.empty() && !sim::whyUnreadable(opts.dataDir)) lock.emplace(opts.dataDir);
  if (lock && lock->state() == FolderLock::State::Taken) {
    const std::string first = lock->other();
    if (first.empty()) {
      std::cout << "Another simulator is using this data folder: " << opts.dataDir << "\n";
    } else {
      std::cout << "Already running with this data folder: " << first << "\n";
      // Opened only on this computer or at the address asked for: anyone who can write the
      // folder could have put another one there
      const std::string at = hostOf(first);
      if (open && (at == "127.0.0.1" || at == "[::1]" || at == "localhost" || at == host)) openBrowserNow(first);
    }
    return 0;
  }

  std::cout << "Starting simulator (scenario " << opts.scenario << ") …" << std::flush;
  sim::Simulation simulation(opts);
  simulation.speed = speed;
  std::cout << " done.\n";
  if (lock && !simulation.storage().persistent()) lock->drop();
  if (lock && lock->state() == FolderLock::State::None && simulation.storage().persistent())
    std::cerr << "The data folder cannot be locked (" << lock->why() << "): a second simulator could use it too.\n";

  // Ist der Port belegt, die nächsten neun versuchen. Ein fehlgeschlagener
  // Bind legt einen httplib-Server still, darum je Versuch ein neuer. Ohne
  // SO_REUSEPORT, sonst bindet ein zweiter Simulator denselben Port, statt
  // auszuweichen: Windows exklusiv, Linux SO_REUSEADDR (meldet belegte Ports
  // trotzdem), macOS ohne Option (BSD erlaubte sonst 127.0.0.1 neben einem
  // Programm auf 0.0.0.0 und finge dessen Anfragen ab).
  std::unique_ptr<httplib::Server> server;
  int bound = -1;
  for (int p = port; p < port + 10 && p <= 65535 && bound < 0; ++p) {
    auto s = std::make_unique<httplib::Server>();
    s->set_socket_options([](socket_t sock) {
#if defined(_WIN32)
      httplib::set_socket_opt(sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, 1);
#elif defined(__APPLE__)
      (void)sock;
#else
      httplib::set_socket_opt(sock, SOL_SOCKET, SO_REUSEADDR, 1);
#endif
    });
    if (s->bind_to_port(host, p)) {
      bound = p;
      server = std::move(s);
    }
  }
  if (bound < 0) {
    std::cerr << "Ports " << port << "–" << port + 9 << " not available\n";
    return 1;
  }
  httplib::Server& svr = *server;
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
    res.set_content(gc::json{{"error", gc::say("api.origin")}}.dump(),
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
      res.set_content(gc::json{{"error", gc::say("auth.required")}}.dump(), "application/json");
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
      res.set_content(gc::json{{"error", gc::say("auth.required")}}.dump(), "application/json");
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

  // Web-App: Ordner (--web, Entwicklung) oder eingebettet (Download-Fassung),
  // jeweils mit Rückfall auf index.html (Single-Page-App).
  if (web.empty() && !GC_EMBEDDED_WEB) web = "web/dist";
  // A --web whose status cannot be read (no permission, a link loop) is no
  // web app, not a crash at the start.
  std::error_code webErr;
  const bool haveDir = !web.empty() && std::filesystem::exists(std::filesystem::path(web) / "index.html", webErr);
  if (webErr)
    std::cerr << "The web app in " << web << " cannot be read; the simulator "
              << (GC_EMBEDDED_WEB ? "serves its built-in one" : "runs without it") << ". Detail: " << webErr.message()
              << ".\n";
  const bool haveWeb = haveDir || GC_EMBEDDED_WEB;
  if (haveDir) svr.set_mount_point("/", web);
  svr.Get(R"(/(?!api/).*)", [&](const httplib::Request& req, httplib::Response& res) {
#if GC_EMBEDDED_WEB
    if (!haveDir) {
      const gc::WebAsset* hit = nullptr;
      const gc::WebAsset* index = nullptr;
      for (size_t i = 0; i < gc::kWebAssetCount; ++i) {
        if (req.path == gc::kWebAssets[i].path) hit = &gc::kWebAssets[i];
        if (std::string(gc::kWebAssets[i].path) == "/index.html") index = &gc::kWebAssets[i];
      }
      if (!hit) hit = index;
      if (hit) {
        res.set_header("Content-Encoding", "gzip");
        res.set_header("Cache-Control", hit == index ? "no-cache" : "public, max-age=31536000, immutable");
        res.set_content(std::string(reinterpret_cast<const char*>(hit->data), hit->size), hit->type);
        return;
      }
    }
#else
    (void)req;
#endif
    if (!haveDir) {
      res.set_content("Web app not built: cd web && npm ci && npm run build", "text/plain; charset=utf-8");
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
      }
    }
    svr.stop();
  });

  const std::string url = "http://" + (host == "0.0.0.0" ? std::string("127.0.0.1") : host) + ":" + std::to_string(bound);
  if (lock) lock->announce(url);
  std::cout << "growcontroller Simulator " << gc::embedded::kVersion << " running: " << url << "\n";
  if (opts.scenario == "demo" && !simulation.demoPassword().empty())
    std::cout << "Demo password: " << simulation.demoPassword() << " (simulator only)\n";
  if (simulation.storage().persistent()) std::cout << "Data: " << simulation.dataDir() << "\n";
  if (!haveWeb) std::cout << "Note: no web app found at " << web << "\n";
  std::cout << "To stop: press Ctrl+C or close this window.\n" << std::flush;
  if (open) openBrowser(url);
  svr.listen_after_bind();
  g_running = false;
  loop.join();
  return 0;
}
