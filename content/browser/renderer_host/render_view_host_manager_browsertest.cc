// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/file_util.h"
#include "base/memory/ref_counted.h"
#include "base/path_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/test/in_process_browser_test.h"
#include "chrome/test/ui_test_utils.h"
#include "content/browser/site_instance.h"
#include "content/browser/tab_contents/tab_contents.h"
#include "content/common/notification_details.h"
#include "content/common/notification_observer.h"
#include "content/common/notification_registrar.h"
#include "content/common/notification_type.h"
#include "net/base/net_util.h"
#include "net/test/test_server.h"

class RenderViewHostManagerTest : public InProcessBrowserTest {
 public:
  RenderViewHostManagerTest() {
    EnableDOMAutomation();
  }

  static bool GetFilePathWithHostAndPortReplacement(
      const std::string& original_file_path,
      const net::HostPortPair& host_port_pair,
      std::string* replacement_path) {
    std::vector<net::TestServer::StringPair> replacement_text;
    replacement_text.push_back(
        make_pair("REPLACE_WITH_HOST_AND_PORT", host_port_pair.ToString()));
    return net::TestServer::GetFilePathWithReplacements(
        original_file_path, replacement_text, replacement_path);
  }
};

// Test for crbug.com/24447.  Following a cross-site link with rel=noreferrer
// and target=_blank should create a new SiteInstance.
IN_PROC_BROWSER_TEST_F(RenderViewHostManagerTest,
                       SwapProcessWithRelNoreferrerAndTargetBlank) {
  // Start two servers with different sites.
  ASSERT_TRUE(test_server()->Start());
  net::TestServer https_server(
      net::TestServer::TYPE_HTTPS,
      FilePath(FILE_PATH_LITERAL("chrome/test/data")));
  ASSERT_TRUE(https_server.Start());

  // Load a page with links that open in a new window.
  std::string replacement_path;
  ASSERT_TRUE(GetFilePathWithHostAndPortReplacement(
      "files/click-noreferrer-links.html",
      https_server.host_port_pair(),
      &replacement_path));
  ui_test_utils::NavigateToURL(browser(),
                               test_server()->GetURL(replacement_path));

  // Get the original SiteInstance for later comparison.
  scoped_refptr<SiteInstance> orig_site_instance(
      browser()->GetSelectedTabContents()->GetSiteInstance());
  EXPECT_TRUE(orig_site_instance != NULL);

  // Test clicking a rel=noreferrer + target=blank link.
  bool success = false;
  EXPECT_TRUE(ui_test_utils::ExecuteJavaScriptAndExtractBool(
      browser()->GetSelectedTabContents()->render_view_host(), L"",
      L"window.domAutomationController.send(clickNoRefTargetBlankLink());",
      &success));
  EXPECT_TRUE(success);

  // Wait for the tab to open.
  if (browser()->tab_count() < 2)
    ui_test_utils::WaitForNewTab(browser());

  // Opens in new tab.
  EXPECT_EQ(2, browser()->tab_count());
  EXPECT_EQ(1, browser()->active_index());
  EXPECT_EQ("/files/title2.html",
            browser()->GetSelectedTabContents()->GetURL().path());

  // Wait for the cross-site transition in the new tab to finish.
  ui_test_utils::WaitForLoadStop(browser()->GetSelectedTabContents());
  EXPECT_FALSE(browser()->GetSelectedTabContents()->render_manager()->
                   pending_render_view_host());

  // Should have a new SiteInstance.
  scoped_refptr<SiteInstance> noref_blank_site_instance(
      browser()->GetSelectedTabContents()->GetSiteInstance());
  EXPECT_NE(orig_site_instance, noref_blank_site_instance);
}

// Test for crbug.com/24447.  Following a cross-site link with just
// target=_blank should not create a new SiteInstance.
IN_PROC_BROWSER_TEST_F(RenderViewHostManagerTest,
                       DontSwapProcessWithOnlyTargetBlank) {
  // Start two servers with different sites.
  ASSERT_TRUE(test_server()->Start());
  net::TestServer https_server(
      net::TestServer::TYPE_HTTPS,
      FilePath(FILE_PATH_LITERAL("chrome/test/data")));
  ASSERT_TRUE(https_server.Start());

  // Load a page with links that open in a new window.
  std::string replacement_path;
  ASSERT_TRUE(GetFilePathWithHostAndPortReplacement(
      "files/click-noreferrer-links.html",
      https_server.host_port_pair(),
      &replacement_path));
  ui_test_utils::NavigateToURL(browser(),
                               test_server()->GetURL(replacement_path));

  // Get the original SiteInstance for later comparison.
  scoped_refptr<SiteInstance> orig_site_instance(
      browser()->GetSelectedTabContents()->GetSiteInstance());
  EXPECT_TRUE(orig_site_instance != NULL);

  // Test clicking a target=blank link.
  bool success = false;
  EXPECT_TRUE(ui_test_utils::ExecuteJavaScriptAndExtractBool(
      browser()->GetSelectedTabContents()->render_view_host(), L"",
      L"window.domAutomationController.send(clickTargetBlankLink());",
      &success));
  EXPECT_TRUE(success);

  // Wait for the tab to open.
  if (browser()->tab_count() < 2)
    ui_test_utils::WaitForNewTab(browser());

  // Opens in new tab.
  EXPECT_EQ(2, browser()->tab_count());
  EXPECT_EQ(1, browser()->active_index());

  // Wait for the cross-site transition in the new tab to finish.
  ui_test_utils::WaitForLoadStop(browser()->GetSelectedTabContents());
  EXPECT_EQ("/files/title2.html",
            browser()->GetSelectedTabContents()->GetURL().path());

  // Should have the same SiteInstance.
  scoped_refptr<SiteInstance> blank_site_instance(
      browser()->GetSelectedTabContents()->GetSiteInstance());
  EXPECT_EQ(orig_site_instance, blank_site_instance);
}

