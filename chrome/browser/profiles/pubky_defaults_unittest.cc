// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/feature_list.h"
#include "chrome/browser/preloading/preloading_features.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/testing_profile.h"
#include "components/omnibox/browser/aim_eligibility_service_features.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

TEST(PubkyDefaultsTest, HomePageAndIdleTraffic) {
  content::BrowserTaskEnvironment task_environment;
  TestingProfile profile;
  PrefService* prefs = profile.GetPrefs();

  EXPECT_EQ(
      "https://pubky.org.w3wfjtcjabczp7jupgiizq7db5ps7f3x39addowas4ops8ba3oto/",
      prefs->GetString(prefs::kHomePage));
  EXPECT_FALSE(prefs->GetBoolean(prefs::kHomePageIsNewTabPage));
  EXPECT_TRUE(prefs->GetBoolean(prefs::kShowHomeButton));
  EXPECT_FALSE(prefs->GetBoolean(prefs::kSearchSuggestEnabled));
  EXPECT_FALSE(base::FeatureList::IsEnabled(features::kPrewarm));
  EXPECT_FALSE(base::FeatureList::IsEnabled(omnibox::kAimEnabled));
}
