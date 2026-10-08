// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/pubky_update/windows_adapter.h"

#include <array>
#include <string>
#include <utility>

#include "base/command_line.h"
#include "base/file_version_info_win.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/version_info/version_info.h"
#include "base/win/registry.h"
#include "base/win/windows_version.h"
#include "build/build_config.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/lifetime/application_lifetime_desktop.h"
#include "chrome/browser/lifetime/termination_notification.h"
#include "chrome/install_static/install_util.h"
#include "chrome/installer/pubky_update/helper_win.h"
#include "chrome/installer/util/helper.h"
#include "chrome/installer/util/install_util.h"
#include "chrome/installer/util/shell_util.h"
#include "chrome/installer/util/util_constants.h"
#include "crypto/hash.h"
#include "net/base/load_flags.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

namespace pubky_update {
namespace {

constexpr wchar_t kInstallerName[] = L"mini_installer.exe";
constexpr wchar_t kEnvelopeName[] = L"record.json";

bool SamePath(const base::FilePath& a, const base::FilePath& b) {
  return base::FilePath::CompareEqualIgnoreCase(a.value(), b.value());
}

HelperLaunch LaunchHelper(const base::CommandLine& command_line,
                          bool elevated) {
  base::LaunchOptions options;
  // ShellExecuteEx "runas": Windows shows the UAC prompt.
  options.elevated = elevated;
  options.start_hidden = true;
  base::Process process = base::LaunchProcess(command_line, options);
  if (!process.IsValid()) {
    return {.declined = elevated && ::GetLastError() == ERROR_CANCELLED};
  }
  HelperLaunch launch;
  launch.started = process.WaitForExit(&launch.exit_code);
  return launch;
}

// Checks the completed download and stores the authenticated envelope next to
// it for the helper, which repeats every check on its own copy.
bool CheckDownload(const base::FilePath& path, const Record& record) {
  base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_READ);
  std::array<uint8_t, crypto::hash::kSha256Size> digest;
  return file.IsValid() && file.GetLength() == record.size &&
         crypto::hash::HashFile(crypto::hash::kSha256, &file, digest) &&
         base::HexEncodeLower(digest) == record.sha256 &&
         base::WriteFile(path.DirName().Append(kEnvelopeName),
                         record.authenticated_envelope);
}

struct RestartWatch {
  base::OnceCallback<void(Controller::RestartResult)> done;
  base::CallbackListSubscription closing;
  base::CallbackListSubscription terminating;
};

void FinishRestart(RestartWatch* watch, Controller::RestartResult result) {
  auto done = std::move(watch->done);
  delete watch;
  std::move(done).Run(result);
}

void OnClosingAllBrowsers(RestartWatch* watch, bool closing) {
  if (!closing) {
    FinishRestart(watch, Controller::RestartResult::kAborted);
  }
}

// Normal restart: unload handlers, session restore and the native
// new_chrome.exe swap. Closing reports false when a dialog cancels it, and the
// app-terminating notification marks the commit.
void RestartAndWatch(base::OnceCallback<void(Controller::RestartResult)> done) {
  // Owned until the first outcome; a restart always ends in one of them.
  auto* watch = new RestartWatch{std::move(done)};
  watch->closing = chrome::AddClosingAllBrowsersCallback(
      base::BindRepeating(&OnClosingAllBrowsers, base::Unretained(watch)));
  watch->terminating = browser_shutdown::AddAppTerminatingCallback(
      base::BindOnce(&FinishRestart, base::Unretained(watch),
                     Controller::RestartResult::kCommitted));
  chrome::AttemptRestart();
}

}  // namespace

WindowsInstallFacts::WindowsInstallFacts() = default;
WindowsInstallFacts::WindowsInstallFacts(const WindowsInstallFacts&) = default;
WindowsInstallFacts& WindowsInstallFacts::operator=(
    const WindowsInstallFacts&) = default;
WindowsInstallFacts::~WindowsInstallFacts() = default;

