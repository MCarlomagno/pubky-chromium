// Copyright 2026 The Pubky Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/frame/pubky_capabilities.h"

#include "third_party/blink/renderer/core/frame/navigator.h"

namespace blink {

const char PubkyCapabilities::kSupplementName[] = "PubkyCapabilities";

PubkyCapabilities* PubkyCapabilities::pubky(Navigator& navigator) {
  auto* capabilities = Supplement<Navigator>::From<PubkyCapabilities>(navigator);
  if (!capabilities) {
    capabilities = MakeGarbageCollected<PubkyCapabilities>(navigator);
    ProvideTo(navigator, capabilities);
  }
  return capabilities;
}

PubkyCapabilities::PubkyCapabilities(Navigator& navigator)
    : Supplement<Navigator>(navigator) {}

void PubkyCapabilities::Trace(Visitor* visitor) const {
  ScriptWrappable::Trace(visitor);
  Supplement<Navigator>::Trace(visitor);
}

}  // namespace blink
