# Initial verification — 24 September 2026

Base: `7fec6741f4f7ea010205dc20afc7dafb90221176`.
Browser: Chromium 156.0.8073.0, macOS ARM64, Xcode 27.0 (`27A266a`).

## Automated checks

- Standalone public-key decoder: canonical encoding, padding, lengths, invalid
  alphabet, subdomains, root dot, and suffix-confusion cases passed.
- **131 network tests passed** with this filter:

  ```text
  SSLClientSocketTest.Pubky*:HttpResponseInfoTest.*:DnsResponseResultExtractorTest.*:HostCacheTest.Pubky*:DnsTransactionTest.Pubky*:HostResolverManagerTest.Pubky*
  ```

  Coverage includes real BoringSSL raw-key handshakes through Chromium's socket
  implementation, wrong-key rejection even with certificate-error bypass set,
  X.509-only server rejection, full handshakes on successive connections, raw-key
  cache serialization, malformed cache rejection, HTTPS port selection,
  restricted ports, both resolver endpoint APIs, DNS search suffix exclusion,
  and failure without DoH even when a caller requests system resolution.

- **17 browser integration/unit tests passed** in `pubky_browser_unittests`:
  the existing omnibox parsing tests, bare-key HTTPS classification, and raw-key
  response delivery into Blink without requiring an X.509 certificate.
- GN header dependency checks passed for `//net:net`,
  `//components/omnibox/browser:*`, and
  `//third_party/blink/renderer/platform:pubky_browser_unittests`.

## Live browser results

| URL host | Direct endpoint | Result |
|---|---|---|
| `8um71us3fyw6h8wbcxb5ar3rwusy1a6u49956ikzojg3gcwd1dty` | `34.65.156.171:6287` | `Pubky Homeserver` |
| `4msqbgpkfcdgnzrrsyp5hgno8rfa4sx15c79ughsq95ikycunowy` | `34.65.63.78:443` | `One key. Two ways to connect.` |

Both loaded with public-key URLs intact and `window.isSecureContext === true`.
NetLog recorded TLS 1.3, RFC 7250 RawPublicKey, and matching Ed25519 keys.
The homeserver's port came from strict DoH HTTPS discovery. No request to
`homeserver.pubky.app` occurred in the final capture.

Fresh-profile settings showed secure DNS enabled with the requested PKDNS URL.
NetLog recorded `secure_dns_mode: 2`. Ordinary `https://example.com/` loaded
successfully. CDP reported a secure security state and an empty X.509 certificate
list for the raw-key site.

The live omnibox controller, in another fresh profile, classified the bare
homeserver key as a URL, with the HTTPS destination ranked above search. That
optional internal diagnostic needed `--disable-crash-on-webui-js-error` because
the upstream debug page emitted a favicon CSP error. The primary TLS acceptance
profile used neither that flag nor any DNS/TLS bypass flags.

## Resolver observation

The new site's diagnostic relay response advertised HTTPS priority 1, while a
separate DoH probe still returned priority 0. The browser loaded that site via
its A record and standard port 443 with full raw-key authentication. The original
homeserver test separately established working non-default-port HTTPS discovery.

## Evidence

[verification/connections.json](verification/connections.json) contains the
machine-checked public connection summary. Raw NetLogs, browser profiles,
screenshots, downloaded dependencies, and compiled binaries remain local build
artifacts. Reproduce the checks using the scripts documented in the README.

## JavaScript capabilities — 25 September 2026

The rebuilt browser exposes a native `navigator.pubky` object with read-only
`supportsPkdns` and `supportsRawPublicKeyTls` boolean getters. These report built-in
support, not current DNS settings or the current page's TLS authentication.

Verification:

- The new HTTP test failed against the previous binary because the API was
  absent, before the implementation was built.
- `bash pubky/scripts/test_capabilities.sh` builds Chromium and ChromeDriver and
  runs the WPT fixtures. Four test cases passed (16 subtests):
  - HTTP, explicitly verified as an insecure context.
  - HTTPS, explicitly verified as a secure context.
  - Dedicated worker, where the Window-only API is absent.
  - A virtual suite with `--disable-blink-features=PubkyCapabilities`, where both
    `navigator.pubky` and the interface constructor are absent.
- The unflagged copy of the disabled-feature fixture is intentionally skipped;
  the same fixture executes in the virtual suite. There were no unexpected
  results.
- HTTP and HTTPS coverage checks native interface identity, both booleans,
  `[SameObject]`, read-only WebIDL descriptors and assignments, rejection of
  script construction, per-frame objects, and access after frame detachment.
- A normal launch without experimental-feature flags loaded the live public-key
  test website and returned both booleans as `true`, with native type
  `[object PubkyCapabilities]`.
- With CDP network emulation setting `navigator.onLine` to `false`, both
  capability booleans remained `true`.
- A separate test profile with secure DNS disabled reported the effective
  `dns_over_https.mode` preference as `off`, while both capability booleans
  remained `true`.

See [verification/capabilities.json](verification/capabilities.json) for the
observed live values. WPT output is in `out/Pubky/layout-test-results/`.

### Build memory

The local build now defaults to four jobs and sets
`blink_bindings_single_process = true`. This prevents each parallel binding
generation action from creating its own CPU-sized Python worker pool. During
the limited rebuild, reported free memory stayed around 73–74%, and existing
swap usage declined. `BUILD_JOBS` remains configurable.
