// SPDX-License-Identifier: AGPL-3.0-or-later
#include "gc/api.hpp"

#include <cctype>
#include <cstring>
#include <sstream>
#include <string_view>

#include "gc/embedded.hpp"
#include "gc/messages.hpp"

namespace gc {

namespace {

std::vector<std::string> split(const std::string& path) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : path) {
    if (c == '/') {
      if (!cur.empty()) out.push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

ApiResponse jsonResp(int status, const json& body) {
  ApiResponse r;
  r.status = status;
  r.body = body.dump();
  return r;
}

ApiResponse fromResult(const Result& r) { return jsonResp(r.status, r.body); }

ApiResponse fail(int status, const std::string& key, const std::string& text) {
  return jsonResp(status, {{"error", {{"key", key}, {"text", text}}}});
}

Epoch qnum(const ApiRequest& r, const char* k, Epoch def) {
  auto it = r.query.find(k);
  if (it == r.query.end() || it->second.empty()) return def;
  try {
    return std::stoll(it->second);
  } catch (...) {
    return def;
  }
}

std::string sessionCookie(const std::string& token, int maxAge) {
  return "gc_session=" + token + "; Path=/; HttpOnly; SameSite=Strict; Max-Age=" + std::to_string(maxAge);
}
// The browser keeps the cookie as long as the hub keeps an unused session (SD-064)
constexpr int kCookieMaxAgeS = static_cast<int>(Auth::kSessionIdle / kSecond);

}  // namespace

bool Api::authorized(const ApiRequest& req) {
  std::lock_guard<std::recursive_mutex> l(hub_.mutex());
  const Ms now = clock_.nowMs();
  if (req.token.empty() || !hub_.auth().check(req.token, now)) return false;
  hub_.auth().seen(req.client, now);  // a phone that stays signed in keeps its own count (SD-051)
  return true;
}

std::string sessionToken(const std::string& authorization, const std::string& cookie) {
  if (authorization.rfind("Bearer ", 0) == 0) return authorization.substr(7);
  // name=value pairs split by ';' (RFC 6265, 4.2.1); the name must match whole
  auto trim = [](std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
  };
  std::string_view rest = cookie;
  while (!rest.empty()) {
    auto semi = rest.find(';');
    std::string_view pair = rest.substr(0, semi);
    rest = semi == std::string_view::npos ? std::string_view{} : rest.substr(semi + 1);
    auto eq = pair.find('=');
    if (eq == std::string_view::npos) continue;
    if (trim(pair.substr(0, eq)) == "gc_session") return std::string(trim(pair.substr(eq + 1)));
  }
  return {};
}

bool hostAllowed(const std::string& hostHeader, const std::string& extraHost) {
  std::string h = hostHeader;
  if (h.empty()) return true;  // HTTP/1.0 ohne Host: kein Browser
  if (h.front() == '[') return h.find(']') != std::string::npos;  // IPv6-Literal
  auto colon = h.rfind(':');
  if (colon != std::string::npos) h.resize(colon);
  for (auto& ch : h) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  if (h == "localhost" || (!extraHost.empty() && h == extraHost)) return true;
  // Namen, die es nur im Heimnetz gibt (mDNS, Router); öffentliche Domains nie.
  for (const char* suffix : {".local", ".lan", ".home.arpa", ".internal", ".fritz.box"}) {
    const size_t n = std::strlen(suffix);
    if (h.size() > n && h.compare(h.size() - n, n, suffix) == 0) return true;
  }
  bool ipv4 = !h.empty();
  for (char ch : h) ipv4 = ipv4 && (std::isdigit(static_cast<unsigned char>(ch)) || ch == '.');
  return ipv4;
}

bool writeAllowed(const std::string& method, const std::string& secFetchSite, const std::string& origin,
                  const std::string& hostHeader) {
  if (method == "GET" || method == "HEAD" || method == "OPTIONS") return true;
  if (!secFetchSite.empty()) return secFetchSite == "same-origin" || secFetchSite == "none";
  if (!origin.empty()) {
    auto p = origin.find("://");
    return p != std::string::npos && origin.substr(p + 3) == hostHeader;
  }
  return true;
}

ApiResponse Api::handle(const ApiRequest& req) {
  try {
    return route(req);
  } catch (const json::exception&) {
    return fail(400, "api.bad_input", "Eingabe hat das falsche Format");
  } catch (const std::exception&) {
    return fail(500, "api.internal", "Interner Fehler – bitte als Problem melden");
  }
}

ApiResponse Api::route(const ApiRequest& req) {
  auto p = split(req.path);
  if (p.size() < 2 || p[0] != "api" || p[1] != "v1") return fail(404, "api.not_found", "Unbekannter Pfad");
  p.erase(p.begin(), p.begin() + 2);
  const std::string& m = req.method;
  auto is = [&](const std::string& method, std::initializer_list<const char*> parts) {
    if (method != m || parts.size() != p.size()) return false;
    size_t i = 0;
    for (const char* s : parts) {
      if (std::string(s) != "*" && p[i] != s) return false;
      ++i;
    }
    return true;
  };
  json body = json::object();
  if (!req.body.empty()) {
    body = json::parse(req.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) return fail(400, "api.json", "Ungültiges JSON");
  }

  // ---- öffentlich
  if (is("GET", {"info"})) return jsonResp(200, hub_.info());
  if (is("GET", {"auth", "session"})) {
    bool ok = authorized(req);
    std::lock_guard<std::recursive_mutex> l(hub_.mutex());
    ApiResponse r = jsonResp(200, {{"authenticated", ok}, {"hasPassword", hub_.auth().hasPassword()}});
    // A page left open asks now and then: its cookie lasts as long as the session is in use (SD-064)
    if (ok) r.headers.emplace_back("Set-Cookie", sessionCookie(req.token, kCookieMaxAgeS));
    return r;
  }
  if (is("POST", {"auth", "setup"})) {
    // Pflichtpasswort bei der Ersteinrichtung; danach nur noch mit Anmeldung änderbar.
    std::lock_guard<std::recursive_mutex> l(hub_.mutex());
    if (hub_.credentialsLost())
      return fail(423, "auth.lost", "Zugangsdaten fehlen, obwohl ein Passwort gesetzt war – Werksreset am Gerät nötig");
    const Auth before = hub_.auth();
    const Ms now = clock_.nowMs();
    Msg e = hub_.auth().setInitialPassword(jstr(body, "password"), jstr(body, "code"), now);
    const bool wrongCode = hub_.auth().wrongSetupCodes() > before.wrongSetupCodes();
    if (wrongCode && hub_.auth().logWrongSetupCode(now))
      hub_.logEvent("auth", "warn", say("ev.auth.code_wrong"), say("ev.auth.code_wrong.text", {{"count", hub_.auth().wrongSetupCodes()}}));
    if (!e.key.empty()) return fail(e.key == "auth.exists" ? 409 : wrongCode ? 403 : 422, e.key, e.text);
    // auth.json first, on disk; only then the lock against a second setup (#68).
    // Not saved: nothing is set, so setup stays possible once the card takes it.
    if (!hub_.saveAuth()) {
      hub_.auth() = before;
      return fromResult(Result::fail(500, say("store.failed")));
    }
    hub_.markPasswordSet();
    const std::int64_t wrong = hub_.auth().wrongSetupCodes();
    hub_.logEvent("auth", wrong > 0 ? "warn" : "info", say("ev.auth.password_set"),
                  wrong > 0 ? say("ev.auth.first_setup_wrong", {{"count", wrong}}) : say("ev.auth.first_setup"));
    Msg err;
    auto tok = hub_.auth().login(jstr(body, "password"), now, err, req.client);
    ApiResponse r = jsonResp(200, {{"ok", true}});
    if (tok) r.headers.emplace_back("Set-Cookie", sessionCookie(*tok, kCookieMaxAgeS));
    return r;
  }
  if (is("POST", {"auth", "login"})) {
    std::lock_guard<std::recursive_mutex> l(hub_.mutex());
    Msg err;
    auto tok = hub_.auth().login(jstr(body, "password"), clock_.nowMs(), err, req.client);
    if (!tok) {
      // Only a counted failure is logged; a try refused during a lockout is
      // not, so guessing cannot wear out the flash or flush the log (SD-051).
      if (err.key == "auth.wrong") hub_.logEvent("auth", "warn", say("ev.auth.failed"), err);
      return fail(err.key == "auth.locked" ? 429 : 401, err.key, err.text);
    }
    hub_.logEvent("auth", "info", say("ev.auth.signed_in"));
    ApiResponse r = jsonResp(200, {{"ok", true}});
    r.headers.emplace_back("Set-Cookie", sessionCookie(*tok, kCookieMaxAgeS));
    return r;
  }
  if (is("POST", {"auth", "logout"})) {
    std::lock_guard<std::recursive_mutex> l(hub_.mutex());
    hub_.auth().logout(req.token);
    ApiResponse r = jsonResp(200, {{"ok", true}});
    r.headers.emplace_back("Set-Cookie", sessionCookie("", 0));
    return r;
  }

  // ---- ab hier nur angemeldet (EN 18031-1 ACM)
  if (!authorized(req)) return fail(401, "auth.required", "Bitte anmelden");

  if (is("PUT", {"auth", "password"})) {
    std::lock_guard<std::recursive_mutex> l(hub_.mutex());
    const Auth before = hub_.auth();
    Msg e = hub_.auth().changePassword(jstr(body, "old"), jstr(body, "new"), clock_.nowMs(), req.client);
    if (!e.key.empty()) {
      if (e.key == "auth.wrong") hub_.logEvent("auth", "warn", say("ev.auth.failed"), e);  // counted like a sign-in
      return fail(e.key == "auth.locked" ? 429 : 422, e.key, e.text);
    }
    if (!hub_.saveAuth()) {  // the old password and the sessions stay, in memory as on disk
      hub_.auth() = before;
      return fromResult(Result::fail(500, say("store.failed")));
    }
    hub_.logEvent("auth", "notice", say("ev.auth.password_changed"), say("ev.auth.password_changed.text"));
    return jsonResp(200, {{"ok", true}});
  }
  if (is("GET", {"state"})) return jsonResp(200, hub_.state());
  if (is("GET", {"config"})) return jsonResp(200, hub_.configJson());
  if (is("GET", {"config", "export"})) {
    ApiResponse r = jsonResp(200, hub_.configJson());
    r.headers.emplace_back("Content-Disposition", "attachment; filename=\"growcontroller-konfiguration.json\"");
    return r;
  }
  if (is("POST", {"config", "import"})) return fromResult(hub_.importConfig(body));
  if (is("GET", {"catalog"})) return jsonResp(200, hub_.catalog().forApi());
  if (is("GET", {"changelog"})) {
    ApiResponse r;
    r.contentType = "text/markdown; charset=utf-8";
    r.body = embedded::kChangelogMd;
    return r;
  }
  if (is("GET", {"history"})) {
    Epoch now = hub_.now();
    auto it = req.query.find("series");
    if (it == req.query.end()) return fail(422, "history.series", "series fehlt");
    Epoch from = qnum(req, "from", now - 24 * 3600), to = qnum(req, "to", now);
    size_t points = static_cast<size_t>(std::max<Epoch>(10, std::min<Epoch>(qnum(req, "points", 600), 4000)));
    json out = json::array();
    std::stringstream ss(it->second);
    std::string s;
    while (std::getline(ss, s, ',')) out.push_back(hub_.history(s, from, to, points));
    return jsonResp(200, {{"from", from}, {"to", to}, {"series", out}});
  }
  if (is("GET", {"events"})) {
    Epoch now = hub_.now();
    auto t = req.query.find("type");
    return jsonResp(200, hub_.events(qnum(req, "from", 0), qnum(req, "to", now), t == req.query.end() ? "" : t->second,
                                     static_cast<size_t>(std::min<Epoch>(qnum(req, "limit", 200), 2000))));
  }
  if (is("GET", {"export.csv"})) {
    Epoch now = hub_.now();
    std::vector<std::string> series;
    auto it = req.query.find("series");
    std::stringstream ss(it == req.query.end() ? "tank.ph,tank.ec,tank.water_temp,tank.level" : it->second);
    std::string s;
    while (std::getline(ss, s, ',')) series.push_back(s);
    ApiResponse r;
    r.contentType = "text/csv; charset=utf-8";
    r.body = hub_.exportCsv(series, qnum(req, "from", now - 7 * 24 * 3600), qnum(req, "to", now));
    r.headers.emplace_back("Content-Disposition", "attachment; filename=\"growcontroller-verlauf.csv\"");
    return r;
  }
  if (is("GET", {"diagnostics"})) return jsonResp(200, hub_.diagnostics());

  if (is("POST", {"setup", "complete"})) return fromResult(hub_.completeSetup());
  if (is("PUT", {"system"})) return fromResult(hub_.setSystem(body));
  if (is("POST", {"devices", "*", "accept"})) return fromResult(hub_.acceptDevice(p[1], jstr(body, "name")));
  if (is("PATCH", {"devices", "*"})) return fromResult(hub_.renameDevice(p[1], jstr(body, "name")));
  if (is("DELETE", {"devices", "*"})) return fromResult(hub_.removeDevice(p[1]));
  if (is("PUT", {"roles", "*"})) {
    double ch = jnum(body, "channel", 0);
    if (!isNum(ch) || ch < 0 || ch > 15) return fail(422, "role.channel", "Kanal ungültig");
    return fromResult(hub_.bindRole(p[1], jstr(body, "device"), static_cast<int>(ch)));
  }
  if (is("DELETE", {"roles", "*"})) return fromResult(hub_.unbindRole(p[1]));
  if (is("POST", {"roles", "*", "test"})) return fromResult(hub_.testRole(p[1]));
  if (is("POST", {"roles", "*", "switch"})) {
    if (!body.contains("on") || !body["on"].is_boolean()) return fail(422, "role.switch", "„on“ muss true oder false sein");
    return fromResult(hub_.switchRole(p[1], body["on"].get<bool>()));
  }
  if (is("PUT", {"tank"})) return fromResult(hub_.putTank(body));
  if (is("PUT", {"zone"})) return fromResult(hub_.putZone(body));
  if (is("POST", {"canisters"})) return fromResult(hub_.putCanister(body));
  if (is("DELETE", {"canisters", "*"})) return fromResult(hub_.deleteCanister(p[1]));
  if (is("POST", {"canisters", "*", "stock"})) return fromResult(hub_.setStock(p[1], jnum(body, "ml")));
  if (is("POST", {"recipes"})) return fromResult(hub_.putRecipe(body));
  if (is("POST", {"recipes", "template"}))
    return fromResult(hub_.applyRecipeTemplate(jstr(body, "id"), body.contains("map") ? body["map"] : json::object(), jstr(body, "lang")));
  if (is("DELETE", {"recipes", "*"})) return fromResult(hub_.deleteRecipe(p[1]));
  if (is("PATCH", {"functions", "*"})) return fromResult(hub_.putFunction(p[1], body));

  if (is("POST", {"mix", "plan"})) return fromResult(hub_.mixPlan(body));
  if (is("POST", {"mix", "start"})) return fromResult(hub_.mixStart(body));
  if (is("POST", {"dose"})) return fromResult(hub_.manualDose(jstr(body, "canister"), jnum(body, "ml")));
  if (is("POST", {"pumps", "*", "calibrate"})) return fromResult(hub_.startCalibration(p[1], jnum(body, "seconds", 30)));
  if (is("POST", {"pumps", "*", "prime"})) return fromResult(hub_.prime(p[1], jnum(body, "seconds", 5)));
  if (is("POST", {"jobs", "*", "continue"})) return fromResult(hub_.jobContinue(p[1]));
  if (is("POST", {"jobs", "*", "abort"})) return fromResult(hub_.jobAbort(p[1]));
  if (is("POST", {"jobs", "*", "resume"})) return fromResult(hub_.jobResume(p[1]));
  if (is("POST", {"jobs", "*", "result"})) return fromResult(hub_.calibrationResult(p[1], jnum(body, "ml")));
  if (is("POST", {"probe"})) return fromResult(hub_.probeCalibration(body));
  if (is("POST", {"latches", "*", "ack"})) return fromResult(hub_.ackLatch(p[1]));
  if (is("POST", {"stop"})) return fromResult(hub_.stop(say("stop.app")));
  if (is("POST", {"resume"})) return fromResult(hub_.resume());
  if (is("POST", {"maintenance"})) return fromResult(hub_.maintenance(jnum(body, "minutes")));
  if (is("POST", {"measure"})) return fromResult(hub_.manualMeasure(body));
  if (is("POST", {"grow", "start"})) return fromResult(hub_.growStart(body));
  if (is("POST", {"grow", "next"})) return fromResult(hub_.growNextPhase());
  if (is("POST", {"grow", "harvest"})) return fromResult(hub_.growHarvest());
  if (is("POST", {"grow", "complete"})) return fromResult(hub_.growComplete());

  if (p.size() >= 1 && p[0] == "update") {
    IUpdater* u = hub_.updater();
    if (!u) return fail(501, "update.none", "Updates auf dieser Plattform nicht verfügbar");
    if (is("GET", {"update"})) return jsonResp(200, u->status());
    if (is("POST", {"update", "check"})) return jsonResp(200, u->check());
    // Nur im sicheren Zustand: keine Dosierung, kein Auftrag, kein offener Zulauf
    // (Kundensicht, regulatorik). Gilt für Installation und Rückkehr.
    auto busy = [&] {
      json st = hub_.state();
      const json& job = st["job"];
      const bool jobActive = !job.is_null() && jstr(job, "state") != "failed";
      return jobActive || !st["dosing"].is_null() || jbool(st["outputs"], "tank.inlet", false);
    };
    if (is("POST", {"update", "install"})) {
      if (busy()) return fail(409, "update.busy", "Update wartet, bis keine Dosierung, kein Auftrag und kein Zulauf läuft");
      hub_.logEvent("system", "notice", say("ev.update.requested"), say("ev.plain", {{"text", jstr(body, "version")}}));
      return jsonResp(200, u->install(jstr(body, "version")));
    }
    if (is("POST", {"update", "rollback"})) {
      if (busy()) return fail(409, "update.busy", "Rückkehr wartet, bis keine Dosierung, kein Auftrag und kein Zulauf läuft");
      hub_.logEvent("system", "notice", say("ev.update.rollback"));
      return jsonResp(200, u->rollback());
    }
  }
  return fail(404, "api.not_found", "Unbekannter Pfad");
}

}  // namespace gc
