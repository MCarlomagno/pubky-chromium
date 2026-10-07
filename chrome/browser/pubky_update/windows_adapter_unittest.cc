// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/pubky_update/windows_adapter.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "chrome/browser/pubky_update/controller.h"
#include "chrome/common/pubky_update/test_record.h"
#include "chrome/installer/pubky_update/helper_win.h"
#include "chrome/installer/util/util_constants.h"
#include "components/prefs/testing_pref_service.h"
#include "crypto/hash.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "services/network/test/test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pubky_update {
namespace {

constexpr std::string_view kInstaller = "MZ signed test installer";
constexpr char kUrl[] =
    "https://github.com/MCarlomagno/pubky-chromium/releases/download/"
    "v156.0.8073.1/mini_installer.exe";

WindowsInstallFacts GoodFacts() {
  WindowsInstallFacts facts;
  facts.x64 = true;
  facts.install_dir = base::FilePath(
      L"C:\\Users\\u\\AppData\\Local\\PubkyChromium\\Application");
  facts.running_exe = facts.install_dir.Append(L"chrome.exe");
  facts.running = base::Version("156.0.8073.0");
  facts.registered = facts.running;
  return facts;
}

WindowsReadback GoodReadback() {
  WindowsReadback readback;
  readback.registered = base::Version("156.0.8073.1");
  readback.installer_error = installer::IN_USE_UPDATED;
  readback.staged = base::Version("156.0.8073.1");
  readback.version_dir = true;
  return readback;
}

TEST(PubkyUpdateWindowsInstallTest, OnlyTheRegisteredInstallAtThisScope) {
  EXPECT_EQ(WindowsInstallError::kNone, CheckWindowsInstall(GoodFacts()));

  WindowsInstallFacts facts = GoodFacts();
  facts.x64 = false;
  EXPECT_EQ(WindowsInstallError::kArchitecture, CheckWindowsInstall(facts));

  facts = GoodFacts();
  facts.install_dir.clear();
  EXPECT_EQ(WindowsInstallError::kNotInstalled, CheckWindowsInstall(facts));
  facts = GoodFacts();
  facts.registered = base::Version();
  EXPECT_EQ(WindowsInstallError::kNotInstalled, CheckWindowsInstall(facts));

  facts = GoodFacts();
  facts.other_scope = base::Version("156.0.8073.0");
  EXPECT_EQ(WindowsInstallError::kAmbiguous, CheckWindowsInstall(facts));

  // A portable copy, or a renamed executable, is not the installed browser.
  facts = GoodFacts();
  facts.running_exe = base::FilePath(L"D:\\portable\\chrome.exe");
  EXPECT_EQ(WindowsInstallError::kPath, CheckWindowsInstall(facts));
  facts = GoodFacts();
  facts.running_exe = facts.install_dir.Append(L"other.exe");
  EXPECT_EQ(WindowsInstallError::kPath, CheckWindowsInstall(facts));

  facts = GoodFacts();
  facts.app_path = base::FilePath(L"C:\\Program Files\\Chromium\\chrome.exe");
  EXPECT_EQ(WindowsInstallError::kForeign, CheckWindowsInstall(facts));
  // Windows paths are case-insensitive.
  facts.app_path = base::FilePath(
      L"c:\\users\\u\\appdata\\local\\pubkychromium\\application\\CHROME.EXE");
  EXPECT_EQ(WindowsInstallError::kNone, CheckWindowsInstall(facts));
}

TEST(PubkyUpdateWindowsInstallTest, ReadyOnlyForTheExpectedInUseUpdate) {
  const base::Version expected("156.0.8073.1");
  EXPECT_TRUE(IsReadyToRestart(GoodReadback(), expected));
  EXPECT_FALSE(IsReadyToRestart(GoodReadback(), base::Version()));

  // Exit code zero also covers a same-version repair.
  WindowsReadback readback = GoodReadback();
  readback.registered = base::Version("156.0.8073.0");
  readback.installer_error = installer::INSTALL_REPAIRED;
  EXPECT_FALSE(IsReadyToRestart(readback, expected));

  readback = GoodReadback();
  readback.registered = base::Version("156.0.8073.2");
  EXPECT_FALSE(IsReadyToRestart(readback, expected));

  readback = GoodReadback();
  readback.installer_error.reset();
  EXPECT_FALSE(IsReadyToRestart(readback, expected));
  readback.installer_error = installer::NEW_VERSION_UPDATED;
  EXPECT_FALSE(IsReadyToRestart(readback, expected));

  readback = GoodReadback();
  readback.staged = base::Version();
  EXPECT_FALSE(IsReadyToRestart(readback, expected));
  readback.staged = base::Version("156.0.8073.0");
  EXPECT_FALSE(IsReadyToRestart(readback, expected));

  readback = GoodReadback();
  readback.version_dir = false;
  EXPECT_FALSE(IsReadyToRestart(readback, expected));
}

class PubkyUpdateWindowsAdapterTest : public testing::Test {
 protected:
  PubkyUpdateWindowsAdapterTest() {
    WindowsAdapter::Native native;
    native.read_facts = base::BindLambdaForTesting([this] { return facts_; });
    native.launch = base::BindLambdaForTesting(
        [this](const base::CommandLine& command_line, bool elevated) {
          ++launches_;
          launched_ = command_line;
          elevated_ = elevated;
          // The helper reads the staged files while the adapter waits.
          std::string bytes;
          EXPECT_TRUE(base::ReadFileToString(
              command_line.GetSwitchValuePath(kInstallerSwitch), &bytes));
          EXPECT_EQ(kInstaller, bytes);
          EXPECT_TRUE(base::ReadFileToString(
              command_line.GetSwitchValuePath(kEnvelopeSwitch), &bytes));
          EXPECT_EQ(envelope_, bytes);
          return launch_;
        });
    native.read_back = base::BindLambdaForTesting(
        [this](const WindowsInstallFacts&, const base::Version& expected) {
          EXPECT_EQ(base::Version("156.0.8073.1"), expected);
          return readback_;
        });
    native.restart = base::BindLambdaForTesting(
        [this](base::OnceCallback<void(Controller::RestartResult)> reply) {
          ++restarts_;
          restart_reply_ = std::move(reply);
        });
    adapter_ = std::make_unique<WindowsAdapter>(factory_.GetSafeWeakWrapper(),
                                                std::move(native));
  }

