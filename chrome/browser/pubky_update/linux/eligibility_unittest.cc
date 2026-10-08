// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/pubky_update/linux/eligibility.h"

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pubky_update {
namespace {
constexpr char kStatus[] =
    "Package: unrelated\nStatus: install ok installed\nVersion: 1\n\n"
    "Package: pubky-chromium\nStatus: install ok installed\n"
    "Architecture: amd64\nVersion: 156.0.8073.0-1\n"
    "Description: test package\n Package: chromium-browser-stable\n\n"
    "Package: chromium-browser-stable\nStatus: install ok installed\n"
    "Version: 999\n";
LinuxEligibilityFacts EligibleFacts() {
  LinuxEligibilityFacts facts;
  facts.architecture = "x86_64";
  facts.executable = base::FilePath("/opt/pubky-chromium/chrome");
  facts.protected_executable = true;
  facts.status = kStatus;
  facts.free_bytes = kStagingSpaceMargin + 123;
  facts.locks_held = false;
  return facts;
}
EligibilityReason Evaluate(const LinuxEligibilityFacts& facts, int size = 123) {
  return EvaluateLinuxEligibility(facts, base::Version("156.0.8073.0"), size);
}
TEST(PubkyUpdateLinuxEligibilityTest, ExactInstalledIdentityAndSpaceBoundary) {
  EXPECT_EQ(EligibilityReason::kEligible, Evaluate(EligibleFacts()));
  auto facts = EligibleFacts();
  facts.free_bytes = kStagingSpaceMargin + 122;
  EXPECT_EQ(EligibilityReason::kLowSpace, Evaluate(facts));
  facts.free_bytes = std::nullopt;
  EXPECT_EQ(EligibilityReason::kSpaceUnavailable, Evaluate(facts));
  facts.free_bytes = -1;
  EXPECT_EQ(EligibilityReason::kSpaceUnavailable, Evaluate(facts));
  EXPECT_EQ(EligibilityReason::kInvalidSize, Evaluate(EligibleFacts(), 0));
  facts = EligibleFacts();
  facts.free_bytes = static_cast<int64_t>(2147483647) + kStagingSpaceMargin;
  EXPECT_EQ(EligibilityReason::kEligible, Evaluate(facts, 2147483647));
}
TEST(PubkyUpdateLinuxEligibilityTest, RejectsPortableForeignAndWritableExecutable) {
  for (const char* path : {"/home/user/src/chrome", "/opt/chromium.org/chromium/chrome",
                           "/opt/pubky-chromium-other/chrome",
                           "/opt/pubky-chromium/pubky-chromium", ""}) {
    auto facts = EligibleFacts();
    facts.executable = base::FilePath(path);
    EXPECT_EQ(EligibilityReason::kWrongExecutable, Evaluate(facts)) << path;
  }
  auto facts = EligibleFacts();
  // Models either user ownership, user write/ACL access or writable parents.
  facts.protected_executable = false;
  EXPECT_EQ(EligibilityReason::kUnprotectedExecutable, Evaluate(facts));
  facts = EligibleFacts();
  facts.architecture = "ARM_64";
  EXPECT_EQ(EligibilityReason::kWrongArchitecture, Evaluate(facts));
  facts.architecture = "x86";
  EXPECT_EQ(EligibilityReason::kWrongArchitecture, Evaluate(facts));
}
TEST(PubkyUpdateLinuxEligibilityTest, StatusMissingBrokenAndNativeVersionMismatch) {
  auto facts = EligibleFacts();
  facts.status.reset();
  EXPECT_EQ(EligibilityReason::kStatusUnavailable, Evaluate(facts));
  facts.status = "Package: chromium-browser-stable\nVersion: 156.0.8073.0-1\n";
  EXPECT_EQ(EligibilityReason::kPackageMissing, Evaluate(facts));
  facts.status = "";
  EXPECT_EQ(EligibilityReason::kPackageMissing, Evaluate(facts));
  for (const char* stanza : {
           "Package: pubky-chromium\nStatus: deinstall ok config-files\nArchitecture: amd64\nVersion: 156.0.8073.0-1\n",
           "Package: pubky-chromium\nStatus: install ok unpacked\nArchitecture: amd64\nVersion: 156.0.8073.0-1\n",
           "Package: pubky-chromium\nStatus: install ok installed\nArchitecture: arm64\nVersion: 156.0.8073.0-1\n",
           "Package: pubky-chromium\nStatus: install ok installed\nArchitecture: amd64\n",
           "Package: pubky-chromium\nStatus: install ok installed\nArchitecture: amd64\nVersion: 156.0.8073.0-1\nVersion: 156.0.8073.0-1\n"}) {
    facts.status = stanza;
    EXPECT_EQ(EligibilityReason::kPackageInvalid, Evaluate(facts)) << stanza;
  }
  facts.status = std::string(kStatus) + "\nPackage: pubky-chromium\n";
  EXPECT_EQ(EligibilityReason::kPackageInvalid, Evaluate(facts));
  for (const char* version : {"156.0.8073.1-1", "156.0.8073.0-2", "156.0.8073.0", "bad"}) {
    facts.status = std::string("Package: pubky-chromium\nStatus: install ok installed\nArchitecture: amd64\nVersion: ") + version + "\n";
    EXPECT_EQ(EligibilityReason::kVersionMismatch, Evaluate(facts)) << version;
  }
}
TEST(PubkyUpdateLinuxEligibilityTest, LockHintHeldUnknownAndReadOnlyFixture) {
  auto facts = EligibleFacts();
  facts.locks_held = true;
  EXPECT_EQ(EligibilityReason::kLockHeld, Evaluate(facts));
  facts.locks_held.reset();
  EXPECT_EQ(EligibilityReason::kLockUnavailable, Evaluate(facts));
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());
  auto path = temp.GetPath().AppendASCII("lock");
  ASSERT_TRUE(base::WriteFile(path, ""));
  ASSERT_TRUE(base::SetPosixFilePermissions(path, 0400));
  EXPECT_EQ(false, IsDpkgLockHeld(path));
  EXPECT_EQ(std::nullopt, IsDpkgLockHeld(temp.GetPath().AppendASCII("absent")));
  EXPECT_EQ(std::nullopt, IsDpkgLockHeld(temp.GetPath()));
  auto link = temp.GetPath().AppendASCII("link");
  ASSERT_TRUE(base::CreateSymbolicLink(path, link));
  EXPECT_EQ(std::nullopt, IsDpkgLockHeld(link));
}
}  // namespace
}  // namespace pubky_update
