// SPDX-License-Identifier: AGPL-3.0-or-later
// Reads the mapped entities from Home Assistant's REST API
// (GET /api/states/<entity_id>, Bearer token) and hands them to the HaBus.
// Runs in its own thread so the hub's tick never waits for the network.
#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <thread>

#include "ha_bus.hpp"

namespace ha {

class Poller {
 public:
  Poller(HaBus& bus, std::string url, std::string token, std::function<gc::Ms()> nowMs);
  ~Poller();

  // One round over all entities; returns "" or why it failed (never the token).
  std::string pollOnce();
  void start(std::chrono::milliseconds every);
  void stop();

 private:
  HaBus& bus_;
  std::string url_, token_;
  std::function<gc::Ms()> nowMs_;
  std::atomic<bool> running_{false};
  std::thread thread_;
};

}  // namespace ha
