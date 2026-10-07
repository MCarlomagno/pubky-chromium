# macOS experimental update feed

The pinned Sparkle 2.10.0 framework is bundled in `third_party/sparkle/`.
`enable_pubky_mac_updates=false` leaves production inert until the release
integrator enables it. The browser never selects a feed URL from preferences.
`SUPublicEDKey` pins the owner's public Ed25519 key; the private key must not
enter this repository.

The signed XML feed at `experimental/macos-arm64.xml` contains exactly one item.
Use Sparkle's standard signed-appcast format (`SURequireSignedFeed=YES`) and an
Ed25519-signed full `.zip` enclosure. Put these signed first-level fields on the
item: `pubky:repository=MCarlomagno/pubky-chromium`,
`pubky:product=Pubky Chromium`, `pubky:platform=macos`,
`pubky:architecture=arm64`, `pubky:channel=experimental`,
`pubky:issued_at` and `pubky:expires_at` (exact UTC `YYYY-MM-DDTHH:MM:SSZ`).
The latter must be later than issuance, within 180 days of it and in the future
at check and apply time. Also set `sparkle:channel=experimental`,
`sparkle:hardwareRequirements=arm64`, a minimum macOS version of at least 12,
`sparkle:version` to the generated `BUILD.PATCH`, and the four-component
`sparkle:shortVersionString` to the actual product version. The enclosure
points to an immutable `.zip` in this repository's versioned GitHub Release,
with its exact length and Sparkle `sparkle:edSignature`.

No deltas, HTML release notes, informational or critical items, extra feed
items, package installers, default-channel entries or auto checks/downloads.
The feed must be signed with the same owner key before publishing. This branch
has no native Mac build, bundle seal, quit/cancel runtime or network-redirect
results; these checks remain with the Mac builder and Marcos's installed test.
