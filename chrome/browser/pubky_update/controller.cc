// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/pubky_update/controller.h"

#include "base/check_is_test.h"
#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/unguessable_token.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "net/base/load_flags.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

namespace pubky_update {
namespace {
constexpr char kLinuxFloor[] = "pubky_update.linux_x64.offered_floor";
constexpr char kWindowsFloor[] = "pubky_update.windows_x64.offered_floor";
}
Controller::Controller(PrefService* local_state) : local_state_(local_state) {}
Controller::~Controller() { Shutdown(); }
void Controller::RegisterLocalState(PrefRegistrySimple* registry) {
  registry->RegisterStringPref(kLinuxFloor, "");
  registry->RegisterStringPref(kWindowsFloor, "");
}
std::unique_ptr<Controller> Controller::CreateForTesting(
    PrefService* local_state, TestBoundaries boundaries) {
  CHECK_IS_TEST();
  auto controller = std::make_unique<Controller>(local_state);
  controller->boundaries_ = std::move(boundaries);
  controller->state_ = "idle";
  return controller;
}
std::string Controller::FloorPref() const {
  return boundaries_->target == Target::kLinuxX64 ? kLinuxFloor : kWindowsFloor;
}
base::DictValue Controller::GetStatus() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  base::DictValue status;
  status.Set("state", state_);
  status.Set("id", id_);
  status.Set("error", static_cast<int>(error_));
  status.Set("version", offer_ ? offer_->version.GetString() : "");
  status.Set("size", offer_ ? base::NumberToString(offer_->size) : "");
  status.Set("canCheck", !shutdown_ && boundaries_.has_value() &&
                            (state_ == "idle" || state_ == "failed" ||
                             state_ == "canceled" || state_ == "no_newer"));
  status.Set("canConfirm", !shutdown_ && state_ == "available" &&
                              boundaries_->download_and_verify);
  status.Set("canCancel", !shutdown_ &&
                             (state_ == "checking" || state_ == "available" ||
                              state_ == "downloading" || state_ == "ready"));
  status.Set("canRestart", !shutdown_ && state_ == "ready" && boundaries_->restart);
  return status;
}
base::CallbackListSubscription Controller::Observe(base::RepeatingClosure cb) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return observers_.Add(std::move(cb));
}
void Controller::Notify() { observers_.Notify(); }
bool Controller::Matches(std::string_view id) const {
  return !shutdown_ && !id_.empty() && id == id_;
}
void Controller::Check() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!GetStatus().FindBool("canCheck").value_or(false)) {
    return;
  }
  weak_factory_.InvalidateWeakPtrs();
  offer_.reset();
  error_ = RecordError::kNone;
  id_ = base::UnguessableToken::Create().ToString();
  state_ = "checking";
  // A listener may synchronously cancel; do not start work after that consent
  // has disappeared. The callback is invalidated on every cancel/new check.
  const auto live_check = weak_factory_.GetWeakPtr();
  Notify();
  if (live_check && state_ == "checking") {
    if (boundaries_->fetch) {
      boundaries_->fetch.Run(FeedUrl(boundaries_->target),
          base::BindOnce(&Controller::OnRecord, weak_factory_.GetWeakPtr()));
    } else {
      FetchMetadata();
    }
  }
}
void Controller::FetchMetadata() {
  if (!boundaries_->metadata_factory) {
    OnMetadata(std::nullopt);
    return;
  }
  constexpr auto annotation = net::DefineNetworkTrafficAnnotation(
      "pubky_explicit_update_metadata", R"(
        semantics {
          sender: "Pubky Chromium installed updater"
          description: "Fetches a signed, fixed-target release catalog. No package download."
          trigger: "Only an explicit Check for updates action in About."
          data: "No account, profile or browser identifiers."
          destination: OTHER
          destination_other: "The fixed MCarlomagno/pubky-chromium raw GitHub feed."
        }
        policy {
          cookies_allowed: NO
          setting: "Production activation is absent until a key and complete platform adapter are approved."
          policy_exception_justification: "Explicit user action; no background checks."
        })");
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = FeedUrl(boundaries_->target);
  request->method = "GET";
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->referrer_policy = net::ReferrerPolicy::NO_REFERRER;
  request->load_flags = net::LOAD_DISABLE_CACHE | net::LOAD_DO_NOT_SEND_AUTH_DATA;
  metadata_loader_ = network::SimpleURLLoader::Create(std::move(request), annotation);
  redirects_ = 0;
  metadata_loader_->SetTimeoutDuration(base::Seconds(30));
  metadata_loader_->SetOnRedirectCallback(base::BindRepeating(
      [](base::WeakPtr<Controller> self, const GURL&,
         const net::RedirectInfo& redirect,
         const network::mojom::URLResponseHead&,
         std::vector<std::string>*) {
        if (!self) {
          return;
        }
        if (++self->redirects_ > 3 ||
            !IsMetadataRedirectAllowed(self->boundaries_->target, redirect.new_url)) {
          self->metadata_loader_.reset();
          self->state_ = "failed";
          self->error_ = RecordError::kOrigin;
          self->Notify();
        }
      }, weak_factory_.GetWeakPtr()));
  metadata_loader_->DownloadToString(boundaries_->metadata_factory.get(),
      base::BindOnce(&Controller::OnMetadata, weak_factory_.GetWeakPtr()),
      kMaxEnvelopeBytes);
}
void Controller::OnMetadata(std::optional<std::string> envelope) {
  metadata_loader_.reset();
  OnRecord(envelope.value_or(""));
}
void Controller::OnRecord(std::string envelope) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (state_ != "checking" || shutdown_) {
    return;
  }
  const std::string floor = local_state_->GetString(FloorPref());
  // A corrupt persisted floor must not silently remove replay protection.
  if (!floor.empty() && !base::Version(floor).IsValid()) {
    error_ = RecordError::kReplay;
    state_ = "failed";
    Notify();
    return;
  }
  auto result = VerifyRecord(envelope, boundaries_->public_key,
                            boundaries_->target, boundaries_->running,
                            boundaries_->installed, base::Version(floor),
                            base::Time::Now());
  error_ = result.error;
  if (!result.record) {
    state_ = error_ == RecordError::kNoNewer ? "no_newer" : "failed";
  } else {
    offer_ = std::move(result.record);
    // Persist on authenticated offer, even when the user later declines it.
    local_state_->SetString(FloorPref(), offer_->version.GetString());
    state_ = "available";
  }
  Notify();
}
void Controller::Confirm(std::string_view id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!Matches(id) || !GetStatus().FindBool("canConfirm").value_or(false)) {
    return;
  }
  if (base::Time::Now() >= offer_->expires_at) {
    state_ = "failed";
    error_ = RecordError::kExpired;
    Notify();
    return;
  }
  state_ = "downloading";
  const auto live_offer = weak_factory_.GetWeakPtr();
  Notify();
  if (live_offer && state_ == "downloading") {
    boundaries_->download_and_verify.Run(*offer_,
        base::BindOnce(&Controller::OnVerified, weak_factory_.GetWeakPtr()));
  }
}
void Controller::OnVerified(bool success) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (state_ != "downloading" || shutdown_) {
    return;
  }
  state_ = success ? "ready" : "failed";
  Notify();
}
void Controller::Cancel(std::string_view id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!Matches(id) || !GetStatus().FindBool("canCancel").value_or(false)) {
    return;
  }
  weak_factory_.InvalidateWeakPtrs();
  state_ = "canceled";
  metadata_loader_.reset();
  id_.clear();
  offer_.reset();
  const auto live_cancel = weak_factory_.GetWeakPtr();
  boundaries_->cancel.Run();
  if (live_cancel) {
    Notify();
  }
}
void Controller::Restart(std::string_view id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!Matches(id) || !GetStatus().FindBool("canRestart").value_or(false)) {
    return;
  }
  if (base::Time::Now() >= offer_->expires_at) {
    weak_factory_.InvalidateWeakPtrs();
    id_.clear();
    offer_.reset();
    state_ = "failed";
    error_ = RecordError::kExpired;
    const auto live_expiry = weak_factory_.GetWeakPtr();
    boundaries_->cancel.Run();
    if (live_expiry) {
      Notify();
    }
    return;
  }
  // No production restart/install path exists in this milestone. A test mock
  // observes explicit commitment; never report installation success here.
  state_ = "committed";
  id_.clear();
  const auto live_transaction = weak_factory_.GetWeakPtr();
  Notify();
  if (live_transaction) {
    boundaries_->restart.Run();
  }
}
void Controller::Detach() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Another About tab may still own the unconfirmed offer.
  if (observers_.empty() && (state_ == "checking" || state_ == "available")) {
    Cancel(id_);
  }
}
void Controller::Shutdown() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutdown_) {
    return;
  }
  weak_factory_.InvalidateWeakPtrs();
  const bool cancel = boundaries_ && state_ != "committed";
  metadata_loader_.reset();
  shutdown_ = true;
  id_.clear();
  offer_.reset();
  state_ = "unsupported";
  // Finish local teardown before calling a boundary that may reenter us.
  if (cancel) {
    boundaries_->cancel.Run();
  }
}
}  // namespace pubky_update
