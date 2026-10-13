// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/color/pubky_color_mixer.h"

#include "chrome/browser/ui/color/chrome_color_provider_utils.h"
#include "ui/color/color_id.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_recipe.h"
#include "ui/color/color_transform.h"
#include "ui/color/ref_color_mixer.h"
#include "ui/color/system_theme.h"

void AddPubkyColorMixer(ui::ColorProvider* provider,
                        const ui::ColorProviderKey& key) {
  using Source = ui::ColorProviderKey::UserColorSource;
  const bool baseline =
      key.user_color_source == Source::kBaseline ||
      (key.user_color_source == Source::kAccent && !key.user_color);
  if (!baseline || key.system_theme != ui::SystemTheme::kDefault ||
      !ShouldApplyChromeMaterialOverrides(key)) {
    return;
  }

  // Tonal palettes generated from the accent replace the blue ref palettes.
  ui::ColorProviderKey pubky_key = key;
  pubky_key.user_color = kPubkyAccentColor;
  pubky_key.user_color_source = Source::kAccent;
  pubky_key.scheme_variant = ui::ColorProviderKey::SchemeVariant::kVibrant;
  ui::AddRefColorMixer(provider, pubky_key);

  ui::ColorMixer& mixer = provider->AddMixer();
  if (key.color_mode == ui::ColorProviderKey::ColorMode::kDark) {
    const ui::ColorTransform raised =
        ui::AlphaBlend({kPubkyLightBaseColor}, {kPubkyDarkBaseColor}, 0x1A);
    mixer[ui::kColorSysBase] = {kPubkyDarkBaseColor};
    mixer[ui::kColorSysHeader] = raised;
    mixer[ui::kColorSysOmniboxContainer] = raised;
    mixer[ui::kColorSysPrimary] = {kPubkyAccentColor};
    mixer[ui::kColorSysOnPrimary] = {kPubkyDarkBaseColor};
  } else {
    mixer[ui::kColorSysBase] = {kPubkyLightBaseColor};
    mixer[ui::kColorSysHeader] =
        ui::AlphaBlend({kPubkyAccentColor}, {kPubkyLightBaseColor}, 0x40);
    mixer[ui::kColorSysOmniboxContainer] =
        ui::AlphaBlend({kPubkyDarkBaseColor}, {kPubkyLightBaseColor}, 0x0F);
    // Lime on white is 1.2:1, so text and focus use a darkened accent.
    mixer[ui::kColorSysPrimary] =
        ui::AlphaBlend({kPubkyAccentColor}, {kPubkyDarkBaseColor}, 0x66);
    mixer[ui::kColorSysOnPrimary] = {kPubkyLightBaseColor};
  }
  mixer[ui::kColorSysStateFocusRing] = {ui::kColorSysPrimary};
}
