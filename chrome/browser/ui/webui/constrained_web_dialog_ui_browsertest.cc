// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/ui/ui_test.h"

#include "base/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/constrained_window_tab_helper.h"
#include "chrome/browser/ui/webui/constrained_web_dialog_ui.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/web_dialogs/test/test_web_dialog_delegate.h"

using content::WebContents;
using ui::WebDialogDelegate;

namespace {

class ConstrainedWebDialogBrowserTestObserver
    : public content::WebContentsObserver {
 public:
  explicit ConstrainedWebDialogBrowserTestObserver(WebContents* contents)
      : content::WebContentsObserver(contents),
        contents_destroyed_(false) {
  }
  virtual ~ConstrainedWebDialogBrowserTestObserver() {}

  bool contents_destroyed() { return contents_destroyed_; }

 private:
  virtual void WebContentsDestroyed(WebContents* tab) OVERRIDE {
    contents_destroyed_ = true;
  }

  bool contents_destroyed_;
};

}  // namespace

class ConstrainedWebDialogBrowserTest : public InProcessBrowserTest {
 public:
  ConstrainedWebDialogBrowserTest() {}

 protected:
  size_t GetConstrainedWindowCount(WebContents* web_contents) const {
    ConstrainedWindowTabHelper* constrained_window_tab_helper =
        ConstrainedWindowTabHelper::FromWebContents(web_contents);
    return constrained_window_tab_helper->constrained_window_count();
  }
};

// Tests that opening/closing the constrained window won't crash it.
IN_PROC_BROWSER_TEST_F(ConstrainedWebDialogBrowserTest, BasicTest) {
  // The delegate deletes itself.
  WebDialogDelegate* delegate = new ui::test::TestWebDialogDelegate(
      GURL(chrome::kChromeUIConstrainedHTMLTestURL));
  WebContents* web_contents = chrome::GetActiveWebContents(browser());
  ASSERT_TRUE(web_contents);

  ConstrainedWebDialogDelegate* dialog_delegate =
      CreateConstrainedWebDialog(browser()->profile(),
                                 delegate,
                                 NULL,
                                 web_contents);
  ASSERT_TRUE(dialog_delegate);
  EXPECT_TRUE(dialog_delegate->GetWindow());
  EXPECT_EQ(1U, GetConstrainedWindowCount(web_contents));
}

// Tests that ReleaseWebContentsOnDialogClose() works.
IN_PROC_BROWSER_TEST_F(ConstrainedWebDialogBrowserTest,
                       ReleaseWebContentsOnDialogClose) {
  // The delegate deletes itself.
  WebDialogDelegate* delegate = new ui::test::TestWebDialogDelegate(
      GURL(chrome::kChromeUIConstrainedHTMLTestURL));
  WebContents* web_contents = chrome::GetActiveWebContents(browser());
  ASSERT_TRUE(web_contents);

  ConstrainedWebDialogDelegate* dialog_delegate =
      CreateConstrainedWebDialog(browser()->profile(),
                                 delegate,
                                 NULL,
                                 web_contents);
  ASSERT_TRUE(dialog_delegate);
  scoped_ptr<WebContents> new_tab(dialog_delegate->GetWebContents());
  ASSERT_TRUE(new_tab.get());
  ASSERT_EQ(1U, GetConstrainedWindowCount(web_contents));

  ConstrainedWebDialogBrowserTestObserver observer(new_tab.get());
  dialog_delegate->ReleaseWebContentsOnDialogClose();
  dialog_delegate->OnDialogCloseFromWebUI();

  ASSERT_FALSE(observer.contents_destroyed());
  EXPECT_EQ(0U, GetConstrainedWindowCount(web_contents));
  new_tab.reset();
  EXPECT_TRUE(observer.contents_destroyed());
}
