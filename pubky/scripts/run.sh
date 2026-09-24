#!/bin/bash
set -euo pipefail
source "$(dirname "$0")/environment.sh"
exec "$SRC_ROOT/out/Pubky/Chromium.app/Contents/MacOS/Chromium" \
  --user-data-dir="$PUBKY_ROOT/profiles/default" \
  --no-first-run --no-default-browser-check "$@"
