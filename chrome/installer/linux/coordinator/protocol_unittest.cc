// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/installer/linux/coordinator/protocol.h"

#include <sys/eventfd.h>
#include <unistd.h>

#include <chrono>
#include <future>


#include "base/files/scoped_file.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pubky_update {
namespace {

GateResult RunGate(const char* bytes, size_t size, bool exited) {
  int ends[2];
  if (pipe(ends) != 0) {
    return GateResult::kOrphaned;
  }
  base::ScopedFD read_end(ends[0]), write_end(ends[1]);
  base::ScopedFD pidfd(eventfd(exited ? 1 : 0, EFD_CLOEXEC));
  if (size && write(write_end.get(), bytes, size) != static_cast<ssize_t>(size)) {
    return GateResult::kOrphaned;
  }
  write_end.reset();
  return WaitForCommittedExit(read_end.get(), pidfd.get());
}

TEST(PubkyUpdateCoordinatorTest, NoCommitOnCrashAbortOrExtraBytes) {
  EXPECT_EQ(GateResult::kOrphaned, RunGate("", 0, true));
  EXPECT_EQ(GateResult::kAborted, RunGate("X", 1, true));
  EXPECT_EQ(GateResult::kAborted, RunGate("XC", 2, true));
  EXPECT_EQ(GateResult::kOrphaned, RunGate("CC", 2, true));
  EXPECT_EQ(GateResult::kCommitted, RunGate("C", 1, true));
}

TEST(PubkyUpdateCoordinatorTest, CommitNeedsOriginExit) {
  int ends[2];
  ASSERT_EQ(0, pipe(ends));
  base::ScopedFD read_end(ends[0]), write_end(ends[1]);
  base::ScopedFD pidfd(eventfd(0, EFD_CLOEXEC));
  ASSERT_TRUE(pidfd.is_valid());
  ASSERT_EQ(1, write(write_end.get(), "C", 1));
  write_end.reset();
  auto result = std::async(std::launch::async, [&] {
    return WaitForCommittedExit(read_end.get(), pidfd.get());
  });
  EXPECT_EQ(std::future_status::timeout,
            result.wait_for(std::chrono::milliseconds(20)));
  uint64_t signal = 1;
  ASSERT_EQ(static_cast<ssize_t>(sizeof(signal)),
            write(pidfd.get(), &signal, sizeof(signal)));
  EXPECT_EQ(GateResult::kCommitted, result.get());
}

}  // namespace
}  // namespace pubky_update
