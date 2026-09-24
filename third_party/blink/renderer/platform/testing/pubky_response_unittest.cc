// Copyright 2026 The Pubky Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "base/test/task_environment.h"
#include "net/ssl/ssl_info.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/platform/web_url.h"
#include "third_party/blink/public/platform/web_url_response.h"
#include "third_party/blink/renderer/platform/loader/fetch/resource_response.h"
#include "third_party/blink/renderer/platform/weborigin/kurl.h"
#include "url/url_util.h"

namespace blink {

namespace {
// The small combined test runner does not run ComponentsTestSuite, which
// normally registers these standard browser schemes for the omnibox tests.
class PubkySchemeEnvironment : public testing::Environment {
 public:
  void SetUp() override {
    schemes_ = std::make_unique<url::ScopedSchemeRegistryForTests>();
    url::AddStandardScheme("chrome-search", url::SCHEME_WITH_HOST);
    url::AddStandardScheme("devtools", url::SCHEME_WITH_HOST);
  }
  void TearDown() override { schemes_.reset(); }

 private:
  std::unique_ptr<url::ScopedSchemeRegistryForTests> schemes_;
};

[[maybe_unused]] testing::Environment* const kSchemeEnvironment =
    testing::AddGlobalTestEnvironment(new PubkySchemeEnvironment);
}  // namespace

TEST(PubkyResponseTest, RawKeyResponseDoesNotRequireX509) {
  // Response metadata does not need a JavaScript isolate.
  base::test::TaskEnvironment task_environment;
  auto head = network::mojom::URLResponseHead::New();
  head->ssl_info.emplace();
  head->ssl_info->verified_raw_public_key.assign(32, 0x42);
  auto response = WebURLResponse::Create(
      WebURL(KURL("https://4msqbgpkfcdgnzrrsyp5hgno8rfa4sx15c79ughsq95ikycunowy/")),
      *head, /*report_security_info=*/true, /*request_id=*/1);
  EXPECT_EQ(response.ToResourceResponse().GetSecurityStyle(),
            SecurityStyle::kSecure);
  const auto& info = response.ToResourceResponse().GetSSLInfo();
  ASSERT_TRUE(info);
  EXPECT_FALSE(info->cert);
  EXPECT_EQ(info->verified_raw_public_key,
            head->ssl_info->verified_raw_public_key);
}

}  // namespace blink
