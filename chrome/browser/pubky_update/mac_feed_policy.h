// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PUBKY_UPDATE_MAC_FEED_POLICY_H_
#define CHROME_BROWSER_PUBKY_UPDATE_MAC_FEED_POLICY_H_

#include <cstdint>
#include <string_view>

#include "base/time/time.h"
#include "url/gurl.h"

namespace pubky_update {

// The caller must first establish Sparkle's signed-feed validation succeeded.
bool AcceptMacFeedItem(std::string_view version,
                       std::string_view running,
                       std::string_view floor,
                       std::string_view channel,
                       std::string_view architecture,
                       std::string_view repository,
                       const GURL& archive,
                       uint64_t length,
                       base::Time issued,
                       base::Time expires,
                       base::Time now);

}  // namespace pubky_update

#endif  // CHROME_BROWSER_PUBKY_UPDATE_MAC_FEED_POLICY_H_
