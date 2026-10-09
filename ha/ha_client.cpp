// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ha_client.hpp"

#include <algorithm>
#include <iostream>

#include <httplib.h>

namespace ha {

namespace {

std::int64_t epochMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

}  // namespace

Poller::Poller(HaBus& bus, std::string url, std::string token, std::function<gc::Ms()> nowMs)
    : bus_(bus), url_(std::move(url)), token_(std::move(token)), nowMs_(std::move(nowMs)) {}

Poller::~Poller() { stop(); }

std::string Poller::pollOnce() {
  auto lostAll = [&] {
    for (const auto& e : bus_.entities()) bus_.lost(e.entityId);
  };
#if !defined(CPPHTTPLIB_OPENSSL_SUPPORT) && !defined(CPPHTTPLIB_SSL_ENABLED)
  if (url_.rfind("https://", 0) == 0) {
    lostAll();
    return "https needs a build with OpenSSL; use http:// in your own network";
  }
#endif
  httplib::Client cli(url_);
  if (!cli.is_valid()) {
    lostAll();
    return "the Home Assistant address is not valid";
  }
  cli.set_connection_timeout(3, 0);
  cli.set_read_timeout(5, 0);
  cli.set_max_timeout(std::chrono::seconds(10));  // per request, also against a server that trickles bytes
  cli.set_payload_max_length(64 * 1024);          // one state is a few hundred bytes
  cli.set_follow_location(false);                 // never carry the token to another host
  rejected_ = false;
  const httplib::Headers headers = {{"Authorization", "Bearer " + token_}};
  std::string problem;
  for (const auto& e : bus_.entities()) {
    if (thread_.joinable() && !running_) break;  // stopping: do not wait for the rest
    auto res = cli.Get("/api/states/" + e.entityId, headers);
    if (!res) {
      lostAll();
      return "Home Assistant not reachable (" + httplib::to_string(res.error()) + ")";
    }
    if (res->status == 401 || res->status == 403) {
      lostAll();
      rejected_ = true;
      return res->status == 401 ? "Home Assistant rejected the token" : "Home Assistant refused access (403, maybe this computer is banned)";
    }
    if (res->status != 200) {
      bus_.lost(e.entityId);
      problem = e.entityId + ": HTTP " + std::to_string(res->status);
      continue;
    }
    auto j = gc::json::parse(res->body, nullptr, false);
    if (!j.is_object()) {
      bus_.lost(e.entityId);
      problem = e.entityId + ": answer is not a JSON object";
      continue;
    }
    // Home Assistant's own time of the answer, so a report's age needs no
    // agreement between the two clocks; without a Date header, ours.
    const auto haNow = parseHttpDateMs(res->get_header_value("Date"));
    bus_.update(e.entityId, j, haNow.value_or(epochMs()), nowMs_());
    if (const std::string f = bus_.fault(e.entityId); !f.empty()) problem = e.entityId + ": " + f;
  }
  return problem;
}

void Poller::start(std::chrono::milliseconds every) {
  running_ = true;
  thread_ = std::thread([this, every] {
    std::string last;
    while (running_) {
      std::string problem;
      try {
        problem = pollOnce();
      } catch (const std::exception& ex) {  // never take the server down
        for (const auto& e : bus_.entities()) bus_.lost(e.entityId);
        problem = std::string("reading failed: ") + ex.what();
      }
      if (problem != last) std::cerr << (problem.empty() ? "Home Assistant: reading again\n" : "Home Assistant: " + problem + "\n");
      last = problem;
      const std::chrono::milliseconds wait = rejected_ ? std::max<std::chrono::milliseconds>(every, kRejectedWait) : every;
      for (auto waited = std::chrono::milliseconds(0); waited < wait && running_; waited += std::chrono::milliseconds(100))
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  });
}

void Poller::stop() {
  running_ = false;
  if (thread_.joinable()) thread_.join();
}

}  // namespace ha
