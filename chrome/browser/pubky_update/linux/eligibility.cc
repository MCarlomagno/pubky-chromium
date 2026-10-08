// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/pubky_update/linux/eligibility.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/posix/eintr_wrapper.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/system/sys_info.h"

namespace pubky_update {
namespace {
constexpr char kExecutable[] = "/opt/pubky-chromium/chrome";
// This is a browser-side eligibility hint, not helper-side authorization.
bool ProtectedExecutable(const base::FilePath& executable) {
  for (auto path = executable;; path = path.DirName()) {
    struct stat info = {};
    if (stat(path.value().c_str(), &info) != 0 || info.st_uid != 0 ||
        (path == executable ? !S_ISREG(info.st_mode) : !S_ISDIR(info.st_mode)) ||
        (info.st_mode & (S_IWGRP | S_IWOTH)) ||
        base::PathIsWritable(path)) {
      return false;
    }
    if (path == path.DirName()) {
      return true;
    }
  }
}
struct InstalledPackage {
  std::string version;
  bool valid = false;
};
// Ignore unrelated paragraphs/description continuations. Parse only the exact
// package stanza; duplicates or broken/multiarch state fail closed.
std::optional<InstalledPackage> ReadPackage(std::string_view status) {
  std::optional<InstalledPackage> result;
  size_t begin = 0;
  while (begin < status.size()) {
    const size_t end = status.find("\n\n", begin);
    const auto stanza = status.substr(begin, end == std::string_view::npos
                                                 ? status.size() - begin
                                                 : end - begin);
    auto lines = base::SplitStringPiece(stanza, "\n", base::KEEP_WHITESPACE,
                                       base::SPLIT_WANT_ALL);
    int packages = 0;
    bool target = false;
    for (auto line : lines) {
      if (line.starts_with("Package:")) {
        ++packages;
        target |= base::TrimWhitespaceASCII(line.substr(8), base::TRIM_ALL) ==
                  "pubky-chromium";
      }
    }
    if (target) {
      if (result) {
        return InstalledPackage{};
      }
      InstalledPackage package;
      int versions = 0, states = 0, architectures = 0;
      bool installed = false, amd64 = false;
      for (auto line : lines) {
        if (line.starts_with("Version:")) {
          ++versions;
          package.version = base::TrimWhitespaceASCII(line.substr(8), base::TRIM_ALL);
        } else if (line.starts_with("Status:")) {
          ++states;
          installed = base::TrimWhitespaceASCII(line.substr(7), base::TRIM_ALL) ==
                      "install ok installed";
        } else if (line.starts_with("Architecture:")) {
          ++architectures;
          amd64 = base::TrimWhitespaceASCII(line.substr(13), base::TRIM_ALL) == "amd64";
        }
      }
      package.valid = packages == 1 && versions == 1 && states == 1 &&
                      architectures == 1 && installed && amd64;
      result = std::move(package);
    }
    if (end == std::string_view::npos) {
      break;
    }
    begin = end + 2;
  }
  return result;
}
}  // namespace

std::optional<bool> IsDpkgLockHeld(const base::FilePath& path) {
  base::File file(HANDLE_EINTR(open(path.value().c_str(),
                                  O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)));
  struct stat info = {};
  if (!file.IsValid() || fstat(file.GetPlatformFile(), &info) != 0 ||
      !S_ISREG(info.st_mode)) {
    return std::nullopt;
  }
  struct flock lock = {};
  lock.l_type = F_WRLCK;
  lock.l_whence = SEEK_SET;
  if (HANDLE_EINTR(fcntl(file.GetPlatformFile(), F_GETLK, &lock)) != 0) {
    return std::nullopt;
  }
  return lock.l_type != F_UNLCK;
}

EligibilityReason EvaluateLinuxEligibility(const LinuxEligibilityFacts& facts,
                                           const base::Version& running,
                                           int signed_size) {
  if (facts.architecture != "x86_64") {
    return EligibilityReason::kWrongArchitecture;
  }
  if (facts.executable != base::FilePath(kExecutable)) {
    return EligibilityReason::kWrongExecutable;
  }
  if (!facts.protected_executable) {
    return EligibilityReason::kUnprotectedExecutable;
  }
  if (!facts.status) {
    return EligibilityReason::kStatusUnavailable;
  }
  auto package = ReadPackage(*facts.status);
  if (!package) {
    return EligibilityReason::kPackageMissing;
  }
  if (!package->valid) {
    return EligibilityReason::kPackageInvalid;
  }
  if (!running.IsValid() || package->version != running.GetString() + "-1") {
    return EligibilityReason::kVersionMismatch;
  }
  if (signed_size <= 0) {
    return EligibilityReason::kInvalidSize;
  }
  if (!facts.free_bytes || *facts.free_bytes < 0) {
    return EligibilityReason::kSpaceUnavailable;
  }
  if (*facts.free_bytes < static_cast<int64_t>(signed_size) + kStagingSpaceMargin) {
    return EligibilityReason::kLowSpace;
  }
  if (!facts.locks_held) {
    return EligibilityReason::kLockUnavailable;
  }
  return *facts.locks_held ? EligibilityReason::kLockHeld
                           : EligibilityReason::kEligible;
}

EligibilityReason ProbeLinuxEligibility(const base::Version& running,
                                        const base::FilePath& staging_base,
                                        int signed_size) {
  LinuxEligibilityFacts facts;
  facts.architecture = base::SysInfo::ProcessCPUArchitecture();
  facts.executable = base::MakeAbsoluteFilePath(base::FilePath("/proc/self/exe"));
  facts.protected_executable = facts.executable == base::FilePath(kExecutable) &&
                               ProtectedExecutable(facts.executable);
  std::string status;
  if (base::ReadFileToStringWithMaxSize(base::FilePath("/var/lib/dpkg/status"),
                                       &status, 64 * 1024 * 1024)) {
    facts.status = std::move(status);
  }
  facts.free_bytes = base::SysInfo::AmountOfFreeDiskSpace(staging_base);
  const auto database = IsDpkgLockHeld(base::FilePath("/var/lib/dpkg/lock"));
  const auto frontend = IsDpkgLockHeld(base::FilePath("/var/lib/dpkg/lock-frontend"));
  if (database && frontend) {
    facts.locks_held = *database || *frontend;
  }
  return EvaluateLinuxEligibility(facts, running, signed_size);
}
}  // namespace pubky_update
