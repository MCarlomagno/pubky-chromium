// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

for (const button of document.querySelectorAll('.key-address button')) {
  button.addEventListener('click', async () => {
    try {
      await navigator.clipboard.writeText(button.previousElementSibling.href);
      button.textContent = 'Copied';
    } catch {
      button.textContent = 'Copy failed';
    }
  });
}
