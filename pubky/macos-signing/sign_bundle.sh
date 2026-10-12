#!/bin/bash
# Signs Chromium.app inside out for Developer ID and notarization, then verifies it.
#   ./sign_bundle.sh <identity> <path/to/Chromium.app>
# <identity> is "Developer ID Application: ..." for a release, or - for the ad hoc dry run.
# Options and entitlements per part follow chrome/installer/mac/signing/parts.py; Sparkle's
# follow its "Code Signing" guide. Runs on macOS; uses only the system bash 3.2 and Xcode tools.
set -euo pipefail
ID=${1:?usage: sign_bundle.sh <identity|-> <Chromium.app>}
APP=${2:?usage: sign_bundle.sh <identity|-> <Chromium.app>}
E=$(cd "$(dirname "$0")" && pwd)
F="$APP/Contents/Frameworks/Chromium Framework.framework"
S="$APP/Contents/Frameworks/Sparkle.framework"
BID=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$APP/Contents/Info.plist")
# An ad hoc signature cannot carry a secure timestamp; notarization needs one.
TS=--timestamp
[ "$ID" = - ] && TS=
HARD=restrict,library,runtime,kill
JIT=restrict,kill,runtime  # library validation is incompatible with the JIT entitlement

sign() { codesign --force --sign "$ID" $TS "$@"; }

xattr -cr "$APP"

# 1. Sparkle, innermost first. No entitlements: the app is not sandboxed.
sign --options runtime "$S/Versions/B/XPCServices/Installer.xpc"
sign --options runtime "$S/Versions/B/XPCServices/Downloader.xpc"
sign --options runtime "$S/Versions/B/Autoupdate"
sign --options runtime "$S/Versions/B/Updater.app"
sign --options runtime "$S"

# 2. Code inside the browser framework.
for lib in "$F/Libraries/"*.dylib; do sign "$lib"; done
sign --options $HARD "$F/Helpers/chrome_crashpad_handler"
sign --options $HARD "$F/Helpers/app_mode_loader"
sign --options $HARD --identifier "$BID.web_app_shortcut_copier" "$F/Helpers/web_app_shortcut_copier"
for h in "$F/Helpers/"*.app; do
  case $h in
    *Renderer\).app) sign --options $JIT --entitlements "$E/helper-renderer-entitlements.plist" "$h" ;;
    *GPU\).app) sign --options $JIT --entitlements "$E/helper-gpu-entitlements.plist" "$h" ;;
    *) sign --options $HARD "$h" ;;
  esac
done

# 3. The browser framework, then the app.
sign "$F"
sign --options $HARD --entitlements "$E/app-entitlements.plist" "$APP"

codesign --verify --deep --strict --verbose=2 "$APP"
