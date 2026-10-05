# Signed catalog v1

This branch implements common verifier/controller/About plumbing. Production is disabled. It contains no approved production key, Linux/Windows installer helper or Mac adapter. The network loader is exercised by repository test boundaries only; the real About handler receives the inert process controller. Test callbacks model download verification/restart, and never count as an installed update.

## Bytes

The envelope is compact ASCII JSON with lexicographically sorted keys, no spaces between tokens and no trailing newline:

    {"format":"pubky-updates-v1","payload":"<padded base64>","signature":"<padded base64>"}

The decoded payload uses the same JSON serialization. Its closed schema contains these 18 fields:

- Integers: `schema=1`, `size` in 1..2147483647. Booleans/floats are not integers.
- Strings: `architecture`, `asset`, `channel`, `expires_at`, `issued_at`, `native_version`, `package_id`, `platform`, `product`, `product_version`, `repository`, `sha256`, `source_revision`, `tag`, `upstream_version`, `url`.

Strings are nonempty printable ASCII, at most 2048 bytes, excluding quote, backslash, `<` and `>`. Restricting these unused characters avoids Python/Chromium escaping differences. Keys are fixed. Chromium parses with RFC JSON options/depth 3 and requires byte-identical reserialization; duplicate keys, whitespace, alternate escaping/number forms and unknown fields fail. The envelope is also canonical, not only the signed payload. Base64 must round-trip byte-for-byte, including padding. Limits are 128 KiB for the envelope, 64 KiB for the payload and 64 signature bytes.

Sign exactly ASCII `PubkyChromiumUpdateCatalogV1` followed by one LF and the payload bytes. Use standard Ed25519, without prehashing. The native verifier imports exactly 32 raw public-key bytes and uses Chromium's one-shot Ed25519 API. No remote key or hash-only fallback exists.

Identity is `MCarlomagno/pubky-chromium`, `Pubky Chromium`, `experimental`, `x64`. Platform is `linux` or `windows`; package identity is respectively `pubky-chromium` or `PubkyChromium`. Product/upstream versions are canonical four-part numeric tuples, each component at most 65535. This v1 narrows Debian native version to `<product_version>-1`; Windows native version equals the product tuple. This removes an arbitrary Debian comparison implementation from the common layer. A future change to the packaging revision convention requires reviewing this contract, not silently signing another form. Native adapters must read and recheck actual installed versions before mutation.

Tag/asset are 1..200 ASCII alphanumeric, dot, hyphen or underscore characters, excluding `.`/`..`. Linux assets end in `.deb`, Windows in `.exe`. URL must byte-match `https://github.com/MCarlomagno/pubky-chromium/releases/download/<tag>/<asset>`, with no credentials, explicit port, query, fragment or escaping. SHA-256 is 64 lowercase hex characters; source revision is 40 lowercase hex characters.

Validity timestamps use exact `YYYY-MM-DDTHH:MM:SSZ` calendar values. Lifetime is positive and at most 180 days; issuance may be at most five minutes ahead of the local clock. Expired records fail. Consent/restart rechecks expiration. Authenticated offers below the greatest Local State offered version fail; equality permits a canceled offer to be retried. Running and installed product tuples must both be older before an offer can authorize a transaction. An otherwise valid record with no newer tuple yields `no_newer`, never an installation candidate.

## Transport and consent

Fixed feeds are the PLAN's experimental `linux-x64.json` and `windows-x64.json` on `update-manifests-v1` at raw.githubusercontent.com. Metadata requests have omitted credentials, no auth data/referrer or custom headers, disabled cache, a 30-second timeout and a 128 KiB download bound. Every redirect must return to the same pinned URL, with at most three redirects. Package redirect policy permits only HTTPS release-assets.githubusercontent.com and objects.githubusercontent.com; native package download/enforcement is not implemented here.

