# Installed updater common layer

Implement in this order:

1. Shared bounded Linux/Windows record verification and fixed feed/redirect policy. Use Chromium JSON, Ed25519, version and URL APIs. Define a restricted ASCII wire format that round-trips through Chromium and Python without ambiguous escaping.
2. One browser-process controller with Local State replay floors, cached refresh and explicit transaction-bound consent. Production stays unsupported: no approved key or complete platform adapter exists. Test-only callbacks exercise native/network boundaries; they cannot be enabled by flags, preferences or renderer input.
3. Connect About messages, cached status, localized disabled/error states and explicit controls under unbranded desktop guards. No changes to normal shutdown or upstream branded/ChromeOS/CfT updating.
4. Add repository gtests, existing About WebUI regression cases and stdlib release-input tool tests. Run available checks; record unavailable native compilation/WebUI runner once, without claiming older outputs validate this branch.

Baseline: 2a6170379f4816571a0f7d476cfdcb6c56d88c31. PR12 is reviewed source, not compiled or shipped. This milestone adds common plumbing, not a working installed updater. Linux privileged helper/coordinator, Windows native installation/elevation and Mac Sparkle remain separate assignments.

## Partial Linux-phase preparation

The first Linux-phase increment extends the existing restart boundary with aborted, failed and committed outcomes (review finding N4). Consent now enters `restarting`; rejected closure restores `ready` with fresh consent, while pending teardown cancels rather than preserving an unacknowledged commit. Repository controller tests cover abort/retry, expiry while waiting, failure cleanup, late replies and observer reentrancy. The About WebUI test covers the waiting-to-retry presentation and fresh ID forwarding. These native/WebUI tests still need execution on the builder.

That increment did not complete Linux adapter items 1-8. The L1 phase below adds items 1-2; items 3-8 remain absent. Production remains inert. Finish the complete adapter before enabling installed updates; do not ship this increment as a working Linux updater.

## Linux L1: eligibility and staging

`chrome/browser/pubky_update/linux/eligibility.cc` resolves the real running executable, requires the exact `/opt/pubky-chromium/chrome` identity and protected root-owned executable/parents, and checks the process's x86_64 architecture. It reads `/var/lib/dpkg/status` with a 64 MiB bound and parses only the exact `pubky-chromium` stanza. Installed status, amd64 and `<running_product_version>-1` must match. Free bytes on the native User Data filesystem must cover the signed size plus 64 MiB. Read-only `F_GETLK` checks both dpkg lock files. Held locks reject; missing/unreadable locks are unknown and reject. This hint neither acquires locks nor guarantees later package-manager readiness.

`linux/stager.cc` creates a unique 0700 `ScopedTempDir` under native User Data. `CreateNewTempDirectory` itself is deprecated and uses system temp; `CreateUniqueTempDirUnderPath` is the pinned API for the required destination. `SimpleURLLoader::DownloadToFile` enforces the signed size while reading its body pipe and deletes partial/overflow files. The request omits credentials, auth data and referrer, disables cache, allows only the common release-CDN redirects (three maximum), and times out after ten minutes. The completed file is opened no-follow/nonblocking, must be regular and exactly sized, and is hashed with `crypto::SecureHash` on a blocking background sequence. A mismatch deletes staging; success writes the exact authenticated envelope alongside the file.

The typed `StageResult` returns an owning `StagedPackage` with path, size, digest and envelope. Its directory deleter runs on the blocking sequence, including canceled or stale results. The controller retains only a result matching its live authenticated offer and rechecks expiry before entering `ready`. Native eligibility codes reach cached About status as `eligibilityReason`; the WebUI type declares the optional field for later platform presentation. The current inert production UI still shows the disabled explanation. Tests alone can bind `LinuxStager` to controller boundaries or replace its eligibility probe. No production adapter or key is registered.

Repository gtests cover eligibility identity/status/version/space/lock branches, real temporary staging through `TestURLLoaderFactory`, size/hash/HTTP failure cleanup, redirects, timeout, cancellation/retry and controller/result binding. C++ compilation, native gtests, WebUI execution and traffic-annotation auditing remain unrun on the author host. The Python catalog and GRIT guard checks do not establish native behavior.

Items 3-8 (coordinator, normal closure/commit, elevation/helper, protected reauthentication, dpkg mutation, readback/relaunch and packaging) are separate phases. User-owned staged bytes can change after verification and must never be treated as privileged authorization. No extraction, execution or privileged operation occurs in L1.
