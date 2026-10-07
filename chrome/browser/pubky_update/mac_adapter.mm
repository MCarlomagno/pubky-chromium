// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/pubky_update/mac_adapter.h"

#import <AppKit/AppKit.h>
#import <Sparkle/Sparkle.h>

#include <string>

#include "base/strings/sys_string_conversions.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "base/unguessable_token.h"
#include "base/version.h"
#include "build/build_config.h"
#include "chrome/browser/pubky_update/mac_feed_policy.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"

namespace pubky_update {
namespace {
constexpr char kFloorPref[] = "pubky_update.macos_arm64.offered_floor";
constexpr char kFeedUrl[] = "https://raw.githubusercontent.com/MCarlomagno/"
                            "pubky-chromium/update-manifests-v1/experimental/macos-arm64.xml";

base::Time ParseDate(NSString* value) {
  if (![value isKindOfClass:NSString.class] || value.length != 20 ||
      ![value hasSuffix:@"Z"]) {
    return {};
  }
  NSDateFormatter* formatter = [[NSDateFormatter alloc] init];
  formatter.locale = [NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"];
  formatter.timeZone = [NSTimeZone timeZoneForSecondsFromGMT:0];
  formatter.lenient = NO;
  formatter.dateFormat = @"yyyy-MM-dd'T'HH:mm:ss'Z'";
  NSDate* date = [formatter dateFromString:value];
  if (!date || ![[formatter stringFromDate:date] isEqualToString:value]) {
    return {};
  }
  return base::Time::FromSecondsSinceUnixEpoch(date.timeIntervalSince1970);
}

bool EligibleBundle() {
#if !defined(ARCH_CPU_ARM64)
  return false;
#endif
  NSBundle* bundle = NSBundle.mainBundle;
  NSURL* url = bundle.bundleURL;
  if (![bundle.bundleIdentifier isEqualToString:@"org.chromium.Chromium"] ||
      ![url.pathExtension isEqualToString:@"app"] ||
      [url.path containsString:@"/AppTranslocation/"] ||
      ![[NSFileManager defaultManager] isWritableFileAtPath:url.URLByDeletingLastPathComponent.path]) {
    return false;
  }
  NSNumber* read_only = nil;
  return [url getResourceValue:&read_only forKey:NSURLVolumeIsReadOnlyKey error:nil] &&
         !read_only.boolValue;
}

bool ValidItem(SUAppcastItem* item, std::string_view running,
               std::string_view floor, base::Time now) {
  const base::Version minimum(
      base::SysNSStringToUTF8(item.minimumSystemVersion));
  const base::Version display(
      base::SysNSStringToUTF8(item.displayVersionString));
  const base::Version native(base::SysNSStringToUTF8(item.versionString));
  if (item.signingValidationStatus != SPUAppcastSigningValidationStatusSucceeded ||
      item.informationOnlyUpdate || item.criticalUpdate ||
      item.deltaUpdate || item.deltaUpdates.count || item.releaseNotesURL ||
      item.fullReleaseNotesURL || item.infoURL || item.itemDescription ||
      ![item.installationType isEqualToString:@"application"] ||
      ![item.hardwareRequirements isEqualToSet:[NSSet setWithObject:@"arm64"]] ||
      !item.arm64HardwareRequirementIsOK || !item.minimumOperatingSystemVersionIsOK ||
      !minimum.IsValid() || minimum < base::Version("12.0") ||
      !display.IsValid() || display.components().size() != 4 ||
      !native.IsValid() || native.components().size() != 2 ||
      display.components()[2] != native.components()[0] ||
      display.components()[3] != native.components()[1] || !item.fileURL) {
    return false;
  }
  NSDictionary* fields = item.propertiesDictionary;
  id enclosure = fields[@"enclosure"];
  if (![enclosure isKindOfClass:NSDictionary.class] ||
      ![enclosure[@"sparkle:edSignature"] isKindOfClass:NSString.class] ||
      ![fields[@"pubky:architecture"] isKindOfClass:NSString.class] ||
      ![fields[@"pubky:repository"] isKindOfClass:NSString.class] ||
      ![fields[@"pubky:platform"] isEqualToString:@"macos"] ||
      ![fields[@"pubky:product"] isEqualToString:@"Pubky Chromium"] ||
      ![fields[@"pubky:channel"] isEqualToString:@"experimental"]) {
    return false;
  }
  return AcceptMacFeedItem(base::SysNSStringToUTF8(item.versionString), running,
                           floor, base::SysNSStringToUTF8(item.channel),
                           base::SysNSStringToUTF8(fields[@"pubky:architecture"]),
                           base::SysNSStringToUTF8(fields[@"pubky:repository"]),
                           GURL(base::SysNSStringToUTF8(item.fileURL.absoluteString)),
                           item.contentLength, ParseDate(fields[@"pubky:issued_at"]),
                           ParseDate(fields[@"pubky:expires_at"]), now);
}
}  // namespace

}  // namespace pubky_update

@interface PubkySparkleDriver : NSObject <SPUUserDriver, SPUUpdaterDelegate>
@property(nonatomic, assign) pubky_update::MacAdapter::Impl* owner;
@end

namespace pubky_update {
class MacAdapter::Impl {
 public:
  Impl(PrefService* prefs, base::RepeatingClosure changed)
      : prefs_(prefs), changed_(std::move(changed)) {
    if (EligibleBundle()) {
      state_ = "idle";
    }
  }
  ~Impl() { Shutdown(); }

