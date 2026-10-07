// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/installer/pubky_update/helper_win.h"

#include <array>
#include <string_view>

#include "base/command_line.h"
#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/win/registry.h"
#include "base/win/scoped_handle.h"
#include "chrome/common/pubky_update/record.h"
#include "chrome/installer/util/util_constants.h"
#include "crypto/hash.h"

namespace pubky_update {
namespace {

// Opens a caller-supplied file without following reparse points and accepts
// only a single-link regular disk file. Writers and deleters are shut out
// while the handle stays open.
base::File OpenInput(const base::FilePath& path) {
  if (!path.IsAbsolute()) {
    return base::File();
  }
  base::win::ScopedHandle handle(::CreateFileW(
      path.value().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
      OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN,
      nullptr));
  if (!handle.is_valid()) {
    return base::File();
  }
  BY_HANDLE_FILE_INFORMATION info = {};
  if (::GetFileType(handle.get()) != FILE_TYPE_DISK ||
      !::GetFileInformationByHandle(handle.get(), &info) ||
      (info.dwFileAttributes &
       (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
      info.nNumberOfLinks != 1) {
    return base::File();
  }
  return base::File(std::move(handle));
}

bool CopyExactly(base::File& from, const base::FilePath& to, int64_t size) {
  base::File out(to, base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  if (!out.IsValid()) {
    return false;
  }
  std::vector<uint8_t> buffer(1 << 16);
  int64_t total = 0;
  while (true) {
    std::optional<size_t> read = from.ReadAtCurrentPos(buffer);
    if (!read) {
      return false;
    }
    if (*read == 0) {
      return total == size;
    }
    total += static_cast<int64_t>(*read);
    if (total > size ||
        !out.WriteAtCurrentPosAndCheck(base::span(buffer).first(*read))) {
      return false;
    }
  }
}

std::optional<std::wstring> ReadFloor(const HelperContext& context) {
  base::win::RegKey key;
  std::wstring floor;
  if (key.Open(context.floor_root, context.floor_key.c_str(),
               KEY_QUERY_VALUE | KEY_WOW64_32KEY) != ERROR_SUCCESS) {
    return std::nullopt;
  }
  const LONG result = key.ReadValue(kAcceptedFloorValue, &floor);
  if (result == ERROR_FILE_NOT_FOUND) {
    return std::wstring();
  }
  return result == ERROR_SUCCESS ? std::optional(floor) : std::nullopt;
}

bool WriteFloor(const HelperContext& context, const base::Version& version) {
  base::win::RegKey key;
  return key.Open(context.floor_root, context.floor_key.c_str(),
                  KEY_SET_VALUE | KEY_WOW64_32KEY) == ERROR_SUCCESS &&
         key.WriteValue(kAcceptedFloorValue,
                        base::ASCIIToWide(version.GetString()).c_str()) ==
             ERROR_SUCCESS;
}

}  // namespace

HelperContext::HelperContext() = default;
HelperContext::HelperContext(HelperContext&&) = default;
HelperContext& HelperContext::operator=(HelperContext&&) = default;
HelperContext::~HelperContext() = default;

int RunHelper(const base::CommandLine& command_line,
              const HelperContext& context) {
  const base::FilePath envelope_path =
      command_line.GetSwitchValuePath(kEnvelopeSwitch);
  const base::FilePath installer_path =
      command_line.GetSwitchValuePath(kInstallerSwitch);
  if (command_line.GetSwitches().size() != 2 ||
      !command_line.GetArgs().empty() || envelope_path.empty() ||
      installer_path.empty()) {
    return kHelperBadArguments;
  }
  if (context.public_key.size() != 32) {
    return kHelperDisabled;
  }
  // Scope comes from where this helper is installed, never from the caller.
  if ((context.system_install && !context.elevated) ||
      context.install_dir.empty() ||
      !context.install_dir.IsParent(context.helper_exe)) {
    return kHelperScope;
  }
  const base::Version installed = context.installed_version.Run();
  if (!installed.IsValid()) {
    return kHelperScope;
  }

  std::string envelope;
  {
    base::File file = OpenInput(envelope_path);
    const int64_t length = file.IsValid() ? file.GetLength() : -1;
    if (length <= 0 || length > static_cast<int64_t>(kMaxEnvelopeBytes)) {
      return kHelperInput;
    }
    envelope.resize(static_cast<size_t>(length));
    if (!file.ReadAndCheck(0, base::as_writable_byte_span(envelope))) {
      return kHelperInput;
    }
  }

  // Never trust a floor supplied by the caller; read the protected one.
  const std::optional<std::wstring> floor = ReadFloor(context);
  if (!floor || (!floor->empty() &&
                 !base::Version(base::WideToASCII(*floor)).IsValid())) {
    return kHelperReplay;
  }
  RecordResult verified = VerifyRecord(
      envelope, context.public_key, Target::kWindowsX64, installed, installed,
      base::Version(base::WideToASCII(*floor)), context.now);
  if (!verified.record) {
    switch (verified.error) {
      case RecordError::kNoNewer:
        return kHelperNotNewer;
      case RecordError::kReplay:
        return kHelperReplay;
      default:
        return kHelperRecord;
    }
  }
  const Record& record = *verified.record;

  base::ScopedTempDir snapshot;
  // CreateNewTempDirectory picks an administrators-only location when the
  // process is elevated.
  if (!snapshot.CreateUniqueTempDir()) {
    return kHelperInput;
  }
  const base::FilePath installer_copy =
      snapshot.GetPath().Append(L"mini_installer.exe");
  {
    base::File file = OpenInput(installer_path);
    if (!file.IsValid()) {
      return kHelperInput;
    }
    if (file.GetLength() != record.size ||
        !CopyExactly(file, installer_copy, record.size)) {
      return kHelperPackage;
    }
  }
  // Held without write or delete sharing from hashing until the installer
  // exits, so the verified bytes are the executed bytes.
  base::File held(installer_copy, base::File::FLAG_OPEN |
                                      base::File::FLAG_READ |
                                      base::File::FLAG_WIN_EXCLUSIVE_WRITE);
  std::array<uint8_t, crypto::hash::kSha256Size> digest;
  if (!held.IsValid() || held.GetLength() != record.size ||
      !crypto::hash::HashFile(crypto::hash::kSha256, &held, digest) ||
      base::HexEncodeLower(digest) != record.sha256) {
    return kHelperPackage;
  }

  // Recheck native state immediately before mutation.
  const base::Version installed_now = context.installed_version.Run();
  if (!installed_now.IsValid() || installed_now != installed ||
      ReadFloor(context) != floor) {
    return kHelperScope;
  }
  if (!WriteFloor(context, record.version)) {
    return kHelperFloor;
  }

  // Native switches only: never downgrade, uninstall or launch the browser
  // from this process.
  base::CommandLine installer(installer_copy);
  installer.AppendSwitch(installer::switches::kDoNotLaunchChrome);
  installer.AppendSwitch(installer::switches::kVerboseLogging);
  if (context.system_install) {
    installer.AppendSwitch(installer::switches::kSystemLevel);
  }
  const std::optional<int> exit_code = context.run.Run(installer);
  if (!exit_code) {
    return kHelperLaunch;
  }
  if (*exit_code == 0) {
    return kHelperInstalled;
  }
  return *exit_code > 0 && *exit_code <= 65535
             ? kHelperInstallerBase + *exit_code
             : kHelperInstallerBase;
}

}  // namespace pubky_update
