// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/pubky_update/linux/stager.h"

#include <array>
#include <fcntl.h>
#include <sys/stat.h>

#include "base/check_is_test.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/posix/eintr_wrapper.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/task/thread_pool.h"
#include "crypto/secure_hash.h"
#include "net/base/load_flags.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

namespace pubky_update {
namespace {
StageResult Prepare(const base::FilePath& user_data_dir,
                    const Record& record,
                    LinuxStager::EligibilityProbe probe,
                    scoped_refptr<base::SequencedTaskRunner> worker) {
  StageResult result;
  result.eligibility = probe.Run(record.size);
  if (result.eligibility != EligibilityReason::kEligible || record.size <= 0 ||
      record.authenticated_envelope.empty() ||
      record.authenticated_envelope.size() > kMaxEnvelopeBytes ||
      record.sha256.size() != 64 || base::Time::Now() >= record.expires_at) {
    return result;
  }
  auto directory = std::make_unique<base::ScopedTempDir>();
  // CreateNewTempDirectory uses system temp. This variant uses mkdtemp under
  // the browser's native User Data selection and never changes global TMPDIR.
  if (!directory->CreateUniqueTempDirUnderPath(user_data_dir, "pubky-update-") ||
      !base::SetPosixFilePermissions(directory->GetPath(), 0700)) {
    return result;
  }
  StagedPackage package;
  package.path = directory->GetPath().AppendASCII("package.deb");
  package.size = record.size;
  package.sha256 = record.sha256;
  package.envelope = record.authenticated_envelope;
  package.directory = StagedPackage::Directory(
      directory.release(), base::OnTaskRunnerDeleter(std::move(worker)));
  result.package = std::move(package);
  return result;
}

StageResult Authenticate(StagedPackage package) {
  base::File file(HANDLE_EINTR(open(package.path.value().c_str(),
                                  O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)));
  struct stat info = {};
  bool valid = file.IsValid() && fstat(file.GetPlatformFile(), &info) == 0 &&
               S_ISREG(info.st_mode) && info.st_size == package.size;
  auto hash = crypto::SecureHash::Create(crypto::SecureHash::SHA256);
  std::array<uint8_t, 64 * 1024> buffer;
  int64_t total = 0;
  while (valid) {
    auto count = file.ReadAtCurrentPos(buffer);
    if (!count || *count > static_cast<uint64_t>(package.size - total)) {
      valid = false;
      break;
    }
    if (*count == 0) {
      break;
    }
    total += *count;
    hash->Update(base::span(buffer).first(*count));
  }
  std::array<uint8_t, 32> digest;
  hash->Finish(digest);
  file.Close();
  valid = valid && total == package.size &&
          base::ToLowerASCII(base::HexEncode(digest)) == package.sha256;
  const auto envelope_path = package.path.DirName().AppendASCII("envelope.json");
  if (valid) {
    base::File envelope(envelope_path, base::File::FLAG_CREATE | base::File::FLAG_WRITE);
    valid = envelope.IsValid() &&
            envelope.WriteAtCurrentPosAndCheck(base::as_byte_span(package.envelope));
  }
  if (!valid) {
    // Delete before reporting a verification failure, not on a UI sequence.
    const bool deleted = package.directory->Delete();
    if (!deleted) {
      // ScopedTempDir will retry on destruction; never expose failed bytes.
      package.directory.reset();
    }
    return {};
  }
  StageResult result;
  result.package = std::move(package);
  return result;
}
}  // namespace

LinuxStager::LinuxStager(
    const base::FilePath& user_data_dir,
    base::Version running,
    scoped_refptr<network::SharedURLLoaderFactory> factory)
    : user_data_dir_(user_data_dir),
      factory_(std::move(factory)),
      worker_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN})),
      probe_(base::BindRepeating(&ProbeLinuxEligibility, std::move(running),
                                user_data_dir)) {}
