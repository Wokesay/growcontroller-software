// SPDX-License-Identifier: AGPL-3.0-or-later
#include <doctest/doctest.h>

#include "gc/auth.hpp"
#include "gc/sha256.hpp"

using namespace gc;

namespace {
void rng(std::uint8_t* p, size_t n) {
  static std::uint8_t x = 1;
  for (size_t i = 0; i < n; ++i) p[i] = x++;
}
}  // namespace

TEST_CASE("SHA-256 und PBKDF2: Prüfvektoren") {
  auto d = sha256(std::string("abc"));
  CHECK(toHex(d.data(), d.size()) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  auto k = pbkdf2Sha256("password", {'s', 'a', 'l', 't'}, 1, 32);
  CHECK(toHex(k.data(), k.size()) == "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");
  auto k2 = pbkdf2Sha256("password", {'s', 'a', 'l', 't'}, 2, 32);
  CHECK(toHex(k2.data(), k2.size()) == "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43");
}

TEST_CASE("Anmeldung: Pflichtpasswort, Mindestlänge, kein zweites Erstpasswort") {
  Auth a(rng);
  CHECK_FALSE(a.hasPassword());
  CHECK(a.setInitialPassword("kurz").key == "auth.too_short");
  CHECK(a.setInitialPassword("geheim-123").key.empty());
  CHECK(a.setInitialPassword("anderes-123").key == "auth.exists");
  Auth b(rng);
  b.load(a.toJson());
  Msg e;
  CHECK(b.login("geheim-123", 0, e, "").has_value());
  CHECK(a.toJson().dump().find("geheim") == std::string::npos);  // nur Hash
}

TEST_CASE("Anmeldung: Sperre nach Fehlversuchen (EN 18031 AUM-6)") {
  Auth a(rng);
  a.setInitialPassword("geheim-123");
  Msg e;
  for (int i = 0; i < 5; ++i) CHECK_FALSE(a.login("falsch", 1000, e, ""));
  CHECK_FALSE(a.login("geheim-123", 2000, e, ""));
  CHECK(e.key == "auth.locked");
  CHECK(a.login("geheim-123", 2000 + 31 * kSecond, e, "").has_value());
}

TEST_CASE("Anmeldung: Sitzung läuft ab, Passwortwechsel meldet alle ab") {
  Auth a(rng);
  a.setInitialPassword("geheim-123");
  Msg e;
  auto t = a.login("geheim-123", 0, e, "");
  REQUIRE(t);
  CHECK(a.check(*t, kHour));
  CHECK_FALSE(a.check(*t, kHour + Auth::kSessionIdle + 1));
  auto t2 = a.login("geheim-123", 0, e, "");
  CHECK(a.changePassword("geheim-123", "neu-geheim-456", 0, "").key.empty());
  CHECK_FALSE(a.check(*t2, 1));
  CHECK(a.changePassword("falsch", "x-123456789", 0, "").key == "auth.wrong");
}

TEST_CASE("Sign-in: a session in use stays signed in; one unused for 12 hours ends (SD-063)") {
  Auth a(rng);
  a.setInitialPassword("geheim-123");
  Msg e;
  auto t = a.login("geheim-123", 0, e, "");
  REQUIRE(t);
  CHECK(a.check(*t, Auth::kSessionIdle - 1));
  CHECK(a.check(*t, 2 * Auth::kSessionIdle - 2));  // renewed by the use before, though past 12 h since signing in
  CHECK_FALSE(a.check(*t, 3 * Auth::kSessionIdle));  // then 12 hours without a request
}

TEST_CASE("Sign-in: a 17th session ends the one used longest ago, not the page left open (SD-063)") {
  Auth a(rng);
  a.setInitialPassword("geheim-123");
  Msg e;
  std::vector<std::string> t;
  for (int i = 0; i < 16; ++i) {
    auto tok = a.login("geheim-123", i, e, "");
    REQUIRE(tok);
    t.push_back(*tok);
  }
  CHECK(a.check(t[0], 100));  // the wall page, signed in first, still in use
  REQUIRE(a.login("geheim-123", 200, e, ""));
  CHECK(a.check(t[0], 300));
  CHECK_FALSE(a.check(t[1], 300));  // used longest ago
  for (int i = 2; i < 16; ++i) CHECK(a.check(t[static_cast<size_t>(i)], 300));
}

// A lockout per client (SD-051): someone guessing in the network locks only
// the clients that never signed in, never one that did.
TEST_CASE("Sign-in: a client that signed in before keeps its own lockout; the others share one") {
  Auth a(rng);
  a.setInitialPassword("geheim-123");
  Msg e;
  REQUIRE(a.login("geheim-123", 0, e, "192.168.1.20"));  // the owner's phone
  for (int i = 0; i < 5; ++i) CHECK_FALSE(a.login("falsch", 1000, e, "192.168.1.66"));
  CHECK_FALSE(a.login("geheim-123", 2000, e, "192.168.1.66"));
  CHECK(e.key == "auth.locked");
  CHECK_FALSE(a.login("geheim-123", 2000, e, "192.168.1.77"));  // never signed in: the shared lockout
  CHECK(e.key == "auth.locked");
  CHECK_FALSE(a.login("geheim-123", 2000, e, ""));  // an unknown address too
  CHECK(e.key == "auth.locked");
  CHECK(a.login("geheim-123", 2000, e, "192.168.1.20").has_value());  // the owner's phone gets in
  // The shared lockout ends after its time
  CHECK(a.login("geheim-123", 2000 + 31 * kSecond, e, "192.168.1.77").has_value());
}

TEST_CASE("Sign-in: a known client that mistypes locks only itself, and a success resets only its own count") {
  Auth a(rng);
  a.setInitialPassword("geheim-123");
  Msg e;
  REQUIRE(a.login("geheim-123", 0, e, "192.168.1.20"));
  for (int i = 0; i < 5; ++i) CHECK_FALSE(a.login("falsch", 1000, e, "192.168.1.20"));
  CHECK_FALSE(a.login("geheim-123", 2000, e, "192.168.1.20"));
  CHECK(e.key == "auth.locked");
  CHECK(a.login("geheim-123", 2000, e, "192.168.1.77").has_value());  // the shared count is untouched
  CHECK(a.login("geheim-123", 2000 + 31 * kSecond, e, "192.168.1.20").has_value());
  // Four failures of others, then a success of a new client: the shared count stays at four
  for (int i = 0; i < 4; ++i) CHECK_FALSE(a.login("falsch", kMinute, e, "192.168.1.66"));
  CHECK(a.login("geheim-123", kMinute, e, "192.168.1.88").has_value());
  CHECK_FALSE(a.login("falsch", kMinute, e, "192.168.1.99"));
  CHECK_FALSE(a.login("geheim-123", kMinute + 1, e, "192.168.1.99"));
  CHECK(e.key == "auth.locked");
  // A client that cannot be told apart ("") never gets a lockout of its own
  CHECK(a.login("geheim-123", kMinute + 31 * kSecond, e, "").has_value());
  CHECK_FALSE(a.login("falsch", kMinute + 31 * kSecond, e, "192.168.1.66"));
  CHECK_FALSE(a.login("geheim-123", kMinute + 32 * kSecond, e, ""));
  CHECK(e.key == "auth.locked");
}

TEST_CASE("Sign-in: at most 16 known clients; the one that signed in longest ago shares the lockout again") {
  Auth a(rng);
  a.setInitialPassword("geheim-123");
  Msg e;
  for (int i = 0; i < 17; ++i) REQUIRE(a.login("geheim-123", i * kSecond, e, "10.0.0." + std::to_string(i + 1)));
  for (int i = 0; i < 5; ++i) CHECK_FALSE(a.login("falsch", kMinute, e, "192.168.1.66"));
  CHECK_FALSE(a.login("geheim-123", kMinute, e, "10.0.0.1"));
  CHECK(e.key == "auth.locked");
  for (int i = 2; i <= 17; ++i) CHECK(a.login("geheim-123", kMinute, e, "10.0.0." + std::to_string(i)).has_value());
}

TEST_CASE("Sign-in: a known client that keeps using its session is not the first to drop out") {
  Auth a(rng);
  a.setInitialPassword("geheim-123");
  Msg e;
  for (int i = 0; i < 16; ++i) REQUIRE(a.login("geheim-123", i * kSecond, e, "10.0.0." + std::to_string(i + 1)));
  a.seen("10.0.0.1", 20 * kSecond);  // the phone signed in first is still in use
  a.seen("192.168.1.99", 20 * kSecond);  // an address never signed in gains nothing
  REQUIRE(a.login("geheim-123", 21 * kSecond, e, "10.0.0.17"));  // 10.0.0.2 drops out instead
  for (int i = 0; i < 5; ++i) CHECK_FALSE(a.login("falsch", kMinute, e, "192.168.1.66"));
  CHECK(a.login("geheim-123", kMinute, e, "10.0.0.1").has_value());
  CHECK_FALSE(a.login("geheim-123", kMinute, e, "10.0.0.2"));
  CHECK(e.key == "auth.locked");
  CHECK_FALSE(a.login("geheim-123", kMinute, e, "192.168.1.99"));
}

TEST_CASE("Sign-in: the shared count forgets failures after a quiet day") {
  Auth a(rng);
  a.setInitialPassword("geheim-123");
  Msg e;
  for (int i = 0; i < 4; ++i) CHECK_FALSE(a.login("falsch", 0, e, "192.168.1.66"));
  CHECK_FALSE(a.login("falsch", 25 * kHour, e, "192.168.1.66"));
  CHECK(e.key == "auth.wrong");
  CHECK(a.login("geheim-123", 25 * kHour, e, "192.168.1.77").has_value());  // not locked: one failure counts
}

TEST_CASE("Password change: a wrong old password counts as a failed attempt; a change keeps only the caller known") {
  Auth a(rng);
  a.setInitialPassword("geheim-123");
  Msg e;
  REQUIRE(a.login("geheim-123", 0, e, "192.168.1.20"));
  REQUIRE(a.login("geheim-123", 0, e, "192.168.1.21"));  // a second device
  for (int i = 0; i < 5; ++i) CHECK(a.changePassword("falsch", "neu-geheim-456", 1000, "192.168.1.20").key == "auth.wrong");
  CHECK(a.changePassword("geheim-123", "neu-geheim-456", 2000, "192.168.1.20").key == "auth.locked");
  CHECK(a.changePassword("geheim-123", "neu-geheim-456", 2000 + 31 * kSecond, "192.168.1.20").key.empty());
  // Someone guessing now: the device that changed the password signs in again
  // (it was signed out); the other one is known no more and shares the lockout
  for (int i = 0; i < 5; ++i) CHECK_FALSE(a.login("falsch", kMinute, e, "192.168.1.66"));
  CHECK(a.login("neu-geheim-456", kMinute, e, "192.168.1.20").has_value());
  CHECK_FALSE(a.login("neu-geheim-456", kMinute, e, "192.168.1.21"));
  CHECK(e.key == "auth.locked");
}

TEST_CASE("Client key: IPv4 as it is, IPv6 in full without its zone, an IPv4-mapped address as IPv4, anything else unknown") {
  CHECK(clientKey("192.168.1.20") == "192.168.1.20");
  CHECK(clientKey("::ffff:192.168.1.20") == "192.168.1.20");
  CHECK(clientKey("::FFFF:c0a8:0114") == "192.168.1.20");
  CHECK(clientKey("::FFFF:192.168.1.20") == "192.168.1.20");  // as lwIP reports an IPv4 peer on an IPv6 socket
  CHECK(clientKey("fe80::1%wlan0") == "fe80:0:0:0:0:0:0:1");
  CHECK(clientKey("2001:DB8:0::1") == "2001:db8:0:0:0:0:0:1");
  CHECK(clientKey("2001:db8::1") != clientKey("2001:db8::2"));  // one /64, two clients
  CHECK(clientKey("1:2:3:4:5:6:7:8") == "1:2:3:4:5:6:7:8");
  CHECK(clientKey("1:2:3:4:5:6:7::") == "1:2:3:4:5:6:7:0");
  CHECK(clientKey("::") == "0:0:0:0:0:0:0:0");
  for (const char* bad : {"", "localhost", "256.1.1.1", "1.2.3", "1.2.3.4.5", "01.2.3.4x", "1:2:3:4:5:6:7:8:9", "1::2::3", "12345::1", ":1:2:3:4:5:6:7", "g::1", "1.2.3.4::", "::1.2.3.4:5"})
    CHECK_MESSAGE(clientKey(bad).empty(), bad);
}
