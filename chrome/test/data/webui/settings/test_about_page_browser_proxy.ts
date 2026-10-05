// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {webUIListenerCallback} from 'chrome://resources/js/cr.js';
import type {AboutPageBrowserProxy} from 'chrome://settings/settings.js';
import {UpdateStatus} from 'chrome://settings/settings.js';
import {TestBrowserProxy} from 'chrome://webui-test/test_browser_proxy.js';

export class TestAboutPageBrowserProxy extends TestBrowserProxy implements
    AboutPageBrowserProxy {
  private updateStatus_: UpdateStatus = UpdateStatus.UPDATED;

  constructor() {
    super([
      'pageReady', 'refreshUpdateStatus', 'openHelpPage', 'openFeedbackDialog',
      // <if expr="not _google_chrome and not is_chrome_for_testing and not is_chromeos and not is_android">
      'checkPubkyUpdate', 'confirmPubkyUpdate', 'cancelPubkyUpdate',
      'restartToApplyPubkyUpdate', 'detachPubkyUpdate',
      // </if>

      // <if expr="is_macosx">
      'promoteUpdater',
      // </if>
    ]);
  }

  setUpdateStatus(updateStatus: UpdateStatus) {
    this.updateStatus_ = updateStatus;
  }

  pageReady() {
    this.methodCalled('pageReady');
  }

  refreshUpdateStatus() {
    webUIListenerCallback('update-status-changed', {
      progress: 1,
      status: this.updateStatus_,
    });
    this.methodCalled('refreshUpdateStatus');
  }

  openFeedbackDialog() {
    this.methodCalled('openFeedbackDialog');
  }

  openHelpPage() {
    this.methodCalled('openHelpPage');
  }
  // <if expr="not _google_chrome and not is_chrome_for_testing and not is_chromeos and not is_android">
  checkPubkyUpdate() { this.methodCalled('checkPubkyUpdate'); }
  confirmPubkyUpdate(id: string) { this.methodCalled('confirmPubkyUpdate', id); }
  cancelPubkyUpdate(id: string) { this.methodCalled('cancelPubkyUpdate', id); }
  restartToApplyPubkyUpdate(id: string) {
    this.methodCalled('restartToApplyPubkyUpdate', id);
  }
  detachPubkyUpdate() { this.methodCalled('detachPubkyUpdate'); }
  // </if>

  // <if expr="is_macosx">
  promoteUpdater() {
    this.methodCalled('promoteUpdater');
  }
  // </if>
}
