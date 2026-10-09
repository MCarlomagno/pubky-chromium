// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/pubky_update/mac_feed_policy.h"

#include <string>

#include "base/strings/string_util.h"
#include "base/version.h"

namespace pubky_update {

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
                       base::Time now) {
  const base::Version offered{std::string(version)};
  const base::Version current{std::string(running)};
  const base::Version previous{std::string(floor)};
  if (!offered.IsValid() || offered.components().size() != 2 ||
      offered.GetString() != version || !current.IsValid() ||
      current.components().size() != 2 || current.GetString() != running ||
      (!floor.empty() && (!previous.IsValid() || previous.GetString() != floor)) ||
      offered <= current || (previous.IsValid() && offered < previous) ||
      channel != "experimental" || architecture != "arm64" ||
      repository != "MCarlomagno/pubky-chromium" || length == 0 ||
      length >= 2147483648ULL || issued.is_null() || expires.is_null() ||
      expires <= issued || expires - issued > base::Days(180) ||
      issued > now + base::Minutes(5) || now >= expires) {
    return false;
  }
  constexpr std::string_view prefix =
      "https://github.com/MCarlomagno/pubky-chromium/releases/download/";
  const std::string& url = archive.spec();
  if (!archive.is_valid() || !base::StartsWith(url, prefix) ||
      archive.has_query() || archive.has_ref() || archive.has_username() ||
      archive.has_password() || archive.has_port() ||
      url.find('%') != std::string::npos || !base::EndsWith(url, ".zip")) {
    return false;
  }
  const std::string_view path = std::string_view(url).substr(prefix.size());
  const size_t separator = path.find('/');
  return separator != std::string_view::npos && separator > 0 &&
         separator + 1 < path.size() &&
         path.find('/', separator + 1) == std::string_view::npos &&
         path.find("..") == std::string_view::npos;
}

}  // namespace pubky_update