  static Record MakeRecord() {
    Record record;
    record.version = base::Version("156.0.8073.1");
    record.native_version = "156.0.8073.1";
    record.url = GURL(kUrl);
    record.size = static_cast<int>(kInstaller.size());
    record.sha256 = base::HexEncodeLower(crypto::hash::Sha256(kInstaller));
    record.authenticated_envelope = "signed envelope";
    record.expires_at = base::Time::Now() + base::Days(1);
    return record;
  }

  // Returns the download result, or nullopt if it never finished.
  std::optional<bool> Download(const Record& record) {
    std::optional<bool> result;
    adapter_->DownloadAndVerify(
        record, base::BindLambdaForTesting([&](bool ok) { result = ok; }));
    task_environment_.RunUntilIdle();
    return result;
  }

  std::pair<InstallOutcome, int> Install() {
    std::optional<std::pair<InstallOutcome, int>> result;
    adapter_->Install(MakeRecord(), base::BindLambdaForTesting(
                                        [&](InstallOutcome outcome, int error) {
                                          result = {outcome, error};
                                        }));
    task_environment_.RunUntilIdle();
    EXPECT_TRUE(result);
    return result.value_or(std::make_pair(InstallOutcome::kFailed, -1));
  }

  base::test::TaskEnvironment task_environment_;
  network::TestURLLoaderFactory factory_;
  WindowsInstallFacts facts_ = GoodFacts();
  HelperLaunch launch_{.started = true, .exit_code = kHelperInstalled};
  WindowsReadback readback_ = GoodReadback();
  std::string envelope_ = "signed envelope";
  int launches_ = 0;
  int restarts_ = 0;
  base::OnceCallback<void(Controller::RestartResult)> restart_reply_;
  bool elevated_ = false;
  base::CommandLine launched_{base::CommandLine::NO_PROGRAM};
  std::unique_ptr<WindowsAdapter> adapter_;
};

TEST_F(PubkyUpdateWindowsAdapterTest, DownloadsVerifiesAndRunsTheHelper) {
  for (bool system : {false, true}) {
    facts_.system_install = system;
    factory_.AddResponse(kUrl, std::string(kInstaller));
    EXPECT_EQ(true, Download(MakeRecord()));
    const base::FilePath staging = adapter_->staging_for_testing();
    ASSERT_FALSE(staging.empty());
    EXPECT_TRUE(base::PathExists(staging.Append(L"mini_installer.exe")));

    EXPECT_EQ(std::make_pair(InstallOutcome::kReady, 0), Install());
    // The helper of the running version inside the registered install,
    // elevated only for a system-level install.
    EXPECT_EQ(facts_.install_dir.Append(L"156.0.8073.0").Append(kHelperExe),
              launched_.GetProgram());
    EXPECT_EQ(system, elevated_);
    EXPECT_EQ(staging,
              launched_.GetSwitchValuePath(kInstallerSwitch).DirName());
    task_environment_.RunUntilIdle();
    EXPECT_FALSE(base::PathExists(staging));
  }
  EXPECT_EQ(2, launches_);

  std::optional<Controller::RestartResult> restarted;
  adapter_->Restart(base::BindLambdaForTesting(
      [&](Controller::RestartResult result) { restarted = result; }));
  EXPECT_EQ(1, restarts_);
  std::move(restart_reply_).Run(Controller::RestartResult::kCommitted);
  EXPECT_EQ(Controller::RestartResult::kCommitted, restarted);
}

TEST_F(PubkyUpdateWindowsAdapterTest, RejectsWrongBytesBeforeAnyHelper) {
  Record record = MakeRecord();
  // Same length, different bytes.
  factory_.AddResponse(kUrl, "MZ signed test installex");
  EXPECT_EQ(false, Download(record));
  // Shorter and longer than the signed size.
  factory_.AddResponse(kUrl, "MZ short");
  EXPECT_EQ(false, Download(record));
  factory_.AddResponse(kUrl, std::string(kInstaller) + "!");
  EXPECT_EQ(false, Download(record));
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(adapter_->staging_for_testing().empty());

  // Without a verified download there is nothing to install.
  EXPECT_EQ(std::make_pair(InstallOutcome::kFailed, int{kHelperInput}),
            Install());
  EXPECT_EQ(0, launches_);
}

TEST_F(PubkyUpdateWindowsAdapterTest, RefusesAnUnsupportedInstall) {
  facts_.other_scope = base::Version("156.0.8073.0");
  EXPECT_EQ(false, Download(MakeRecord()));
  EXPECT_EQ(0, factory_.NumPending());
  EXPECT_EQ(0, launches_);
}

TEST_F(PubkyUpdateWindowsAdapterTest, PackageRedirects) {
  const struct {
    const char* target;
    int hops;
    bool ok;
  } kCases[] = {
      {"https://release-assets.githubusercontent.com/asset", 1, true},
      {"https://evil.example/mini_installer.exe", 1, false},
      {"http://release-assets.githubusercontent.com/asset", 1, false},
      {"https://release-assets.githubusercontent.com/asset", 4, false},
  };
  for (const auto& test_case : kCases) {
    network::TestURLLoaderFactory::Redirects redirects;
    for (int i = 0; i < test_case.hops; ++i) {
      net::RedirectInfo redirect;
      redirect.new_url = GURL(test_case.target);
      redirects.emplace_back(redirect,
                             network::CreateURLResponseHead(net::HTTP_FOUND));
    }
    factory_.AddResponse(
        GURL(kUrl), network::CreateURLResponseHead(net::HTTP_OK),
        std::string(kInstaller), network::URLLoaderCompletionStatus(net::OK),
        std::move(redirects));
    EXPECT_EQ(test_case.ok, Download(MakeRecord())) << test_case.target;
  }
}

TEST_F(PubkyUpdateWindowsAdapterTest, CancelDropsTheDownloadAndStaging) {
  bool called = false;
  adapter_->DownloadAndVerify(
      MakeRecord(), base::BindLambdaForTesting([&](bool) { called = true; }));
  task_environment_.RunUntilIdle();
  ASSERT_TRUE(factory_.IsPending(kUrl));
  const base::FilePath staging = adapter_->staging_for_testing();
  ASSERT_TRUE(base::DirectoryExists(staging));

  adapter_->Cancel();
  task_environment_.RunUntilIdle();
  EXPECT_FALSE(base::PathExists(staging));
  EXPECT_EQ(0, factory_.NumPending());
  EXPECT_FALSE(called);
}

TEST_F(PubkyUpdateWindowsAdapterTest, HelperOutcomes) {
  constexpr int kSingletonFailed =
      kHelperInstallerBase +
      static_cast<int>(installer::SETUP_SINGLETON_ACQUISITION_FAILED);
  const struct {
    HelperLaunch launch;
    std::optional<DWORD> installer_error;
    InstallOutcome outcome;
    int error;
  } kCases[] = {
      // UAC refusal.
      {{.declined = true},
       installer::IN_USE_UPDATED,
       InstallOutcome::kDeclined,
       0},
      {{}, installer::IN_USE_UPDATED, InstallOutcome::kFailed, kHelperLaunch},
      {{.started = true, .exit_code = kHelperReplay},
       installer::IN_USE_UPDATED,
       InstallOutcome::kFailed,
       kHelperReplay},
      {{.started = true, .exit_code = kSingletonFailed},
       installer::IN_USE_UPDATED,
       InstallOutcome::kFailed,
       kSingletonFailed},
      // The helper exited zero but native state is a repair, not the update.
      {{.started = true, .exit_code = kHelperInstalled},
       installer::INSTALL_REPAIRED,
       InstallOutcome::kFailed,
       kHelperInstallerBase + static_cast<int>(installer::INSTALL_REPAIRED)},
  };
  for (const auto& test_case : kCases) {
    factory_.AddResponse(kUrl, std::string(kInstaller));
    ASSERT_EQ(true, Download(MakeRecord()));
    const base::FilePath staging = adapter_->staging_for_testing();
    launch_ = test_case.launch;
    readback_.installer_error = test_case.installer_error;
    EXPECT_EQ(std::make_pair(test_case.outcome, test_case.error), Install());
    task_environment_.RunUntilIdle();
    EXPECT_FALSE(base::PathExists(staging));
  }
  EXPECT_EQ(0, restarts_);
}

// The adapter bound into the About controller, as the browser would.
TEST_F(PubkyUpdateWindowsAdapterTest, ControllerFlow) {
  TestingPrefServiceSimple prefs;
  Controller::RegisterLocalState(prefs.registry());
  Controller::TestBoundaries boundaries;
  boundaries.target = Target::kWindowsX64;
  std::ranges::copy(test::PublicKey(), boundaries.public_key.begin());
  boundaries.running = base::Version("156.0.8073.0");
  boundaries.installed = boundaries.running;
  base::DictValue record = test::Record(base::Time::Now(), Target::kWindowsX64);
  record.Set("sha256", base::HexEncodeLower(crypto::hash::Sha256(kInstaller)));
  record.Set("size", static_cast<int>(kInstaller.size()));
  envelope_ = test::Envelope(record);
  boundaries.fetch = base::BindLambdaForTesting(
      [&](GURL, base::OnceCallback<void(std::string)> reply) {
        std::move(reply).Run(envelope_);
      });
  WindowsAdapter* adapter = adapter_.get();
  boundaries.download_and_verify = base::BindRepeating(
      &WindowsAdapter::DownloadAndVerify, base::Unretained(adapter));
  boundaries.install =
      base::BindRepeating(&WindowsAdapter::Install, base::Unretained(adapter));
  boundaries.cancel =
      base::BindRepeating(&WindowsAdapter::Cancel, base::Unretained(adapter));
  boundaries.restart =
      base::BindRepeating(&WindowsAdapter::Restart, base::Unretained(adapter));
  auto controller = Controller::CreateForTesting(&prefs, std::move(boundaries));
  auto status = [&] { return controller->GetStatus(); };

  controller->Check();
  ASSERT_EQ("available", *status().FindString("state"));
  const std::string id = *status().FindString("id");
  factory_.AddResponse(kUrl, std::string(kInstaller));
  controller->Confirm(id);
  EXPECT_EQ("downloading", *status().FindString("state"));

  // Stop between verification and the helper's result to observe the
  // installing state.
  std::string seen;
  auto subscription = controller->Observe(base::BindLambdaForTesting([&] {
    if (*status().FindString("state") == "installing") {
      EXPECT_FALSE(*status().FindBool("canCancel"));
      controller->Cancel(id);
      seen = *status().FindString("state");
    }
  }));
  task_environment_.RunUntilIdle();
  EXPECT_EQ("installing", seen);
  EXPECT_EQ(1, launches_);
  EXPECT_EQ("ready", *status().FindString("state"));
  EXPECT_FALSE(*status().FindBool("canCancel"));
  EXPECT_TRUE(*status().FindBool("canRestart"));

  controller->Restart(id);
  controller->Restart(id);
  EXPECT_EQ(1, restarts_);
  EXPECT_EQ("restarting", *status().FindString("state"));
  // A beforeunload dialog canceled the restart; the update stays installed.
  std::move(restart_reply_).Run(Controller::RestartResult::kAborted);
  EXPECT_EQ("ready", *status().FindString("state"));
  controller->Restart(*status().FindString("id"));
  std::move(restart_reply_).Run(Controller::RestartResult::kCommitted);
  EXPECT_EQ(2, restarts_);
  EXPECT_EQ("committed", *status().FindString("state"));
}

}  // namespace
}  // namespace pubky_update
