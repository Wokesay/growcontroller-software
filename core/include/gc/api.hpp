// SPDX-License-Identifier: AGPL-3.0-or-later
// Schicht 11: REST-API /api/v1, transportneutral. Die Plattform (Simulator:
// cpp-httplib; ESP32: esp_http_server) übersetzt nur Anfrage und Antwort.
// Die Web-UI nutzt ausschließlich diese API – was die UI kann, kann die API.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "gc/hub.hpp"

namespace gc {

// A user signed in by the platform in front of the hub (Home Assistant's
// Ingress, SD-077): its ID and a name to show, both already checked and
// cleaned by the platform's own transport, never taken from a request it
// did not vouch for.
struct PlatformUser {
  std::string id, name;
  // How the event log names the user: names need not be unique, so with the
  // start of the ID (a name made from the ID has it already)
  std::string actor() const {
    const std::string tag = id.substr(0, 8);
    const bool tagged = name.size() >= tag.size() && name.compare(name.size() - tag.size(), tag.size(), tag) == 0;
    return tagged ? name : name + " (" + tag + ")";
  }
};

struct ApiRequest {
  std::string method, path;
  std::map<std::string, std::string> query;
  std::string body;
  std::string token;  // aus Cookie gc_session oder "Authorization: Bearer"
  std::string client;  // the peer's address as clientKey() gives it, never from a header; "" when unknown
  std::optional<PlatformUser> user;  // counts only with platformSignIn() on
};

// Herkunftsprüfung für die Transportschicht, ohne Plattform-Header.
// DNS-Rebinding: Der Host-Kopf muss eine IP, `localhost`, ein Name im Heimnetz
// (.local, .lan, .home.arpa, .internal, .fritz.box) oder `extraHost` sein.
bool hostAllowed(const std::string& hostHeader, const std::string& extraHost = "");
// CSRF: Schreibende Anfragen eines Browsers nur von derselben Herkunft
// (Sec-Fetch-Site, sonst Origin gegen Host). Ohne beide Köpfe: kein Browser.
bool writeAllowed(const std::string& method, const std::string& secFetchSite, const std::string& origin,
                  const std::string& hostHeader);
// The session token from a Cookie header: the cookie gc_session by its exact
// name; "" without one.
std::string sessionCookieValue(const std::string& cookieHeader);

struct ApiResponse {
  int status = 200;
  std::string body;
  std::string contentType = "application/json";
  std::vector<std::pair<std::string, std::string>> headers;
};

class Api {
 public:
  Api(Hub& hub, const IClock& clock) : hub_(hub), clock_(clock) {}
  // Fängt jede Ausnahme ab: falsche Eingaben → 400, sonst 500. Der Server
  // stürzt an einer Anfrage nie ab.
  ApiResponse handle(const ApiRequest& req);
  bool authorized(const ApiRequest& req);
  // On (the Home Assistant app, SD-077): a request with a platform user is
  // signed in without the hub's password; the hub's own password is only
  // for direct access, set, changed and removed only by such a user; no
  // first password and no change of it come over the direct way. Off (the
  // simulator, the ESP): a platform user counts for nothing.
  void platformSignIn(bool on) { platform_ = on; }
  // Where the browser sends the session cookie: "/" by default; the Home
  // Assistant app narrows it to the API, the only part that needs it.
  void cookiePath(std::string path) { cookiePath_ = std::move(path); }
  // A request whose user the platform signed in (SD-077)
  bool viaPlatform(const ApiRequest& req) const { return platform_ && req.user.has_value(); }

 private:
  ApiResponse route(const ApiRequest& req);
  Hub& hub_;
  const IClock& clock_;
  bool platform_ = false;
  std::string cookiePath_ = "/";
};

}  // namespace gc
