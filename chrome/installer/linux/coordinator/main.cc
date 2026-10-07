// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
// The transaction is a fixed-size struct read from a pipe.
#pragma allow_unsafe_buffers
#pragma allow_unsafe_libc_calls
#endif

#include "chrome/installer/linux/coordinator/protocol.h"

#include <unistd.h>

#include <cstring>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/posix/eintr_wrapper.h"

namespace {

bool ReadTransaction(pubky_update::Transaction* transaction) {
  auto* bytes = reinterpret_cast<char*>(transaction);
  size_t left = sizeof(*transaction);
  while (left) {
    const ssize_t count = HANDLE_EINTR(read(pubky_update::kTransactionPipe, bytes, left));
    if (count <= 0) {
      return false;
    }
    bytes += count;
    left -= count;
  }
  char extra = 0;
  if (HANDLE_EINTR(read(pubky_update::kTransactionPipe, &extra, 1)) != 0) {
    return false;
  }
  return memchr(transaction->staging_dir, 0, sizeof(transaction->staging_dir)) &&
         memchr(transaction->user_data_dir, 0, sizeof(transaction->user_data_dir)) &&
         memchr(transaction->profile_dir, 0, sizeof(transaction->profile_dir));
}

}  // namespace

int main(int argc, char**) {
  if (argc != 1) {
    return 1;
  }
  pubky_update::Transaction transaction{};
  if (!ReadTransaction(&transaction)) {
    return 1;
  }
  const base::FilePath staging_dir(transaction.staging_dir);
  const base::FilePath user_data_dir(transaction.user_data_dir);
  if (!staging_dir.IsAbsolute() || !user_data_dir.IsAbsolute() ||
      staging_dir.DirName() != user_data_dir ||
      !staging_dir.BaseName().value().starts_with("pubky-update-") ||
      !base::DirectoryExists(staging_dir)) {
    return 1;
  }
  base::ScopedTempDir staging;
  if (!staging.Set(staging_dir)) {
    return 1;
  }
  switch (pubky_update::WaitForCommittedExit(pubky_update::kCoordinatorPipe,
                                              pubky_update::kOriginPidfd)) {
    case pubky_update::GateResult::kAborted:
      staging.Take();
      return 0;
    case pubky_update::GateResult::kOrphaned:
      return 1;
    case pubky_update::GateResult::kCommitted:
      break;
  }
  // L3 will invoke the authenticated installer here; never install in L2.
  if (!staging.Delete()) {
    return 1;
  }
  close(pubky_update::kCoordinatorPipe);
  close(pubky_update::kOriginPidfd);
  close(pubky_update::kTransactionPipe);
  std::vector<std::string> args = {"/usr/bin/pubky-chromium-stable",
                                   "--restore-last-session"};
  args.push_back("--user-data-dir=" + user_data_dir.value());
  if (transaction.profile_dir[0]) {
    args.push_back(std::string("--profile-directory=") + transaction.profile_dir);
  }
  std::vector<char*> c_args;
  for (std::string& arg : args) {
    c_args.push_back(arg.data());
  }
  c_args.push_back(nullptr);
  execv(args[0].c_str(), c_args.data());
  return 1;
}