LinuxStager::~LinuxStager() { Cancel(); }
void LinuxStager::BindForTesting(Controller::TestBoundaries& boundaries) {
  CHECK_IS_TEST();
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  boundaries.download_and_verify = base::BindRepeating(
      &LinuxStager::Start, boundary_weak_factory_.GetWeakPtr());
  boundaries.cancel = base::BindRepeating(&LinuxStager::Cancel,
                                         boundary_weak_factory_.GetWeakPtr());
}
void LinuxStager::SetEligibilityProbeForTesting(EligibilityProbe probe) {
  CHECK_IS_TEST();
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(!reply_);
  probe_ = std::move(probe);
}
void LinuxStager::Start(const Record& record,
                        base::OnceCallback<void(StageResult)> reply) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (reply_) {
    std::move(reply).Run({});
    return;
  }
  Cancel();
  reply_ = std::move(reply);
  if (!factory_) {
    Finish({});
    return;
  }
  worker_->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&Prepare, user_data_dir_, record, probe_, worker_),
      base::BindOnce(&LinuxStager::OnPrepared, weak_factory_.GetWeakPtr(), record));
}
void LinuxStager::OnPrepared(Record record, StageResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!result.package) {
    Finish(std::move(result));
    return;
  }
  package_ = std::move(result.package);
  constexpr auto annotation = net::DefineNetworkTrafficAnnotation(
      "pubky_explicit_update_package", R"(
        semantics {
          sender: "Pubky Chromium installed updater"
          description: "Downloads the exact package named in an authenticated release catalog."
          trigger: "Only explicit Download and install consent in About."
          data: "No account, profile or browser identifiers."
          user_data { type: NONE }
          destination: OTHER
          destination_other: "MCarlomagno/pubky-chromium GitHub Release and its allowed asset CDN."
          internal { contacts { owners: "chrome/browser/OWNERS" } }
          last_reviewed: "2026-10-06"
        }
        policy {
          cookies_allowed: NO
          setting: "No production adapter/key is registered until the complete Linux path is approved."
          policy_exception_justification: "Explicit user action; no background downloads."
        })");
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = record.url;
  request->method = "GET";
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->referrer_policy = net::ReferrerPolicy::NO_REFERRER;
  request->load_flags = net::LOAD_DISABLE_CACHE | net::LOAD_DO_NOT_SEND_AUTH_DATA;
  loader_ = network::SimpleURLLoader::Create(std::move(request), annotation);
  redirects_ = 0;
  loader_->SetTimeoutDuration(base::Minutes(10));
  loader_->SetOnRedirectCallback(base::BindRepeating(
      [](base::WeakPtr<LinuxStager> self, const GURL&,
         const net::RedirectInfo& redirect,
         const network::mojom::URLResponseHead&, std::vector<std::string>*) {
        if (self && (++self->redirects_ > 3 ||
                     !IsPackageRedirectAllowed(redirect.new_url))) {
          self->Finish({});
        }
      }, weak_factory_.GetWeakPtr()));
  // The loader enforces this while consuming the pipe, without retaining
  // overflow or partial results; its file I/O is already off-sequence.
  loader_->DownloadToFile(factory_.get(),
      base::BindOnce(&LinuxStager::OnDownloaded, weak_factory_.GetWeakPtr()),
      package_->path, record.size);
}
void LinuxStager::OnDownloaded(base::FilePath path) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  loader_.reset();
  if (path.empty() || path != package_->path) {
    Finish({});
    return;
  }
  auto package = std::move(*package_);
  package_.reset();
  worker_->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&Authenticate, std::move(package)),
      base::BindOnce(&LinuxStager::Finish, weak_factory_.GetWeakPtr()));
}
void LinuxStager::Finish(StageResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  loader_.reset();
  package_.reset();
  auto reply = std::move(reply_);
  std::move(reply).Run(std::move(result));
}
void LinuxStager::Cancel() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  weak_factory_.InvalidateWeakPtrs();
  loader_.reset();
  package_.reset();
  reply_.Reset();
}
}  // namespace pubky_update
