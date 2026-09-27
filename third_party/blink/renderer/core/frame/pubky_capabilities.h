// Copyright 2026 The Pubky Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_FRAME_PUBKY_CAPABILITIES_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_FRAME_PUBKY_CAPABILITIES_H_

#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/platform/bindings/script_wrappable.h"
#include "third_party/blink/renderer/platform/supplementable.h"

namespace blink {

class Navigator;

// Build capabilities, independent of DNS settings, connectivity, and the
// authentication method used for the current document.
class CORE_EXPORT PubkyCapabilities final : public ScriptWrappable,
                                           public Supplement<Navigator> {
  DEFINE_WRAPPERTYPEINFO();

 public:
  static const char kSupplementName[];
  static PubkyCapabilities* pubky(Navigator&);

  explicit PubkyCapabilities(Navigator&);

  bool supportsPkdns() const { return true; }
  bool supportsRawPublicKeyTls() const { return true; }

  void Trace(Visitor*) const override;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_FRAME_PUBKY_CAPABILITIES_H_
