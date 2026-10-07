// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "chrome/installer/linux/coordinator/helper.h"

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "base/base64.h"
#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/files/scoped_temp_dir.h"
#include "base/posix/eintr_wrapper.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/version.h"
#include "chrome/common/pubky_update/record.h"
#include "crypto/secure_hash.h"

namespace {
constexpr char kStateDir[] = "/var/lib/pubky-chromium/updates";
constexpr char kKey[] = "RGzK2rB/P4diYxd70MkBDc4FDUO0JikBqrFaSWTVOfI=";

bool ProtectedDirectory(const char* path) {
  if (mkdir(path, 0700) != 0 && errno != EEXIST) return false;
  struct stat st = {};
  return lstat(path, &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == 0 &&
         !(st.st_mode & 077);
}

bool WriteAll(int fd, base::span<const uint8_t> bytes) {
  while (!bytes.empty()) {
    const ssize_t n = HANDLE_EINTR(write(fd, bytes.data(), bytes.size()));
    if (n <= 0) return false;
    bytes = bytes.subspan(n);
  }
  return true;
}

bool CopyBounded(int source, const std::string& destination, size_t max,
                 std::string* contents, std::string* digest) {
  base::ScopedFD output(open(destination.c_str(),
                             O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                             0600));
  if (!output.is_valid()) return false;
  struct stat input = {};
  if (fstat(source, &input) || input.st_size <= 0 ||
      static_cast<uint64_t>(input.st_size) > max) return false;
  auto hash = crypto::SecureHash::Create(crypto::SecureHash::SHA256);
  std::array<uint8_t, 64 * 1024> buffer;
  size_t total = 0;
  for (;;) {
    const ssize_t n = HANDLE_EINTR(read(source, buffer.data(), buffer.size()));
    if (n < 0 || static_cast<size_t>(n) > max - total) return false;
    if (n == 0) break;
    total += n;
    hash->Update(base::span(buffer).first(n));
    if (contents) contents->append(reinterpret_cast<const char*>(buffer.data()), n);
    if (!WriteAll(output.get(), base::span(buffer).first(n))) return false;
  }
  if (total != static_cast<uint64_t>(input.st_size) || fsync(output.get()))
    return false;
  std::array<uint8_t, 32> bytes;
  hash->Finish(bytes);
  if (digest) *digest = base::ToLowerASCII(base::HexEncode(bytes));
  return true;
}

// Fixed executable and arguments; the inherited environment is never passed on.
bool Run(const char* program, std::vector<std::string> args,
         std::string* output = nullptr) {
  int pipes[2];
  if (pipe2(pipes, O_CLOEXEC) != 0) return false;
  base::ScopedFD reader(pipes[0]), writer(pipes[1]);
  const pid_t pid = fork();
  if (pid < 0) return false;
  if (pid == 0) {
    if (dup2(writer.get(), STDOUT_FILENO) < 0) _exit(127);
    reader.reset();
    writer.reset();
    base::ScopedFD null(open("/dev/null", O_WRONLY | O_CLOEXEC));
    if (!null.is_valid() || dup2(null.get(), STDERR_FILENO) < 0) _exit(127);
    std::vector<char*> argv = {const_cast<char*>(program)};
    for (auto& arg : args) argv.push_back(arg.data());
    argv.push_back(nullptr);
    char* env[] = {const_cast<char*>("PATH=/usr/bin:/bin"),
                   const_cast<char*>("LC_ALL=C"),
                   const_cast<char*>("DEBIAN_FRONTEND=noninteractive"), nullptr};
    execve(program, argv.data(), env);
    _exit(127);
  }
  writer.reset();
  std::array<char, 4096> buffer;
  size_t total = 0;
  bool bounded = true;
  for (;;) {
    const ssize_t count = HANDLE_EINTR(read(reader.get(), buffer.data(), buffer.size()));
    if (count < 0) { bounded = false; break; }
    if (count == 0) break;
    total += count;
    if (total > 64 * 1024) { bounded = false; break; }
    if (output) output->append(buffer.data(), count);
  }
  if (!bounded) kill(pid, SIGKILL);
  int status = 0;
  if (HANDLE_EINTR(waitpid(pid, &status, 0)) != pid) return false;
  return bounded && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool Installed(std::string* version) {
  std::string state;
  if (!Run("/usr/bin/dpkg-query",
           {"-W", "-f=${Status}\t${Version}\t${Architecture}\n", "pubky-chromium"},
           &state)) return false;
  const size_t first = state.find('\t'), second = state.find('\t', first + 1);
  if (first == std::string::npos || second == std::string::npos ||
      state.substr(0, first) != "install ok installed" ||
      state.substr(second) != "\tamd64\n" ||
      state.find('\n') != state.size() - 1) return false;
  *version = state.substr(first + 1, second - first - 1);
  const auto suffix = version->rfind("-1");
  if (suffix != version->size() - 2 ||
      !base::Version(version->substr(0, suffix)).IsValid()) return false;
  struct stat executable = {};
  return stat("/opt/pubky-chromium/chrome", &executable) == 0 &&
         S_ISREG(executable.st_mode) && executable.st_uid == 0 &&
         !(executable.st_mode & 022);
}

bool SafeSimulation(const std::string& package) {
  std::string output;
  if (!Run("/usr/bin/apt-get",
           {"-s", "-o", "APT::Sandbox::User=root", "--no-download", "install", package},
           &output)) return false;
  bool found = false;
  size_t begin = 0;
  while (begin < output.size()) {
    const size_t end = output.find('\n', begin);
    const auto line = output.substr(begin, end - begin);
    if (line.starts_with("Remv ") || line.starts_with("Purg ") ||
        line.starts_with("Inst ") || line.starts_with("Conf ")) {
      if (!line.starts_with("Inst pubky-chromium ") &&
          !line.starts_with("Conf pubky-chromium ")) return false;
      if (line.starts_with("Inst ")) found = true;
    }
    if (end == std::string::npos) break;
    begin = end + 1;
  }
  return found;
}

int Install(std::string_view staging, uid_t uid) {
  if (!ProtectedDirectory("/var/lib/pubky-chromium") ||
      !ProtectedDirectory(kStateDir)) return 1;
  base::ScopedFD lock(open((std::string(kStateDir) + "/lock").c_str(),
                           O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600));
  struct stat lock_info = {};
  if (!lock.is_valid() || fstat(lock.get(), &lock_info) ||
      !S_ISREG(lock_info.st_mode) || lock_info.st_uid != 0 ||
      (lock_info.st_mode & 077) || flock(lock.get(), LOCK_EX | LOCK_NB)) return 2;

  base::ScopedFD envelope(pubky_update::OpenStagedFile(staging, "envelope.json", uid));
  base::ScopedFD package(pubky_update::OpenStagedFile(staging, "package.deb", uid));
  if (!envelope.is_valid() || !package.is_valid()) return 3;
  std::string name = std::string(kStateDir) + "/snapshot-XXXXXX";
  std::vector<char> pattern(name.begin(), name.end());
  pattern.push_back(0);
  if (!mkdtemp(pattern.data())) return 3;
  base::ScopedTempDir snapshot;
  if (!snapshot.Set(base::FilePath(pattern.data()))) return 3;
  const std::string root = snapshot.GetPath().value();
  std::string data, digest;
  if (!CopyBounded(envelope.get(), root + "/envelope.json",
                   pubky_update::kMaxEnvelopeBytes, &data, nullptr) ||
      !CopyBounded(package.get(), root + "/package.deb", 2147483647, nullptr,
                   &digest)) return 4;
  std::string installed;
  if (!Installed(&installed)) return 5;
  std::string key_bytes;
  if (!base::Base64Decode(kKey, &key_bytes) || key_bytes.size() != 32) return 5;
  base::ScopedFD floor(open((std::string(kStateDir) + "/floor").c_str(),
                            O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600));
  struct stat floor_info = {};
  if (!floor.is_valid() || fstat(floor.get(), &floor_info) ||
      !S_ISREG(floor_info.st_mode) || floor_info.st_uid != 0 ||
      (floor_info.st_mode & 077) || floor_info.st_size > 32) return 5;
  std::array<char, 33> previous = {};
  const ssize_t count = HANDLE_EINTR(read(floor.get(), previous.data(), 32));
  if (count < 0) return 5;
  const std::string floor_value(previous.data(), count);
  const base::Version floor_version(floor_value);
  if (!floor_value.empty() && !floor_version.IsValid()) return 5;
  const base::Version current(installed.substr(0, installed.size() - 2));
  auto record = pubky_update::VerifyRecord(
      data, base::as_byte_span(key_bytes), pubky_update::Target::kLinuxX64,
      current, current, floor_version, base::Time::Now());
  if (!record.record || record.record->size !=
                            static_cast<int>(base::GetFileSize(snapshot.GetPath().AppendASCII("package.deb")).value_or(-1)) ||
      record.record->sha256 != digest) return 6;
  std::string control_text;
  if (!Run("/usr/bin/dpkg-deb", {"-f", root + "/package.deb"}, &control_text))
    return 7;
  auto control = pubky_update::ParseControl(control_text);
  if (!control || control->version != record.record->native_version ||
      !SafeSimulation(root + "/package.deb")) return 7;
  // Remember an accepted signed version before a potentially partial dpkg run.
  const std::string accepted = record.record->version.GetString();
  if (ftruncate(floor.get(), 0) || lseek(floor.get(), 0, SEEK_SET) != 0 ||
      !WriteAll(floor.get(), base::as_byte_span(accepted)) || fsync(floor.get()))
    return 8;
  if (!Run("/usr/bin/dpkg", {"-i", root + "/package.deb"})) return 9;
  std::string updated;
  if (!Installed(&updated) || updated != control->version) return 10;
  return 0;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 2 || getuid() != 0 || geteuid() != 0) return 1;
  const char* caller = getenv("PKEXEC_UID");
  unsigned int uid = 0;
  if (!caller || !base::StringToUint(caller, &uid) || uid == 0 ||
      uid == static_cast<unsigned int>(-1)) return 1;
  struct passwd* account = getpwuid(uid);
  if (!account || account->pw_uid != uid || account->pw_shell[0] == '\0')
    return 1;
  return Install(argv[1], uid);
}
