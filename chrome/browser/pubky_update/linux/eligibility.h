// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PUBKY_UPDATE_LINUX_ELIGIBILITY_H_
#define CHROME_BROWSER_PUBKY_UPDATE_LINUX_ELIGIBILITY_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/files/file_path.h"
#include "base/version.h"
#include "chrome/browser/pubky_update/staged_package.h"

namespace pubky_update {
inline constexpr int64_t kStagingSpaceMargin = 64 * 1024 * 1024;
struct LinuxEligibilityFacts {
  std::string architecture;
  base::FilePath executable;
  bool protected_executable = false;
  std::optional<std::string> status;
  std::optional<int64_t> free_bytes;
  std::optional<bool> locks_held;
};
// Pure evaluation seam. Tests supply filesystem/status facts without changing
// host package state or requiring root-owned test files.
EligibilityReason EvaluateLinuxEligibility(const LinuxEligibilityFacts& facts,
                                           const base::Version& running,
                                           int signed_size);
// Blocking, read-only. No shell, subprocess, lock acquisition or privilege.
EligibilityReason ProbeLinuxEligibility(const base::Version& running,
                                        const base::FilePath& staging_base,
                                        int signed_size);
// F_GETLK observes dpkg's POSIX locks without acquiring them. An absent or
// unreadable lock file is unknown, not evidence of readiness.
std::optional<bool> IsDpkgLockHeld(const base::FilePath& path);
}  // namespace pubky_update
#endif  // CHROME_BROWSER_PUBKY_UPDATE_LINUX_ELIGIBILITY_H_
