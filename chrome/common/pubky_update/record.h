// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_COMMON_PUBKY_UPDATE_RECORD_H_
#define CHROME_COMMON_PUBKY_UPDATE_RECORD_H_

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "base/containers/span.h"
#include "base/time/time.h"
#include "base/version.h"
#include "url/gurl.h"

namespace pubky_update {
inline constexpr size_t kMaxEnvelopeBytes = 128 * 1024;
inline constexpr size_t kMaxPayloadBytes = 64 * 1024;
inline constexpr char kDomain[] = "PubkyChromiumUpdateCatalogV1\n";
enum class Target { kLinuxX64, kWindowsX64 };

struct Record {
  base::Version version;
  std::string native_version;
  GURL url;
  int size = 0;
  std::string sha256;
  std::string authenticated_envelope;
  base::Time expires_at;
};

enum class RecordError {
  kNone, kSchema, kSignature, kTarget, kOrigin, kVersion, kReplay, kExpired,
  kNoNewer
};
struct RecordResult {
  std::optional<Record> record;
  RecordError error = RecordError::kNone;
};

// The owner's approved production key, pinned at compile time.
inline constexpr char kProductionPublicKey[] =
    "RGzK2rB/P4diYxd70MkBDc4FDUO0JikBqrFaSWTVOfI=";
// Strict base64 of 32 raw Ed25519 bytes. Anything else, including the public
// RFC 8032 test key, yields nullopt so the updater stays disabled.
std::optional<std::array<uint8_t, 32>> DecodePinnedKey(std::string_view base64);
std::optional<std::array<uint8_t, 32>> ProductionPublicKey();

GURL FeedUrl(Target target);
bool IsPackageRedirectAllowed(const GURL& url);
bool IsMetadataRedirectAllowed(Target target, const GURL& url);
// All version inputs come from native installation state, never the renderer.
RecordResult VerifyRecord(std::string_view envelope,
                          base::span<const uint8_t> public_key,
                          Target target,
                          const base::Version& running,
                          const base::Version& installed,
                          const base::Version& offered_floor,
                          base::Time now);
}  // namespace pubky_update
#endif  // CHROME_COMMON_PUBKY_UPDATE_RECORD_H_