WindowsInstallError CheckWindowsInstall(const WindowsInstallFacts& facts) {
  if (!facts.x64) {
    return WindowsInstallError::kArchitecture;
  }
  if (facts.install_dir.empty() || !facts.registered.IsValid() ||
      !facts.running.IsValid()) {
    return WindowsInstallError::kNotInstalled;
  }
  // Pubky registered at both scopes: the right target is not knowable.
  if (facts.other_scope.IsValid()) {
    return WindowsInstallError::kAmbiguous;
  }
  // A portable copy, or a registration redirected to another directory.
  if (!SamePath(facts.running_exe.DirName(), facts.install_dir) ||
      !SamePath(facts.running_exe.BaseName(),
                base::FilePath(installer::kChromeExe))) {
    return WindowsInstallError::kPath;
  }
  if (facts.app_path &&
      !SamePath(*facts.app_path,
                facts.install_dir.Append(installer::kChromeExe))) {
    return WindowsInstallError::kForeign;
  }
  return WindowsInstallError::kNone;
}

WindowsInstallFacts ReadWindowsInstallFacts() {
  WindowsInstallFacts facts;
  facts.system_install = install_static::IsSystemInstall();
#if defined(ARCH_CPU_X86_64)
  facts.x64 = base::win::OSInfo::GetArchitecture() ==
                  base::win::OSInfo::X64_ARCHITECTURE &&
              !base::win::OSInfo::IsRunningEmulatedOnArm64();
#endif
  facts.running_exe = base::PathService::CheckedGet(base::FILE_EXE);
  facts.running = version_info::GetVersion();
  facts.install_dir = installer::GetInstalledDirectory(facts.system_install);
  facts.registered = InstallUtil::GetChromeVersion(facts.system_install);
  facts.other_scope = InstallUtil::GetChromeVersion(!facts.system_install);
  const std::wstring app_paths_key =
      std::wstring(ShellUtil::kAppPathsRegistryKey)
          .append(L"\\")
          .append(installer::kChromeExe);
  base::win::RegKey key;
  std::wstring app_path;
  if (key.Open(facts.system_install ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER,
               app_paths_key.c_str(), KEY_QUERY_VALUE) == ERROR_SUCCESS &&
      key.ReadValue(L"", &app_path) == ERROR_SUCCESS) {
    facts.app_path = base::FilePath(app_path);
  }
  return facts;
}

WindowsReadback ReadWindowsReadback(const WindowsInstallFacts& facts,
                                    const base::Version& expected) {
  WindowsReadback readback;
  readback.registered = InstallUtil::GetChromeVersion(facts.system_install);
  base::win::RegKey key;
  DWORD error = 0;
  if (key.Open(facts.system_install ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER,
               install_static::GetClientStateKeyPath().c_str(),
               KEY_QUERY_VALUE | KEY_WOW64_32KEY) == ERROR_SUCCESS &&
      key.ReadValueDW(installer::kInstallerError, &error) == ERROR_SUCCESS) {
    readback.installer_error = error;
  }
  if (auto info = FileVersionInfoWin::CreateFileVersionInfoWin(
          facts.install_dir.Append(installer::kChromeNewExe))) {
    readback.staged = info->GetFileVersion();
  }
  readback.version_dir =
      expected.IsValid() && base::DirectoryExists(facts.install_dir.AppendASCII(
                                expected.GetString()));
  return readback;
}

bool IsReadyToRestart(const WindowsReadback& readback,
                      const base::Version& expected) {
  return expected.IsValid() && readback.registered.IsValid() &&
         readback.registered == expected &&
         readback.installer_error ==
             static_cast<DWORD>(installer::IN_USE_UPDATED) &&
         readback.staged.IsValid() && readback.staged == expected &&
         readback.version_dir;
}

WindowsAdapter::Native::Native() = default;
WindowsAdapter::Native::Native(const Native&) = default;
WindowsAdapter::Native& WindowsAdapter::Native::operator=(const Native&) =
    default;
WindowsAdapter::Native::~Native() = default;

// static
WindowsAdapter::Native WindowsAdapter::DefaultNative() {
  Native native;
  native.read_facts = base::BindRepeating(&ReadWindowsInstallFacts);
  native.launch = base::BindRepeating(&LaunchHelper);
  native.read_back = base::BindRepeating(&ReadWindowsReadback);
  native.restart = base::BindRepeating(&RestartAndWatch);
  return native;
}

