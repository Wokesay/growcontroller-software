// Testhilfe: spricht die REST-API des Kerns ohne HTTP an (gleicher Code wie
// im Server), merkt sich das Sitzungs-Token aus Set-Cookie.
#pragma once

#include <string>
#include <utility>

#include "scenario.hpp"

namespace test {

struct Client {
  sim::Simulation& sim;
  std::string token = {};

  std::pair<int, gc::json> call(const std::string& method, const std::string& pathQ, const gc::json& body = nullptr) {
    gc::ApiRequest r;
    r.method = method;
    auto q = pathQ.find('?');
    r.path = pathQ.substr(0, q);
    if (q != std::string::npos) {
      std::string rest = pathQ.substr(q + 1);
      size_t pos = 0;
      while (pos <= rest.size()) {
        size_t amp = rest.find('&', pos);
        std::string kv = rest.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        auto eq = kv.find('=');
        if (eq != std::string::npos) r.query[kv.substr(0, eq)] = kv.substr(eq + 1);
        if (amp == std::string::npos) break;
        pos = amp + 1;
      }
    }
    r.body = body.is_null() ? "" : body.dump();
    r.token = token;
    gc::ApiResponse res;
    {
      std::lock_guard<std::recursive_mutex> l(sim.mutex());
      res = sim.api().handle(r);
    }
    for (const auto& [k, v] : res.headers)
      if (k == "Set-Cookie" && v.rfind("gc_session=", 0) == 0) token = v.substr(11, v.find(';') - 11);
    auto j = gc::json::parse(res.body, nullptr, false);
    return {res.status, j.is_discarded() ? gc::json(res.body) : j};
  }
  gc::json ok(const std::string& method, const std::string& path, const gc::json& body = nullptr) {
    auto [st, j] = call(method, path, body);
    if (st != 200) throw std::runtime_error(method + " " + path + " → " + std::to_string(st) + " " + j.dump());
    return j;
  }
  gc::json state() { return ok("GET", "/api/v1/state"); }
};

inline sim::Options opts(const std::string& scenario) {
  sim::Options o;
  o.scenario = scenario;
  o.prefillHours = 0;
  o.password = "demo-passwort";
  o.startEpoch = 1790000000;
  return o;
}

}  // namespace test
