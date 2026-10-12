# macOS signing kit

Signs the unsigned `Chromium.app` from the Mac release build with a Developer ID
Application identity, for notarization. Signing runs on the owner's Mac; the
builder holds no certificate, key or notary credential.

| File | Use |
|---|---|
| `sign_bundle.sh` | Signs the bundle inside out, then runs `codesign --verify --deep --strict`. |
| `app-entitlements.plist` | The app. Copied unchanged from `chrome/app/`. |
| `helper-renderer-entitlements.plist` | Renderer helpers (JIT). Copied unchanged from `chrome/app/`. |
| `helper-gpu-entitlements.plist` | GPU helpers (JIT). Copied unchanged from `chrome/app/`. |

Options and entitlements per part follow Chromium's own signing tool,
`chrome/installer/mac/signing/parts.py`. The Sparkle 2.10.0 parts follow
Sparkle's code signing guide and get no entitlements, because the app is not
sandboxed. No step uses `--deep` to sign.

## Signing order

`sign_bundle.sh <identity> <Chromium.app>` runs these commands in this order.
`$F` is `Contents/Frameworks/Chromium Framework.framework`, `$S` is
`Contents/Frameworks/Sparkle.framework`, and `$TS` is `--timestamp` (left out
for the ad hoc identity `-`, which cannot carry a secure timestamp).

```sh
xattr -cr Chromium.app

# 1. Sparkle: XPC services, Autoupdate, Updater, then the framework
codesign --force --sign "$ID" $TS --options runtime "$S/Versions/B/XPCServices/Installer.xpc"
codesign --force --sign "$ID" $TS --options runtime "$S/Versions/B/XPCServices/Downloader.xpc"
codesign --force --sign "$ID" $TS --options runtime "$S/Versions/B/Autoupdate"
codesign --force --sign "$ID" $TS --options runtime "$S/Versions/B/Updater.app"
codesign --force --sign "$ID" $TS --options runtime "$S"

# 2. Libraries and helpers inside the browser framework
codesign --force --sign "$ID" $TS "$F/Libraries/libaperitif.dylib"   # and every other *.dylib there
codesign --force --sign "$ID" $TS --options restrict,library,runtime,kill "$F/Helpers/chrome_crashpad_handler"
codesign --force --sign "$ID" $TS --options restrict,library,runtime,kill "$F/Helpers/app_mode_loader"
codesign --force --sign "$ID" $TS --options restrict,library,runtime,kill \
  --identifier org.chromium.Chromium.web_app_shortcut_copier "$F/Helpers/web_app_shortcut_copier"
codesign --force --sign "$ID" $TS --options restrict,library,runtime,kill "$F/Helpers/Chromium Helper.app"
#   ... same for Helper (Alerts), (Aperitif), (Aperitif Alerts)
codesign --force --sign "$ID" $TS --options restrict,kill,runtime \
  --entitlements helper-renderer-entitlements.plist "$F/Helpers/Chromium Helper (Renderer).app"
#   ... same for Helper (Aperitif Renderer)
codesign --force --sign "$ID" $TS --options restrict,kill,runtime \
  --entitlements helper-gpu-entitlements.plist "$F/Helpers/Chromium Helper (GPU).app"
#   ... same for Helper (Aperitif GPU)

# 3. The browser framework, then the app
codesign --force --sign "$ID" $TS "$F"
codesign --force --sign "$ID" $TS --options restrict,library,runtime,kill \
  --entitlements app-entitlements.plist Chromium.app

codesign --verify --deep --strict --verbose=2 Chromium.app
```

The JIT helpers leave out `library` because library validation is
incompatible with the JIT entitlement. Hardened runtime is on for every
executable.

## Dry run with the ad hoc identity

Run on a Mac before the first real signing, on a scratch copy of the unsigned
app:

```sh
./sign_bundle.sh - "Pubky Chromium/Chromium.app"
```

It must end with `valid on disk` and `satisfies its Designated Requirement`.
Do not ship the result: with hardened runtime, library validation expects a
Team ID, and ad hoc code has none.

## Release: sign, notarize, staple, then Sparkle

Run in the folder that holds the unsigned `Pubky Chromium/` directory.
`<profile>` is the `notarytool` Keychain profile.

```sh
./sign_bundle.sh "Developer ID Application: <Name> (<TEAMID>)" "Pubky Chromium/Chromium.app"
ditto -c -k --keepParent "Pubky Chromium/Chromium.app" notarize.zip
xcrun notarytool submit notarize.zip --keychain-profile <profile> --wait
xcrun stapler staple "Pubky Chromium/Chromium.app"
xcrun stapler validate "Pubky Chromium/Chromium.app"
spctl -a -vv "Pubky Chromium/Chromium.app"   # accepted, source=Notarized Developer ID
rm notarize.zip
ditto -c -k --sequesterRsrc --keepParent "Pubky Chromium" <release-asset>.zip
```

Sign `<release-asset>.zip` and the feed with the Sparkle Ed25519 key only
after stapling: the Sparkle signature covers the exact ZIP bytes, and stapling
changes the app.

## Bundle checks

Checked on the 156.0.8073.3 Apple Silicon build:

- `LSMinimumSystemVersion` is 13.0 on the app and every helper app, and 12.0
  on the Sparkle parts. The browser framework has none, as frameworks need none.
- Bundle identifiers: the app is `org.chromium.Chromium`, the framework
  `org.chromium.Chromium.framework`, and the helpers
  `org.chromium.Chromium.helper` (plain, GPU and their Aperitif variants),
  `org.chromium.Chromium.helper.renderer` (renderer and Aperitif renderer) and
  `org.chromium.Chromium.framework.AlertNotificationService` (both alert
  helpers). Helpers that share an identifier are variants of one role, as in
  upstream Chromium, and their code signatures carry the same identifiers.
  Sparkle's parts are under `org.sparkle-project`.
- Existing signatures: Chromium's executables carry only the linker's ad hoc
  signature. Sparkle's parts arrive ad hoc signed with hardened runtime, and
  `Autoupdate` carries an ad hoc `com.apple.application-identifier`
  entitlement that the re-sign drops. Nothing is signed with a certificate.
  `--force` replaces every one of these signatures from the inside out.
