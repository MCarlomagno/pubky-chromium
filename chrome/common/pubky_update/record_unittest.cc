// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/pubky_update/record.h"

#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "chrome/common/pubky_update/test_record.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pubky_update {
namespace {
class PubkyUpdateRecordTest : public testing::Test {
 protected:
  const base::Time now_ = base::Time::Now();
  RecordResult Verify(std::string_view envelope, Target target = Target::kLinuxX64,
                      std::string_view running = "156.0.8073.0",
                      std::string_view installed = "156.0.8073.0",
                      std::string_view floor = "") {
    return VerifyRecord(envelope, test::PublicKey(), target,
                        base::Version(running), base::Version(installed),
                        base::Version(floor), now_);
  }
};
TEST_F(PubkyUpdateRecordTest, TargetsAndForkVersion) {
  for (auto target : {Target::kLinuxX64, Target::kWindowsX64}) {
    auto envelope = test::Envelope(test::Record(now_, target));
    ASSERT_TRUE(Verify(envelope, target).record);
    EXPECT_EQ(RecordError::kNoNewer,
              Verify(envelope, target, "156.0.8073.1").error);
    EXPECT_EQ(RecordError::kNoNewer,
              Verify(envelope, target, "156.0.8073.0", "156.0.8073.1").error);
    EXPECT_EQ(RecordError::kReplay,
              Verify(envelope, target, "156.0.8073.0", "156.0.8073.0", "156.0.8073.2").error);
    EXPECT_TRUE(Verify(envelope, target, "156.0.8073.0", "156.0.8073.0", "156.0.8073.1").record);
  }
}
TEST_F(PubkyUpdateRecordTest, SignatureDomainAndKey) {
  auto payload = *base::WriteJson(test::Record(now_));
  EXPECT_EQ(RecordError::kSignature, Verify(test::Envelope(payload, "")).error);
  auto envelope = test::Envelope(payload);
  auto wrong_key = test::PublicKey();
  wrong_key[0] ^= 1;
  EXPECT_EQ(RecordError::kSignature,
      VerifyRecord(envelope, wrong_key, Target::kLinuxX64,
          base::Version("156.0.8073.0"), base::Version("156.0.8073.0"),
          base::Version(), now_).error);
  payload[0] ^= 1;
  base::DictValue outer;
  outer.Set("format", "pubky-updates-v1");
  outer.Set("payload", base::Base64Encode(payload));
  outer.Set("signature", std::string(88, 'A'));
  EXPECT_FALSE(Verify(*base::WriteJson(outer)).record);
}
TEST_F(PubkyUpdateRecordTest, ClosedCanonicalBoundedSchema) {
  auto record = test::Record(now_);
  record.Set("command", "anything");
  EXPECT_EQ(RecordError::kSchema, Verify(test::Envelope(record)).error);
  record.Remove("command");
  record.Set("size", true);
  EXPECT_EQ(RecordError::kSchema, Verify(test::Envelope(record)).error);
  record.Set("size", 123);
  const auto payload = *base::WriteJson(record);
  EXPECT_EQ(RecordError::kSchema, Verify(test::Envelope(payload + "\n")).error);
  EXPECT_EQ(RecordError::kSchema,
            Verify(test::Envelope("{\"size\":123," + payload.substr(1))).error);
  EXPECT_EQ(RecordError::kSchema, Verify(std::string(kMaxEnvelopeBytes + 1, 'x')).error);
  EXPECT_FALSE(Verify(test::Envelope(std::string(kMaxPayloadBytes + 1, 'x'))).record);
  EXPECT_FALSE(Verify(test::Envelope(record).substr(0, 100)).record);
}
TEST_F(PubkyUpdateRecordTest, IdentityOriginsAndExpiry) {
  for (auto field : {"platform", "repository", "channel", "architecture", "package_id"}) {
    auto record = test::Record(now_);
    record.Set(field, "wrong");
    EXPECT_EQ(RecordError::kTarget, Verify(test::Envelope(record)).error);
  }
  for (auto url : {"https://evil.example/pubky.deb",
                  "https://github.com/MCarlomagno/pubky-chromium/releases/download/v156.0.8073.1/pubky.deb?q=1",
                  "https://github.com:443/MCarlomagno/pubky-chromium/releases/download/v156.0.8073.1/pubky.deb"}) {
    auto record = test::Record(now_);
    record.Set("url", url);
    EXPECT_EQ(RecordError::kOrigin, Verify(test::Envelope(record)).error);
  }
  for (auto expiry : {now_, now_ + base::Days(181)}) {
    auto record = test::Record(now_);
    record.Set("expires_at", test::Timestamp(expiry));
    EXPECT_EQ(RecordError::kExpired, Verify(test::Envelope(record)).error);
  }
  auto record = test::Record(now_);
  record.Set("issued_at", test::Timestamp(now_ + base::Minutes(6)));
  EXPECT_EQ(RecordError::kExpired, Verify(test::Envelope(record)).error);
  record = test::Record(now_);
  record.Set("native_version", "156.0.8073.2-1");
  EXPECT_EQ(RecordError::kVersion, Verify(test::Envelope(record)).error);
}
TEST_F(PubkyUpdateRecordTest, FixedFeedAndRedirectPolicy) {
  EXPECT_NE(FeedUrl(Target::kLinuxX64), FeedUrl(Target::kWindowsX64));
  EXPECT_TRUE(IsMetadataRedirectAllowed(Target::kLinuxX64, FeedUrl(Target::kLinuxX64)));
  EXPECT_FALSE(IsMetadataRedirectAllowed(Target::kLinuxX64, FeedUrl(Target::kWindowsX64)));
  EXPECT_TRUE(IsPackageRedirectAllowed(GURL("https://release-assets.githubusercontent.com/path?token=public")));
  for (auto url : {"http://release-assets.githubusercontent.com/path", "https://github.com/path",
                  "https://evil.release-assets.githubusercontent.com/path",
                  "https://user@objects.githubusercontent.com/path", "https://objects.githubusercontent.com:8443/path"}) {
    EXPECT_FALSE(IsPackageRedirectAllowed(GURL(url)));
  }
}
TEST_F(PubkyUpdateRecordTest, PublicOpenSslFixture) {
  base::FilePath root;
  ASSERT_TRUE(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
  std::string envelope;
  ASSERT_TRUE(base::ReadFileToString(root.AppendASCII("pubky/update/testdata/linux-x64.json"), &envelope));
  base::Time time;
  ASSERT_TRUE(base::Time::FromUTCString("2026-10-05T18:00:00Z", &time));
  auto result = VerifyRecord(envelope, test::PublicKey(), Target::kLinuxX64,
      base::Version("156.0.8073.0"), base::Version("156.0.8073.0"), base::Version(), time);
  ASSERT_TRUE(result.record);
  EXPECT_EQ("156.0.8073.1", result.record->version.GetString());
}
}  // namespace
}  // namespace pubky_update
