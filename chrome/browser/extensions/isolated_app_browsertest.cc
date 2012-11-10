// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/utf_string_conversions.h"
#include "chrome/browser/automation/automation_util.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/extensions/extension_host.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tab_contents/tab_contents.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test_utils.h"
#include "net/base/mock_host_resolver.h"

using content::ExecuteJavaScript;
using content::ExecuteJavaScriptAndExtractString;
using content::NavigationController;
using content::WebContents;
using content::RenderViewHost;

namespace {

std::wstring WrapForJavascriptAndExtract(
    const wchar_t* javascript_expression) {
  return std::wstring(L"window.domAutomationController.send(") +
      javascript_expression + L")";
}

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

  const extensions::Extension* GetInstalledApp(WebContents* contents) {
    const extensions::Extension* installed_app = NULL;
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
//
// TODO(ajwong): Also test what happens if an app spans multiple sites in its
// extent.  These origins should also be isolated, but still have origin-based
// separation as you would expect.
//
// TODO(ajwong): Add test for session storage. In one tab, navigate to a
// normal page and set X=ss_normal. Then navigate to an isolated URL in the
// same origin and verify X does not exist.  Set X=ss_isolated. Navigate back to
// a normal webpage, and verify X is still ss_normal. Navigate to the isolate
// URL and verify that X is ss_isolated.
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
  WebContents* tab1 = chrome::GetWebContentsAt(browser(), 0);
  WebContents* tab2 = chrome::GetWebContentsAt(browser(), 1);
  WebContents* tab3 = chrome::GetWebContentsAt(browser(), 2);
  ASSERT_TRUE(GetInstalledApp(tab1));
  ASSERT_TRUE(GetInstalledApp(tab2));
  ASSERT_TRUE(!GetInstalledApp(tab3));

  // Check that tabs see cannot each other's localStorage even though they are
  // in the same origin.
  RenderViewHost* app1_rvh = tab1->GetRenderViewHost();
  RenderViewHost* app2_rvh = tab2->GetRenderViewHost();
  RenderViewHost* non_app_rvh = tab3->GetRenderViewHost();
  ASSERT_TRUE(ExecuteJavaScript(
      app1_rvh, L"", L"window.localStorage.setItem('testdata', 'ls_app1');"));
  ASSERT_TRUE(ExecuteJavaScript(
      app2_rvh, L"", L"window.localStorage.setItem('testdata', 'ls_app2');"));
  ASSERT_TRUE(ExecuteJavaScript(
      non_app_rvh, L"",
      L"window.localStorage.setItem('testdata', 'ls_normal');"));

  ASSERT_TRUE(ExecuteJavaScript(
      app1_rvh, L"", L"window.localStorage.getItem('testdata');"));

  const std::wstring& kRetrieveLocalStorage =
      WrapForJavascriptAndExtract(L"window.localStorage.getItem('testdata')");
  std::string result;
  ASSERT_TRUE(ExecuteJavaScriptAndExtractString(
      app1_rvh, L"", kRetrieveLocalStorage.c_str(), &result));
  EXPECT_EQ("ls_app1", result);
  ASSERT_TRUE(ExecuteJavaScriptAndExtractString(
      app2_rvh, L"", kRetrieveLocalStorage.c_str(), &result));
  EXPECT_EQ("ls_app2", result);
  ASSERT_TRUE(ExecuteJavaScriptAndExtractString(
      non_app_rvh, L"", kRetrieveLocalStorage.c_str(), &result));
  EXPECT_EQ("ls_normal", result);

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
  chrome::SelectNumberedTab(browser(), 0);
  content::CrashTab(tab1);
  content::WindowedNotificationObserver observer(
      content::NOTIFICATION_LOAD_STOP,
      content::Source<NavigationController>(
          &chrome::GetActiveWebContents(browser())->GetController()));
  chrome::Reload(browser(), CURRENT_TAB);
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

  // Check that tabs see each other's cookies.
  EXPECT_TRUE(HasCookie(chrome::GetWebContentsAt(browser(), 0), "app2=4"));
  EXPECT_TRUE(HasCookie(chrome::GetWebContentsAt(browser(), 0), "normalPage=5"));
  EXPECT_TRUE(HasCookie(chrome::GetWebContentsAt(browser(), 0), "nonAppFrame=6"));
  EXPECT_TRUE(HasCookie(chrome::GetWebContentsAt(browser(), 1), "app1=3"));
  EXPECT_TRUE(HasCookie(chrome::GetWebContentsAt(browser(), 1), "normalPage=5"));
  EXPECT_TRUE(HasCookie(chrome::GetWebContentsAt(browser(), 1), "nonAppFrame=6"));
  EXPECT_TRUE(HasCookie(chrome::GetWebContentsAt(browser(), 2), "app1=3"));
  EXPECT_TRUE(HasCookie(chrome::GetWebContentsAt(browser(), 2), "app2=4"));
  EXPECT_TRUE(HasCookie(chrome::GetWebContentsAt(browser(), 2), "nonAppFrame=6"));

  // Check that all tabs share the same localStorage if they have the same
  // origin.
  RenderViewHost* app1_rvh =
      chrome::GetWebContentsAt(browser(), 0)->GetRenderViewHost();
  RenderViewHost* app2_rvh =
      chrome::GetWebContentsAt(browser(), 1)->GetRenderViewHost();
  RenderViewHost* non_app_rvh =
      chrome::GetWebContentsAt(browser(), 2)->GetRenderViewHost();
  ASSERT_TRUE(ExecuteJavaScript(
      app1_rvh, L"", L"window.localStorage.setItem('testdata', 'ls_app1');"));
  ASSERT_TRUE(ExecuteJavaScript(
      app2_rvh, L"", L"window.localStorage.setItem('testdata', 'ls_app2');"));
  ASSERT_TRUE(ExecuteJavaScript(
      non_app_rvh, L"",
      L"window.localStorage.setItem('testdata', 'ls_normal');"));

  const std::wstring& kRetrieveLocalStorage =
      WrapForJavascriptAndExtract(L"window.localStorage.getItem('testdata')");
  std::string result;
  ASSERT_TRUE(ExecuteJavaScriptAndExtractString(
      app1_rvh, L"", kRetrieveLocalStorage.c_str(), &result));
  EXPECT_EQ("ls_normal", result);
  ASSERT_TRUE(ExecuteJavaScriptAndExtractString(
      app2_rvh, L"", kRetrieveLocalStorage.c_str(), &result));
  EXPECT_EQ("ls_normal", result);
  ASSERT_TRUE(ExecuteJavaScriptAndExtractString(
      non_app_rvh, L"", kRetrieveLocalStorage.c_str(), &result));
  EXPECT_EQ("ls_normal", result);
}

// Tests that isolated apps processes do not render top-level non-app pages.
// This is true even in the case of the OAuth workaround for hosted apps,
// where non-app popups may be kept in the hosted app process.
IN_PROC_BROWSER_TEST_F(IsolatedAppTest, IsolatedAppProcessModel) {
  host_resolver()->AddRule("*", "127.0.0.1");
  ASSERT_TRUE(test_server()->Start());

  ASSERT_TRUE(LoadExtension(test_data_dir_.AppendASCII("isolated_apps/app1")));

  // The app under test acts on URLs whose host is "localhost",
  // so the URLs we navigate to must have host "localhost".
  GURL base_url = test_server()->GetURL(
      "files/extensions/isolated_apps/");
  GURL::Replacements replace_host;
  std::string host_str("localhost");  // Must stay in scope with replace_host.
  replace_host.SetHostStr(host_str);
  base_url = base_url.ReplaceComponents(replace_host);

  // Create three tabs in the isolated app in different ways.
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), base_url.Resolve("app1/main.html"),
      CURRENT_TAB, ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), base_url.Resolve("app1/main.html"),
      NEW_FOREGROUND_TAB, ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  // For the third tab, use window.open to keep it in process with an opener.
  OpenWindow(chrome::GetWebContentsAt(browser(), 0),
             base_url.Resolve("app1/main.html"), true, NULL);

  // In a fourth tab, use window.open to a non-app URL.  It should open in a
  // separate process, even though this would trigger the OAuth workaround
  // for hosted apps (from http://crbug.com/59285).
  OpenWindow(chrome::GetWebContentsAt(browser(), 0),
             base_url.Resolve("non_app/main.html"), false, NULL);

  // We should now have four tabs, the first and third sharing a process.
  // The second one is an independent instance in a separate process.
  ASSERT_EQ(4, browser()->tab_count());
  int process_id_0 =
      chrome::GetWebContentsAt(browser(), 0)->GetRenderProcessHost()->GetID();
  int process_id_1 =
      chrome::GetWebContentsAt(browser(), 1)->GetRenderProcessHost()->GetID();
  EXPECT_NE(process_id_0, process_id_1);
  EXPECT_EQ(process_id_0,
            chrome::GetWebContentsAt(browser(), 2)->GetRenderProcessHost()->GetID());
  EXPECT_NE(process_id_0,
            chrome::GetWebContentsAt(browser(), 3)->GetRenderProcessHost()->GetID());

  // Navigating the second tab out of the app should cause a process swap.
  const GURL& non_app_url(base_url.Resolve("non_app/main.html"));
  NavigateInRenderer(chrome::GetWebContentsAt(browser(), 1), non_app_url);
  EXPECT_NE(process_id_1,
            chrome::GetWebContentsAt(browser(), 1)->GetRenderProcessHost()->GetID());
}
