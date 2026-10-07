// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/installer/pubky_update/helper_win.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "base/command_line.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/bind.h"
#include "base/test/test_reg_util_win.h"
#include "base/win/registry.h"
#include "chrome/common/pubky_update/test_record.h"
#include "chrome/installer/util/util_constants.h"
#include "crypto/hash.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pubky_update {
namespace {

constexpr wchar_t kKey[] = L"Software\\PubkyChromiumUpdateTest";

class PubkyUpdateHelperTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_NO_FATAL_FAILURE(registry_.OverrideRegistry(HKEY_CURRENT_USER));
    base::win::RegKey key;
    ASSERT_EQ(ERROR_SUCCESS, key.Create(HKEY_CURRENT_USER, kKey,
                                        KEY_ALL_ACCESS | KEY_WOW64_32KEY));
    ASSERT_TRUE(dir_.CreateUniqueTempDir());
    envelope_ = dir_.GetPath().Append(L"record.json");
    installer_ = dir_.GetPath().Append(L"mini_installer.exe");
    ASSERT_TRUE(base::WriteFile(installer_, kInstaller));
    WriteEnvelope(Record());

    context_.public_key = test::PublicKey();
    context_.install_dir = dir_.GetPath().Append(L"Application");
    context_.helper_exe =
        context_.install_dir.Append(L"156.0.8073.0").Append(kHelperExe);
    context_.floor_root = HKEY_CURRENT_USER;
    context_.floor_key = kKey;
    context_.installed_version = base::BindLambdaForTesting(
        [this] { return base::Version(installed_[version_reads_++ ? 1 : 0]); });
    context_.now = base::Time::Now();
    context_.run = base::BindLambdaForTesting(
        [this](const base::CommandLine& command_line) -> std::optional<int> {
          ++runs_;
          ran_ = command_line;
          const base::FilePath program = command_line.GetProgram();
          // The installer runs from the helper's snapshot, which nobody can
          // replace or delete until it exits.
          EXPECT_NE(installer_, program);
          std::string bytes;
          EXPECT_TRUE(base::ReadFileToString(program, &bytes));
          EXPECT_EQ(kInstaller, bytes);
          EXPECT_FALSE(base::File(program, base::File::FLAG_OPEN |
                                               base::File::FLAG_WRITE)
                           .IsValid());
          EXPECT_FALSE(base::DeleteFile(program));
          return exit_code_;
        });
  }

  base::DictValue Record() {
    base::DictValue record =
        test::Record(base::Time::Now(), Target::kWindowsX64);
    record.Set("sha256",
               base::HexEncodeLower(crypto::hash::Sha256(kInstaller)));
    record.Set("size", static_cast<int>(kInstaller.size()));
    return record;
  }
  void WriteEnvelope(const base::DictValue& record) {
    ASSERT_TRUE(base::WriteFile(envelope_, test::Envelope(record)));
  }
  int Run() {
    base::CommandLine command_line(base::FilePath(L"helper.exe"));
    command_line.AppendSwitchPath(kEnvelopeSwitch, envelope_);
    command_line.AppendSwitchPath(kInstallerSwitch, installer_);
    version_reads_ = 0;
    return RunHelper(command_line, context_);
  }
  std::wstring Floor() {
    base::win::RegKey key(HKEY_CURRENT_USER, kKey,
                          KEY_QUERY_VALUE | KEY_WOW64_32KEY);
    std::wstring floor;
    key.ReadValue(kAcceptedFloorValue, &floor);
    return floor;
  }
  void SetFloor(const wchar_t* floor) {
    base::win::RegKey key(HKEY_CURRENT_USER, kKey,
                          KEY_SET_VALUE | KEY_WOW64_32KEY);
    ASSERT_EQ(ERROR_SUCCESS, key.WriteValue(kAcceptedFloorValue, floor));
  }

  static constexpr std::string_view kInstaller = "MZ test installer bytes";
  registry_util::RegistryOverrideManager registry_;
  base::ScopedTempDir dir_;
  base::FilePath envelope_;
  base::FilePath installer_;
  HelperContext context_;
  // First and later installed-version reads.
  std::array<std::string, 2> installed_ = {"156.0.8073.0", "156.0.8073.0"};
  int version_reads_ = 0;
  int runs_ = 0;
  std::optional<int> exit_code_ = 0;
  base::CommandLine ran_{base::CommandLine::NO_PROGRAM};
};

