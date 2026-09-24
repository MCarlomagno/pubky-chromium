// Copyright 2026 The Pubky Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_PUBKY_PUBLIC_KEY_H_
#define NET_BASE_PUBKY_PUBLIC_KEY_H_

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace net {

// The final DNS label is a canonical z-base-32 Ed25519 public key. A DNS
// root dot is permitted; nonzero padding bits are not. URL canonicalization
// lowercases hostnames before they reach this function.
inline std::optional<std::array<uint8_t, 32>> ParsePubkyPublicKey(
    std::string_view hostname) {
  constexpr std::string_view kAlphabet = "ybndrfg8ejkmcpqxot1uwisza345h769";
  if (hostname.ends_with('.')) {
    hostname.remove_suffix(1);
  }
  const size_t dot = hostname.rfind('.');
  const std::string_view label =
      dot == std::string_view::npos ? hostname : hostname.substr(dot + 1);
  if (label.size() != 52) {
    return std::nullopt;
  }

  std::array<uint8_t, 32> key{};
  uint32_t accumulator = 0;
  unsigned bits = 0;
  size_t output = 0;
  for (char character : label) {
    const size_t value = kAlphabet.find(character);
    if (value == std::string_view::npos) {
      return std::nullopt;
    }
    accumulator = (accumulator << 5) | static_cast<uint32_t>(value);
    bits += 5;
    if (bits >= 8) {
      bits -= 8;
      key[output++] = static_cast<uint8_t>(accumulator >> bits);
      accumulator &= (1u << bits) - 1;
    }
  }
  if (accumulator != 0) {
    return std::nullopt;
  }
  return key;
}

}  // namespace net

#endif  // NET_BASE_PUBKY_PUBLIC_KEY_H_
