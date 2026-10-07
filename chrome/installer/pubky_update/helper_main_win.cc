// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// One-shot installer helper for Pubky Chromium's explicit About updates. It
// lives in the versioned install directory, so an update never overwrites a
// running copy. System-level installs start it through a UAC prompt.

#include <windows.h>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/process/process_info.h"
#include "chrome/common/pubky_update/record.h"
#include "chrome/install_static/install_util.h"
#include "chrome/install_static/product_install_details.h"
#include "chrome/installer/pubky_update/helper_win.h"
#include "chrome/installer/util/helper.h"
#include "chrome/installer/util/install_util.h"

namespace {

std::optional<int> RunAndWait(const base::CommandLine& command_line) {
  base::LaunchOptions options;
  options.start_hidden = true;
  base::Process process = base::LaunchProcess(command_line, options);
  int exit_code = 0;
  if (!process.IsValid() || !process.WaitForExit(&exit_code)) {
    return std::nullopt;
  }
  return exit_code;
}

}  // namespace

extern "C" int WINAPI wWinMain(HINSTANCE, HINSTANCE, wchar_t*, int) {
  base::AtExitManager exit_manager;
  base::CommandLine::Init(0, nullptr);
  install_static::InitializeProductDetailsForPrimaryModule();

  pubky_update::HelperContext context;
  if (const auto key = pubky_update::ProductionPublicKey()) {
    context.public_key.assign(key->begin(), key->end());
  }
  context.system_install = install_static::IsSystemInstall();
  context.elevated = base::IsCurrentProcessElevated();
  context.helper_exe = base::PathService::CheckedGet(base::FILE_EXE);
  context.install_dir =
      installer::GetInstalledDirectory(context.system_install);
  context.floor_root =
      context.system_install ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
  context.floor_key = install_static::GetClientsKeyPath();
  context.installed_version = base::BindRepeating(
      &InstallUtil::GetChromeVersion, context.system_install);
  context.now = base::Time::Now();
  context.run = base::BindRepeating(&RunAndWait);
  return pubky_update::RunHelper(*base::CommandLine::ForCurrentProcess(),
                                 context);
}
