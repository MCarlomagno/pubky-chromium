# Installed updater common layer

Implement in this order:

1. Shared bounded Linux/Windows record verification and fixed feed/redirect policy. Use Chromium JSON, Ed25519, version and URL APIs. Define a restricted ASCII wire format that round-trips through Chromium and Python without ambiguous escaping.
2. One browser-process controller with Local State replay floors, cached refresh and explicit transaction-bound consent. Production stays unsupported: no approved key or complete platform adapter exists. Test-only callbacks exercise native/network boundaries; they cannot be enabled by flags, preferences or renderer input.
3. Connect About messages, cached status, localized disabled/error states and explicit controls under unbranded desktop guards. No changes to normal shutdown or upstream branded/ChromeOS/CfT updating.
4. Add repository gtests, existing About WebUI regression cases and stdlib release-input tool tests. Run available checks; record unavailable native compilation/WebUI runner once, without claiming older outputs validate this branch.

Baseline: 2a6170379f4816571a0f7d476cfdcb6c56d88c31. PR12 is reviewed source, not compiled or shipped. This milestone adds common plumbing, not a working installed updater. Linux privileged helper/coordinator, Windows native installation/elevation and Mac Sparkle remain separate assignments.
