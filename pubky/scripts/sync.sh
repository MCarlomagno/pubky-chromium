#!/bin/bash
set -euo pipefail
source "$(dirname "$0")/environment.sh"
if [[ ! -f "$SRC_ROOT/../.gclient" ]]; then
  printf '%s\n' "Copy pubky/gclient.py to the checkout's parent directory as .gclient first." >&2
  exit 1
fi
cd "$SRC_ROOT/.."
# The solution is unmanaged: synchronize dependencies without resetting the
# checked-out Pubky branch to the original upstream revision.
exec caffeinate -i gclient sync --jobs "${SYNC_JOBS:-8}"
