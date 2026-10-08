// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/installer/linux/coordinator/protocol.h"

#include <poll.h>
#include <unistd.h>

#include "base/posix/eintr_wrapper.h"

namespace pubky_update {

GateResult WaitForCommittedExit(int pipe, int pidfd) {
  char message = 0;
  const ssize_t first = HANDLE_EINTR(read(pipe, &message, 1));
  if (first != 1 || message != kCommit) {
    return first == 1 && message == kAbort ? GateResult::kAborted
                                           : GateResult::kOrphaned;
  }
  bool pipe_closed = false;
  bool origin_exited = false;
  for (;;) {
    pollfd events[2] = {{origin_exited ? -1 : pidfd, POLLIN, 0},
                        {pipe_closed ? -1 : pipe, POLLIN | POLLHUP, 0}};
    if (HANDLE_EINTR(poll(events, 2, -1)) <= 0) {
      return GateResult::kOrphaned;
    }
    if (events[1].revents) {
      char remainder = 0;
      const ssize_t count = HANDLE_EINTR(read(pipe, &remainder, 1));
      if (count != 0) {
        return GateResult::kOrphaned;
      }
      pipe_closed = true;
    }
    if (events[0].revents) {
      if (!(events[0].revents & POLLIN)) {
        return GateResult::kOrphaned;
      }
      origin_exited = true;
    }
    if (origin_exited && pipe_closed) {
      return GateResult::kCommitted;
    }
  }
}

}  // namespace pubky_update
