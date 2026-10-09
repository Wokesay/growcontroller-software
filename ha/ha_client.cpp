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
    : bus_(bus), url_(std::move(url)), token_(std::move(token)), nowMs_(std::move(nowMs)) {}

Poller::~Poller() { stop(); }

std::string Poller::pollOnce() {
  auto lostAll = [&] {
    for (const auto& e : bus_.entities()) bus_.lost(e.entityId);
  };
#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
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
  const httplib::Headers headers = {{"Authorization", "Bearer " + token_}};
  std::string problem;
  for (const auto& e : bus_.entities()) {
    auto res = cli.Get("/api/states/" + e.entityId, headers);
    if (!res) {
      lostAll();
      return "Home Assistant not reachable (" + httplib::to_string(res.error()) + ")";
    }
    if (res->status == 401) {
      lostAll();
      return "Home Assistant rejected the token";
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
    bus_.update(j, epochMs(), nowMs_());
  }
  return problem;
}

void Poller::start(std::chrono::milliseconds every) {
  running_ = true;
  thread_ = std::thread([this, every] {
    std::string last;
    while (running_) {
      std::string problem = pollOnce();
      if (problem != last) std::cerr << (problem.empty() ? "Home Assistant: reading again\n" : "Home Assistant: " + problem + "\n");
      last = problem;
      for (auto waited = std::chrono::milliseconds(0); waited < every && running_; waited += std::chrono::milliseconds(100))
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  });
}

void Poller::stop() {
  running_ = false;
  if (thread_.joinable()) thread_.join();
}

}  // namespace ha
