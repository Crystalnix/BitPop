// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/utf_string_conversions.h"
#include "chrome/browser/automation/automation_util.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/extensions/extension_host.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tab_contents/tab_contents_wrapper.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/browser/renderer_host/render_view_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"
#include "net/base/mock_host_resolver.h"

using content::NavigationController;
using content::WebContents;

namespace {

class IsolatedAppTest : public ExtensionBrowserTest {
 public:
  // Returns whether the given tab's current URL has the given cookie.
  bool WARN_UNUSED_RESULT HasCookie(WebContents* contents, std::string cookie) {
    int value_size;
    std::string actual_cookie;
    automation_util::GetCookies(contents->GetURL(), contents, &value_size,
                                &actual_cookie);
    return actual_cookie.find(cookie) != std::string::npos;
  }

  const Extension* GetInstalledApp(WebContents* contents) {
    const Extension* installed_app = NULL;
    Profile* profile =
        Profile::FromBrowserContext(contents->GetBrowserContext());
    ExtensionService* service = profile->GetExtensionService();
    if (service) {
      installed_app = service->GetInstalledAppForRenderer(
          contents->GetRenderProcessHost()->GetID());
    }
    return installed_app;
  }

 private:
  virtual void SetUpCommandLine(CommandLine* command_line) {
    ExtensionBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch(switches::kEnableExperimentalExtensionApis);
  }
};

}  // namespace

// Tests that cookies set within an isolated app are not visible to normal
// pages or other apps.
IN_PROC_BROWSER_TEST_F(IsolatedAppTest, CookieIsolation) {
  host_resolver()->AddRule("*", "127.0.0.1");
  ASSERT_TRUE(test_server()->Start());

  ASSERT_TRUE(LoadExtension(test_data_dir_.AppendASCII("isolated_apps/app1")));
  ASSERT_TRUE(LoadExtension(test_data_dir_.AppendASCII("isolated_apps/app2")));

  // The app under test acts on URLs whose host is "localhost",
  // so the URLs we navigate to must have host "localhost".
  GURL base_url = test_server()->GetURL(
      "files/extensions/isolated_apps/");
  GURL::Replacements replace_host;
  std::string host_str("localhost");  // Must stay in scope with replace_host.
  replace_host.SetHostStr(host_str);
  base_url = base_url.ReplaceComponents(replace_host);

  ui_test_utils::NavigateToURLWithDisposition(
      browser(), base_url.Resolve("app1/main.html"),
      CURRENT_TAB, ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), base_url.Resolve("app2/main.html"),
      NEW_FOREGROUND_TAB, ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), base_url.Resolve("non_app/main.html"),
      NEW_FOREGROUND_TAB, ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);

  ASSERT_EQ(3, browser()->tab_count());

  // Ensure first two tabs have installed apps.
  WebContents* tab1 = browser()->GetWebContentsAt(0);
  WebContents* tab2 = browser()->GetWebContentsAt(1);
  WebContents* tab3 = browser()->GetWebContentsAt(2);
  ASSERT_TRUE(GetInstalledApp(tab1));
  ASSERT_TRUE(GetInstalledApp(tab2));
  ASSERT_TRUE(!GetInstalledApp(tab3));

  // Check that each tab sees its own cookie.
  EXPECT_TRUE(HasCookie(tab1, "app1=3"));
  EXPECT_TRUE(HasCookie(tab2, "app2=4"));
  EXPECT_TRUE(HasCookie(tab3, "normalPage=5"));

  // Check that app1 tab cannot see the other cookies.
  EXPECT_FALSE(HasCookie(tab1, "app2"));
  EXPECT_FALSE(HasCookie(tab1, "normalPage"));

  // Check that app2 tab cannot see the other cookies.
  EXPECT_FALSE(HasCookie(tab2, "app1"));
  EXPECT_FALSE(HasCookie(tab2, "normalPage"));

  // Check that normal tab cannot see the other cookies.
  EXPECT_FALSE(HasCookie(tab3, "app1"));
  EXPECT_FALSE(HasCookie(tab3, "app2"));

  // Check that the non_app iframe cookie is associated with app1 and not the
  // normal tab.  (For now, iframes are always rendered in their parent
  // process, even if they aren't in the app manifest.)
  EXPECT_TRUE(HasCookie(tab1, "nonAppFrame=6"));
  EXPECT_FALSE(HasCookie(tab3, "nonAppFrame"));

  // Check that isolation persists even if the tab crashes and is reloaded.
  browser()->SelectNumberedTab(1);
  ui_test_utils::CrashTab(tab1);
  ui_test_utils::WindowedNotificationObserver observer(
      content::NOTIFICATION_LOAD_STOP,
      content::Source<NavigationController>(
          &browser()->GetSelectedTabContentsWrapper()->web_contents()->
              GetController()));
  browser()->Reload(CURRENT_TAB);
  observer.Wait();
  EXPECT_TRUE(HasCookie(tab1, "app1=3"));
  EXPECT_FALSE(HasCookie(tab1, "app2"));
  EXPECT_FALSE(HasCookie(tab1, "normalPage"));
}

