// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "ui/events/keycodes/keyboard_codes.h"

class PubkyStartPageInteractiveTest : public InProcessBrowserTest {};

IN_PROC_BROWSER_TEST_F(PubkyStartPageInteractiveTest, TabThroughLinks) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(),
                                           chrome::ChromeUINewTabURLAsGURL()));
  auto* tab = browser()->GetTabStripModel()->GetActiveWebContents();
  ASSERT_TRUE(content::ExecJs(tab, "document.querySelector('nav a').focus()"));
  tab->Focus();
  for (int index = 0; index < 6; ++index) {
    EXPECT_EQ(index, content::EvalJs(tab,
                                     "[...document.querySelectorAll('nav a')]"
                                     ".indexOf(document.activeElement)"));
    if (index < 5) {
      ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_TAB,
                                                  false, false, false, false));
    }
  }
}
