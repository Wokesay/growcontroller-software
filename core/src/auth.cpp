// SPDX-License-Identifier: AGPL-3.0-or-later
#include "gc/auth.hpp"

#include <algorithm>

#include "gc/sha256.hpp"

namespace gc {

namespace {
// Vergleich in konstanter Zeit: verrät nicht, ab welcher Stelle der Hash abweicht.
bool sameHash(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  unsigned diff = 0;
  for (size_t i = 0; i < a.size(); ++i) diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
  return diff == 0;
}
}  // namespace

Msg Auth::checkStrength(const std::string& pw) {
  if (pw.size() < 8) return {"auth.too_short", "Mindestens 8 Zeichen", json::object()};
  if (pw.size() > 128) return {"auth.too_long", "Höchstens 128 Zeichen", json::object()};
  return {};
}

std::string Auth::hashOf(const std::string& pw, const std::string& saltHex, std::uint32_t it) const {
  auto dk = pbkdf2Sha256(pw, fromHex(saltHex), it, 32);
  return toHex(dk.data(), dk.size());
}

Msg Auth::setInitialPassword(const std::string& pw) {
  if (hasPassword()) return {"auth.exists", "Passwort ist bereits gesetzt", json::object()};
  Msg m = checkStrength(pw);
  if (!m.key.empty()) return m;
  std::uint8_t salt[16];
  rng_(salt, sizeof salt);
  salt_ = toHex(salt, sizeof salt);
  iterations_ = kIterations;
  hash_ = hashOf(pw, salt_, iterations_);
  return {};
}

Msg Auth::changePassword(const std::string& oldPw, const std::string& newPw) {
  if (!hasPassword() || !sameHash(hashOf(oldPw, salt_, iterations_), hash_))
    return {"auth.wrong", "Altes Passwort stimmt nicht", json::object()};
  Msg m = checkStrength(newPw);
  if (!m.key.empty()) return m;
  std::uint8_t salt[16];
  rng_(salt, sizeof salt);
  salt_ = toHex(salt, sizeof salt);
  hash_ = hashOf(newPw, salt_, iterations_);
  sessions_.clear();  // alle anderen Sitzungen ungültig
  return {};
}

std::optional<std::string> Auth::login(const std::string& pw, Ms now, Msg& err) {
  if (!hasPassword()) {
    err = {"auth.no_password", "Noch kein Passwort gesetzt", json::object()};
    return std::nullopt;
  }
  if (now < lockedUntil_) {
    err = {"auth.locked", "Zu viele Fehlversuche. Bitte " + std::to_string((lockedUntil_ - now) / 1000 + 1) + " s warten.",
           {{"waitS", (lockedUntil_ - now) / 1000 + 1}}};
    return std::nullopt;
  }
  if (!sameHash(hashOf(pw, salt_, iterations_), hash_)) {
    failures_++;
    if (failures_ >= 5) {
      // 30 s, dann verdoppeln bis 15 min (EN 18031-1 AUM-6)
      Ms wait = std::min<Ms>(30 * kSecond << std::min(failures_ - 5, 5), 15 * kMinute);
      lockedUntil_ = now + wait;
    }
    err = {"auth.wrong", "Passwort falsch", json::object()};
    return std::nullopt;
  }
  failures_ = 0;
  std::uint8_t tok[24];
  rng_(tok, sizeof tok);
  std::string t = toHex(tok, sizeof tok);
  // abgelaufene Sitzungen aufräumen, höchstens 16 gleichzeitig
  for (auto it = sessions_.begin(); it != sessions_.end();)
    it = (now - it->second > kSessionIdle) ? sessions_.erase(it) : std::next(it);
  if (sessions_.size() >= 16) sessions_.erase(sessions_.begin());
  sessions_[t] = now;
  return t;
}

bool Auth::check(const std::string& token, Ms now) {
  auto it = sessions_.find(token);
  if (it == sessions_.end()) return false;
  if (now - it->second > kSessionIdle) {
    sessions_.erase(it);
    return false;
  }
  it->second = now;
  return true;
}

json Auth::toJson() const { return {{"salt", salt_}, {"hash", hash_}, {"iterations", iterations_}}; }

void Auth::load(const json& j) {
  salt_ = jstr(j, "salt");
  hash_ = jstr(j, "hash");
  double it = jnum(j, "iterations", kIterations);
  iterations_ = isNum(it) && it >= 1000 && it <= 1e7 ? static_cast<std::uint32_t>(it) : kIterations;
}

}  // namespace gc
