// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PUBKY_UPDATE_WINDOWS_ADAPTER_H_
#define CHROME_BROWSER_PUBKY_UPDATE_WINDOWS_ADAPTER_H_

#include <windows.h>

#include <memory>
#include <optional>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/version.h"
#include "chrome/browser/pubky_update/controller.h"
#include "chrome/common/pubky_update/record.h"

namespace base {
class CommandLine;
class SequencedTaskRunner;
}  // namespace base
namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}  // namespace network

namespace pubky_update {

// The running browser and its native registration at the running scope.
struct WindowsInstallFacts {
  WindowsInstallFacts();
  WindowsInstallFacts(const WindowsInstallFacts&);
  WindowsInstallFacts& operator=(const WindowsInstallFacts&);
  ~WindowsInstallFacts();

  bool system_install = false;
  // Native x64 Windows (not x64 emulation on arm64).
  bool x64 = false;
  base::FilePath running_exe;
  base::Version running;
  // installer::GetInstalledDirectory(); empty without a registered install.
  base::FilePath install_dir;
  // "pv" at this scope and at the other one.
  base::Version registered;
  base::Version other_scope;
  // App Paths\chrome.exe at this scope, if any. The installer rewrites it, so
  // a value naming another browser is a foreign registration.
  std::optional<base::FilePath> app_path;
};

enum class WindowsInstallError {
  kNone,
  kArchitecture,
  kNotInstalled,
  kAmbiguous,
  kPath,
  kForeign,
};
WindowsInstallError CheckWindowsInstall(const WindowsInstallFacts& facts);
// Blocking registry and file reads.
WindowsInstallFacts ReadWindowsInstallFacts();

// Native state after the helper exits.
struct WindowsReadback {
  base::Version registered;
  std::optional<DWORD> installer_error;
  // PE version of new_chrome.exe; invalid if absent.
  base::Version staged;
  bool version_dir = false;
};
// Blocking.
WindowsReadback ReadWindowsReadback(const WindowsInstallFacts& facts,
                                    const base::Version& expected);
// The running browser stays open, so a real update is always the in-use form:
// "pv" and the staged new_chrome.exe at the new version. An exit code of zero
// alone is not enough; a same-version repair also exits zero.
bool IsReadyToRestart(const WindowsReadback& readback,
                      const base::Version& expected);

struct HelperLaunch {
  bool started = false;
  // The user refused the UAC prompt.
  bool declined = false;
  int exit_code = 0;
};

// Downloads, installs and restarts for one confirmed offer at a time. Bind it
// into Controller's download_and_verify, install, cancel and restart.
class WindowsAdapter {
 public:
  // Native boundaries; tests replace them.
  struct Native {
    Native();
    Native(const Native&);
    Native& operator=(const Native&);
    ~Native();

    base::RepeatingCallback<WindowsInstallFacts()> read_facts;
    // Starts the helper, elevated for system installs, and waits. Blocking.
    base::RepeatingCallback<HelperLaunch(const base::CommandLine&, bool)>
        launch;
    base::RepeatingCallback<WindowsReadback(const WindowsInstallFacts&,
                                            const base::Version&)>
        read_back;
    // Starts a normal restart and reports whether it closed the browser or
    // was canceled, for example by a beforeunload dialog.
    base::RepeatingCallback<void(
        base::OnceCallback<void(Controller::RestartResult)>)>
        restart;
  };
  static Native DefaultNative();

  WindowsAdapter(scoped_refptr<network::SharedURLLoaderFactory> factory,
                 Native native);
  WindowsAdapter(const WindowsAdapter&) = delete;
  WindowsAdapter& operator=(const WindowsAdapter&) = delete;
  ~WindowsAdapter();

  void DownloadAndVerify(const Record& record,
                         base::OnceCallback<void(bool)> done);
  void Install(const Record& record,
               base::OnceCallback<void(InstallOutcome, int)> done);
  // Drops pending work and deletes the download. A running helper is not
  // interrupted.
  void Cancel();
  void Restart(base::OnceCallback<void(Controller::RestartResult)> done);
  // Binds download, install, cancel and restart into the controller. The
  // adapter keeps ownership of the verified download.
  void BindTo(Controller::TestBoundaries& boundaries);

  const base::FilePath& staging_for_testing() const { return staging_; }

 private:
  struct Staging;
  void OnStaging(Record record, Staging staging);
  void OnDownloaded(Record record, base::FilePath path);
  void Finish(bool success);
  void OnInstalled(base::Version expected,
                   std::pair<HelperLaunch, WindowsReadback> result);
  void DeleteStaging();

  scoped_refptr<network::SharedURLLoaderFactory> factory_;
  Native native_;
  scoped_refptr<base::SequencedTaskRunner> blocking_;
  std::optional<WindowsInstallFacts> facts_;
  base::FilePath staging_;
  int redirects_ = 0;
  std::unique_ptr<network::SimpleURLLoader> loader_;
  base::OnceCallback<void(bool)> verified_;
  base::OnceCallback<void(InstallOutcome, int)> installed_;
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<WindowsAdapter> weak_factory_{this};
};

}  // namespace pubky_update

#endif  // CHROME_BROWSER_PUBKY_UPDATE_WINDOWS_ADAPTER_H_
