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
