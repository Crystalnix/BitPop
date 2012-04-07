// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/browser.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/navigation_details.h"
#include "content/public/browser/web_contents.h"

using content::WebContents;

namespace {

const char kSharedWorkerTestPage[] =
    "files/workers/workers_ui_shared_worker.html";
const char kSharedWorkerJs[] =
    "files/workers/workers_ui_shared_worker.js";

class WorkersUITest : public InProcessBrowserTest {
 public:
  WorkersUITest() {
    set_show_window(true);
    EnableDOMAutomation();
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(WorkersUITest);
};

// The test fails on Mac OS X, see crbug.com/89583
#if defined(OS_MACOSX)
#define MAYBE_SharedWorkersList DISABLED_SharedWorkersList
#else
#define MAYBE_SharedWorkersList FLAKY_SharedWorkersList
#endif
IN_PROC_BROWSER_TEST_F(WorkersUITest, MAYBE_SharedWorkersList) {
  ASSERT_TRUE(test_server()->Start());
  GURL url = test_server()->GetURL(kSharedWorkerTestPage);
  ui_test_utils::NavigateToURL(browser(), url);

  ui_test_utils::NavigateToURLWithDisposition(
      browser(),
      GURL(chrome::kChromeUIWorkersURL),
      NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);

  WebContents* web_contents = browser()->GetSelectedWebContents();
  ASSERT_TRUE(web_contents != NULL);

  std::string result;
  ASSERT_TRUE(
      ui_test_utils::ExecuteJavaScriptAndExtractString(
          web_contents->GetRenderViewHost(),
          L"",
          L"window.domAutomationController.send("
          L"'' + document.getElementsByTagName('td')[1].textContent);",
          &result));
  ASSERT_TRUE(result.find(kSharedWorkerJs) != std::string::npos);
}

}  // namespace