struct WindowsAdapter::Staging {
  WindowsInstallFacts facts;
  WindowsInstallError error = WindowsInstallError::kNone;
  base::FilePath dir;
};

WindowsAdapter::WindowsAdapter(
    scoped_refptr<network::SharedURLLoaderFactory> factory,
    Native native)
    : factory_(std::move(factory)),
      native_(std::move(native)),
      // The helper wait can outlast browser shutdown; never block on it.
      blocking_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::WithBaseSyncPrimitives(),
           base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN})) {}

WindowsAdapter::~WindowsAdapter() {
  Cancel();
}

void WindowsAdapter::BindTo(Controller::TestBoundaries& boundaries) {
  boundaries.download_and_verify = base::BindRepeating(
      [](WindowsAdapter* adapter, const Record& record,
         base::OnceCallback<void(StageResult)> done) {
        adapter->DownloadAndVerify(
            record, base::BindOnce(
                        [](WindowsAdapter* adapter, Record record,
                           base::OnceCallback<void(StageResult)> done,
                           bool verified) {
                          StageResult result;
                          if (verified) {
                            StagedPackage package;
                            package.path = adapter->staging_.Append(kInstallerName);
                            package.size = record.size;
                            package.sha256 = record.sha256;
                            package.envelope = record.authenticated_envelope;
                            result.package.emplace(std::move(package));
                          }
                          std::move(done).Run(std::move(result));
                        },
                        base::Unretained(adapter), record, std::move(done)));
      },
      base::Unretained(this));
  boundaries.install =
      base::BindRepeating(&WindowsAdapter::Install, base::Unretained(this));
  boundaries.cancel =
      base::BindRepeating(&WindowsAdapter::Cancel, base::Unretained(this));
  boundaries.restart = base::BindRepeating(
      [](WindowsAdapter* adapter, StagedPackage&,
         base::OnceCallback<void(Controller::RestartResult)> done) {
        adapter->Restart(std::move(done));
      },
      base::Unretained(this));
}

void WindowsAdapter::DownloadAndVerify(const Record& record,
                                       base::OnceCallback<void(bool)> done) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  Cancel();
  verified_ = std::move(done);
  blocking_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](base::RepeatingCallback<WindowsInstallFacts()> read_facts) {
            Staging staging;
            staging.facts = read_facts.Run();
            staging.error = CheckWindowsInstall(staging.facts);
            if (staging.error == WindowsInstallError::kNone &&
                !base::CreateNewTempDirectory(L"pubky_update", &staging.dir)) {
              staging.dir.clear();
            }
            return staging;
          },
          native_.read_facts),
      base::BindOnce(&WindowsAdapter::OnStaging, weak_factory_.GetWeakPtr(),
                     record));
}

void WindowsAdapter::OnStaging(Record record, Staging staging) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  staging_ = staging.dir;
  if (staging.error != WindowsInstallError::kNone || staging_.empty() ||
      !factory_) {
    Finish(false);
    return;
  }
  facts_ = std::move(staging.facts);
  constexpr auto annotation =
      net::DefineNetworkTrafficAnnotation("pubky_explicit_update_package", R"(
        semantics {
          sender: "Pubky Chromium installed updater"
          description: "Downloads the signed Windows installer named by an authenticated update record."
          trigger: "Only Download and install in About, after an explicit check showed a newer signed version."
          data: "No account, profile or browser identifiers."
          user_data {
            type: NONE
          }
          destination: OTHER
          destination_other: "The MCarlomagno/pubky-chromium GitHub release asset named in the signed record."
          internal {
            contacts {
              owners: "chrome/browser/OWNERS"
            }
          }
          last_reviewed: "2026-10-06"
        }
        policy {
          cookies_allowed: NO
          setting: "Production activation is absent until a key and complete platform adapter are approved."
          policy_exception_justification: "Explicit user action; no background downloads."
        })");
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = record.url;
  request->method = "GET";
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->referrer_policy = net::ReferrerPolicy::NO_REFERRER;
  request->load_flags = net::LOAD_DISABLE_CACHE;
  loader_ = network::SimpleURLLoader::Create(std::move(request), annotation);
  redirects_ = 0;
  loader_->SetOnRedirectCallback(base::BindRepeating(
      [](base::WeakPtr<WindowsAdapter> self, const GURL&,
         const net::RedirectInfo& redirect,
         const network::mojom::URLResponseHead&, std::vector<std::string>*) {
        if (self && (++self->redirects_ > 3 ||
                     !IsPackageRedirectAllowed(redirect.new_url))) {
          self->loader_.reset();
          self->Finish(false);
        }
      },
      weak_factory_.GetWeakPtr()));
  loader_->DownloadToFile(factory_.get(),
                          base::BindOnce(&WindowsAdapter::OnDownloaded,
                                         weak_factory_.GetWeakPtr(), record),
                          staging_.Append(kInstallerName), record.size);
}

