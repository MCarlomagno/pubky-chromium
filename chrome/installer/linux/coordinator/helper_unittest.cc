// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "chrome/installer/linux/coordinator/helper.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pubky_update {
namespace {
TEST(PubkyUpdateHelperTest, ControlMustMatchSignedTarget) {
  const char control[] = "Package: pubky-chromium\nArchitecture: amd64\n"
                         "Version: 156.0.8073.1-1\nPre-Depends: dpkg\n"
                         "Description: test\n continuation\n";
  auto parsed = ParseControl(control);
  ASSERT_TRUE(parsed);
  EXPECT_EQ(parsed->version, "156.0.8073.1-1");
  EXPECT_FALSE(ParseControl(std::string(control) + "Replaces: chromium-browser\n"));
  EXPECT_FALSE(ParseControl(std::string(control) + "Conflicts: chrome\n"));
  EXPECT_FALSE(ParseControl(std::string(control) + "Version: 999-1\n"));
  EXPECT_FALSE(ParseControl(std::string(control) + "Breaks: chromium\n"));
  EXPECT_FALSE(ParseControl(std::string(control) + "Architecture: arm64\n"));
  EXPECT_FALSE(ParseControl("Package: other\nArchitecture: amd64\nVersion: 1\n"));
  EXPECT_FALSE(ParseControl("Package: pubky-chromium\nArchitecture: arm64\nVersion: 1\n"));
}

TEST(PubkyUpdateHelperTest, StagingRejectsLinksAndNonRegularInputs) {
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());
  auto stage = temp.GetPath().AppendASCII("pubky-update-test");
  ASSERT_TRUE(base::CreateDirectory(stage));
  ASSERT_EQ(chmod(stage.value().c_str(), 0700), 0);
  const auto file = stage.AppendASCII("package.deb");
  ASSERT_TRUE(base::WriteFile(file, "data"));
  int fd = OpenStagedFile(stage.value(), "package.deb", getuid());
  ASSERT_GE(fd, 0);
  close(fd);
  EXPECT_EQ(OpenStagedFile(stage.value(), "package.deb", getuid() + 1), -1);
  EXPECT_EQ(OpenStagedFile(stage.value() + "/..", "package.deb", getuid()), -1);
  EXPECT_EQ(OpenStagedFile(stage.value() + "/.", "package.deb", getuid()), -1);
  EXPECT_EQ(OpenStagedFile(stage.value(), "other", getuid()), -1);
  ASSERT_TRUE(base::DeleteFile(file));
  ASSERT_TRUE(base::CreateSymbolicLink(temp.GetPath(), file));
  EXPECT_EQ(OpenStagedFile(stage.value(), "package.deb", getuid()), -1);
  ASSERT_TRUE(base::DeleteFile(file));
  ASSERT_EQ(mkfifo(file.value().c_str(), 0600), 0);
  EXPECT_EQ(OpenStagedFile(stage.value(), "package.deb", getuid()), -1);
}
}  // namespace
}  // namespace pubky_update
