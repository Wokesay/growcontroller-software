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
  CHECK(b.login("geheim-123", 0, e).has_value());
  CHECK(a.toJson().dump().find("geheim") == std::string::npos);  // nur Hash
}

TEST_CASE("Anmeldung: Sperre nach Fehlversuchen (EN 18031 AUM-6)") {
  Auth a(rng);
  a.setInitialPassword("geheim-123");
  Msg e;
  for (int i = 0; i < 5; ++i) CHECK_FALSE(a.login("falsch", 1000, e));
  CHECK_FALSE(a.login("geheim-123", 2000, e));
  CHECK(e.key == "auth.locked");
  CHECK(a.login("geheim-123", 2000 + 31 * kSecond, e).has_value());
}

TEST_CASE("Anmeldung: Sitzung läuft ab, Passwortwechsel meldet alle ab") {
  Auth a(rng);
  a.setInitialPassword("geheim-123");
  Msg e;
  auto t = a.login("geheim-123", 0, e);
  REQUIRE(t);
  CHECK(a.check(*t, kHour));
  CHECK_FALSE(a.check(*t, kHour + Auth::kSessionIdle + 1));
  auto t2 = a.login("geheim-123", 0, e);
  CHECK(a.changePassword("geheim-123", "neu-geheim-456").key.empty());
  CHECK_FALSE(a.check(*t2, 1));
  CHECK(a.changePassword("falsch", "x-123456789").key == "auth.wrong");
}
