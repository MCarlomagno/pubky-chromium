# Installed updater common layer

Implement in this order:

1. Shared bounded Linux/Windows record verification and fixed feed/redirect policy. Use Chromium JSON, Ed25519, version and URL APIs. Define a restricted ASCII wire format that round-trips through Chromium and Python without ambiguous escaping.
2. One browser-process controller with Local State replay floors, cached refresh and explicit transaction-bound consent. Production stays unsupported: no approved key or complete platform adapter exists. Test-only callbacks exercise native/network boundaries; they cannot be enabled by flags, preferences or renderer input.
3. Connect About messages, cached status, localized disabled/error states and explicit controls under unbranded desktop guards. No changes to normal shutdown or upstream branded/ChromeOS/CfT updating.
4. Add repository gtests, existing About WebUI regression cases and stdlib release-input tool tests. Run available checks; record unavailable native compilation/WebUI runner once, without claiming older outputs validate this branch.

Baseline: 2a6170379f4816571a0f7d476cfdcb6c56d88c31. PR12 is reviewed source, not compiled or shipped. This milestone adds common plumbing, not a working installed updater. Linux privileged helper/coordinator, Windows native installation/elevation and Mac Sparkle remain separate assignments.

## Partial Linux-phase preparation

The first Linux-phase increment extends the existing restart boundary with aborted, failed and committed outcomes (review finding N4). Consent now enters `restarting`; rejected closure restores `ready` with fresh consent, while pending teardown cancels rather than preserving an unacknowledged commit. Repository controller tests cover abort/retry, expiry while waiting, failure cleanup, late replies and observer reentrancy. The About WebUI test covers the waiting-to-retry presentation and fresh ID forwarding. These native/WebUI tests still need execution on the builder.

This increment does not complete Linux adapter items 1-8. No download implementation, coordinator executable, installed eligibility, privileged helper, polkit policy, protected snapshot, dpkg invocation, readback or relaunch was added. Production remains inert. Finish those items together before enabling installed updates; do not ship this increment as a working Linux updater.
