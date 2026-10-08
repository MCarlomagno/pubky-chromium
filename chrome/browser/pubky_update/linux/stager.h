// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PUBKY_UPDATE_LINUX_STAGER_H_
#define CHROME_BROWSER_PUBKY_UPDATE_LINUX_STAGER_H_

#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "chrome/browser/pubky_update/controller.h"
#include "chrome/browser/pubky_update/linux/eligibility.h"

namespace pubky_update {
// Browser-sequence owner, one download at a time. The controller must be
// destroyed before this owner. No production registration exists in L1.
class LinuxStager {
 public:
  using EligibilityProbe = base::RepeatingCallback<EligibilityReason(int)>;
  LinuxStager(const base::FilePath& user_data_dir,
              base::Version running,
              scoped_refptr<network::SharedURLLoaderFactory> factory);
  ~LinuxStager();
  void BindForTesting(Controller::TestBoundaries& boundaries);
  void SetEligibilityProbeForTesting(EligibilityProbe probe);
  void Start(const Record& record, base::OnceCallback<void(StageResult)> reply);
  void Cancel();

 private:
  void OnPrepared(Record record, StageResult result);
  void OnDownloaded(base::FilePath path);
  void Finish(StageResult result);
  const base::FilePath user_data_dir_;
  scoped_refptr<network::SharedURLLoaderFactory> factory_;
  scoped_refptr<base::SequencedTaskRunner> worker_;
  EligibilityProbe probe_;
  base::OnceCallback<void(StageResult)> reply_;
  std::optional<StagedPackage> package_;
  std::unique_ptr<network::SimpleURLLoader> loader_;
  int redirects_ = 0;
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<LinuxStager> boundary_weak_factory_{this};
  base::WeakPtrFactory<LinuxStager> weak_factory_{this};
};
}  // namespace pubky_update
#endif  // CHROME_BROWSER_PUBKY_UPDATE_LINUX_STAGER_H_
