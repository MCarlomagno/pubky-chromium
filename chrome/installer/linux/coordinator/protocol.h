// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_INSTALLER_LINUX_COORDINATOR_PROTOCOL_H_
#define CHROME_INSTALLER_LINUX_COORDINATOR_PROTOCOL_H_

namespace pubky_update {

inline constexpr int kCoordinatorPipe = 3;
inline constexpr int kOriginPidfd = 4;
inline constexpr int kTransactionPipe = 5;
inline constexpr char kCommit = 'C';
inline constexpr char kAbort = 'X';
enum class GateResult { kAborted, kOrphaned, kCommitted };

struct Transaction {
  char staging_dir[1024];
  char user_data_dir[1024];
  char profile_dir[256];
};

// Returns true only if the browser explicitly committed and then exited.
GateResult WaitForCommittedExit(int pipe, int pidfd);

}  // namespace pubky_update

#endif  // CHROME_INSTALLER_LINUX_COORDINATOR_PROTOCOL_H_