void WindowsAdapter::OnDownloaded(Record record, base::FilePath path) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  loader_.reset();
  if (path.empty()) {
    Finish(false);
    return;
  }
  blocking_->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&CheckDownload, path, record),
      base::BindOnce(&WindowsAdapter::Finish, weak_factory_.GetWeakPtr()));
}

void WindowsAdapter::Finish(bool success) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!success) {
    DeleteStaging();
  }
  if (verified_) {
    std::move(verified_).Run(success);
  }
}

void WindowsAdapter::Install(
    const Record& record,
    base::OnceCallback<void(InstallOutcome, int)> done) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!facts_ || staging_.empty()) {
    std::move(done).Run(InstallOutcome::kFailed, kHelperInput);
    return;
  }
  installed_ = std::move(done);
  // The helper of the running version, inside the protected install.
  base::CommandLine helper(
      facts_->install_dir.AppendASCII(facts_->running.GetString())
          .Append(kHelperExe));
  helper.AppendSwitchPath(kEnvelopeSwitch, staging_.Append(kEnvelopeName));
  helper.AppendSwitchPath(kInstallerSwitch, staging_.Append(kInstallerName));
  blocking_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](Native native, WindowsInstallFacts facts, base::CommandLine helper,
             base::Version expected) {
            HelperLaunch launch =
                native.launch.Run(helper, facts.system_install);
            return std::make_pair(
                launch, launch.started ? native.read_back.Run(facts, expected)
                                       : WindowsReadback());
          },
          native_, *facts_, helper, record.version),
      base::BindOnce(&WindowsAdapter::OnInstalled, weak_factory_.GetWeakPtr(),
                     record.version));
}

void WindowsAdapter::OnInstalled(
    base::Version expected,
    std::pair<HelperLaunch, WindowsReadback> result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DeleteStaging();
  const auto& [launch, readback] = result;
  InstallOutcome outcome = InstallOutcome::kFailed;
  int error = 0;
  if (launch.declined) {
    outcome = InstallOutcome::kDeclined;
  } else if (!launch.started) {
    error = kHelperLaunch;
  } else if (launch.exit_code != kHelperInstalled) {
    error = launch.exit_code;
  } else if (!IsReadyToRestart(readback, expected)) {
    // The installer said yes but native state disagrees; report what setup
    // recorded, or the generic helper failure if nothing was recorded.
    error = kHelperInstallerBase +
            static_cast<int>(readback.installer_error.value_or(0));
  } else {
    outcome = InstallOutcome::kReady;
  }
  std::move(installed_).Run(outcome, error);
}

void WindowsAdapter::Cancel() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  weak_factory_.InvalidateWeakPtrs();
  loader_.reset();
  verified_.Reset();
  installed_.Reset();
  facts_.reset();
  DeleteStaging();
}

void WindowsAdapter::Restart(
    base::OnceCallback<void(Controller::RestartResult)> done) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  native_.restart.Run(std::move(done));
}

void WindowsAdapter::DeleteStaging() {
  if (!staging_.empty()) {
    blocking_->PostTask(FROM_HERE, base::GetDeletePathRecursivelyCallback(
                                       std::exchange(staging_, {})));
  }
}

}  // namespace pubky_update
