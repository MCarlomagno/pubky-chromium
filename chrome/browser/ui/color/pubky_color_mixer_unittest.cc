// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/color/pubky_color_mixer.h"

#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/color/chrome_color_mixers.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/color/color_id.h"
#include "ui/color/color_mixers.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"
#include "ui/gfx/color_utils.h"

namespace {

using ColorMode = ui::ColorProviderKey::ColorMode;

struct Expected {
  ColorMode mode;
  SkColor frame;
  SkColor toolbar;
  SkColor omnibox;
  SkColor focus_ring;
};

void AddMixers(ui::ColorProvider& provider, const ui::ColorProviderKey& key) {
  ui::AddColorMixers(&provider, key);
  AddChromeColorMixers(&provider, key);
}

TEST(PubkyColorMixerTest, DefaultThemeUsesPubkyPalette) {
  constexpr Expected kCases[] = {
      {ColorMode::kLight, SkColorSetRGB(0xF1, 0xFF, 0xBF), SK_ColorWHITE,
       SkColorSetRGB(0xF0, 0xF0, 0xF1), SkColorSetRGB(0x53, 0x69, 0x06)},
      {ColorMode::kDark, SkColorSetRGB(0x1E, 0x1E, 0x23), kPubkyDarkBaseColor,
       SkColorSetRGB(0x1E, 0x1E, 0x23), kPubkyAccentColor},
  };
  for (const Expected& e : kCases) {
    SCOPED_TRACE(e.mode == ColorMode::kDark ? "dark" : "light");
    ui::ColorProviderKey key;
    key.color_mode = e.mode;
    ui::ColorProvider provider;
    AddMixers(provider, key);

    EXPECT_EQ(provider.GetColor(ui::kColorFrameActive), e.frame);
    EXPECT_EQ(provider.GetColor(kColorToolbar), e.toolbar);
    EXPECT_EQ(provider.GetColor(kColorTabBackgroundActiveFrameActive),
              e.toolbar);
    EXPECT_EQ(provider.GetColor(kColorNewTabPageBackground), e.toolbar);
    EXPECT_EQ(provider.GetColor(kColorToolbarBackgroundSubtleEmphasis),
              e.omnibox);
    EXPECT_EQ(provider.GetColor(ui::kColorFocusableBorderFocused),
              e.focus_ring);

    // Text needs 4.5:1 (WCAG AA); the focus ring is non-text and needs 3:1.
    const struct {
      ui::ColorId text;
      ui::ColorId background;
    } kTextPairs[] = {
        {kColorTabForegroundActiveFrameActive,
         kColorTabBackgroundActiveFrameActive},
        {kColorTabForegroundInactiveFrameActive, ui::kColorFrameActive},
        {kColorToolbarText, kColorToolbar},
        {kColorOmniboxText, kColorToolbarBackgroundSubtleEmphasis},
        {kColorOmniboxTextDimmed, kColorToolbarBackgroundSubtleEmphasis},
        {kColorNewTabPageText, kColorNewTabPageBackground},
        {ui::kColorSysPrimary, ui::kColorSysBase},
        {ui::kColorSysPrimary, ui::kColorSysOmniboxContainer},
        {ui::kColorSysOnPrimary, ui::kColorSysPrimary},
    };
    for (const auto& pair : kTextPairs) {
      EXPECT_GE(
          color_utils::GetContrastRatio(provider.GetColor(pair.text),
                                        provider.GetColor(pair.background)),
          4.5f)
          << "text " << pair.text << " on " << pair.background;
    }
    EXPECT_GE(color_utils::GetContrastRatio(e.focus_ring, e.toolbar), 3.0f);
    EXPECT_GE(color_utils::GetContrastRatio(e.focus_ring, e.omnibox), 3.0f);
  }
}

TEST(PubkyColorMixerTest, UserColorIsKept) {
  for (ColorMode mode : {ColorMode::kLight, ColorMode::kDark}) {
    ui::ColorProviderKey key;
    key.color_mode = mode;
    key.user_color = SK_ColorRED;
    ui::ColorProvider ui_only;
    ui::AddColorMixers(&ui_only, key);
    ui::ColorProvider provider;
    AddMixers(provider, key);

    EXPECT_EQ(provider.GetColor(ui::kColorSysPrimary),
              ui_only.GetColor(ui::kColorSysPrimary));
    EXPECT_EQ(provider.GetColor(ui::kColorSysHeader),
              ui_only.GetColor(ui::kColorSysHeader));
  }
}

}  // namespace
