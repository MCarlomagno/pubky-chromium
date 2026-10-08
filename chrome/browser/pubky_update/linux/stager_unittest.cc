// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/pubky_update/linux/stager.h"

#include <algorithm>

#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "chrome/common/pubky_update/test_record.h"
#include "components/prefs/testing_pref_service.h"
#include "crypto/hash.h"
#include "net/base/load_flags.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "services/network/test/test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pubky_update {
namespace {
constexpr char kBody[] = "package bytes, not executed";
class PubkyUpdateLinuxStagerTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(user_data_.CreateUniqueTempDir());
    auto payload = test::Record(base::Time::Now());
    payload.Set("size", static_cast<int>(std::string_view(kBody).size()));
    payload.Set("sha256", base::ToLowerASCII(base::HexEncode(crypto::hash::Sha256(kBody))));
    envelope_ = test::Envelope(payload);
    auto verified = VerifyRecord(envelope_, test::PublicKey(), Target::kLinuxX64,
        base::Version("156.0.8073.0"), base::Version("156.0.8073.0"),
        base::Version(), base::Time::Now());
    ASSERT_TRUE(verified.record);
    record_ = *verified.record;
    stager_ = std::make_unique<LinuxStager>(user_data_.GetPath(),
        base::Version("156.0.8073.0"), factory_.GetSafeWeakWrapper());
    SetEligibility(EligibilityReason::kEligible);
  }
  void SetEligibility(EligibilityReason reason) {
    stager_->SetEligibilityProbeForTesting(base::BindRepeating(
        [](EligibilityReason reason, int size) {
          EXPECT_GT(size, 0);
          return reason;
        }, reason));
  }
  void Start() {
    replied_ = false;
    result_.reset();
    stager_->Start(record_, base::BindLambdaForTesting([this](StageResult result) {
      replied_ = true;
      result_ = std::move(result);
    }));
    environment_.RunUntilIdle();
  }
  base::FilePath Directory() {
    base::FileEnumerator dirs(user_data_.GetPath(), false,
                              base::FileEnumerator::DIRECTORIES);
    return dirs.Next();
  }
  void ExpectFailedAndEmpty() {
    environment_.RunUntilIdle();
    ASSERT_TRUE(replied_);
    ASSERT_TRUE(result_);
    EXPECT_FALSE(result_->package);
    EXPECT_TRUE(base::IsDirectoryEmpty(user_data_.GetPath()));
  }
  base::test::TaskEnvironment environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  base::ScopedTempDir user_data_;
  network::TestURLLoaderFactory factory_;
  std::unique_ptr<LinuxStager> stager_;
  Record record_;
  std::string envelope_;
  bool replied_ = false;
  std::optional<StageResult> result_;
};
TEST_F(PubkyUpdateLinuxStagerTest, ExactHashSizeEnvelopeAndPrivateOwnership) {
  Start();
  ASSERT_FALSE(replied_);
  auto directory = Directory();
  ASSERT_FALSE(directory.empty());
  int mode = 0;
  ASSERT_TRUE(base::GetPosixFilePermissions(directory, &mode));
  EXPECT_EQ(0700, mode);
  const network::ResourceRequest* request = nullptr;
  ASSERT_TRUE(factory_.IsPending(record_.url.spec(), &request));
  EXPECT_EQ(network::mojom::CredentialsMode::kOmit, request->credentials_mode);
  EXPECT_TRUE(request->referrer.is_empty());
  EXPECT_TRUE(request->headers.IsEmpty());
  EXPECT_TRUE(request->load_flags & net::LOAD_DISABLE_CACHE);
  ASSERT_TRUE(factory_.SimulateResponseForPendingRequest(record_.url.spec(), kBody));
  environment_.RunUntilIdle();
  ASSERT_TRUE(replied_);
  ASSERT_TRUE(result_->package);
  EXPECT_EQ(record_.size, result_->package->size);
  EXPECT_EQ(record_.sha256, result_->package->sha256);
  EXPECT_EQ(envelope_, result_->package->envelope);
  EXPECT_EQ(directory.AppendASCII("package.deb"), result_->package->path);
  std::string bytes;
  ASSERT_TRUE(base::ReadFileToString(result_->package->path, &bytes));
  EXPECT_EQ(kBody, bytes);
  ASSERT_TRUE(base::ReadFileToString(directory.AppendASCII("envelope.json"), &bytes));
  EXPECT_EQ(envelope_, bytes);
  result_.reset();
  environment_.RunUntilIdle();
  EXPECT_FALSE(base::PathExists(directory));
}
TEST_F(PubkyUpdateLinuxStagerTest, OverflowTruncationHashAndHttpErrorsDelete) {
  for (const std::string& body : {std::string(kBody) + "overflow", std::string("short"),
                                std::string(std::string_view(kBody).size(), 'x')}) {
    Start();
    ASSERT_TRUE(factory_.SimulateResponseForPendingRequest(record_.url.spec(), body));
    ExpectFailedAndEmpty();
  }
  factory_.AddResponse(record_.url.spec(), kBody, net::HTTP_NOT_FOUND);
  Start();
  ExpectFailedAndEmpty();
}
TEST_F(PubkyUpdateLinuxStagerTest, EligibilityFailureDoesNotCreateDirectoryOrRequest) {
  SetEligibility(EligibilityReason::kLockHeld);
  Start();
  ExpectFailedAndEmpty();
  EXPECT_EQ(EligibilityReason::kLockHeld, result_->eligibility);
  EXPECT_EQ(0, factory_.NumPending());
}
TEST_F(PubkyUpdateLinuxStagerTest, CancelPreparationDownloadAndRetry) {
  stager_->Start(record_, base::BindLambdaForTesting([this](StageResult) {
    replied_ = true;
  }));
  stager_->Cancel();
  environment_.RunUntilIdle();
  EXPECT_FALSE(replied_);
  EXPECT_TRUE(base::IsDirectoryEmpty(user_data_.GetPath()));
  Start();
  ASSERT_TRUE(factory_.IsPending(record_.url.spec()));
  auto directory = Directory();
  stager_->Cancel();
  environment_.RunUntilIdle();
  EXPECT_FALSE(replied_);
  EXPECT_FALSE(base::PathExists(directory));
  EXPECT_FALSE(factory_.IsPending(record_.url.spec()));
  factory_.AddResponse(record_.url.spec(), kBody);
  Start();
  ASSERT_TRUE(result_->package);
}
TEST_F(PubkyUpdateLinuxStagerTest, TimeoutDeletesStaging) {
  Start();
  ASSERT_TRUE(factory_.IsPending(record_.url.spec()));
  environment_.FastForwardBy(base::Minutes(11));
  ExpectFailedAndEmpty();
}
TEST_F(PubkyUpdateLinuxStagerTest, RedirectHostSchemeAndCountAreBounded) {
  for (const char* url : {"https://evil.example/package.deb",
                          "http://release-assets.githubusercontent.com/package.deb",
                          "https://release-assets.githubusercontent.com.evil.example/package.deb",
                          "https://user@objects.githubusercontent.com/package.deb",
                          "https://objects.githubusercontent.com:444/package.deb"}) {
    net::RedirectInfo redirect;
    redirect.new_url = GURL(url);
    network::TestURLLoaderFactory::Redirects redirects;
    redirects.emplace_back(redirect, network::CreateURLResponseHead(net::HTTP_FOUND));
    factory_.AddResponse(record_.url, network::CreateURLResponseHead(net::HTTP_OK),
        kBody, network::URLLoaderCompletionStatus(net::OK), std::move(redirects));
    Start();
    ExpectFailedAndEmpty();
  }
  for (int count : {3, 4}) {
    network::TestURLLoaderFactory::Redirects redirects;
    for (int i = 0; i < count; ++i) {
      net::RedirectInfo redirect;
      redirect.new_url = GURL(i % 2 ? "https://objects.githubusercontent.com/package.deb"
                                   : "https://release-assets.githubusercontent.com/package.deb");
      redirects.emplace_back(redirect, network::CreateURLResponseHead(net::HTTP_FOUND));
    }
    factory_.AddResponse(record_.url, network::CreateURLResponseHead(net::HTTP_OK),
        kBody, network::URLLoaderCompletionStatus(net::OK), std::move(redirects));
    Start();
    if (count == 3) {
      ASSERT_TRUE(result_->package);
      result_.reset();
      environment_.RunUntilIdle();
    } else {
      ExpectFailedAndEmpty();
    }
  }
}
TEST_F(PubkyUpdateLinuxStagerTest, ControllerReadyOnlyAfterRealStagingAndEligibility) {
  TestingPrefServiceSimple prefs;
  Controller::RegisterLocalState(prefs.registry());
  Controller::TestBoundaries boundaries;
  std::ranges::copy(test::PublicKey(), boundaries.public_key.begin());
  boundaries.running = base::Version("156.0.8073.0");
  boundaries.installed = boundaries.running;
  boundaries.fetch = base::BindRepeating(
      [](std::string envelope, GURL, base::OnceCallback<void(std::string)> reply) {
        std::move(reply).Run(std::move(envelope));
      }, envelope_);
  stager_->BindTo(boundaries);
  auto controller = Controller::CreateForTesting(&prefs, std::move(boundaries));
  controller->Check();
  EXPECT_EQ(0, factory_.NumPending());
  controller->Confirm(*controller->GetStatus().FindString("id"));
  environment_.RunUntilIdle();
  EXPECT_EQ("downloading", *controller->GetStatus().FindString("state"));
  ASSERT_TRUE(factory_.SimulateResponseForPendingRequest(record_.url.spec(), kBody));
  environment_.RunUntilIdle();
  EXPECT_EQ("ready", *controller->GetStatus().FindString("state"));
  EXPECT_FALSE(controller->GetStatus().FindBool("canRestart").value());
  controller->Cancel(*controller->GetStatus().FindString("id"));
  environment_.RunUntilIdle();
  EXPECT_TRUE(base::IsDirectoryEmpty(user_data_.GetPath()));
  SetEligibility(EligibilityReason::kVersionMismatch);
  controller->Check();
  controller->Confirm(*controller->GetStatus().FindString("id"));
  environment_.RunUntilIdle();
  EXPECT_EQ("failed", *controller->GetStatus().FindString("state"));
  EXPECT_EQ(static_cast<int>(EligibilityReason::kVersionMismatch),
            controller->GetStatus().FindInt("eligibilityReason").value());
  EXPECT_EQ(0, factory_.NumPending());
  controller->Shutdown();
}
}  // namespace
}  // namespace pubky_update
