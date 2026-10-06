// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/pubky_update/controller.h"

#include <algorithm>
#include <optional>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "chrome/common/pubky_update/test_record.h"
#include "components/prefs/testing_pref_service.h"
#include "net/base/load_flags.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_utils.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pubky_update {
namespace {
class PubkyUpdateControllerTest : public testing::Test {
 protected:
  PubkyUpdateControllerTest() {
    Controller::RegisterLocalState(prefs_.registry());
    Controller::TestBoundaries boundaries;
    auto key = test::PublicKey();
    std::ranges::copy(key, boundaries.public_key.begin());
    boundaries.running = base::Version("156.0.8073.0");
    boundaries.installed = boundaries.running;
    boundaries.fetch = base::BindRepeating(
        [](PubkyUpdateControllerTest* self, GURL url,
           base::OnceCallback<void(std::string)> reply) {
          EXPECT_EQ(FeedUrl(Target::kLinuxX64), url);
          ++self->fetches_;
          self->metadata_reply_ = std::move(reply);
        }, base::Unretained(this));
    boundaries.download_and_verify = base::BindRepeating(
        [](PubkyUpdateControllerTest* self, const Record& record,
           base::OnceCallback<void(StageResult)> reply) {
          EXPECT_EQ("156.0.8073.1", record.version.GetString());
          ++self->downloads_;
          self->download_record_ = record;
          self->download_reply_ = std::move(reply);
        }, base::Unretained(this));
    boundaries.cancel = base::BindRepeating(
        [](PubkyUpdateControllerTest* self) { ++self->cancels_; },
        base::Unretained(this));
    boundaries.restart = base::BindRepeating(
        [](PubkyUpdateControllerTest* self,
           base::OnceCallback<void(Controller::RestartResult)> reply) {
          ++self->restarts_;
          if (self->immediate_restart_result_) {
            std::move(reply).Run(*self->immediate_restart_result_);
          } else {
            self->restart_reply_ = std::move(reply);
          }
        },
        base::Unretained(this));
    controller_ = Controller::CreateForTesting(&prefs_, std::move(boundaries));
  }
  std::string Id() { return *controller_->GetStatus().FindString("id"); }
  std::string State() { return *controller_->GetStatus().FindString("state"); }
  void Offer() {
    controller_->Check();
    std::move(metadata_reply_).Run(test::Envelope(test::Record(base::Time::Now())));
  }
  void Ready() {
    Offer();
    controller_->Confirm(Id());
    std::move(download_reply_).Run(Staged());
  }
  StageResult Staged() {
    StagedPackage package;
    package.path = base::FilePath(FILE_PATH_LITERAL("/fake/package"));
    package.size = download_record_->size;
    package.sha256 = download_record_->sha256;
    package.envelope = download_record_->authenticated_envelope;
    StageResult result;
    result.package = std::move(package);
    return result;
  }
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  TestingPrefServiceSimple prefs_;
  int fetches_ = 0, downloads_ = 0, cancels_ = 0, restarts_ = 0;
  base::OnceCallback<void(std::string)> metadata_reply_;
  base::OnceCallback<void(StageResult)> download_reply_;
  std::optional<Record> download_record_;
  base::OnceCallback<void(Controller::RestartResult)> restart_reply_;
  std::optional<Controller::RestartResult> immediate_restart_result_;
  std::unique_ptr<Controller> controller_;
};
TEST_F(PubkyUpdateControllerTest, LocalReadsAndProductionAreInert) {
  auto tab1 = controller_->Observe(base::DoNothing());
  auto tab2 = controller_->Observe(base::DoNothing());
  controller_->GetStatus();
  controller_->GetStatus();
  EXPECT_EQ(0, fetches_);
  EXPECT_EQ(0, downloads_);
  Controller production(&prefs_);
  production.Check();
  production.Confirm("renderer-url-or-path");
  production.Restart("anything");
  EXPECT_EQ("unsupported", *production.GetStatus().FindString("state"));
  EXPECT_FALSE(production.GetStatus().FindBool("canCheck").value());
}
TEST_F(PubkyUpdateControllerTest, DuplicateStaleConsentAndCancel) {
  controller_->Check();
  const auto id = Id();
  controller_->Check();
  controller_->Confirm(id);
  EXPECT_EQ(1, fetches_);
  EXPECT_EQ(0, downloads_);
  std::move(metadata_reply_).Run(test::Envelope(test::Record(base::Time::Now())));
  EXPECT_EQ("available", State());
  controller_->Confirm("stale");
  controller_->Restart(id);
  EXPECT_EQ(0, downloads_);
  EXPECT_EQ(0, restarts_);
  controller_->Confirm(id);
  controller_->Confirm(id);
  EXPECT_EQ(1, downloads_);
  controller_->Cancel("stale");
  EXPECT_EQ("downloading", State());
  controller_->Cancel(id);
  std::move(download_reply_).Run(Staged());
  EXPECT_EQ("canceled", State());
  EXPECT_EQ(0, restarts_);
  EXPECT_EQ("156.0.8073.1", prefs_.GetString("pubky_update.linux_x64.offered_floor"));
  Offer();
  EXPECT_NE(id, Id());
  EXPECT_EQ("available", State());
  controller_->Confirm(id);
  EXPECT_EQ(1, downloads_);
}
TEST_F(PubkyUpdateControllerTest, TwoTabsAndUnconfirmedDetach) {
  auto tab1 = controller_->Observe(base::DoNothing());
  auto tab2 = controller_->Observe(base::DoNothing());
  controller_->Check();
  tab1 = {};
  controller_->Detach();
  EXPECT_EQ("checking", State());
  tab2 = {};
  controller_->Detach();
  std::move(metadata_reply_).Run(test::Envelope(test::Record(base::Time::Now())));
  EXPECT_EQ("canceled", State());
  EXPECT_EQ(0, downloads_);
}
TEST_F(PubkyUpdateControllerTest, ConfirmedWorkSurvivesDetachButNotShutdown) {
  Offer();
  controller_->Confirm(Id());
  controller_->Detach();
  EXPECT_EQ("downloading", State());
  controller_->Shutdown();
  std::move(download_reply_).Run(Staged());
  EXPECT_EQ("unsupported", State());
  EXPECT_EQ(0, restarts_);
}
TEST_F(PubkyUpdateControllerTest, FailureRetryAndExplicitRestart) {
  controller_->Check();
  std::move(metadata_reply_).Run("not metadata");
  EXPECT_EQ("failed", State());
  Offer();
  auto id = Id();
  controller_->Confirm(id);
  std::move(download_reply_).Run({});
  EXPECT_EQ("failed", State());
  controller_->Restart(id);
  EXPECT_EQ(0, restarts_);
  Offer();
  id = Id();
  controller_->Confirm(id);
  std::move(download_reply_).Run(Staged());
  EXPECT_EQ("ready", State());
  controller_->Restart("stale");
  EXPECT_EQ(0, restarts_);
  controller_->Restart(id);
  controller_->Restart(id);
  EXPECT_EQ(1, restarts_);
  EXPECT_EQ("restarting", State());
  std::move(restart_reply_).Run(Controller::RestartResult::kCommitted);
  EXPECT_EQ("committed", State());
}
TEST_F(PubkyUpdateControllerTest, AbortedRestartRequiresFreshConsent) {
  Ready();
  const auto id = Id();
  controller_->Restart(id);
  EXPECT_TRUE(Id().empty());
  EXPECT_FALSE(controller_->GetStatus().FindBool("canRestart").value());
  EXPECT_FALSE(controller_->GetStatus().FindBool("canCancel").value());
  std::move(restart_reply_).Run(Controller::RestartResult::kAborted);
  EXPECT_EQ("ready", State());
  EXPECT_TRUE(controller_->GetStatus().FindBool("canRestart").value());
  EXPECT_TRUE(controller_->GetStatus().FindBool("canCancel").value());
  EXPECT_NE(id, Id());
  EXPECT_FALSE(Id().empty());
  EXPECT_EQ(0, cancels_);
  controller_->Restart(id);
  EXPECT_EQ(1, restarts_);
  controller_->Restart(Id());
  EXPECT_EQ(2, restarts_);
  std::move(restart_reply_).Run(Controller::RestartResult::kCommitted);
  EXPECT_EQ("committed", State());
  controller_->Shutdown();
  EXPECT_EQ(0, cancels_);
}
TEST_F(PubkyUpdateControllerTest, SynchronousAbortRestoresFreshConsent) {
  Ready();
  const auto id = Id();
  immediate_restart_result_ = Controller::RestartResult::kAborted;
  controller_->Restart(id);
  EXPECT_EQ("ready", State());
  EXPECT_NE(id, Id());
  EXPECT_TRUE(controller_->GetStatus().FindBool("canRestart").value());
  EXPECT_EQ(1, restarts_);
  controller_->Restart(id);
  EXPECT_EQ(1, restarts_);
}
TEST_F(PubkyUpdateControllerTest, FailedRestartCleansTransactionAndAllowsCheck) {
  Ready();
  const auto id = Id();
  controller_->Restart(id);
  std::move(restart_reply_).Run(Controller::RestartResult::kFailed);
  EXPECT_EQ("failed", State());
  EXPECT_EQ(1, cancels_);
  EXPECT_TRUE(Id().empty());
  EXPECT_TRUE(controller_->GetStatus().FindString("version")->empty());
  EXPECT_TRUE(controller_->GetStatus().FindBool("canCheck").value());
  controller_->Restart(id);
  EXPECT_EQ(1, restarts_);
  Offer();
  EXPECT_EQ("available", State());
}
TEST_F(PubkyUpdateControllerTest, AbortAfterExpiryCannotRestoreReady) {
  Ready();
  controller_->Restart(Id());
  task_environment_.AdvanceClock(base::Days(31));
  std::move(restart_reply_).Run(Controller::RestartResult::kAborted);
  EXPECT_EQ("failed", State());
  EXPECT_EQ(static_cast<int>(RecordError::kExpired),
            controller_->GetStatus().FindInt("error").value());
  EXPECT_EQ(1, cancels_);
  EXPECT_FALSE(controller_->GetStatus().FindBool("canRestart").value());
}
TEST_F(PubkyUpdateControllerTest, ShutdownDisarmsPendingRestartAndDropsReply) {
  Ready();
  controller_->Restart(Id());
  controller_->Shutdown();
  EXPECT_EQ(1, cancels_);
  std::move(restart_reply_).Run(Controller::RestartResult::kCommitted);
  EXPECT_EQ("unsupported", State());
  EXPECT_FALSE(controller_->GetStatus().FindBool("canRestart").value());
  controller_->Shutdown();
  EXPECT_EQ(1, cancels_);
}
TEST_F(PubkyUpdateControllerTest, ShutdownNotificationPreventsRestartBoundary) {
  Ready();
  auto tab = controller_->Observe(base::BindLambdaForTesting([this] {
    if (State() == "restarting") {
      controller_->Shutdown();
    }
  }));
  controller_->Restart(Id());
  EXPECT_EQ(0, restarts_);
  EXPECT_EQ(1, cancels_);
  EXPECT_EQ("unsupported", State());
}
TEST_F(PubkyUpdateControllerTest, AbortNotificationCanCancelRetainedOffer) {
  Ready();
  controller_->Restart(Id());
  auto tab = controller_->Observe(base::BindLambdaForTesting([this] {
    if (State() == "ready") {
      controller_->Cancel(Id());
    }
  }));
  std::move(restart_reply_).Run(Controller::RestartResult::kAborted);
  EXPECT_EQ("canceled", State());
  EXPECT_EQ(1, cancels_);
  EXPECT_EQ(1, restarts_);
}
TEST_F(PubkyUpdateControllerTest, NotificationCancellationPreventsBoundaryWork) {
  auto tab = controller_->Observe(base::BindLambdaForTesting([this] {
    if (State() == "checking" || State() == "downloading") {
      controller_->Cancel(Id());
    }
  }));
  controller_->Check();
  EXPECT_EQ("canceled", State());
  EXPECT_EQ(0, fetches_);
  tab = {};
  Offer();
  tab = controller_->Observe(base::BindLambdaForTesting([this] {
    if (State() == "downloading") {
      controller_->Cancel(Id());
    }
  }));
  controller_->Confirm(Id());
  EXPECT_EQ("canceled", State());
  EXPECT_EQ(0, downloads_);
}
TEST_F(PubkyUpdateControllerTest, CorruptFloorFailsClosed) {
  prefs_.SetString("pubky_update.linux_x64.offered_floor", "bad");
  Offer();
  EXPECT_EQ("failed", State());
  EXPECT_EQ(0, downloads_);
}
TEST_F(PubkyUpdateControllerTest, RejectsStagedResultNotBoundToOffer) {
  for (int field = 0; field < 5; ++field) {
    Offer();
    controller_->Confirm(Id());
    auto result = Staged();
    switch (field) {
      case 0: result.package->path.clear(); break;
      case 1: ++result.package->size; break;
      case 2: result.package->sha256 = std::string(64, 'b'); break;
      case 3: result.package->envelope += "\n"; break;
      case 4: result.eligibility = EligibilityReason::kLowSpace; break;
    }
    std::move(download_reply_).Run(std::move(result));
    EXPECT_EQ("failed", State());
    EXPECT_FALSE(controller_->GetStatus().FindBool("canRestart").value());
  }
  EXPECT_EQ(static_cast<int>(EligibilityReason::kLowSpace),
            controller_->GetStatus().FindInt("eligibilityReason").value());
}
TEST_F(PubkyUpdateControllerTest, ExpiryDuringDownloadCannotBecomeReady) {
  Offer();
  controller_->Confirm(Id());
  task_environment_.AdvanceClock(base::Days(31));
  std::move(download_reply_).Run(Staged());
  EXPECT_EQ("failed", State());
  EXPECT_EQ(static_cast<int>(RecordError::kExpired),
            controller_->GetStatus().FindInt("error").value());
  EXPECT_FALSE(controller_->GetStatus().FindBool("canRestart").value());
}
TEST_F(PubkyUpdateControllerTest, ExpiryIsRecheckedAtConsent) {
  Offer();
  auto id = Id();
  task_environment_.AdvanceClock(base::Days(31));
  controller_->Confirm(id);
  EXPECT_EQ("failed", State());
  EXPECT_EQ(0, downloads_);
  Offer();
  id = Id();
  controller_->Confirm(id);
  std::move(download_reply_).Run(Staged());
  task_environment_.AdvanceClock(base::Days(31));
  controller_->Restart(id);
  EXPECT_EQ("failed", State());
  EXPECT_EQ(0, restarts_);
}
TEST(PubkyUpdateTransportTest, OnlyExplicitCheckStartsBoundedCredentiallessRequest) {
  base::test::TaskEnvironment environment;
  TestingPrefServiceSimple prefs;
  Controller::RegisterLocalState(prefs.registry());
  network::TestURLLoaderFactory factory;
  Controller::TestBoundaries boundaries;
  std::ranges::copy(test::PublicKey(), boundaries.public_key.begin());
  boundaries.running = base::Version("156.0.8073.0");
  boundaries.installed = boundaries.running;
  boundaries.metadata_factory = factory.GetSafeWeakWrapper();
  boundaries.cancel = base::DoNothing();
  auto controller = Controller::CreateForTesting(&prefs, std::move(boundaries));
  auto tab = controller->Observe(base::DoNothing());
  controller->GetStatus();
  controller->GetStatus();
  EXPECT_EQ(0, factory.NumPending());
  controller->Check();
  controller->Check();
  ASSERT_EQ(1, factory.NumPending());
  const network::ResourceRequest* request = nullptr;
  ASSERT_TRUE(factory.IsPending(FeedUrl(Target::kLinuxX64).spec(), &request));
  EXPECT_EQ(network::mojom::CredentialsMode::kOmit, request->credentials_mode);
  EXPECT_TRUE(request->referrer.is_empty());
  EXPECT_TRUE(request->headers.IsEmpty());
  EXPECT_TRUE(request->load_flags & net::LOAD_DO_NOT_SEND_AUTH_DATA);
  ASSERT_TRUE(factory.SimulateResponseForPendingRequest(
      FeedUrl(Target::kLinuxX64).spec(), test::Envelope(test::Record(base::Time::Now()))));
  environment.RunUntilIdle();
  EXPECT_EQ("available", *controller->GetStatus().FindString("state"));
  EXPECT_FALSE(controller->GetStatus().FindBool("canConfirm").value());
  auto id = *controller->GetStatus().FindString("id");
  controller->Cancel(id);
  controller->Check();
  ASSERT_TRUE(factory.SimulateResponseForPendingRequest(
      FeedUrl(Target::kLinuxX64).spec(), std::string(kMaxEnvelopeBytes + 1, 'x')));
  environment.RunUntilIdle();
  EXPECT_EQ("failed", *controller->GetStatus().FindString("state"));
  controller->Check();
  id = *controller->GetStatus().FindString("id");
  controller->Cancel(id);
  environment.RunUntilIdle();
  EXPECT_FALSE(factory.IsPending(FeedUrl(Target::kLinuxX64).spec()));
  for (int count : {1, 4}) {
    network::TestURLLoaderFactory::Redirects redirects;
    for (int i = 0; i < count; ++i) {
      net::RedirectInfo redirect;
      redirect.new_url = count == 1 ? GURL("https://evil.example/feed")
                                   : FeedUrl(Target::kLinuxX64);
      redirects.emplace_back(redirect, network::CreateURLResponseHead(net::HTTP_FOUND));
    }
    factory.AddResponse(FeedUrl(Target::kLinuxX64),
        network::CreateURLResponseHead(net::HTTP_OK),
        test::Envelope(test::Record(base::Time::Now())),
        network::URLLoaderCompletionStatus(net::OK), std::move(redirects));
    controller->Check();
    environment.RunUntilIdle();
    EXPECT_EQ("failed", *controller->GetStatus().FindString("state"));
    EXPECT_EQ(static_cast<int>(RecordError::kOrigin),
              controller->GetStatus().FindInt("error").value());
  }
}
}  // namespace
}  // namespace pubky_update
