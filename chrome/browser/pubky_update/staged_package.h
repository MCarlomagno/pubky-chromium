// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PUBKY_UPDATE_STAGED_PACKAGE_H_
#define CHROME_BROWSER_PUBKY_UPDATE_STAGED_PACKAGE_H_

#include <memory>
#include <optional>
#include <string>

#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/task/sequenced_task_runner.h"

namespace pubky_update {
// Stable native reason codes, exposed in cached About status. Lock readiness
// is only a hint; the installer must take/recheck its own locks later.
enum class EligibilityReason {
  kEligible = 0,
  kWrongArchitecture,
  kWrongExecutable,
  kUnprotectedExecutable,
  kStatusUnavailable,
  kPackageMissing,
  kPackageInvalid,
  kVersionMismatch,
  kSpaceUnavailable,
  kLowSpace,
  kLockUnavailable,
  kLockHeld,
  kInvalidSize,
};

struct StagedPackage {
  using Directory = std::unique_ptr<base::ScopedTempDir, base::OnTaskRunnerDeleter>;
  // Owning the result keeps its files alive. Discard/cancel deletes them on the
  // blocking sequence, including results delivered after a weak reply expires.
  Directory directory{nullptr, base::OnTaskRunnerDeleter(nullptr)};
  base::FilePath path;
  int64_t size = 0;
  std::string sha256;
  std::string envelope;
};
struct StageResult {
  std::optional<StagedPackage> package;
  EligibilityReason eligibility = EligibilityReason::kEligible;
};
}  // namespace pubky_update
#endif  // CHROME_BROWSER_PUBKY_UPDATE_STAGED_PACKAGE_H_