The process controller owns Local State floors, the offer, opaque ID and cancellation. About attachment, refresh and UpgradeDetector notification call only cached status. Explicit Check is the only metadata-request entry. Duplicate checks/confirmation, stale IDs and callbacks after cancellation are rejected. Two subscribed tabs share the same controller; the last tab detaching cancels unconfirmed work. Confirmed test-boundary work survives detachment but is canceled on teardown. Check/confirm/restart notifications retain weak transaction lifetime guards. Restart commitment is distinct from success; no production shutdown/install path was added.

Only `CreateForTesting`, guarded by `CHECK_IS_TEST`, can supply a key/network factory/native callbacks. No renderer argument, preference or command-line flag activates it. Production presents the disabled explanation and cannot confirm/restart. Google, ChromeOS, Android and all CfT configurations retain their upstream update path. GRIT now receives the existing `is_chrome_for_testing` GN argument so unbranded CfT is excluded as well.

## Public release-input tool

Run from the repository root with Python 3 and OpenSSL 3 available. No Python package installation is needed. Supply public metadata containing every schema field and the frozen package. `prepare` calculates the actual size/digest; it never executes the package. Use a new output directory:

    python3 pubky/update/catalog.py prepare metadata.json --package final-package.deb --target linux --running 156.0.8073.0 --installed 156.0.8073.0 --output release-inputs/linux-x64

The tuple above is an example, not an allocated release version. Use actual current running/installed/native/package values. Optional `--floor` rejects older offers. Do not use `--now` for production publication; that override is for public fixture checks.

Give the owner `catalog.json` and `catalog.bin`. After format/tool review, the proposed owner-only Sparkle procedure can sign the `.bin` bytes; this task neither validates Sparkle execution nor requests a production key. Agents must never receive private key material. The public tool exposes no signing operation.

Assemble only after independent public verification:

    python3 pubky/update/catalog.py envelope release-inputs/linux-x64/catalog.json --signature linux-x64.signature.b64 --public-key approved-public-key.b64 --target linux --running 156.0.8073.0 --installed 156.0.8073.0 --output linux-x64.json
    python3 pubky/update/catalog.py verify linux-x64.json --public-key approved-public-key.b64 --target linux --running 156.0.8073.0 --installed 156.0.8073.0

Use `--target windows` and the `.exe` package/identity for Windows. Public key/signature files contain one padded-base64 value; surrounding file whitespace is stripped, not accepted inside the encoded value. Verification uses OpenSSL `pkeyutl -verify -pubin -rawin` with a generated public RFC 8410 DER wrapper. Existing output files/directories are never overwritten. Any package/payload change requires another owner signature. Publish immutable packages before signed feed heads.

## Tests and remaining phases

    python3 pubky/update/catalog_test.py -v

`testdata/` contains a public OpenSSL-generated catalog fixture using RFC 8032 section 7.1 TEST 1 key material. It is not a production release, digest or key. Native `PubkyUpdateRecordTest.PublicOpenSslFixture` reads the same bytes to check Chromium interoperability; that native test must run before claiming cross-verifier success. Its data dependency is declared in GN. The test-only helper contains the published RFC seed for signed malformed-record tests and is in a `testonly` target, never browser code.

Builder commands (not run here): build `unit_tests` and run `PubkyUpdateRecordTest.*:PubkyUpdateControllerTest.*:PubkyUpdateTransportTest.*`; build/run the existing `SettingsAboutPageTest.AllBuilds` WebUI suite. Follow the owned-output/four-job/build-lock policy. Missing GN/dependencies/output are recorded in the task handoff, not replaced by source-grep checks.

Next assignments still need complete Linux dpkg/polkit/coordinator, Windows installation/elevation/in-use readback, and Mac Sparkle/packaging/cancellation paths. They must provide actual native installed-version/eligibility checks, bound package download and digest verification, protected reauthentication, installer results and normal consent-aware shutdown. No release/version allocation, updater-enabled build or installed-product claim is made by this milestone.