  base::DictValue Status() const {
    base::DictValue status;
    status.Set("state", state_);
    status.Set("id", id_);
    status.Set("version", version_);
    status.Set("size", base::NumberToString(size_));
    status.Set("error", 0);
    status.Set("canCheck", (!updater_ || !updater_.sessionInProgress) &&
                               (state_ == "idle" || state_ == "failed" ||
                                state_ == "canceled" || state_ == "no_newer"));
    status.Set("canConfirm", state_ == "available");
    status.Set("canCancel", state_ == "checking" || state_ == "available" ||
                                state_ == "downloading" || state_ == "ready" ||
                                state_ == "verifying");
    status.Set("canRestart", state_ == "ready");
    return status;
  }
  void SetState(std::string state) {
    state_ = std::move(state);
    changed_.Run();
  }
  void Check() {
    if (!Status().FindBool("canCheck").value_or(false)) {
      return;
    }
    NSString* native_version = NSBundle.mainBundle.infoDictionary[@"CFBundleVersion"];
    if (![native_version isKindOfClass:NSString.class] || !EligibleBundle()) {
      SetState("unsupported");
      return;
    }
    running_ = base::SysNSStringToUTF8(native_version);
    if (!updater_) {
      driver_ = [[PubkySparkleDriver alloc] init];
      driver_.owner = this;
      updater_ = [[SPUUpdater alloc] initWithHostBundle:NSBundle.mainBundle
                                      applicationBundle:NSBundle.mainBundle
                                             userDriver:driver_ delegate:driver_];
      updater_.automaticallyChecksForUpdates = NO;
      updater_.automaticallyDownloadsUpdates = NO;
      updater_.sendsSystemProfile = NO;
      NSError* error = nil;
      if (![updater_ startUpdater:&error]) {
        updater_ = nil;
        driver_.owner = nullptr;
        driver_ = nil;
        SetState("failed");
        return;
      }
    }
    id_ = base::UnguessableToken::Create().ToString();
    version_.clear();
    size_ = 0;
    feed_valid_ = false;
    SetState("checking");
    [updater_ checkForUpdates];
  }
  void Confirm(std::string_view id) {
    if (state_ == "available" && id == id_ && found_reply_) {
      auto reply = found_reply_;
      found_reply_ = nil;
      SetState("downloading");
      reply(SPUUserUpdateChoiceInstall);
    }
  }
  void Cancel(std::string_view id) {
    if (id != id_ || !Status().FindBool("canCancel").value_or(false)) {
      return;
    }
    id_.clear();
    version_.clear();
    SetState("canceled");
    if (found_reply_) {
      auto reply = found_reply_;
      found_reply_ = nil;
      reply(SPUUserUpdateChoiceSkip);
    } else if (ready_reply_) {
      auto reply = ready_reply_;
      ready_reply_ = nil;
      reply(SPUUserUpdateChoiceSkip);
    } else if (cancel_download_) {
      auto cancel = cancel_download_;
      cancel_download_ = nil;
      cancel();
    } else if (cancel_check_) {
      auto cancel = cancel_check_;
      cancel_check_ = nil;
      cancel();
    } else {
      cancel_when_ready_ = true;
    }
  }
  void Restart(std::string_view id) {
    if (state_ != "ready" || id != id_ || !ready_reply_ ||
        base::Time::Now() >= expires_) {
      if (state_ == "ready" && base::Time::Now() >= expires_) {
        Cancel(id);
      }
      return;
    }
    id_.clear();
    auto reply = ready_reply_;
    ready_reply_ = nil;
    committed_ = true;
    SetState("restarting");
    reply(SPUUserUpdateChoiceInstall);
  }
  void Detach() {
    if (state_ == "checking" || state_ == "available" ||
        state_ == "ready" || state_ == "verifying") {
      Cancel(id_);
    }
  }
  bool MayQuit() {
    if (committed_ || !updater_ || !updater_.sessionInProgress) {
      return true;
    }
    quit_pending_ = true;
    if (Status().FindBool("canCancel").value_or(false)) {
      Cancel(id_);
    }
    return false;
  }
  void Shutdown() {
    if (driver_) {
      driver_.owner = nullptr;
    }
    if (committed_) {
      return;
    }
    quit_pending_ = false;
    if (Status().FindBool("canCancel").value_or(false)) {
      Cancel(id_);
    }
  }
  void CycleFinished() {
    cancel_check_ = nil;
    cancel_download_ = nil;
    if (found_reply_) {
      auto reply = found_reply_;
      found_reply_ = nil;
      reply(SPUUserUpdateChoiceSkip);
    }
    if (ready_reply_) {
      auto reply = ready_reply_;
      ready_reply_ = nil;
      reply(SPUUserUpdateChoiceSkip);
    }
    cancel_when_ready_ = false;
    if (state_ == "checking" || state_ == "available" ||
        state_ == "downloading" || state_ == "verifying") {
      SetState("failed");
    } else {
      changed_.Run();
    }
    if (quit_pending_) {
      quit_pending_ = false;
      [NSApp terminate:nil];
    }
  }

