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

struct ApiResponse {
  int status = 200;
  std::string body;
  std::string contentType = "application/json";
  std::vector<std::pair<std::string, std::string>> headers;
};

class Api {
 public:
  Api(Hub& hub, const IClock& clock) : hub_(hub), clock_(clock) {}
  ApiResponse handle(const ApiRequest& req);
  bool authorized(const ApiRequest& req);

 private:
  Hub& hub_;
  const IClock& clock_;
};

}  // namespace gc
