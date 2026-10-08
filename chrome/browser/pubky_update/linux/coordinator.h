// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PUBKY_UPDATE_LINUX_COORDINATOR_H_
#define CHROME_BROWSER_PUBKY_UPDATE_LINUX_COORDINATOR_H_

#include "base/callback_list.h"
#include "base/files/scoped_file.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/pubky_update/controller.h"

namespace pubky_update {

// One pending consent, owned by the browser process; no production binding yet.
class LinuxCoordinator {
 public:
  LinuxCoordinator();
  ~LinuxCoordinator();
  LinuxCoordinator(const LinuxCoordinator&) = delete;
  LinuxCoordinator& operator=(const LinuxCoordinator&) = delete;

  void BindForTesting(Controller::TestBoundaries& boundaries);
  void Begin(StagedPackage& package,
             base::OnceCallback<void(Controller::RestartResult)> reply);
  void Abort();

 private:
  void OnTerminating();
  base::ScopedFD commit_pipe_;
  base::CallbackListSubscription terminating_;
  base::CallbackListSubscription closing_;
  base::OnceCallback<void(Controller::RestartResult)> reply_;
  raw_ptr<StagedPackage> package_ = nullptr;
};

}  // namespace pubky_update

#endif  // CHROME_BROWSER_PUBKY_UPDATE_LINUX_COORDINATOR_H_
