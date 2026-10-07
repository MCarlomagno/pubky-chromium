// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/pubky_update/record.h"

#include <algorithm>
#include <array>

#include "base/base64.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"
#include "crypto/keypair.h"
#include "crypto/sign.h"

namespace pubky_update {
namespace {
constexpr std::array<std::string_view, 16> kStrings = {
    "architecture", "asset", "channel", "expires_at", "issued_at",
    "native_version", "package_id", "platform", "product", "product_version",
    "repository", "sha256", "source_revision", "tag", "upstream_version", "url"};

bool StrictBase64(std::string_view text, std::string* bytes) {
  return base::Base64Decode(text, bytes) && base::Base64Encode(*bytes) == text;
}
bool PlainString(std::string_view s) {
  return !s.empty() && s.size() <= 2048 &&
         std::ranges::all_of(s, [](unsigned char c) {
           return c >= 0x20 && c <= 0x7e && c != '"' && c != '\\' &&
                  c != '<' && c != '>';
         });
}
bool Hex(std::string_view s, size_t length) {
  return s.size() == length && std::ranges::all_of(s, [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  });
}
bool Segment(std::string_view s) {
  return !s.empty() && s.size() <= 200 && s != "." && s != ".." &&
         std::ranges::all_of(s, [](char c) {
           return base::IsAsciiAlpha(c) || base::IsAsciiDigit(c) || c == '-' ||
                  c == '_' || c == '.';
         });
}
bool Version(const base::Version& v, std::string_view text) {
  return v.IsValid() && v.components().size() == 4 && v.GetString() == text &&
         std::ranges::all_of(v.components(), [](uint32_t n) { return n <= 65535; });
}
bool Timestamp(std::string_view text, base::Time* time) {
  // FromUTCString accepts other date forms; limit the wire form first.
  if (text.size() != 20 || text[4] != '-' || text[7] != '-' || text[10] != 'T' ||
      text[13] != ':' || text[16] != ':' || text[19] != 'Z') {
    return false;
  }
  for (size_t i = 0; i < text.size(); ++i) {
    if (i != 4 && i != 7 && i != 10 && i != 13 && i != 16 && i != 19 &&
        !base::IsAsciiDigit(text[i])) {
      return false;
    }
  }
  if (!base::Time::FromUTCString(std::string(text).c_str(), time)) {
    return false;
  }
  base::Time::Exploded e;
  time->UTCExplode(&e);
  // Reject normalization of invalid calendar values and leap seconds.
  return base::StringPrintf("%04d-%02d-%02dT%02d:%02d:%02dZ", e.year, e.month,
                            e.day_of_month, e.hour, e.minute, e.second) == text;
}
bool SecureUrl(const GURL& url) {
  return url.is_valid() && url.SchemeIs("https") && !url.has_username() &&
         !url.has_password() && !url.has_port() && !url.has_ref();
}
RecordResult Fail(RecordError error) { return {.error = error}; }
}  // namespace

std::optional<std::array<uint8_t, 32>> DecodePinnedKey(std::string_view base64) {
  // RFC 8032 section 7.1 TEST 1, used by the public fixtures.
  constexpr std::string_view kTestKey =
      "11qYAYKxCrfVS/7TyWQHOg7hcvPapiMlrwIaaPcHURo=";
  std::string bytes;
  if (base64 == kTestKey || !StrictBase64(base64, &bytes) || bytes.size() != 32) {
    return std::nullopt;
  }
  std::array<uint8_t, 32> key;
  std::ranges::copy(bytes, key.begin());
  return key;
}
std::optional<std::array<uint8_t, 32>> ProductionPublicKey() {
  return DecodePinnedKey(kProductionPublicKey);
}

GURL FeedUrl(Target target) {
  return GURL(base::StrCat({
      "https://raw.githubusercontent.com/MCarlomagno/pubky-chromium/",
      "update-manifests-v1/experimental/",
      target == Target::kLinuxX64 ? "linux-x64.json" : "windows-x64.json"}));
}
bool IsMetadataRedirectAllowed(Target target, const GURL& url) {
  return SecureUrl(url) && url == FeedUrl(target);
}
bool IsPackageRedirectAllowed(const GURL& url) {
  return SecureUrl(url) &&
         (url.host() == "release-assets.githubusercontent.com" ||
          url.host() == "objects.githubusercontent.com");
}

RecordResult VerifyRecord(std::string_view envelope,
                          base::span<const uint8_t> public_key,
                          Target target,
                          const base::Version& running,
                          const base::Version& installed,
                          const base::Version& offered_floor,
                          base::Time now) {
  if (envelope.size() > kMaxEnvelopeBytes) {
    return Fail(RecordError::kSchema);
  }
  auto outer = base::JSONReader::ReadDict(envelope, base::JSON_PARSE_RFC, 3);
  if (!outer || outer->size() != 3 ||
      !outer->FindString("format") ||
      *outer->FindString("format") != "pubky-updates-v1" ||
      !outer->FindString("payload") || !outer->FindString("signature") ||
      base::WriteJson(*outer) != envelope) {
    return Fail(RecordError::kSchema);
  }
  std::string payload, signature;
  if (!StrictBase64(*outer->FindString("payload"), &payload) ||
      payload.size() > kMaxPayloadBytes ||
      !StrictBase64(*outer->FindString("signature"), &signature) ||
      signature.size() != 64 || public_key.size() != 32) {
    return Fail(RecordError::kSignature);
  }
  auto key = crypto::keypair::PublicKey::FromEd25519PublicKey(public_key.first<32>());
  const std::string message = base::StrCat({kDomain, payload});
  if (!crypto::sign::Verify(crypto::sign::ED25519, key,
                                   base::as_byte_span(message),
                                   base::as_byte_span(signature))) {
    return Fail(RecordError::kSignature);
  }
  auto dict = base::JSONReader::ReadDict(payload, base::JSON_PARSE_RFC, 3);
  if (!dict || dict->size() != 18 || dict->FindInt("schema") != 1 ||
      !dict->FindInt("size") || *dict->FindInt("size") <= 0 ||
      base::WriteJson(*dict) != payload) {
    return Fail(RecordError::kSchema);
  }
  for (auto field : kStrings) {
    const std::string* s = dict->FindString(field);
    if (!s || !PlainString(*s)) {
      return Fail(RecordError::kSchema);
    }
  }
  auto get = [&](std::string_view field) -> const std::string& {
    return *dict->FindString(field);
  };
  const bool is_linux = target == Target::kLinuxX64;
  if (get("repository") != "MCarlomagno/pubky-chromium" ||
      get("product") != "Pubky Chromium" || get("channel") != "experimental" ||
      get("architecture") != "x64" ||
      get("platform") != (is_linux ? "linux" : "windows") ||
      get("package_id") != (is_linux ? "pubky-chromium" : "PubkyChromium")) {
    return Fail(RecordError::kTarget);
  }
  if (!Segment(get("tag")) || !Segment(get("asset")) ||
      !base::EndsWith(get("asset"), is_linux ? ".deb" : ".exe") ||
      !Hex(get("sha256"), 64) || !Hex(get("source_revision"), 40)) {
    return Fail(RecordError::kSchema);
  }
  Record record;
  record.url = GURL(get("url"));
  const std::string expected_url = base::StrCat({
      "https://github.com/MCarlomagno/pubky-chromium/releases/download/",
      get("tag"), "/", get("asset")});
  if (!SecureUrl(record.url) || record.url.has_query() ||
      get("url") != expected_url || record.url.spec() != expected_url) {
    return Fail(RecordError::kOrigin);
  }
  record.version = base::Version(get("product_version"));
  base::Version upstream(get("upstream_version"));
  record.native_version = get("native_version");
  // v1 uses the generated product tuple and a fixed Debian revision. Native
  // helpers must still read and compare the actual installed package/PE version.
  if (!Version(record.version, get("product_version")) ||
      !Version(upstream, get("upstream_version")) ||
      record.native_version != get("product_version") + (is_linux ? "-1" : "") ||
      !running.IsValid() || !installed.IsValid() ||
      !Version(running, running.GetString()) ||
      !Version(installed, installed.GetString())) {
    return Fail(RecordError::kVersion);
  }
  if (offered_floor.IsValid() && record.version < offered_floor) {
    return Fail(RecordError::kReplay);
  }
  base::Time issued;
  if (!Timestamp(get("issued_at"), &issued) ||
      !Timestamp(get("expires_at"), &record.expires_at) ||
      record.expires_at <= issued || record.expires_at - issued > base::Days(180) ||
      issued > now + base::Minutes(5) || now >= record.expires_at) {
    return Fail(RecordError::kExpired);
  }
  record.size = *dict->FindInt("size");
  record.sha256 = get("sha256");
  record.authenticated_envelope = envelope;
  if (record.version <= running || record.version <= installed) {
    return Fail(RecordError::kNoNewer);
  }
  return {.record = std::move(record)};
}
}  // namespace pubky_update