// Test for crbug.com/24447.  Following a cross-site link with rel=noreferrer
// and no target=_blank should not create a new SiteInstance.
IN_PROC_BROWSER_TEST_F(RenderViewHostManagerTest,
                       DontSwapProcessWithOnlyRelNoreferrer) {
  // Start two servers with different sites.
  ASSERT_TRUE(test_server()->Start());
  net::TestServer https_server(
      net::TestServer::TYPE_HTTPS,
      FilePath(FILE_PATH_LITERAL("chrome/test/data")));
  ASSERT_TRUE(https_server.Start());

  // Load a page with links that open in a new window.
  std::string replacement_path;
  ASSERT_TRUE(GetFilePathWithHostAndPortReplacement(
      "files/click-noreferrer-links.html",
      https_server.host_port_pair(),
      &replacement_path));
  ui_test_utils::NavigateToURL(browser(),
                               test_server()->GetURL(replacement_path));

  // Get the original SiteInstance for later comparison.
  scoped_refptr<SiteInstance> orig_site_instance(
      browser()->GetSelectedTabContents()->GetSiteInstance());
  EXPECT_TRUE(orig_site_instance != NULL);

  // Test clicking a rel=noreferrer link.
  bool success = false;
  EXPECT_TRUE(ui_test_utils::ExecuteJavaScriptAndExtractBool(
      browser()->GetSelectedTabContents()->render_view_host(), L"",
      L"window.domAutomationController.send(clickNoRefLink());",
      &success));
  EXPECT_TRUE(success);

  // Wait for the cross-site transition in the current tab to finish.
  ui_test_utils::WaitForLoadStop(browser()->GetSelectedTabContents());

  // Opens in same tab.
  EXPECT_EQ(1, browser()->tab_count());
  EXPECT_EQ(0, browser()->active_index());
  EXPECT_EQ("/files/title2.html",
            browser()->GetSelectedTabContents()->GetURL().path());

  // Should have the same SiteInstance.
  scoped_refptr<SiteInstance> noref_site_instance(
      browser()->GetSelectedTabContents()->GetSiteInstance());
  EXPECT_EQ(orig_site_instance, noref_site_instance);
}

// Test for crbug.com/76666.  A cross-site navigation that fails with a 204
// error should not make us ignore future renderer-initiated navigations.
IN_PROC_BROWSER_TEST_F(RenderViewHostManagerTest, ClickLinkAfter204Error) {
  // Start two servers with different sites.
  ASSERT_TRUE(test_server()->Start());
  net::TestServer https_server(
      net::TestServer::TYPE_HTTPS,
      FilePath(FILE_PATH_LITERAL("chrome/test/data")));
  ASSERT_TRUE(https_server.Start());

  // Load a page with links that open in a new window.
  // The links will point to the HTTPS server.
  std::string replacement_path;
  ASSERT_TRUE(GetFilePathWithHostAndPortReplacement(
      "files/click-noreferrer-links.html",
      https_server.host_port_pair(),
      &replacement_path));
  ui_test_utils::NavigateToURL(browser(),
                               test_server()->GetURL(replacement_path));

  // Get the original SiteInstance for later comparison.
  scoped_refptr<SiteInstance> orig_site_instance(
      browser()->GetSelectedTabContents()->GetSiteInstance());
  EXPECT_TRUE(orig_site_instance != NULL);

  // Load a cross-site page that fails with a 204 error.
  ui_test_utils::NavigateToURL(browser(), https_server.GetURL("nocontent"));

  // We should still be looking at the normal page.
  scoped_refptr<SiteInstance> post_nav_site_instance(
      browser()->GetSelectedTabContents()->GetSiteInstance());
  EXPECT_EQ(orig_site_instance, post_nav_site_instance);
  EXPECT_EQ("/files/click-noreferrer-links.html",
            browser()->GetSelectedTabContents()->GetURL().path());

  // Renderer-initiated navigations should work.
  bool success = false;
  EXPECT_TRUE(ui_test_utils::ExecuteJavaScriptAndExtractBool(
      browser()->GetSelectedTabContents()->render_view_host(), L"",
      L"window.domAutomationController.send(clickNoRefLink());",
      &success));
  EXPECT_TRUE(success);

  // Wait for the cross-site transition in the current tab to finish.
  ui_test_utils::WaitForLoadStop(browser()->GetSelectedTabContents());

  // Opens in same tab.
  EXPECT_EQ(1, browser()->tab_count());
  EXPECT_EQ(0, browser()->active_index());
  EXPECT_EQ("/files/title2.html",
            browser()->GetSelectedTabContents()->GetURL().path());

  // Should have the same SiteInstance.
  scoped_refptr<SiteInstance> noref_site_instance(
      browser()->GetSelectedTabContents()->GetSiteInstance());
  EXPECT_EQ(orig_site_instance, noref_site_instance);
}
