// Schicht 11: REST-API /api/v1, transportneutral. Die Plattform (Simulator:
// cpp-httplib; ESP32: esp_http_server) übersetzt nur Anfrage und Antwort.
// Die Web-UI nutzt ausschließlich diese API – was die UI kann, kann die API.
#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "gc/hub.hpp"

namespace gc {

struct ApiRequest {
  std::string method, path;
  std::map<std::string, std::string> query;
  std::string body;
  std::string token;  // aus Cookie gc_session oder "Authorization: Bearer"
};

// Herkunftsprüfung für die Transportschicht, ohne Plattform-Header.
// DNS-Rebinding: Der Host-Kopf muss eine IP, `localhost`, ein Name im Heimnetz
// (.local, .lan, .home.arpa, .internal, .fritz.box) oder `extraHost` sein.
bool hostAllowed(const std::string& hostHeader, const std::string& extraHost = "");
// CSRF: Schreibende Anfragen eines Browsers nur von derselben Herkunft
// (Sec-Fetch-Site, sonst Origin gegen Host). Ohne beide Köpfe: kein Browser.
bool writeAllowed(const std::string& method, const std::string& secFetchSite, const std::string& origin,
                  const std::string& hostHeader);

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

 private:
  ApiResponse route(const ApiRequest& req);
  Hub& hub_;
  const IClock& clock_;
};

}  // namespace gc
