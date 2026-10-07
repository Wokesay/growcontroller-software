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
  Msg changePassword(const std::string& oldPw, const std::string& newPw);
  // Anmeldung mit Sperre nach Fehlversuchen. Bei Erfolg ein Sitzungs-Token.
  std::optional<std::string> login(const std::string& pw, Ms now, Msg& err);
  bool check(const std::string& token, Ms now);
  void logout(const std::string& token) { sessions_.erase(token); }
  void logoutAll() { sessions_.clear(); }

  json toJson() const;  // nur Hash und Salz, nie Sitzungen
  void load(const json& j);

  static constexpr std::uint32_t kIterations = 10000;
  static constexpr Ms kSessionIdle = 12 * kHour;

 private:
  static Msg checkStrength(const std::string& pw);
  std::string hashOf(const std::string& pw, const std::string& saltHex, std::uint32_t it) const;
  RandomFn rng_;
  std::string salt_, hash_;
  std::uint32_t iterations_ = kIterations;
  std::map<std::string, Ms> sessions_;  // Token → zuletzt benutzt
  int failures_ = 0;
  Ms lockedUntil_ = 0;
};

}  // namespace gc
