// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_COMMON_PUBKY_UPDATE_TEST_RECORD_H_
#define CHROME_COMMON_PUBKY_UPDATE_TEST_RECORD_H_

#include <array>
#include <string>
#include <vector>

#include "base/base64.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/common/pubky_update/record.h"
#include "crypto/keypair.h"
#include "crypto/sign.h"

namespace pubky_update::test {
// RFC 8032 section 7.1 TEST 1. Public, test-only key material; never linked
// into the browser, helper or release-input tool.
inline std::vector<uint8_t> KeyBytes(std::string_view hex) {
  std::vector<uint8_t> bytes;
  base::HexStringToBytes(hex, &bytes);
  return bytes;
}
inline std::vector<uint8_t> PublicKey() {
  return KeyBytes("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
}
inline std::string Timestamp(base::Time time) {
  base::Time::Exploded e;
  time.UTCExplode(&e);
  return base::StringPrintf("%04d-%02d-%02dT%02d:%02d:%02dZ", e.year, e.month,
                            e.day_of_month, e.hour, e.minute, e.second);
}
inline base::DictValue Record(base::Time now, Target target = Target::kLinuxX64) {
  const bool is_linux = target == Target::kLinuxX64;
  base::DictValue r;
  r.Set("architecture", "x64");
  r.Set("asset", is_linux ? "pubky.deb" : "mini_installer.exe");
  r.Set("channel", "experimental");
  r.Set("expires_at", Timestamp(now + base::Days(30)));
  r.Set("issued_at", Timestamp(now - base::Minutes(1)));
  r.Set("native_version", is_linux ? "156.0.8073.1-1" : "156.0.8073.1");
  r.Set("package_id", is_linux ? "pubky-chromium" : "PubkyChromium");
  r.Set("platform", is_linux ? "linux" : "windows");
  r.Set("product", "Pubky Chromium");
  r.Set("product_version", "156.0.8073.1");
  r.Set("repository", "MCarlomagno/pubky-chromium");
  r.Set("schema", 1);
  r.Set("sha256", std::string(64, 'a'));
  r.Set("size", 123);
  r.Set("source_revision", std::string(40, 'b'));
  r.Set("tag", "v156.0.8073.1");
  r.Set("upstream_version", "156.0.8073.0");
  r.Set("url", std::string("https://github.com/MCarlomagno/pubky-chromium/releases/download/v156.0.8073.1/") +
                   (is_linux ? "pubky.deb" : "mini_installer.exe"));
  return r;
}
inline std::string Envelope(std::string_view payload,
                            std::string_view domain = kDomain) {
  auto seed = KeyBytes("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60");
  auto key = crypto::keypair::PrivateKey::FromEd25519PrivateKey(
      base::span(seed).first<32>());
  auto signature = crypto::sign::Sign(crypto::sign::ED25519, key,
      base::as_byte_span(std::string(domain) + std::string(payload)));
  base::DictValue outer;
  outer.Set("format", "pubky-updates-v1");
  outer.Set("payload", base::Base64Encode(payload));
  outer.Set("signature", base::Base64Encode(signature));
  return *base::WriteJson(outer);
}
inline std::string Envelope(const base::DictValue& record) {
  return Envelope(*base::WriteJson(record));
}
}  // namespace pubky_update::test
#endif  // CHROME_COMMON_PUBKY_UPDATE_TEST_RECORD_H_