TEST_F(PubkyUpdateHelperTest, RunsOnlyTheVerifiedSnapshot) {
  EXPECT_EQ(kHelperInstalled, Run());
  EXPECT_EQ(1, runs_);
  EXPECT_TRUE(ran_.HasSwitch(installer::switches::kDoNotLaunchChrome));
  EXPECT_FALSE(ran_.HasSwitch(installer::switches::kSystemLevel));
  EXPECT_FALSE(ran_.HasSwitch(installer::switches::kAllowDowngrade));
  EXPECT_EQ(L"156.0.8073.1", Floor());
  // The snapshot is removed once the installer exits.
  EXPECT_FALSE(base::PathExists(ran_.GetProgram()));

  context_.system_install = true;
  context_.elevated = true;
  installed_[0] = installed_[1] = "156.0.8073.0";
  EXPECT_EQ(kHelperInstalled, Run());
  EXPECT_TRUE(ran_.HasSwitch(installer::switches::kSystemLevel));
}

TEST_F(PubkyUpdateHelperTest, RefusesBeforeMutation) {
  base::CommandLine extra(base::FilePath(L"helper.exe"));
  extra.AppendSwitchPath(kEnvelopeSwitch, envelope_);
  extra.AppendSwitchPath(kInstallerSwitch, installer_);
  extra.AppendSwitch(installer::switches::kAllowDowngrade);
  EXPECT_EQ(kHelperBadArguments, RunHelper(extra, context_));

  context_.public_key.clear();
  EXPECT_EQ(kHelperDisabled, Run());
  context_.public_key = test::PublicKey();

  context_.system_install = true;
  EXPECT_EQ(kHelperScope, Run());
  context_.system_install = false;
  const base::FilePath helper = context_.helper_exe;
  context_.helper_exe = dir_.GetPath().Append(kHelperExe);
  EXPECT_EQ(kHelperScope, Run());
  context_.helper_exe = helper;

  const base::FilePath envelope = envelope_;
  envelope_ = dir_.GetPath();
  EXPECT_EQ(kHelperInput, Run());
  envelope_ = dir_.GetPath().Append(L"missing.json");
  EXPECT_EQ(kHelperInput, Run());
  envelope_ = envelope;

  ASSERT_TRUE(base::WriteFile(
      envelope_, test::Envelope(*base::WriteJson(Record()), "wrong domain\n")));
  EXPECT_EQ(kHelperRecord, Run());
  WriteEnvelope(Record());

  installed_[0] = installed_[1] = "156.0.8073.1";
  EXPECT_EQ(kHelperNotNewer, Run());
  installed_[0] = installed_[1] = "156.0.8073.0";

  SetFloor(L"156.0.8073.2");
  EXPECT_EQ(kHelperReplay, Run());
  SetFloor(L"not a version");
  EXPECT_EQ(kHelperReplay, Run());
  SetFloor(L"");

  ASSERT_TRUE(base::WriteFile(installer_, "MZ test installer bytez"));
  EXPECT_EQ(kHelperPackage, Run());
  ASSERT_TRUE(base::WriteFile(installer_, "MZ shorter"));
  EXPECT_EQ(kHelperPackage, Run());

  EXPECT_EQ(0, runs_);
  EXPECT_EQ(L"", Floor());
}

TEST_F(PubkyUpdateHelperTest, NativeVersionChangeBeforeLaunchAborts) {
  installed_[1] = "156.0.8073.1";
  EXPECT_EQ(kHelperScope, Run());
  EXPECT_EQ(0, runs_);
  EXPECT_EQ(L"", Floor());
}

TEST_F(PubkyUpdateHelperTest, SameVersionRetryAndInstallerFailures) {
  SetFloor(L"156.0.8073.1");
  exit_code_ = installer::SAME_VERSION_REPAIR_FAILED;
  EXPECT_EQ(kHelperInstallerBase +
                static_cast<int>(installer::SAME_VERSION_REPAIR_FAILED),
            Run());
  exit_code_ = std::nullopt;
  EXPECT_EQ(kHelperLaunch, Run());
  EXPECT_EQ(2, runs_);
}

}  // namespace
}  // namespace pubky_update
