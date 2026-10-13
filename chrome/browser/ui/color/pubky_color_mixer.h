// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COLOR_PUBKY_COLOR_MIXER_H_
#define CHROME_BROWSER_UI_COLOR_PUBKY_COLOR_MIXER_H_

#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_provider_key.h"

namespace ui {
class ColorProvider;
}

inline constexpr SkColor kPubkyAccentColor = SkColorSetRGB(0xC8, 0xFF, 0x00);
inline constexpr SkColor kPubkyDarkBaseColor = SkColorSetRGB(0x05, 0x05, 0x0A);
inline constexpr SkColor kPubkyLightBaseColor = SK_ColorWHITE;

// Replaces the baseline (blue) palette of the default theme with the Pubky
// palette. Does nothing for user colors, custom, system, grayscale
// (incognito) and high contrast themes.
void AddPubkyColorMixer(ui::ColorProvider* provider,
                        const ui::ColorProviderKey& key);

#endif  // CHROME_BROWSER_UI_COLOR_PUBKY_COLOR_MIXER_H_