// Ensure that cookies are not isolated if the isolated apps are not installed.
IN_PROC_BROWSER_TEST_F(IsolatedAppTest, NoCookieIsolationWithoutApp) {
  host_resolver()->AddRule("*", "127.0.0.1");
  ASSERT_TRUE(test_server()->Start());

  // The app under test acts on URLs whose host is "localhost",
  // so the URLs we navigate to must have host "localhost".
  GURL base_url = test_server()->GetURL(
      "files/extensions/isolated_apps/");
  GURL::Replacements replace_host;
  std::string host_str("localhost");  // Must stay in scope with replace_host.
  replace_host.SetHostStr(host_str);
  base_url = base_url.ReplaceComponents(replace_host);

  ui_test_utils::NavigateToURLWithDisposition(
      browser(), base_url.Resolve("app1/main.html"),
      CURRENT_TAB, ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), base_url.Resolve("app2/main.html"),
      NEW_FOREGROUND_TAB, ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), base_url.Resolve("non_app/main.html"),
      NEW_FOREGROUND_TAB, ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);

  ASSERT_EQ(3, browser()->tab_count());

  // Check that tabs see each others' cookies.
  EXPECT_TRUE(HasCookie(browser()->GetWebContentsAt(0), "app2=4"));
  EXPECT_TRUE(HasCookie(browser()->GetWebContentsAt(0), "normalPage=5"));
  EXPECT_TRUE(HasCookie(browser()->GetWebContentsAt(0), "nonAppFrame=6"));
  EXPECT_TRUE(HasCookie(browser()->GetWebContentsAt(1), "app1=3"));
  EXPECT_TRUE(HasCookie(browser()->GetWebContentsAt(1), "normalPage=5"));
  EXPECT_TRUE(HasCookie(browser()->GetWebContentsAt(1), "nonAppFrame=6"));
  EXPECT_TRUE(HasCookie(browser()->GetWebContentsAt(2), "app1=3"));
  EXPECT_TRUE(HasCookie(browser()->GetWebContentsAt(2), "app2=4"));
  EXPECT_TRUE(HasCookie(browser()->GetWebContentsAt(2), "nonAppFrame=6"));
}

