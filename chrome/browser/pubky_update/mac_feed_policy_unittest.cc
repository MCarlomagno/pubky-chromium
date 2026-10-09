// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/pubky_update/mac_feed_policy.h"

#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pubky_update {
namespace {

TEST(PubkyUpdateMacTest, SignedItemMustHaveCurrentTargetAndExpiry) {
  const base::Time now = base::Time::Now();
  auto check = [&](std::string_view version, std::string_view floor,
                   std::string_view channel, std::string_view architecture,
                   const GURL& url, base::Time expiry) {
    return AcceptMacFeedItem(version, "8073.1", floor, channel, architecture,
                             "MCarlomagno/pubky-chromium", url, 4096,
                             now - base::Days(1), expiry, now);
  };
  const GURL url("https://github.com/MCarlomagno/pubky-chromium/"
                 "releases/download/v156.0.8073.2/pubky-chromium-macos-arm64.zip");
  EXPECT_TRUE(check("8073.2", "8073.2", "experimental", "arm64", url,
                    now + base::Days(1)));
  EXPECT_FALSE(check("8073.1", "", "experimental", "arm64", url,
                     now + base::Days(1)));
  EXPECT_FALSE(check("8073.2", "8073.3", "experimental", "arm64", url,
                     now + base::Days(1)));
  EXPECT_FALSE(check("8073.2", "broken", "experimental", "arm64", url,
                     now + base::Days(1)));
  EXPECT_FALSE(check("8073.2", "", "stable", "arm64", url,
                     now + base::Days(1)));
  EXPECT_FALSE(check("8073.2", "", "experimental", "x64", url,
                     now + base::Days(1)));
  EXPECT_FALSE(check("8073.2", "", "experimental", "arm64", url,
                     now - base::Seconds(1)));
  EXPECT_FALSE(check("8073.2", "", "experimental", "arm64",
                     GURL("https://github.com/other/repo/releases/download/v2/app.zip"),
                     now + base::Days(1)));
  EXPECT_FALSE(check("8073.2", "", "experimental", "arm64",
                     GURL(url.spec() + "?alternate=1"), now + base::Days(1)));
}

}  // namespace
}  // namespace pubky_update
