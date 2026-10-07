// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_INSTALLER_PUBKY_UPDATE_HELPER_WIN_H_
#define CHROME_INSTALLER_PUBKY_UPDATE_HELPER_WIN_H_

#include <windows.h>

#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/time/time.h"
#include "base/version.h"

namespace base {
class CommandLine;
}

namespace pubky_update {

inline constexpr wchar_t kHelperExe[] = L"pubky_update_helper.exe";
inline constexpr char kEnvelopeSwitch[] = "pubky-update-envelope";
inline constexpr char kInstallerSwitch[] = "pubky-update-installer";
// Highest authenticated version this installation accepted, stored beside the
// native "pv" value. At system level only administrators can write it.
inline constexpr wchar_t kAcceptedFloorValue[] = L"PubkyUpdateAcceptedFloor";

// Helper process exit codes. The browser reads them after the helper exits.
enum HelperResult : int {
  kHelperInstalled = 0,
  kHelperBadArguments = 1,
  // No production key is compiled in, so every record is refused.
  kHelperDisabled = 2,
  // Wrong privilege, or the helper does not belong to the registered install.
  kHelperScope = 3,
  // Missing, linked, nonregular or oversized input file.
  kHelperInput = 4,
  kHelperRecord = 5,
  kHelperNotNewer = 6,
  kHelperReplay = 7,
  kHelperPackage = 8,
  kHelperLaunch = 9,
  kHelperFloor = 10,
  // A failed installer exit code N in [1, 65535] is reported as base + N.
  kHelperInstallerBase = 1000,
};

struct HelperContext {
  HelperContext();
  HelperContext(HelperContext&&);
  HelperContext& operator=(HelperContext&&);
  ~HelperContext();

  // Raw Ed25519 key. Empty until the owner's approved key is pinned.
  std::vector<uint8_t> public_key;
  bool system_install = false;
  bool elevated = false;
  base::FilePath helper_exe;
  // installer::GetInstalledDirectory() for this scope; empty if not installed.
  base::FilePath install_dir;
  HKEY floor_root = nullptr;
  std::wstring floor_key;
  base::RepeatingCallback<base::Version()> installed_version;
  base::Time now;
  // Runs the installer and waits. Returns its exit code, or nullopt if it did
  // not start. The helper keeps the file open against writes and deletion for
  // the whole call.
  base::RepeatingCallback<std::optional<int>(const base::CommandLine&)> run;
};

// Copies the caller's record and installer into a private snapshot, checks
// them again and runs only that snapshot. Returns a HelperResult.
int RunHelper(const base::CommandLine& command_line,
              const HelperContext& context);

}  // namespace pubky_update

#endif  // CHROME_INSTALLER_PUBKY_UPDATE_HELPER_WIN_H_
