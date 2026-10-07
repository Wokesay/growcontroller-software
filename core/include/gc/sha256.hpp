// SPDX-License-Identifier: AGPL-3.0-or-later
// SHA-256, HMAC-SHA-256 und PBKDF2 (FIPS 180-4, RFC 2104, RFC 8018).
// Auf dem Gerät später durch mbedTLS mit Hardware-Beschleunigung ersetzbar.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace gc {

using Digest = std::array<std::uint8_t, 32>;

Digest sha256(const std::uint8_t* data, size_t len);
Digest sha256(const std::string& s);
Digest hmacSha256(const std::vector<std::uint8_t>& key, const std::uint8_t* data, size_t len);
std::vector<std::uint8_t> pbkdf2Sha256(const std::string& password, const std::vector<std::uint8_t>& salt,
                                       std::uint32_t iterations, size_t outLen);
std::string toHex(const std::uint8_t* data, size_t len);
std::vector<std::uint8_t> fromHex(const std::string& hex);

}  // namespace gc
