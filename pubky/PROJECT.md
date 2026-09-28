# Pubky Chromium project

Status: S0 foundations in progress. This document records the approved operating boundaries; it does not claim that the browser has been rebuilt or validated by this project.

## Product and first milestone

Build on the existing Pubky Chromium fork. The long-term goal is native Pubky identity and scoped sessions, with public-key navigation, authenticated connections, and identity UI. The immediate milestone is a reproducible Linux baseline and an evidence-backed map of the remaining work. No session storage, authenticated writes, replacement of existing browsing protections, mobile implementation, release signing, or new resolver implementation is authorized in this first milestone.

Repository: `MCarlomagno/pubky-chromium`, development branch `pubky`.

Adopted source revision: `77445742d82c8cfc482b2fea0476fe5f84d16032` from `SeverinAlexB/pubky-chromium`. Its recorded Chromium base is `7fec6741f4f7ea010205dc20afc7dafb90221176`. Keep the inherited `DEPS` and `pubky/depot-tools-revision.txt` pins until an approved update. Preserve Chromium branding/licensing restrictions and third-party notices.

The inherited project documents macOS ARM64 results in `pubky/VERIFICATION.md`. Those are the source author's results, not independent Linux validation by this project. The current `navigator.pubky` interface reports built-in capabilities; it is not a privileged session API.

## Scope and permissions

- Autonomous work is limited to this fork and explicitly approved goals. Before any repository action outside this fork, request owner approval naming the repository, purpose, and actions. Record possible external dependencies here or in this project's issues; do not open external issues, comments, branches, or PRs without permission.
- An approved plan can proceed through implementation, testing, independent review, repair, and merge without another owner prompt for the same decisions. Record the agreement and acceptance checks in the issue or decision record before dispatch.
- Questions and exploratory ideas are discussion, not authorization. New product, protocol, architecture, security, or privacy decisions go to the owner with a recommendation and alternatives.
- No new paid infrastructure, releases, deployment, or credential sharing is authorized. Use existing resources within their approved limits.
- Internal conversations and private source material must not be copied into public issues, commits, or artifacts. Public claims need public evidence or explicit disclosure approval.

## Target platforms and build gate

Linux x86_64 is the initial reproduction target on the designated Linux builder. A resource-capped trial there is approved; the previous cloud-only assumption is superseded. No cloud purchase is approved. macOS remains an inherited, unverified-by-us platform; macOS-specific changes need actual macOS validation. Windows and mobile are outside the first milestone.

Before fetching dependencies or building, record the checkout/dependency/output isolation plan, resource limits, exact revision, commands, and any required outside-repository download permission. A Git worktree does not by itself isolate Chromium's `gclient`/`DEPS` dependencies. Never share writable build outputs between tasks. Heavy Linux builds use the designated host lock and run one at a time.

The inherited `pubky/args.gn`, `pubky/gclient.py`, and scripts target macOS ARM64 and use `caffeinate`. Do not run them on Linux as if they were validated Linux commands. The S0 build-plan issue must establish the Linux invocation and tests before the first build.

Existing check targets to preserve:

- `net_unittests`, focused filter: `SSLClientSocketTest.Pubky*:HttpResponseInfoTest.*:DnsResponseResultExtractorTest.*:HostCacheTest.Pubky*:DnsTransactionTest.Pubky*:HostResolverManagerTest.Pubky*`.
- `third_party/blink/renderer/platform:pubky_browser_unittests` and its produced `pubky_browser_unittests` executable.
- Pubky capability web-platform tests described in `pubky/scripts/test_capabilities.sh`.
- Live public-key navigation, deliberate wrong-key rejection, and ordinary HTTPS browsing, without DNS/TLS bypass flags.

Use `gn gen` and `autoninja -C <isolated-output> -j <approved-limit> <target>` with recorded Linux arguments; these placeholders describe the build contract, not a completed or runnable Linux setup. Record literal commands, results, revision, and relevant artifact hashes in each validation handoff. A full Linux baseline build belongs to the subsequent baseline work; S0 must provide the prerequisites and measured checkout/setup evidence or explicitly approved amendments to that gate.

## Branches, review, and integration

Use one branch per issue, based on the pinned development branch, named `s0/<issue-number>-<description>` for foundation work. Give each implementation an isolated writable workspace. Do not force-push or change inherited dependency pins as incidental cleanup. Upstream synchronization cadence and rebase cost remain an S0 decision/evidence item; there are no automatic upstream merges yet.

An issue is Ready only when its goal, boundaries, dependencies, acceptance checks, and approval context are sufficient to execute. Independent work can proceed in parallel. Shared protocol/interface changes, dependency upgrades, broad refactors, and builds must be coordinated; review takes priority over starting another implementation.

Authors self-review and exercise the relevant behavior. A different engineer reviews the exact final revision, checking both the diff and the evidence. The author does not merge their own work. New commits invalidate affected review/test evidence. Integration is sequential, with dependent changes checked against the new base.

Autonomous merging requires agreement with the approved scope, independent approval, and passing required checks. Missing CI is not evidence of a pass. Documentation-only changes require accurate source references, consistent instructions, and verification of the published diff; they must not claim unrun builds. Do not waive failures or weaken tests to make progress. Escalate after two failed attempts or two unsuccessful review/repair cycles, with the failure and what can continue.

Because engineers share one GitHub account, agent cross-review is not a separate GitHub approval identity. Review evidence must name the reviewing engineer and exact commit. Findings, author replies, and decisions belong on the PR. Agreement must be supported by code, contracts, or execution evidence, not by agents talking each other into approval.

## S0 acceptance ledger

S0 is not complete when the repository and board exist. Track these gates in issues and link the exact artifacts/revisions:

| Gate | Current state |
|---|---|
| Mission, exclusions, repository, first platform and operating permissions | Recorded here; independent review pending |
| Base adoption, source/tool pins, patch scope, licensing, update policy and maintenance-cost evidence | Pins adopted; maintenance assessment pending |
| Identifier grammar: PKD sites versus Pubky identities, search/fallback, malformed/Unicode input | Decision brief and review pending |
| TLS trust model: key binding, secure context, downgrade, mixed content, connection display | Decision brief and review pending |
| Profile/avatar lookup and privacy defaults | Owner decision pending; no new lookups implemented |
| Scoped-session design brief and origin-permission/XSS analysis | Design and independent review pending; implementation deferred |
| Threat model and Google-service inventory/replacement policy | Analysis and owner decisions pending |
| Linux checkout/build isolation, resource/access limits, first-fetch runbook/evidence | Plan and scoped dependency-download permission pending |
| Dependency ledger and independent protocol/state-machine review | Pending |

Preserve the original roadmap's substantive gates. Record an explicit amendment when a previous environment assumption is replaced; do not silently mark an unmeasured or unavailable gate complete. Security-sensitive runtime implementation starts only after its relevant contracts and S0 review are settled.

The public issues hold actionable work and sanitized evidence. The private planning board can hold decision requests and future work, but does not make linked public issues private. Later roadmap stages remain high-level planning items until their scope is approved.