// Ensure that an isolated app never shares a process with WebUIs, non-isolated
// extensions, and normal webpages.  None of these should ever comingle
// RenderProcessHosts even if we hit the process limit.
IN_PROC_BROWSER_TEST_F(IsolatedAppTest, ProcessOverflow) {
  // Set max renderers to 1 to force running out of processes.
  content::RenderProcessHost::SetMaxRendererProcessCountForTest(1);

  host_resolver()->AddRule("*", "127.0.0.1");
  ASSERT_TRUE(test_server()->Start());

  ASSERT_TRUE(LoadExtension(test_data_dir_.AppendASCII("isolated_apps/app1")));
  ASSERT_TRUE(LoadExtension(test_data_dir_.AppendASCII("isolated_apps/app2")));
  ASSERT_TRUE(LoadExtension(test_data_dir_.AppendASCII("hosted_app")));
  ASSERT_TRUE(
      LoadExtension(test_data_dir_.AppendASCII("api_test/app_process")));

  // The app under test acts on URLs whose host is "localhost",
  // so the URLs we navigate to must have host "localhost".
  GURL base_url = test_server()->GetURL(
      "files/extensions/");
  GURL::Replacements replace_host;
  std::string host_str("localhost");  // Must stay in scope with replace_host.
  replace_host.SetHostStr(host_str);
  base_url = base_url.ReplaceComponents(replace_host);

  // Load an extension before adding tabs.
  const Extension* extension1 = LoadExtension(
      test_data_dir_.AppendASCII("api_test/browser_action/basics"));
  ASSERT_TRUE(extension1);
  GURL extension1_url = extension1->url();

  // Create multiple tabs for each type of renderer that might exist.
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), base_url.Resolve("isolated_apps/app1/main.html"),
      CURRENT_TAB, ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL(chrome::kChromeUINewTabURL),
      NEW_FOREGROUND_TAB, ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), base_url.Resolve("hosted_app/main.html"),
      NEW_FOREGROUND_TAB, ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), base_url.Resolve("test_file.html"),
      NEW_FOREGROUND_TAB, ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);

  ui_test_utils::NavigateToURLWithDisposition(
      browser(), base_url.Resolve("isolated_apps/app2/main.html"),
      NEW_FOREGROUND_TAB, ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL(chrome::kChromeUINewTabURL),
      NEW_FOREGROUND_TAB, ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), base_url.Resolve("api_test/app_process/path1/empty.html"),
      NEW_FOREGROUND_TAB, ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), base_url.Resolve("test_file_with_body.html"),
      NEW_FOREGROUND_TAB, ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);

  // Load another copy of isolated app 1.
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), base_url.Resolve("isolated_apps/app1/main.html"),
      NEW_FOREGROUND_TAB, ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);

  // Load another extension.
  const Extension* extension2 = LoadExtension(
      test_data_dir_.AppendASCII("api_test/browser_action/close_background"));
  ASSERT_TRUE(extension2);
  GURL extension2_url = extension2->url();

  // Get tab processes.
  ASSERT_EQ(9, browser()->tab_count());
  content::RenderProcessHost* isolated1_host =
      browser()->GetWebContentsAt(0)->GetRenderProcessHost();
  content::RenderProcessHost* ntp1_host =
      browser()->GetWebContentsAt(1)->GetRenderProcessHost();
  content::RenderProcessHost* hosted1_host =
      browser()->GetWebContentsAt(2)->GetRenderProcessHost();
  content::RenderProcessHost* web1_host =
      browser()->GetWebContentsAt(3)->GetRenderProcessHost();

  content::RenderProcessHost* isolated2_host =
      browser()->GetWebContentsAt(4)->GetRenderProcessHost();
  content::RenderProcessHost* ntp2_host =
      browser()->GetWebContentsAt(5)->GetRenderProcessHost();
  content::RenderProcessHost* hosted2_host =
      browser()->GetWebContentsAt(6)->GetRenderProcessHost();
  content::RenderProcessHost* web2_host =
      browser()->GetWebContentsAt(7)->GetRenderProcessHost();

  content::RenderProcessHost* second_isolated1_host =
      browser()->GetWebContentsAt(8)->GetRenderProcessHost();

  // Get extension processes.
  ExtensionProcessManager* process_manager =
    browser()->GetProfile()->GetExtensionProcessManager();
  content::RenderProcessHost* extension1_host =
      process_manager->GetSiteInstanceForURL(extension1_url)->GetProcess();
  content::RenderProcessHost* extension2_host =
      process_manager->GetSiteInstanceForURL(extension2_url)->GetProcess();

  // An isolated app only shares with other instances of itself, not other
  // isolated apps or anything else.
  EXPECT_EQ(isolated1_host, second_isolated1_host);
  EXPECT_NE(isolated1_host, isolated2_host);
  EXPECT_NE(isolated1_host, ntp1_host);
  EXPECT_NE(isolated1_host, hosted1_host);
  EXPECT_NE(isolated1_host, web1_host);
  EXPECT_NE(isolated1_host, extension1_host);
  EXPECT_NE(isolated2_host, ntp1_host);
  EXPECT_NE(isolated2_host, hosted1_host);
  EXPECT_NE(isolated2_host, web1_host);
  EXPECT_NE(isolated2_host, extension1_host);

  // Everything else is clannish.  WebUI only shares with other WebUI.
  EXPECT_EQ(ntp1_host, ntp2_host);
  EXPECT_NE(ntp1_host, hosted1_host);
  EXPECT_NE(ntp1_host, web1_host);
  EXPECT_NE(ntp1_host, extension1_host);

  // Hosted apps only share with each other.
  EXPECT_EQ(hosted1_host, hosted2_host);
  EXPECT_NE(hosted1_host, web1_host);
  EXPECT_NE(hosted1_host, extension1_host);

  // Web pages only share with each other.
  EXPECT_EQ(web1_host, web2_host);
  EXPECT_NE(web1_host, extension1_host);

  // Extensions only share with each other.
  EXPECT_EQ(extension1_host, extension2_host);
}
