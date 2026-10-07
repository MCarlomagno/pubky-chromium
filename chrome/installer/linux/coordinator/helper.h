// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef CHROME_INSTALLER_LINUX_COORDINATOR_HELPER_H_
#define CHROME_INSTALLER_LINUX_COORDINATOR_HELPER_H_

#include <sys/types.h>

#include <optional>
#include <string>
#include <string_view>

namespace pubky_update {

struct Control {
  std::string package;
  std::string architecture;
  std::string version;
};

// Reject ambiguous or policy-changing control fields before invoking dpkg.
std::optional<Control> ParseControl(std::string_view text);
// Traverse each component without links and require caller-owned private staging.
int OpenStagedFile(std::string_view directory, std::string_view name, uid_t uid);

}  // namespace pubky_update
#endif  // CHROME_INSTALLER_LINUX_COORDINATOR_HELPER_H_
