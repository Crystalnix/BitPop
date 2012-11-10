// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/logging.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/test/base/ui_test_utils.h"

namespace extensions {
namespace {

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, ActiveTab) {
  ASSERT_TRUE(StartTestServer());

  const Extension* extension =
      LoadExtension(test_data_dir_.AppendASCII("active_tab"));
  ASSERT_TRUE(extension);

  ExtensionService* service =
      ExtensionSystem::Get(browser()->profile())->extension_service();

  // Shouldn't be initially granted based on activeTab.
  {
    ResultCatcher catcher;
    ui_test_utils::NavigateToURL(browser(), test_server()->GetURL("page.html"));
    EXPECT_TRUE(catcher.GetNextResult()) << message_;
  }

  // Granting to the extension should give it access to page.html.
  {
    ResultCatcher catcher;
    service->toolbar_model()->ExecuteBrowserAction(extension, browser(), NULL);
    EXPECT_TRUE(catcher.GetNextResult()) << message_;
  }

  // Changing page should go back to it not having access.
  {
    ResultCatcher catcher;
    ui_test_utils::NavigateToURL(browser(),
                                 test_server()->GetURL("final_page.html"));
    EXPECT_TRUE(catcher.GetNextResult()) << message_;
  }
}

}  // namespace
}  // namespace extensions
