// SPDX-License-Identifier: AGPL-3.0-or-later
// Reads Home Assistant's states through its REST API (GET /api/states,
// Bearer token) and hands them to the HaBus.
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

  // One round: all states in one request; returns "" or why it failed (never
  // the token). After stop() it reads nothing until start() again.
  std::string pollOnce();
  // The largest answer taken: all states of a large installation (a state is
  // a few hundred bytes to a few KB).
  static constexpr size_t kMaxAnswer = 16 * 1024 * 1024;
  // Home Assistant refused the token (401/403) in the last round. Running,
  // the poller then stops until restart: Home Assistant counts every failed
  // login and bans the computer after a few, however slowly they come.
  bool rejected() const { return rejected_; }
  void start(std::chrono::milliseconds every);
  void stop();

 private:
  HaBus& bus_;
  std::string url_, token_;
  std::string origin_, base_;  // http(s)://host[:port] and the path in front of the API ("" or e.g. "/core")
  std::function<gc::Ms()> nowMs_;
  std::atomic<bool> stopping_{false};  // set by stop(), read by the polling thread
  std::atomic<bool> rejected_{false};
  bool warnedNoDate_ = false;          // only pollOnce() touches it, one round at a time
  std::thread thread_;
};

}  // namespace ha
