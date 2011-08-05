// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "base/file_path.h"
#include "chrome/browser/ui/webui/web_ui_browsertest.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/ui_test_utils.h"
#include "googleurl/src/gurl.h"

class MostVisitedWebUITest : public WebUIBrowserTest {
 public:
  virtual ~MostVisitedWebUITest() {}

  virtual void SetUpInProcessBrowserTestFixture() {
    WebUIBrowserTest::SetUpInProcessBrowserTestFixture();
    AddLibrary(FilePath(FILE_PATH_LITERAL("most_visited_page_test.js")));
  }

  virtual void SetUpCommandLine(CommandLine* command_line) {
    command_line->AppendSwitch(switches::kNewTabPage4);
  }

  virtual void SetUpOnMainThread() {
    ui_test_utils::NavigateToURL(browser(), GURL(chrome::kChromeUINewTabURL));
  }
};

WEB_UI_UNITTEST_F(MostVisitedWebUITest, refreshDataBasic);
WEB_UI_UNITTEST_F(MostVisitedWebUITest, refreshDataOrdering);
WEB_UI_UNITTEST_F(MostVisitedWebUITest, refreshDataPinning);
