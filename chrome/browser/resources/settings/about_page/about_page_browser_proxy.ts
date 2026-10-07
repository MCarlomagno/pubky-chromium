// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview A helper object used from the "About" section to interact with
 * the browser.
 */

/**
 * Enumeration of all possible update statuses. The string literals must match
 * the ones defined at |AboutHandler::UpdateStatusToString|.
 * @enum {string}
 */
export enum UpdateStatus {
  CHECKING = 'checking',
  UPDATING = 'updating',
  NEARLY_UPDATED = 'nearly_updated',
  UPDATED = 'updated',
  FAILED = 'failed',
  FAILED_HTTP = 'failed_http',
  FAILED_DOWNLOAD = 'failed_download',
  DISABLED = 'disabled',
  DISABLED_BY_ADMIN = 'disabled_by_admin',
  NEED_PERMISSION_TO_UPDATE = 'need_permission_to_update',
}

// <if expr="_google_chrome and is_macosx">
export interface PromoteUpdaterStatus {
  hidden: boolean;
  disabled: boolean;
  actionable: boolean;
  text?: string;
}
// </if>

export interface UpdateStatusChangedEvent {
  status: UpdateStatus;
  progress?: number;
  message?: string;
  connectionTypes?: string;
  version?: string;
  size?: string;
}
// <if expr="not _google_chrome and not is_chrome_for_testing and not is_chromeos and not is_android">
export interface PubkyUpdateStatus {
  state: string;
  id: string;
  version: string;
  size: string;
  error: number;
  // Native eligibility reason, cached only. Production activation is absent.
  eligibilityReason?: number;
  // Native installer error code, for install_failed.
  installError?: number;
  canCheck: boolean;
  canCancel: boolean;
  canConfirm: boolean;
  canRestart: boolean;
}
// </if>


export interface AboutPageBrowserProxy {
  // <if expr="not _google_chrome and not is_chrome_for_testing and not is_chromeos and not is_android">
  checkPubkyUpdate(): void;
  confirmPubkyUpdate(id: string): void;
  cancelPubkyUpdate(id: string): void;
  restartToApplyPubkyUpdate(id: string): void;
  detachPubkyUpdate(): void;
  // </if>
  /**
   * Indicates to the browser that the page is ready.
   */
  pageReady(): void;

  /**
   * Request update status from the browser. It results in one or more
   * 'update-status-changed' WebUI events.
   */
  refreshUpdateStatus(): void;

  /** Opens the help page. */
  openHelpPage(): void;

  // <if expr="_google_chrome">
  /**
   * Opens the feedback dialog.
   */
  openFeedbackDialog(): void;

  // </if>

  // <if expr="_google_chrome and is_macosx">
  /**
   * Triggers setting up auto-updates for all users.
   */
  promoteUpdater(): void;
  // </if>
}

export class AboutPageBrowserProxyImpl implements AboutPageBrowserProxy {
  // <if expr="not _google_chrome and not is_chrome_for_testing and not is_chromeos and not is_android">
  checkPubkyUpdate() { chrome.send('checkPubkyUpdate'); }
  confirmPubkyUpdate(id: string) { chrome.send('confirmPubkyUpdate', [id]); }
  cancelPubkyUpdate(id: string) { chrome.send('cancelPubkyUpdate', [id]); }
  restartToApplyPubkyUpdate(id: string) {
    chrome.send('restartToApplyPubkyUpdate', [id]);
  }
  detachPubkyUpdate() { chrome.send('detachPubkyUpdate'); }
  // </if>
  pageReady() {
    chrome.send('aboutPageReady');
  }

  refreshUpdateStatus() {
    chrome.send('refreshUpdateStatus');
  }

  // <if expr="_google_chrome and is_macosx">
  promoteUpdater() {
    chrome.send('promoteUpdater');
  }
  // </if>

  openHelpPage() {
    chrome.send('openHelpPage');
  }

  // <if expr="_google_chrome">
  openFeedbackDialog() {
    chrome.send('openFeedbackDialog');
  }
  // </if>

  static getInstance(): AboutPageBrowserProxy {
    return instance || (instance = new AboutPageBrowserProxyImpl());
  }

  static setInstance(obj: AboutPageBrowserProxy) {
    instance = obj;
  }
}

let instance: AboutPageBrowserProxy|null = null;
