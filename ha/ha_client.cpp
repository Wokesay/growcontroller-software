// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ha_client.hpp"

#include <iostream>

#include <httplib.h>

namespace ha {

namespace {

std::int64_t epochMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

}  // namespace

Poller::Poller(HaBus& bus, std::string url, std::string token, std::function<gc::Ms()> nowMs)
    : bus_(bus), url_(std::move(url)), token_(std::move(token)), nowMs_(std::move(nowMs)) {
  std::string err;
  address_ = parseAddress(url_, err);  // the same check as the mapping's, whoever passes the address in
}

Poller::~Poller() { stop(); }

std::string Poller::pollOnce() {
  if (stopping_) return "";
#if !defined(CPPHTTPLIB_OPENSSL_SUPPORT) && !defined(CPPHTTPLIB_SSL_ENABLED)
  if (address_ && address_->tls) {
    bus_.lostAll();
    bus_.setConnection("unreachable");
    return "https needs a build with OpenSSL; use http:// in your own network";
  }
#endif
  // Built from the checked parts, so the library reads the same host and port
  Address origin = address_ ? *address_ : Address();
  origin.base.clear();
  httplib::Client cli(origin.url());
  if (!address_ || !cli.is_valid()) {
    bus_.lostAll();
    bus_.setConnection("unreachable");
    return "the Home Assistant address is not valid";
  }
  cli.set_connection_timeout(3, 0);
  cli.set_read_timeout(5, 0);
  cli.set_max_timeout(std::chrono::seconds(10));  // per request, also against a server that trickles bytes
  cli.set_payload_max_length(kMaxAnswer);
  cli.set_follow_location(false);  // never carry the token to another host
  rejected_ = false;
  // All states in one request: the selected entities and the candidates the
  // user can pick from. Everything else in it is dropped by the bus.
  const httplib::Headers headers = {{"Authorization", "Bearer " + token_}};
  auto res = cli.Get(address_->base + "/api/states", headers);
  if (!res) {
    bus_.lostAll();
    bus_.setConnection("unreachable");
    return "Home Assistant not reachable (" + httplib::to_string(res.error()) + ")";
  }
  if (res->status == 401 || res->status == 403) {
    bus_.lostAll();
    bus_.setConnection("refused");
    rejected_ = true;
    return res->status == 401 ? "Home Assistant rejected the token" : "Home Assistant refused access (403, maybe this computer is banned)";
  }
  if (res->status != 200) {
    bus_.lostAll();
    bus_.setConnection("unreachable");
    return "Home Assistant answered HTTP " + std::to_string(res->status);
  }
  const gc::json j = parseStates(res->body);
  if (!j.is_array()) {
    bus_.lostAll();
    bus_.setConnection("unreachable");
    return "Home Assistant's answer is not a list of states";
  }
  bus_.setConnection("ok");
  // Home Assistant's own time of the answer, so a report's age needs no
  // agreement between the two clocks; without a Date header, ours.
  const auto haNow = parseHttpDateMs(res->get_header_value("Date"));
  if (!haNow && !warnedNoDate_) {
    warnedNoDate_ = true;
    std::cerr << "Home Assistant: no Date header; report ages use this computer's clock, keep both clocks synced\n";
  }
  bus_.updateAll(j, haNow.value_or(epochMs()), nowMs_());
  std::string problem;
  for (const auto& e : bus_.entities())
    if (const std::string f = bus_.fault(e.entityId); !f.empty()) problem = e.entityId + ": " + f;
  return problem;
}

void Poller::start(std::chrono::milliseconds every) {
  stop();  // a thread that ended on a refused token is joined first
  stopping_ = false;
  thread_ = std::thread([this, every] {
    std::string last;
    while (!stopping_) {
      std::string problem;
      try {
        problem = pollOnce();
      } catch (const std::exception& ex) {  // never take the server down
        bus_.lostAll();
        bus_.setConnection("unreachable");
        problem = std::string("reading failed: ") + ex.what();
      }
      if (stopping_) break;  // a round cut short is no news
      if (problem != last) std::cerr << (problem.empty() ? "Home Assistant: reading again\n" : "Home Assistant: " + problem + "\n");
      last = problem;
      if (rejected_) {
        std::cerr << "Home Assistant: reading stopped; fix the token and restart\n";
        break;
      }
      for (auto waited = std::chrono::milliseconds(0); waited < every && !stopping_; waited += std::chrono::milliseconds(100))
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  });
}

void Poller::stop() {
  stopping_ = true;
  if (thread_.joinable()) thread_.join();
}

}  // namespace ha
