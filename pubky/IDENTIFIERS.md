# Identifier grammar

Status: October 2026. This document defines which text the browser treats as
a Pubky address, and what it does with it. Each rule is marked:

- **Implemented**: the browser does this today.
- **Decided**: agreed behaviour that is not implemented yet.

It assumes resolution moves to PKResolve
([TLS_TRUST_MODEL.md](TLS_TRUST_MODEL.md)), so the browser's rules match
PKResolve's name rules. PKResolve references are to
`pubky/pkresolve@e88b7072`.

## Building blocks

```text
key       = 52 lowercase z-base-32 characters, canonical encoding
label     = 1-63 characters from [a-z0-9_-], not starting or ending with "-"
key-host  = *(label ".") key ["."]          ; at most 253 characters
```

- **Key.** An Ed25519 public key in z-base-32: alphabet
  `ybndrfg8ejkmcpqxot1uwisza345h769`, exactly 52 characters, with the unused
  final bits zero (so the last character is `y` or `o`). Implemented in
  `net/base/pubky_public_key.h`.
- **The key is always the last label.** Everything before it is a name
  under the key's authority: `www.<key>`, `app.<key>` and `_pubky.<key>` are
  all key hosts owned by `<key>`. `<key>.com` and `<key>.evil` are ordinary
  domains. Implemented.
- **Case.** Hosts are lowercased before any check, so `https://<KEY>/` is
  the same host as `https://<key>/`. Implemented (URL canonicalization), and
  PKResolve does the same.
- **Trailing dot.** `<key>.` and `<key>` are the same name for resolution
  and TLS. As for ordinary domains, the browser keeps them as separate
  origins. Implemented.
- **Label rules.** Labels before the key follow PKResolve's `ServiceName`
  rules above. A host that breaks them fails to resolve; it never falls back
  to ordinary DNS. Decided.
- **Valid point.** The browser does not check that the key is a valid
  Ed25519 point; a key that is not cannot complete a TLS handshake.
  PKResolve rejects it during resolution. Implemented (via TLS).

## Website addresses

A website addressed by a key is reached over HTTPS, authenticated by that
key (see [TLS_TRUST_MODEL.md](TLS_TRUST_MODEL.md)).

| Input | Meaning | Status |
|---|---|---|
| `https://<key-host>[:port]/path` | Canonical form. | Implemented |
| `<key-host>[/path]` typed or pasted in the omnibox | Navigates to `https://<key-host>/path`. No HTTP fallback. | Implemented (`AutocompleteInputTest.PubkyBareKeyIsExplicitHttps`) |
| `<key-host>:port` | HTTPS on that port. Routing is looked up at `_<port>._https.<key-host>` (RFC 9460). | Implemented |
| `http://<key-host>/...` | Rewritten to `https://` before any request, with no fallback. Plain HTTP has no key authentication. | Decided. Today the page loads over plain HTTP when there is no HTTPS record. |
| Bare key from the command line or another path outside the omnibox | Same as typed input: `https://`. | Decided. Today these become `http://`, which the rule above upgrades. |
| `https://_pubky.<key>/...` | The key's homeserver. It is a separate service from the website at `<key>`, under the same key. | Implemented as an ordinary key host |

## Identity and resource addresses

These follow the Pubky SDK (`pubky-sdk/src/actors/storage/resource.rs`).

| Input | Meaning | Status |
|---|---|---|
| `pubky<key>/<path>` | A file on the key's homeserver. This is the SDK's preferred form. | Decided |
| `pubky://<key>/<path>` | Same file, deep-link form. | Decided |
| `pubky<key>` without a path | A Pubky identity. | Decided |
| `pk:<key>` | Not recognized. It is a Pubky App convention, not an SDK form. | Decided |

Decided behaviour for these forms:

- **Resource addresses** load from
  `https://_pubky.<key>/storage/<key>/<path>`, the SDK's transport URL.
  The address bar keeps the `pubky://<key>/<path>` form. Navigation and
  connections follow the website rules above.
- **An identity without a path** opens `https://<key>/` until identity cards
  exist. Identity cards then replace this with a card for the key.
- **Never sent to search.** The omnibox recognizes all of these forms
  locally, so a pasted Pubky address is not sent to the search provider as
  a query.
- **Raw key after `pubky://`.** `pubky://pubky<key>/` is invalid, as in the
  SDK.

## Input that is not a key

- A string of 51 or 53 characters, or with a character outside the alphabet,
  is not a key. The omnibox treats it as ordinary input and does not guess a
  correction. Implemented.

## Display

- **Show the key in full.** The key is the identity; it is never shortened,
  replaced or hidden. Implemented.
- **Emphasize the key, not the labels.** Labels before the key are chosen by
  the key holder and can imitate other sites, for example
  `paypal.com.<key>`. The address bar treats the key as the registrable part
  of the host, as Chromium does for `example.com`: it is the emphasized part
  and is kept when the URL is simplified. Decided.
- **No Unicode labels.** Labels before the key are shown in their ASCII form
  (`xn--…`), never decoded to Unicode. Decided.
- **Page info** names the connection as authenticated by a public key.
  Implemented in English only; localization is pending.

## Changes needed to match this document

- Upgrade `http://` key hosts to `https://`, with no fallback.
- Use `https://` for bare keys outside the omnibox.
- Recognize `pubky<key>/<path>`, `pubky://<key>/<path>` and `pubky<key>`.
- Emphasize the key in the address bar and keep labels in ASCII.
- Pass key hosts to PKResolve as lowercase names without the trailing dot,
  with the URL port, and fail closed on `InvalidServiceName`.
- Localize the page info strings.
