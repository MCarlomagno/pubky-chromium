// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "chrome/installer/linux/coordinator/helper.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "base/files/scoped_file.h"

namespace pubky_update {

std::optional<Control> ParseControl(std::string_view text) {
  if (text.size() > 16 * 1024) {
    return std::nullopt;
  }
  Control control;
  bool package = false, architecture = false, version = false;
  while (!text.empty()) {
    const size_t end = text.find('\n');
    const auto line = text.substr(0, end);
    if (line.find('\r') != std::string_view::npos) {
      return std::nullopt;
    }
    if (line.empty() || line.front() == ' ' || line.front() == '\t') {
      if (end == std::string_view::npos) break;
      text.remove_prefix(end + 1);
      continue;
    }
    const size_t colon = line.find(": ");
    if (colon == std::string_view::npos) {
      return std::nullopt;
    }
    const auto key = line.substr(0, colon);
    const auto value = line.substr(colon + 2);
    if (key == "Package") {
      if (package) return std::nullopt;
      package = true;
      control.package = value;
    } else if (key == "Architecture") {
      if (architecture) return std::nullopt;
      architecture = true;
      control.architecture = value;
    } else if (key == "Version") {
      if (version) return std::nullopt;
      version = true;
      control.version = value;
    } else if (key == "Conflicts" || key == "Replaces" || key == "Breaks") {
      return std::nullopt;
    }
    if (end == std::string_view::npos) break;
    text.remove_prefix(end + 1);
  }
  if (!package || !architecture || !version ||
      control.package != "pubky-chromium" || control.architecture != "amd64" ||
      control.version.empty()) {
    return std::nullopt;
  }
  return control;
}

int OpenStagedFile(std::string_view directory, std::string_view name, uid_t uid) {
  if (!directory.starts_with('/') || directory.size() > 1023 ||
      directory.find('\0') != std::string_view::npos ||
      (name != "package.deb" && name != "envelope.json")) {
    return -1;
  }
  base::ScopedFD current(open("/", O_PATH | O_DIRECTORY | O_CLOEXEC));
  base::ScopedFD parent;
  size_t pos = 1;
  while (pos < directory.size()) {
    size_t end = directory.find('/', pos);
    if (end == std::string_view::npos) end = directory.size();
    const auto component = directory.substr(pos, end - pos);
    if (component.empty() || component == "." || component == "..") {
      return -1;
    }
    parent = std::move(current);
    current.reset(openat(parent.get(), std::string(component).c_str(),
                         O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (!current.is_valid()) return -1;
    if (end == directory.size()) {
      struct stat stage = {}, data = {};
      if (!component.starts_with("pubky-update-") ||
          fstat(current.get(), &stage) || fstat(parent.get(), &data) ||
          !S_ISDIR(stage.st_mode) || !S_ISDIR(data.st_mode) ||
          stage.st_uid != uid || data.st_uid != uid ||
          (stage.st_mode & 0777) != 0700) {
        return -1;
      }
      int fd = openat(current.get(), std::string(name).c_str(),
                      O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
      struct stat file = {};
      if (fd < 0) return -1;
      if (fstat(fd, &file) || !S_ISREG(file.st_mode) || file.st_uid != uid ||
          file.st_nlink != 1) {
        close(fd);
        return -1;
      }
      return fd;
    }
    pos = end + 1;
  }
  return -1;
}

}  // namespace pubky_update
