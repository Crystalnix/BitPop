// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/ref_counted.h"
#include "base/path_service.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/test/automation/automation_proxy.h"
#include "chrome/test/automation/browser_proxy.h"
#include "chrome/test/automation/extension_proxy.h"
#include "chrome/test/automation/tab_proxy.h"
#include "chrome/test/layout_test_http_server.h"
#include "chrome/test/ui/ui_test.h"

namespace {

// These tests are not meant to test the extension system itself, but to verify
// the correctness of ExtensionProxy and the AutomationProvider implementation
// behind it.
class ExtensionProxyUITest : public UITest {
 public:
  ExtensionProxyUITest() {}

  virtual void SetUp() {
    UITest::SetUp();
    simple_extension_= InstallSimpleBrowserActionExtension();
    ASSERT_TRUE(simple_extension_.get());
  }

 protected:
  // Installs a simple browser action extension from the sample_extensions
  // folder. Returns an ExtensionProxy, which could be NULL.
  scoped_refptr<ExtensionProxy> InstallSimpleBrowserActionExtension() {
    return automation()->InstallExtension(
        test_data_directory_.AppendASCII("extensions").AppendASCII("uitest").
            AppendASCII("simple_browser_action.crx"), false);
  }

  // Installs a extension which, when clicking the browser action, renames
  // the current tab to the tab's index. Returns an ExtensionProxy,
  // which could be NULL.
  scoped_refptr<ExtensionProxy> InstallRenameTabExtension() {
    return automation()->InstallExtension(
        test_data_directory_.AppendASCII("extensions").AppendASCII("uitest").
            AppendASCII("rename_tab.crx"), false);
  }

  // The google translate extension, which is installed on test setup.
  scoped_refptr<ExtensionProxy> simple_extension_;

