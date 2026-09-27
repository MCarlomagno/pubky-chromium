#!/bin/bash
set -euo pipefail
source "$(dirname "$0")/environment.sh"
cd "$SRC_ROOT"
gn gen out/Pubky --args="$(< "$PUBKY_ROOT/args.gn")"
caffeinate -i autoninja -C out/Pubky -j "${BUILD_JOBS:-4}" net_unittests
out/Pubky/net_unittests --gtest_filter='SSLClientSocketTest.Pubky*:HttpResponseInfoTest.*:DnsResponseResultExtractorTest.*:HostCacheTest.Pubky*:DnsTransactionTest.Pubky*:HostResolverManagerTest.Pubky*' --test-launcher-jobs=1
caffeinate -i autoninja -C out/Pubky -j "${BUILD_JOBS:-4}" third_party/blink/renderer/platform:pubky_browser_unittests
out/Pubky/pubky_browser_unittests --test-launcher-jobs=1
exec caffeinate -i autoninja -C out/Pubky -j "${BUILD_JOBS:-4}" chrome
