// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/pubky_update/buildflags.h"

#if BUILDFLAG(PUBKY_UPDATE_UI)
#include <utility>

#include "base/version.h"
#include "chrome/browser/pubky_update/controller.h"
#include "chrome/browser/ui/webui/settings/about_handler.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_profile.h"
#include "components/prefs/testing_pref_service.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_web_contents_factory.h"
#include "content/public/test/test_web_ui.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace settings {
namespace {

class TestPubkyAboutHandler : public AboutHandler {
 public:
  explicit TestPubkyAboutHandler(Profile* profile) : AboutHandler(profile) {}
  using AboutHandler::set_web_ui;
};

TEST(PubkyAboutHandlerTest, AttachAndRefreshDoNotFetchMetadata) {
  content::BrowserTaskEnvironment environment;
  TestingProfile profile;
  content::TestWebContentsFactory contents_factory;
  content::TestWebUI web_ui;
  web_ui.set_web_contents(contents_factory.CreateWebContents(&profile));
  network::TestURLLoaderFactory factory;
  TestingPrefServiceSimple prefs;
  pubky_update::Controller::RegisterLocalState(prefs.registry());
  pubky_update::Controller::TestBoundaries boundaries;
  boundaries.running = base::Version("156.0.8073.0");
  boundaries.installed = boundaries.running;
  boundaries.metadata_factory = factory.GetSafeWeakWrapper();
  auto controller = pubky_update::Controller::CreateForTesting(
      &prefs, std::move(boundaries));
  auto* process = TestingBrowserProcess::GetGlobal();
  process->SetPubkyUpdateControllerForTesting(controller.get());
  {
    TestPubkyAboutHandler handler(&profile);
    handler.set_web_ui(&web_ui);
    handler.RegisterMessages();
    web_ui.HandleReceivedMessage("aboutPageReady", base::ListValue());
    web_ui.HandleReceivedMessage("refreshUpdateStatus", base::ListValue());
    environment.RunUntilIdle();
    EXPECT_EQ("idle", *controller->GetStatus().FindString("state"));
    EXPECT_EQ(0, factory.NumPending());

    web_ui.HandleReceivedMessage("checkPubkyUpdate", base::ListValue());
    EXPECT_EQ("checking", *controller->GetStatus().FindString("state"));
    EXPECT_EQ(1, factory.NumPending());
  }
  process->SetPubkyUpdateControllerForTesting(nullptr);
}

}  // namespace
}  // namespace settings
#endif  // BUILDFLAG(PUBKY_UPDATE_UI)
