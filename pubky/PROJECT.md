# Pubky Chromium project

Status: S0 foundations in progress. This document records the approved operating boundaries; it does not claim that the browser has been rebuilt or validated by this project.

## Product and first milestone

Build on the existing Pubky Chromium fork. The long-term goal is native Pubky identity and scoped sessions, with public-key navigation, authenticated connections, and identity UI. The immediate milestone is a reproducible Linux baseline and an evidence-backed map of the remaining work. No session storage, authenticated writes, replacement of existing browsing protections, mobile implementation, release signing, or new resolver implementation is authorized in this first milestone.

Repository: `MCarlomagno/pubky-chromium`, development branch `pubky`.

Adopted source revision: `77445742d82c8cfc482b2fea0476fe5f84d16032` from `SeverinAlexB/pubky-chromium`. Its recorded Chromium base is `7fec6741f4f7ea010205dc20afc7dafb90221176`. Keep the inherited `DEPS` and `pubky/depot-tools-revision.txt` pins until an approved update. Preserve Chromium branding/licensing restrictions and third-party notices.

The inherited project documents macOS ARM64 results in `pubky/VERIFICATION.md`. Those are the source author's results, not independent Linux validation by this project. The current `navigator.pubky` interface reports built-in capabilities; it is not a privileged session API.

## Scope and standing approvals

- Autonomous work is limited to this fork and explicitly approved goals. Before any repository action outside this fork, request owner approval naming the repository, purpose, and actions. Record possible external dependencies here or in this project's issues; do not open external issues, comments, branches, or PRs without permission.
- An approved assignment carries these standing approvals: a branch off `pubky` named `s0/<issue-or-topic>-<description>`, the tests that apply to the change, and a pull request against `pubky`. Do not ask for them again. Everything else in this list still needs the owner.
- Work reaches an engineer in one of two ways. While the owner is online, the owner or the architect assigns it directly in chat. For work queued while the owner is away, the owner labels an issue `ready` plus `mini-1` or `mini-2`; the queue sync sets `in-progress`, and the engineer's result moves it to `review` (waiting for the owner) or `blocked` (stopped after two failed attempts, with the reason in a comment). Either way the result is a pull request with one handoff comment.
- Questions and exploratory ideas are discussion, not authorization. New product, protocol, architecture, security, or privacy decisions go to the owner with a recommendation and alternatives.
- No new paid infrastructure, releases, deployment, or credential sharing is authorized. Use existing resources within their approved limits.
- Internal conversations and private source material must not be copied into public issues, commits, or artifacts. Public claims need public evidence or explicit disclosure approval.

## Target platforms and build gate

Linux x86_64 is the initial reproduction target on the designated Linux builder. A resource-capped trial there is approved; the previous cloud-only assumption is superseded. No cloud purchase is approved. macOS remains an inherited, unverified-by-us platform; macOS-specific changes need actual macOS validation. Windows is part of the first milestone; Windows-specific changes need Windows validation before a release. Mobile is outside the first milestone.

Do not compile Chromium for routine work; compilation is reserved for releases. Each engineer works in its own checkout on its own machine. Release builds run on the designated builder under its host lock, one at a time. Running `gclient sync` or changing dependency pins needs the owner. Never delete or clean an `out/` directory.

The inherited `pubky/args.gn`, `pubky/gclient.py`, and scripts target macOS ARM64 and use `caffeinate`. Do not run them on Linux as if they were validated Linux commands.

Release checks to preserve:

- `net_unittests`, focused filter: `SSLClientSocketTest.Pubky*:HttpResponseInfoTest.*:DnsResponseResultExtractorTest.*:HostCacheTest.Pubky*:DnsTransactionTest.Pubky*:HostResolverManagerTest.Pubky*`.
- `third_party/blink/renderer/platform:pubky_browser_unittests` and its produced `pubky_browser_unittests` executable.
- Pubky capability web-platform tests described in `pubky/scripts/test_capabilities.sh`.
- Live public-key navigation, deliberate wrong-key rejection, and ordinary HTTPS browsing, without DNS/TLS bypass flags.

For routine work, run the tests for the changed code that need no Chromium build (for example Python tests and `PRESUBMIT_test.py`). Passing tests are enough for review. Record the commands, the revision, and the results in the handoff comment. When preparing a release, build with `autoninja -C <output> <target>` and run the release checks above.

## Branches, review, and integration

Use one branch per issue, based on the pinned development branch, named `s0/<issue-number>-<description>` for foundation work. Give each implementation an isolated writable workspace. Do not force-push or change inherited dependency pins as incidental cleanup. Upstream synchronization cadence and rebase cost remain an S0 decision/evidence item; there are no automatic upstream merges yet.

An issue is Ready only when its goal, boundaries, dependencies, acceptance checks, and approval context are sufficient to execute. Independent work can proceed in parallel. Shared protocol/interface changes, dependency upgrades, broad refactors, and builds must be coordinated; review takes priority over starting another implementation.

Authors self-review and exercise the behavior they changed. The owner reviews and merges pull requests; a second engineer reviews only when the owner labels the PR `ready` with the other machine's label. New commits invalidate affected review evidence. Integration is sequential, with dependent changes checked against the new base.

Engineers do not merge. Documentation-only pull requests merge without review. Missing CI is not evidence of a pass. Documentation-only changes must not claim unrun builds. Do not waive failures or weaken tests to make progress. Stop after two failed attempts or two unsuccessful review/repair cycles and report the failure and what can continue.

Engineers share one GitHub account, so a review comment names the reviewing engineer and the exact commit. Findings, author replies, and decisions belong on the PR.

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
