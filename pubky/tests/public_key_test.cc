#include "net/base/pubky_public_key.h"

#include <cassert>
#include <string>

int main() {
  constexpr auto kHost =
      "4msqbgpkfcdgnzrrsyp5hgno8rfa4sx15c79ughsq95ikycunowy";
  const std::array<uint8_t, 32> expected = {
      0xd2, 0xec, 0xe0, 0x99, 0xaa, 0x2b, 0x06, 0x61,
      0x5c, 0x84, 0xb0, 0x1b, 0xbe, 0x18, 0x50, 0x39,
      0x0b, 0x8d, 0x59, 0xf2, 0xdb, 0x3b, 0xf9, 0x9b,
      0x96, 0x77, 0xf7, 0x55, 0x01, 0x93, 0x14, 0x28};
  assert(net::ParsePubkyPublicKey(kHost) == expected);
  assert(net::ParsePubkyPublicKey(std::string(kHost) + ".") == expected);
  assert(net::ParsePubkyPublicKey(std::string("www.") + kHost) == expected);
  assert(!net::ParsePubkyPublicKey("example.com"));
  assert(!net::ParsePubkyPublicKey(std::string(kHost) + ".com"));
  assert(!net::ParsePubkyPublicKey(std::string(kHost) + ".evil."));
  assert(!net::ParsePubkyPublicKey(std::string(kHost).substr(1)));
  assert(!net::ParsePubkyPublicKey(std::string(kHost) + "y"));
  assert(!net::ParsePubkyPublicKey(std::string(52, '0')));
  // Reject every nonzero padding variant of this canonical key.
  constexpr std::string_view alphabet = "ybndrfg8ejkmcpqxot1uwisza345h769";
  for (size_t i = 1; i < 16; ++i) {
    std::string noncanonical(kHost);
    noncanonical.back() = alphabet[i];
    assert(!net::ParsePubkyPublicKey(noncanonical));
  }
}
