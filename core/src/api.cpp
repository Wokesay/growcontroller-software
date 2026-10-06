#include "gc/api.hpp"

#include <sstream>

#include "gc/embedded.hpp"

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

}  // namespace

bool Api::authorized(const ApiRequest& req) {
  std::lock_guard<std::recursive_mutex> l(hub_.mutex());
  return !req.token.empty() && hub_.auth().check(req.token, clock_.nowMs());
}

ApiResponse Api::handle(const ApiRequest& req) {
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
    return jsonResp(200, {{"authenticated", ok}, {"hasPassword", hub_.auth().hasPassword()}});
  }
  if (is("POST", {"auth", "setup"})) {
    // Pflichtpasswort bei der Ersteinrichtung; danach nur noch mit Anmeldung änderbar.
    std::lock_guard<std::recursive_mutex> l(hub_.mutex());
    Msg e = hub_.auth().setInitialPassword(jstr(body, "password"));
    if (!e.key.empty()) return fail(e.key == "auth.exists" ? 409 : 422, e.key, e.text);
    hub_.logEvent("auth", "info", "Passwort gesetzt", "Ersteinrichtung");
    Msg err;
    auto tok = hub_.auth().login(jstr(body, "password"), clock_.nowMs(), err);
    hub_.flush();
    ApiResponse r = jsonResp(200, {{"ok", true}});
    if (tok) r.headers.emplace_back("Set-Cookie", sessionCookie(*tok, 12 * 3600));
    return r;
  }
  if (is("POST", {"auth", "login"})) {
    std::lock_guard<std::recursive_mutex> l(hub_.mutex());
    Msg err;
    auto tok = hub_.auth().login(jstr(body, "password"), clock_.nowMs(), err);
    if (!tok) {
      hub_.logEvent("auth", "warn", "Anmeldung fehlgeschlagen", err.text);
      return fail(err.key == "auth.locked" ? 429 : 401, err.key, err.text);
    }
    hub_.logEvent("auth", "info", "Angemeldet", "");
    ApiResponse r = jsonResp(200, {{"ok", true}});
    r.headers.emplace_back("Set-Cookie", sessionCookie(*tok, 12 * 3600));
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
    Msg e = hub_.auth().changePassword(jstr(body, "old"), jstr(body, "new"));
    if (!e.key.empty()) return fail(422, e.key, e.text);
    hub_.logEvent("auth", "notice", "Passwort geändert", "Alle Sitzungen abgemeldet");
    hub_.flush();
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
  if (is("GET", {"catalog"})) return jsonResp(200, hub_.catalog().raw);
  if (is("GET", {"changelog"})) {
    ApiResponse r;
    r.contentType = "text/markdown; charset=utf-8";
    r.body = embedded::kChangelogMd;
    return r;
  }
  if (is("GET", {"history"})) {
    Epoch now = clock_.epoch();
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
    Epoch now = clock_.epoch();
    auto t = req.query.find("type");
    return jsonResp(200, hub_.events(qnum(req, "from", 0), qnum(req, "to", now), t == req.query.end() ? "" : t->second,
                                     static_cast<size_t>(std::min<Epoch>(qnum(req, "limit", 200), 2000))));
  }
  if (is("GET", {"export.csv"})) {
    Epoch now = clock_.epoch();
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
  if (is("PUT", {"roles", "*"})) return fromResult(hub_.bindRole(p[1], jstr(body, "device"), body.value("channel", 0)));
  if (is("DELETE", {"roles", "*"})) return fromResult(hub_.unbindRole(p[1]));
  if (is("PUT", {"tank"})) return fromResult(hub_.putTank(body));
  if (is("POST", {"canisters"})) return fromResult(hub_.putCanister(body));
  if (is("DELETE", {"canisters", "*"})) return fromResult(hub_.deleteCanister(p[1]));
  if (is("POST", {"canisters", "*", "stock"})) return fromResult(hub_.setStock(p[1], jnum(body, "ml")));
  if (is("POST", {"recipes"})) return fromResult(hub_.putRecipe(body));
  if (is("POST", {"recipes", "template"})) return fromResult(hub_.applyRecipeTemplate(jstr(body, "id")));
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
  if (is("POST", {"stop"})) return fromResult(hub_.stop("Web-UI"));
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
    if (is("POST", {"update", "install"})) {
      // Nur im sicheren Zustand: keine Dosierung, kein Auftrag (Kundensicht, regulatorik)
      json st = hub_.state();
      if (!st["job"].is_null() || !st["dosing"].is_null())
        return fail(409, "update.busy", "Update wartet, bis keine Dosierung und kein Auftrag läuft");
      hub_.logEvent("system", "notice", "Update angefordert", jstr(body, "version"));
      return jsonResp(200, u->install(jstr(body, "version")));
    }
    if (is("POST", {"update", "rollback"})) {
      hub_.logEvent("system", "notice", "Rückkehr zur Vorversion angefordert", "");
      return jsonResp(200, u->rollback());
    }
  }
  return fail(404, "api.not_found", "Unbekannter Pfad");
}

}  // namespace gc
