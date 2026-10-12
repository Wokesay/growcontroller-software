// SPDX-License-Identifier: AGPL-3.0-or-later
// Anmeldung (EN 18031-1 ACM/AUM): Pflichtpasswort bei der Ersteinrichtung,
// kein Standardpasswort, kein Überspringen; gesalzener PBKDF2-Hash; Sperrzeit
// nach Fehlversuchen. Ein Anmeldemodell für Web-UI und API.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>

#include "gc/common.hpp"

namespace gc {

using RandomFn = std::function<void(std::uint8_t*, size_t)>;

class Auth {
 public:
  explicit Auth(RandomFn rng) : rng_(std::move(rng)) {}

  bool hasPassword() const { return !hash_.empty(); }
  // Liefert leeren Schlüssel bei Erfolg, sonst den Grund.
  Msg setInitialPassword(const std::string& pw);
  // A wrong old password counts as a failed attempt of `client`.
  Msg changePassword(const std::string& oldPw, const std::string& newPw, Ms now, const std::string& client);
  // Anmeldung mit Sperre nach Fehlversuchen. Bei Erfolg ein Sitzungs-Token.
  // `client` is the peer's key (clientKey), "" when unknown.
  std::optional<std::string> login(const std::string& pw, Ms now, Msg& err, const std::string& client);
  bool check(const std::string& token, Ms now);
  // A known client using its session counts as seen now: the one seen
  // longest ago is the first to lose its own count (SD-051).
  void seen(const std::string& client, Ms now);
  void logout(const std::string& token) { sessions_.erase(token); }
  void logoutAll() { sessions_.clear(); }

  json toJson() const;  // nur Hash und Salz, nie Sitzungen
  void load(const json& j);

  static constexpr std::uint32_t kIterations = 10000;
  static constexpr Ms kSessionIdle = 12 * kHour;
  // Lockout per client (SD-051): a client that signed in before has a count
  // of its own; every other client shares one, which also brakes guessing
  // from many addresses. Kept in memory only.
  static constexpr size_t kKnownClients = 16;
  static constexpr Ms kForgetFailures = 24 * kHour;

 private:
  struct Attempts {
    int failures = 0;
    Ms lockedUntil = 0;
    Ms lastFailure = 0;
  };
  struct Known {
    Attempts tries;
    Ms lastSeen = 0;  // signed in or used its session
  };
  static Msg checkStrength(const std::string& pw);
  Attempts& attemptsOf(const std::string& client);
  static bool locked(const Attempts& a, Ms now, Msg& err);
  static void failed(Attempts& a, Ms now);
  void remember(const std::string& client, Ms now);
  std::string hashOf(const std::string& pw, const std::string& saltHex, std::uint32_t it) const;
  RandomFn rng_;
  std::string salt_, hash_;
  std::uint32_t iterations_ = kIterations;
  std::map<std::string, Ms> sessions_;  // Token → zuletzt benutzt
  Attempts shared_;                    // every client that has not signed in
  std::map<std::string, Known> known_;  // clientKey → its own count
};

// The key of a peer's address for the lockout: IPv4 as a.b.c.d, IPv6 as
// eight groups in lowercase hex without its zone, an IPv4-mapped IPv6 address
// as IPv4; anything else is "", an unknown client.
std::string clientKey(const std::string& address);

}  // namespace gc
