// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PUBKY_UPDATE_CONTROLLER_H_
#define CHROME_BROWSER_PUBKY_UPDATE_CONTROLLER_H_

#include <array>
#include <memory>
#include <optional>
#include <string>

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/values.h"
#include "chrome/browser/pubky_update/staged_package.h"
#include "chrome/common/pubky_update/record.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

class PrefService;
class PrefRegistrySimple;
namespace network {
class SimpleURLLoader;
}
namespace pubky_update {

enum class InstallOutcome { kReady, kDeclined, kFailed };

// UI-thread, process-wide owner. No production adapter/key is shipped yet.
class Controller {
 public:
  enum class RestartResult { kAborted, kFailed, kCommitted };

  explicit Controller(PrefService* local_state);
  ~Controller();
  static void RegisterLocalState(PrefRegistrySimple* registry);
  base::DictValue GetStatus() const;
  base::CallbackListSubscription Observe(base::RepeatingClosure callback);
  void Check();
  void Confirm(std::string_view id);
  void Cancel(std::string_view id);
  void Restart(std::string_view id);
  void Detach();
  void Shutdown();

  // Only native unit tests can supply these boundaries. No command-line,
  // preference or WebUI entry point can supply trust or execute code.
  struct TestBoundaries {
    Target target = Target::kLinuxX64;
    std::array<uint8_t, 32> public_key{};
    base::Version running;
    base::Version installed;
    scoped_refptr<network::SharedURLLoaderFactory> metadata_factory;
    base::RepeatingCallback<void(GURL, base::OnceCallback<void(std::string)>)>
        fetch;
    base::RepeatingCallback<void(const Record&,
                                base::OnceCallback<void(StageResult)>)>
        download_and_verify;
    // Windows installs after verification, before restart consent. Cancel is
    // not offered once this starts. The int is a native error code.
    base::RepeatingCallback<void(const Record&,
                                 base::OnceCallback<void(InstallOutcome, int)>)>
        install;
    base::RepeatingClosure cancel;
    // Reply only after disarming on abort/failure, or after the normal
    // app-terminating boundary commits the coordinator. EOF is not commitment.
    base::RepeatingCallback<void(StagedPackage&,
                                 base::OnceCallback<void(RestartResult)>)>
        restart;
  };
  static std::unique_ptr<Controller> CreateForTesting(
      PrefService* local_state, TestBoundaries boundaries);

 private:
  void OnRecord(std::string envelope);
  void FetchMetadata();
  void OnMetadata(std::optional<std::string> envelope);
  void OnVerified(StageResult result);
  void OnInstalled(InstallOutcome outcome, int error);
  void OnRestartResult(RestartResult result);
  void Notify();
  bool Matches(std::string_view id) const;
  std::string FloorPref() const;
  const raw_ptr<PrefService> local_state_;
  std::optional<TestBoundaries> boundaries_;
  std::string state_ = "unsupported";
  std::string id_;
  RecordError error_ = RecordError::kNone;
  int install_error_ = 0;
  std::optional<Record> offer_;
  std::optional<StagedPackage> staged_;
  EligibilityReason eligibility_ = EligibilityReason::kEligible;
  bool shutdown_ = false;
  int redirects_ = 0;
  std::unique_ptr<network::SimpleURLLoader> metadata_loader_;
  base::RepeatingClosureList observers_;
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<Controller> weak_factory_{this};
};
}  // namespace pubky_update
#endif  // CHROME_BROWSER_PUBKY_UPDATE_CONTROLLER_H_
