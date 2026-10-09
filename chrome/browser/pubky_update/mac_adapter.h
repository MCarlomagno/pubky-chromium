// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PUBKY_UPDATE_MAC_ADAPTER_H_
#define CHROME_BROWSER_PUBKY_UPDATE_MAC_ADAPTER_H_

#include <memory>
#include <string_view>

#include "base/functional/callback.h"
#include "base/values.h"

class PrefRegistrySimple;
class PrefService;

namespace pubky_update {

class MacAdapter {
 public:
  class Impl;
  MacAdapter(PrefService* local_state, base::RepeatingClosure changed);
  ~MacAdapter();
  static void RegisterLocalState(PrefRegistrySimple* registry);
  base::DictValue GetStatus() const;
  void Check();
  void Confirm(std::string_view id);
  void Cancel(std::string_view id);
  void Restart(std::string_view id);
  void Detach();
  void Shutdown();
  // Called before Chromium begins normal quit. Returns false while cancellation
  // must finish before the application's final termination notification.
  bool MayQuit();

 private:
  std::unique_ptr<Impl> impl_;
};

}  // namespace pubky_update

#endif  // CHROME_BROWSER_PUBKY_UPDATE_MAC_ADAPTER_H_