 private:
  DISALLOW_COPY_AND_ASSIGN(ExtensionProxyUITest);
};

TEST_F(ExtensionProxyUITest, NoSuchExtension) {
  ASSERT_TRUE(simple_extension_->Uninstall());

  // A proxy referring to an uninstalled extension should return false for all
  // calls without actually invoking the extension system.
  ASSERT_FALSE(simple_extension_->Uninstall());
  ASSERT_FALSE(simple_extension_->Enable());
  ASSERT_FALSE(simple_extension_->Disable());
  scoped_refptr<BrowserProxy> browser = automation()->GetBrowserWindow(0);
  ASSERT_TRUE(browser.get());
  ASSERT_FALSE(simple_extension_->
               ExecuteActionInActiveTabAsync(browser.get()));
  ASSERT_FALSE(simple_extension_->MoveBrowserAction(0));
  std::string name, version;
  int index;
  ASSERT_FALSE(simple_extension_->GetName(&name));
  ASSERT_FALSE(simple_extension_->GetVersion(&version));
  ASSERT_FALSE(simple_extension_->GetBrowserActionIndex(&index));
}

TEST_F(ExtensionProxyUITest, EnableDisable) {
  ASSERT_TRUE(simple_extension_->Disable());
  ASSERT_TRUE(simple_extension_->Enable());
  ASSERT_TRUE(simple_extension_->Disable());
}

TEST_F(ExtensionProxyUITest, Uninstall) {
  ASSERT_TRUE(simple_extension_->Uninstall());

  // Uninstall a disabled extension.
  simple_extension_ = InstallSimpleBrowserActionExtension();
  ASSERT_TRUE(simple_extension_.get());
  ASSERT_TRUE(simple_extension_->Disable());
  ASSERT_TRUE(simple_extension_->Uninstall());

  // Uninstall a just enabled extension.
  simple_extension_ = InstallSimpleBrowserActionExtension();
  ASSERT_TRUE(simple_extension_.get());
  ASSERT_TRUE(simple_extension_->Disable());
  ASSERT_TRUE(simple_extension_->Enable());
  ASSERT_TRUE(simple_extension_->Uninstall());
}

// http://crbug.com/44370
TEST_F(ExtensionProxyUITest, DISABLED_ExecuteBrowserActionInActiveTabAsync) {
  scoped_refptr<BrowserProxy> browser = automation()->GetBrowserWindow(0);
  ASSERT_TRUE(browser.get());
  scoped_refptr<ExtensionProxy> rename_tab_extension =
      InstallRenameTabExtension();
  ASSERT_TRUE(rename_tab_extension.get());

  // Need to use http in order for the extension to be able to inject
  // javascript into the page. Extensions do not have permissions for
  // chrome://* urls.
  FilePath path;
  // The root directory for the http server does not matter in this case,
  // but we have to pick something.
  ASSERT_TRUE(PathService::Get(chrome::DIR_TEST_DATA, &path));
  // TODO(phajdan.jr): Use net/test/test_server instead of layout test server.
  LayoutTestHttpServer http_server(path, 1365);
  ASSERT_TRUE(http_server.Start());
  GURL localhost = GURL("http://localhost:1365");
  NavigateToURL(localhost);

  // Click the browser action, which should rename the tab title to
  // the tab's index.
  ASSERT_TRUE(rename_tab_extension->
              ExecuteActionInActiveTabAsync(browser.get()));
  ASSERT_NO_FATAL_FAILURE(automation()->EnsureExtensionTestResult());

  scoped_refptr<TabProxy> display_tab = browser->GetTab(0);
  ASSERT_TRUE(display_tab);
  std::wstring title_wstring;
  ASSERT_TRUE(display_tab->GetTabTitle(&title_wstring));
  ASSERT_STREQ(L"0", title_wstring.c_str());

  // Click the action again right after navigating to a new page.
  ASSERT_TRUE(browser->AppendTab(localhost));
  display_tab = browser->GetTab(1);
  ASSERT_TRUE(display_tab);
  ASSERT_TRUE(rename_tab_extension->
              ExecuteActionInActiveTabAsync(browser.get()));
  ASSERT_NO_FATAL_FAILURE(automation()->EnsureExtensionTestResult());
  ASSERT_TRUE(display_tab->GetTabTitle(&title_wstring));
  ASSERT_STREQ(L"1", title_wstring.c_str());

  // Do not forget to stop the server.
  ASSERT_TRUE(http_server.Stop());
}

// Flaky, http://crbug.com/59441.
TEST_F(ExtensionProxyUITest, FLAKY_MoveBrowserAction) {
  scoped_refptr<ExtensionProxy> rename_tab_extension =
      InstallRenameTabExtension();
  ASSERT_TRUE(rename_tab_extension.get());
  ASSERT_NO_FATAL_FAILURE(simple_extension_->
                          EnsureBrowserActionIndexMatches(0));
  ASSERT_NO_FATAL_FAILURE(rename_tab_extension->
                          EnsureBrowserActionIndexMatches(1));

  // Move google translate to the end, then beginning, and verify.
  ASSERT_TRUE(simple_extension_->MoveBrowserAction(1));
  ASSERT_NO_FATAL_FAILURE(simple_extension_->
                          EnsureBrowserActionIndexMatches(1));
  ASSERT_NO_FATAL_FAILURE(rename_tab_extension->
                          EnsureBrowserActionIndexMatches(0));
  ASSERT_TRUE(simple_extension_->MoveBrowserAction(0));
  ASSERT_NO_FATAL_FAILURE(simple_extension_->
                          EnsureBrowserActionIndexMatches(0));
  ASSERT_NO_FATAL_FAILURE(rename_tab_extension->
                          EnsureBrowserActionIndexMatches(1));

  // Try moving browser action to invalid index.
  ASSERT_FALSE(simple_extension_->MoveBrowserAction(-1));
  ASSERT_FALSE(simple_extension_->MoveBrowserAction(2));
}

// Flaky, http://crbug.com/59440.
TEST_F(ExtensionProxyUITest, FLAKY_GetProperty) {
  ASSERT_NO_FATAL_FAILURE(simple_extension_->
                          EnsureIdMatches("aiglobglfckejlcpcbdokbkbjeemfhno"));
  ASSERT_NO_FATAL_FAILURE(simple_extension_->
                          EnsureNameMatches("Browser Action"));
  ASSERT_NO_FATAL_FAILURE(simple_extension_->
                          EnsureVersionMatches("0.1.1"));
  ASSERT_NO_FATAL_FAILURE(simple_extension_->
                          EnsureBrowserActionIndexMatches(0));
}

}  // namespace
