// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/pubky_update/linux/coordinator.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "base/containers/span.h"
#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/posix/eintr_wrapper.h"
#include "base/process/launch.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/lifetime/application_lifetime_desktop.h"
#include "chrome/browser/lifetime/termination_notification.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/sessions/session_data_service.h"
#include "chrome/browser/sessions/session_data_service_factory.h"
#include "chrome/common/buildflags.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/installer/linux/coordinator/protocol.h"

namespace pubky_update {
namespace {
constexpr char kCoordinator[] = "/opt/pubky-chromium/pubky-update-coordinator";

template <size_t N>
bool CopyField(char (&field)[N], const std::string& value) {
  if (value.size() >= N || value.find('\0') != std::string::npos) {
    return false;
  }
  base::span(field).first(value.size()).copy_from(base::span(value));
  return true;
}
}  // namespace

LinuxCoordinator::LinuxCoordinator() = default;
LinuxCoordinator::~LinuxCoordinator() { Abort(); }

void LinuxCoordinator::BindTo(Controller::TestBoundaries& boundaries) {
  boundaries.cancel = base::BindRepeating(
      [](LinuxCoordinator* self, base::RepeatingClosure cancel) {
        self->Abort();
        if (cancel) {
          cancel.Run();
        }
      },
      base::Unretained(this), std::move(boundaries.cancel));
  boundaries.restart = base::BindRepeating(&LinuxCoordinator::Begin,
                                           base::Unretained(this));
}

void LinuxCoordinator::Begin(
    StagedPackage& package,
    base::OnceCallback<void(Controller::RestartResult)> reply) {
  if (reply_ || !package.directory || !package.directory->IsValid() ||
      package.path != package.directory->GetPath().AppendASCII("package.deb") ||
      !base::PathExists(package.path) ||
      !base::PathExists(package.path.DirName().AppendASCII("envelope.json"))) {
    std::move(reply).Run(Controller::RestartResult::kFailed);
    return;
  }
  const base::FilePath stage = package.directory->GetPath();
  const auto* command = base::CommandLine::ForCurrentProcess();
  Transaction transaction{};
  if (!CopyField(transaction.staging_dir, stage.value()) ||
      !CopyField(transaction.user_data_dir, stage.DirName().value()) ||
      !CopyField(transaction.profile_dir,
                 command->GetSwitchValueASCII(switches::kProfileDirectory))) {
    std::move(reply).Run(Controller::RestartResult::kFailed);
    return;
  }
  int control[2], input[2];
  if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, control) != 0) {
    std::move(reply).Run(Controller::RestartResult::kFailed);
    return;
  }
  base::ScopedFD sender(control[1]), receiver(control[0]);
  if (pipe2(input, O_CLOEXEC) != 0) {
    std::move(reply).Run(Controller::RestartResult::kFailed);
    return;
  }
  base::ScopedFD writer(input[1]), reader(input[0]);
  base::ScopedFD origin(HANDLE_EINTR(syscall(SYS_pidfd_open, getpid(), 0)));
  if (!origin.is_valid()) {
    std::move(reply).Run(Controller::RestartResult::kFailed);
    return;
  }
  base::LaunchOptions options;
  options.fds_to_remap = {{receiver.get(), kCoordinatorPipe},
                          {origin.get(), kOriginPidfd},
                          {reader.get(), kTransactionPipe}};
  base::CommandLine coordinator{base::FilePath(kCoordinator)};
  auto process = base::LaunchProcess(coordinator, options);
  if (!process.IsValid() ||
      HANDLE_EINTR(write(writer.get(), &transaction, sizeof(transaction))) !=
          static_cast<ssize_t>(sizeof(transaction))) {
    std::move(reply).Run(Controller::RestartResult::kFailed);
    return;
  }
  writer.reset();
  reader.reset();
  receiver.reset();
  origin.reset();
  package_ = &package;
  commit_pipe_ = std::move(sender);
  reply_ = std::move(reply);
  terminating_ = browser_shutdown::AddAppTerminatingCallback(base::BindOnce(
      &LinuxCoordinator::OnTerminating, base::Unretained(this)));
  closing_ = chrome::AddClosingAllBrowsersCallback(base::BindRepeating(
      [](LinuxCoordinator* self, bool closing) {
        if (!closing) {
          self->Abort();
        }
      }, base::Unretained(this)));
#if BUILDFLAG(ENABLE_SESSION_SERVICE)
  for (Profile* profile :
       g_browser_process->profile_manager()->GetLoadedProfiles()) {
    if (!profile->IsOffTheRecord()) {
      profile->SaveSessionState();
      if (auto* service = SessionDataServiceFactory::GetForProfile(profile)) {
        service->SetForceKeepSessionState();
      }
    }
  }
#endif
  chrome::AttemptExit();
}

void LinuxCoordinator::Abort() {
  if (!reply_) {
    return;
  }
  closing_ = {};
  terminating_ = {};
  const char abort = kAbort;
  HANDLE_EINTR(send(commit_pipe_.get(), &abort, 1, MSG_NOSIGNAL));
  commit_pipe_.reset();
  package_ = nullptr;
  std::move(reply_).Run(Controller::RestartResult::kAborted);
}

void LinuxCoordinator::OnTerminating() {
  if (!reply_) {
    return;
  }
  closing_ = {};
  terminating_ = {};
  const char commit = kCommit;
  if (HANDLE_EINTR(send(commit_pipe_.get(), &commit, 1, MSG_NOSIGNAL)) != 1) {
    commit_pipe_.reset();
    package_ = nullptr;
    std::move(reply_).Run(Controller::RestartResult::kFailed);
    return;
  }
  package_->directory->Take();
  package_ = nullptr;
  commit_pipe_.reset();
  std::move(reply_).Run(Controller::RestartResult::kCommitted);
}

}  // namespace pubky_update
