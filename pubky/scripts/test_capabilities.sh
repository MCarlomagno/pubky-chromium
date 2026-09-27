#!/bin/bash
set -euo pipefail
source "$(dirname "$0")/environment.sh"
cd "$SRC_ROOT"
gn gen out/Pubky --args="$(< "$PUBKY_ROOT/args.gn")"
caffeinate -i autoninja -C out/Pubky -j "${BUILD_JOBS:-4}" chrome chromedriver
exec vpython3 third_party/blink/tools/run_wpt_tests.py \
  -t Pubky --product=chrome --no-show-results --no-retry-failures \
  --child-processes=1 external/wpt/pubky \
  virtual/pubky-capabilities-disabled/external/wpt/pubky/capabilities-disabled.tentative.html