  PrefService* prefs_;
  base::RepeatingClosure changed_;
  __strong PubkySparkleDriver* driver_ = nil;
  __strong SPUUpdater* updater_ = nil;
  void (^found_reply_)(SPUUserUpdateChoice) = nil;
  void (^ready_reply_)(SPUUserUpdateChoice) = nil;
  void (^cancel_check_)(void) = nil;
  void (^cancel_download_)(void) = nil;
  std::string state_ = "unsupported";
  std::string id_;
  std::string version_;
  std::string running_;
  uint64_t size_ = 0;
  base::Time expires_;
  bool feed_valid_ = false;
  bool cancel_when_ready_ = false;
  bool quit_pending_ = false;
  bool committed_ = false;
};

void MacAdapter::RegisterLocalState(PrefRegistrySimple* registry) {
  registry->RegisterStringPref(kFloorPref, "");
}
MacAdapter::MacAdapter(PrefService* state, base::RepeatingClosure changed)
    : impl_(std::make_unique<Impl>(state, std::move(changed))) {}
MacAdapter::~MacAdapter() = default;
base::DictValue MacAdapter::GetStatus() const { return impl_->Status(); }
void MacAdapter::Check() { impl_->Check(); }
void MacAdapter::Confirm(std::string_view id) { impl_->Confirm(id); }
void MacAdapter::Cancel(std::string_view id) { impl_->Cancel(id); }
void MacAdapter::Restart(std::string_view id) { impl_->Restart(id); }
void MacAdapter::Detach() { impl_->Detach(); }
void MacAdapter::Shutdown() { impl_->Shutdown(); }
bool MacAdapter::MayQuit() { return impl_->MayQuit(); }
}  // namespace pubky_update

@implementation PubkySparkleDriver
- (NSString*)feedURLStringForUpdater:(SPUUpdater*)updater {
  return [NSString stringWithUTF8String:pubky_update::kFeedUrl];
}
- (NSSet<NSString*>*)allowedChannelsForUpdater:(SPUUpdater*)updater {
  return [NSSet setWithObject:@"experimental"];
}
- (BOOL)updater:(SPUUpdater*)updater
    mayPerformUpdateCheck:(SPUUpdateCheck)check
                   error:(NSError* __autoreleasing*)error {
  if (check == SPUUpdateCheckUpdates && self.owner &&
      self.owner->state_ == "checking") {
    return YES;
  }
  if (error) {
    *error = [NSError errorWithDomain:@"PubkyUpdater" code:1
                            userInfo:@{NSLocalizedDescriptionKey : @"No active update check"}];
  }
  return NO;
}
- (NSArray<NSString*>*)allowedSystemProfileKeysForUpdater:(SPUUpdater*)updater {
  return @[];
}
- (NSArray*)feedParametersForUpdater:(SPUUpdater*)updater
                 sendingSystemProfile:(BOOL)sendingProfile {
  return @[];
}
- (void)updater:(SPUUpdater*)updater didFinishLoadingAppcast:(SUAppcast*)appcast {
  auto* owner = self.owner;
  if (!owner || owner->state_ != "checking") {
    return;
  }
  owner->feed_valid_ = appcast.signingValidationStatus ==
                       SPUAppcastSigningValidationStatusSucceeded &&
                       appcast.items.count == 1;
  if (owner->feed_valid_) {
    SUAppcastItem* item = appcast.items.firstObject;
    owner->feed_valid_ = pubky_update::ValidItem(
        item, "0.0", owner->prefs_->GetString(pubky_update::kFloorPref),
        base::Time::Now());
  }
}
- (BOOL)updater:(SPUUpdater*)updater
    shouldProceedWithUpdate:(SUAppcastItem*)item
                updateCheck:(SPUUpdateCheck)check
                      error:(NSError* __autoreleasing*)error {
  auto* owner = self.owner;
  if (!owner || check != SPUUpdateCheckUpdates || owner->state_ != "checking" ||
      !owner->feed_valid_ ||
      !pubky_update::ValidItem(item, owner->running_,
                               owner->prefs_->GetString(pubky_update::kFloorPref),
                               base::Time::Now())) {
    if (error) {
      *error = [NSError errorWithDomain:@"PubkyUpdater" code:1
                              userInfo:@{NSLocalizedDescriptionKey : @"Update feed rejected"}];
    }
    return NO;
  }
  owner->version_ = base::SysNSStringToUTF8(item.displayVersionString);
  owner->size_ = item.contentLength;
  owner->expires_ = pubky_update::ParseDate(item.propertiesDictionary[@"pubky:expires_at"]);
  owner->prefs_->SetString(pubky_update::kFloorPref,
                           base::SysNSStringToUTF8(item.versionString));
  return YES;
}
- (void)updater:(SPUUpdater*)updater
    willDownloadUpdate:(SUAppcastItem*)item
            withRequest:(NSMutableURLRequest*)request {
  request.HTTPShouldHandleCookies = NO;
  [request setValue:nil forHTTPHeaderField:@"Authorization"];
  [request setValue:nil forHTTPHeaderField:@"Referer"];
}
- (void)showUpdatePermissionRequest:(SPUUpdatePermissionRequest*)request
                              reply:(void (^)(SUUpdatePermissionResponse*))reply {
  reply([[SUUpdatePermissionResponse alloc] initWithAutomaticUpdateChecks:NO
                                                sendSystemProfile:NO]);
}
- (void)showUserInitiatedUpdateCheckWithCancellation:(void (^)(void))cancellation {
  if (self.owner) {
    self.owner->cancel_check_ = [cancellation copy];
    if (self.owner->cancel_when_ready_) {
      self.owner->cancel_when_ready_ = false;
      self.owner->cancel_check_();
    }
  } else {
    cancellation();
  }
}
- (void)showUpdateFoundWithAppcastItem:(SUAppcastItem*)item
                                 state:(SPUUserUpdateState*)state
                                 reply:(void (^)(SPUUserUpdateChoice))reply {
  auto* owner = self.owner;
  if (!owner || owner->cancel_when_ready_ || owner->state_ != "checking" ||
      state.stage != SPUUserUpdateStageNotDownloaded || !state.userInitiated ||
      !owner->feed_valid_) {
    reply(SPUUserUpdateChoiceSkip);
    return;
  }
  owner->cancel_check_ = nil;
  owner->found_reply_ = [reply copy];
  owner->SetState("available");
}
- (void)showUpdateReleaseNotesWithDownloadData:(SPUDownloadData*)data {}
- (void)showUpdateReleaseNotesFailedToDownloadWithError:(NSError*)error {}
- (void)showUpdateNotFoundWithError:(NSError*)error
                   acknowledgement:(void (^)(void))acknowledgement {
  if (self.owner && self.owner->state_ == "checking") {
    self.owner->SetState(self.owner->feed_valid_ ? "no_newer" : "failed");
  }
  acknowledgement();
}
- (void)showUpdaterError:(NSError*)error
         acknowledgement:(void (^)(void))acknowledgement {
  if (self.owner && self.owner->state_ != "canceled") {
    self.owner->SetState("failed");
  }
  acknowledgement();
}
- (void)showDownloadInitiatedWithCancellation:(void (^)(void))cancellation {
  if (self.owner) {
    self.owner->cancel_download_ = [cancellation copy];
    if (self.owner->cancel_when_ready_) {
      self.owner->cancel_when_ready_ = false;
      self.owner->cancel_download_();
    }
  } else {
    cancellation();
  }
}
- (void)showDownloadDidReceiveExpectedContentLength:(uint64_t)length {}
- (void)showDownloadDidReceiveDataOfLength:(uint64_t)length {}
- (void)showDownloadDidStartExtractingUpdate {
  if (self.owner) {
    self.owner->cancel_download_ = nil;
    self.owner->SetState("downloading");
  }
}
- (void)showExtractionReceivedProgress:(double)progress {}
- (void)showReadyToInstallAndRelaunch:(void (^)(SPUUserUpdateChoice))reply {
  auto* owner = self.owner;
  if (!owner || owner->cancel_when_ready_ || owner->quit_pending_ ||
      owner->state_ == "canceled") {
    reply(SPUUserUpdateChoiceSkip);
    return;
  }
  owner->ready_reply_ = [reply copy];
  owner->SetState("ready");
}
- (void)showInstallingUpdateWithApplicationTerminated:(BOOL)terminated
                          retryTerminatingApplication:(void (^)(void))retry {
  if (self.owner) {
    self.owner->SetState("restarting");
  }
}
- (void)showUpdateInstalledAndRelaunched:(BOOL)relaunched
                         acknowledgement:(void (^)(void))acknowledgement {
  acknowledgement();
}
- (void)dismissUpdateInstallation {
  if (self.owner) {
    self.owner->CycleFinished();
  }
}
- (void)updater:(SPUUpdater*)updater
    didFinishUpdateCycleForUpdateCheck:(SPUUpdateCheck)check
                                 error:(NSError*)error {
  if (self.owner) {
    self.owner->CycleFinished();
  }
}
@end
