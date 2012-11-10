// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "chrome/browser/extensions/api/web_request/web_request_api.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_test_message_listener.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/login/login_prompt.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test_utils.h"
#include "net/base/mock_host_resolver.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebInputEvent.h"

using content::WebContents;

namespace {

class CancelLoginDialog : public content::NotificationObserver {
 public:
  CancelLoginDialog() {
    registrar_.Add(this,
                   chrome::NOTIFICATION_AUTH_NEEDED,
                   content::NotificationService::AllSources());
  }

  virtual ~CancelLoginDialog() {}

  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) {
    LoginHandler* handler =
        content::Details<LoginNotificationDetails>(details).ptr()->handler();
    handler->CancelAuth();
  }

 private:
  content::NotificationRegistrar registrar_;

 DISALLOW_COPY_AND_ASSIGN(CancelLoginDialog);
};

}  // namespace

class ExtensionWebRequestApiTest : public ExtensionApiTest {
 public:
  virtual void SetUpInProcessBrowserTestFixture() {
    // TODO(battre): remove this when declarative webRequest API becomes stable.
    CommandLine::ForCurrentProcess()->AppendSwitch(
        switches::kEnableExperimentalExtensionApis);

    ExtensionApiTest::SetUpInProcessBrowserTestFixture();
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(StartTestServer());
  }

  void RunPermissionTest(
      const char* extension_directory,
      bool load_extension_with_incognito_permission,
      bool wait_for_extension_loaded_in_incognito,
      const char* expected_content_regular_window,
      const char* exptected_content_incognito_window);
};

IN_PROC_BROWSER_TEST_F(ExtensionWebRequestApiTest, WebRequestApi) {
  ASSERT_TRUE(RunExtensionSubtest("webrequest", "test_api.html")) << message_;
}

IN_PROC_BROWSER_TEST_F(ExtensionWebRequestApiTest, WebRequestSimple) {
  ASSERT_TRUE(RunExtensionSubtest("webrequest", "test_simple.html")) <<
      message_;
}

IN_PROC_BROWSER_TEST_F(ExtensionWebRequestApiTest, WebRequestComplex) {
  ASSERT_TRUE(RunExtensionSubtest("webrequest", "test_complex.html")) <<
      message_;
}

// Flaky (sometimes crash): http://crbug.com/140976
IN_PROC_BROWSER_TEST_F(ExtensionWebRequestApiTest,
                       DISABLED_WebRequestAuthRequired) {
  CancelLoginDialog login_dialog_helper;

  ASSERT_TRUE(RunExtensionSubtest("webrequest", "test_auth_required.html")) <<
      message_;
}

// This test times out regularly on win_rel trybots. See http://crbug.com/122178
#if defined(OS_WIN)
#define MAYBE_WebRequestBlocking DISABLED_WebRequestBlocking
#else
#define MAYBE_WebRequestBlocking WebRequestBlocking
#endif
IN_PROC_BROWSER_TEST_F(ExtensionWebRequestApiTest, MAYBE_WebRequestBlocking) {
  ASSERT_TRUE(RunExtensionSubtest("webrequest", "test_blocking.html")) <<
      message_;
}

IN_PROC_BROWSER_TEST_F(ExtensionWebRequestApiTest, WebRequestNewTab) {
  // Wait for the extension to set itself up and return control to us.
  ASSERT_TRUE(RunExtensionSubtest("webrequest", "test_newTab.html"))
      << message_;

  WebContents* tab = chrome::GetActiveWebContents(browser());
  content::WaitForLoadStop(tab);

  ResultCatcher catcher;

  ExtensionService* service = browser()->profile()->GetExtensionService();
  const extensions::Extension* extension =
      service->GetExtensionById(last_loaded_extension_id_, false);
  GURL url = extension->GetResourceURL("newTab/a.html");

  ui_test_utils::NavigateToURL(browser(), url);

  // There's a link on a.html with target=_blank. Click on it to open it in a
  // new tab.
  WebKit::WebMouseEvent mouse_event;
  mouse_event.type = WebKit::WebInputEvent::MouseDown;
  mouse_event.button = WebKit::WebMouseEvent::ButtonLeft;
  mouse_event.x = 7;
  mouse_event.y = 7;
  mouse_event.clickCount = 1;
  tab->GetRenderViewHost()->ForwardMouseEvent(mouse_event);
  mouse_event.type = WebKit::WebInputEvent::MouseUp;
  tab->GetRenderViewHost()->ForwardMouseEvent(mouse_event);

  ASSERT_TRUE(catcher.GetNextResult()) << catcher.message();
}

IN_PROC_BROWSER_TEST_F(ExtensionWebRequestApiTest, WebRequestDeclarative) {
  ASSERT_TRUE(RunExtensionSubtest("webrequest", "test_declarative.html")) <<
      message_;
}

void ExtensionWebRequestApiTest::RunPermissionTest(
    const char* extension_directory,
    bool load_extension_with_incognito_permission,
    bool wait_for_extension_loaded_in_incognito,
    const char* expected_content_regular_window,
    const char* exptected_content_incognito_window) {
  ResultCatcher catcher;
  catcher.RestrictToProfile(browser()->profile());
  ResultCatcher catcher_incognito;
  catcher_incognito.RestrictToProfile(
      browser()->profile()->GetOffTheRecordProfile());

  ExtensionTestMessageListener listener("done", true);
  ExtensionTestMessageListener listener_incognito("done_incognito", true);

  ASSERT_TRUE(LoadExtensionWithOptions(
      test_data_dir_.AppendASCII("webrequest_permissions")
                    .AppendASCII(extension_directory),
      load_extension_with_incognito_permission,
      false));

  // Test that navigation in regular window is properly redirected.
  EXPECT_TRUE(listener.WaitUntilSatisfied());

  // This navigation should be redirected.
  ui_test_utils::NavigateToURL(
      browser(),
      test_server()->GetURL("files/extensions/test_file.html"));

  std::string body;
  WebContents* tab = chrome::GetActiveWebContents(browser());
  ASSERT_TRUE(content::ExecuteJavaScriptAndExtractString(
        tab->GetRenderViewHost(), L"",
        L"window.domAutomationController.send(document.body.textContent)",
        &body));
  EXPECT_EQ(expected_content_regular_window, body);

  // Test that navigation in OTR window is properly redirected.
  Browser* otr_browser = ui_test_utils::OpenURLOffTheRecord(
      browser()->profile(), GURL("about:blank"));

  if (wait_for_extension_loaded_in_incognito)
    EXPECT_TRUE(listener_incognito.WaitUntilSatisfied());

  // This navigation should be redirected if
  // load_extension_with_incognito_permission is true.
  ui_test_utils::NavigateToURL(
      otr_browser,
      test_server()->GetURL("files/extensions/test_file.html"));

  body.clear();
  WebContents* otr_tab = chrome::GetActiveWebContents(otr_browser);
  ASSERT_TRUE(content::ExecuteJavaScriptAndExtractString(
      otr_tab->GetRenderViewHost(), L"",
      L"window.domAutomationController.send(document.body.textContent)",
      &body));
  EXPECT_EQ(exptected_content_incognito_window, body);
}

IN_PROC_BROWSER_TEST_F(ExtensionWebRequestApiTest,
                       WebRequestDeclarativePermissionSpanning1) {
  // Test spanning with incognito permission.
  RunPermissionTest("spanning", true, false, "redirected1", "redirected1");
}

IN_PROC_BROWSER_TEST_F(ExtensionWebRequestApiTest,
                       WebRequestDeclarativePermissionSpanning2) {
  // Test spanning without incognito permission.
  RunPermissionTest("spanning", false, false, "redirected1", "");
}


IN_PROC_BROWSER_TEST_F(ExtensionWebRequestApiTest,
                       WebRequestDeclarativePermissionSplit1) {
  // Test split with incognito permission.
  RunPermissionTest("split", true, true, "redirected1", "redirected2");
}

IN_PROC_BROWSER_TEST_F(ExtensionWebRequestApiTest,
                       WebRequestDeclarativePermissionSplit2) {
  // Test split without incognito permission.
  RunPermissionTest("split", false, false, "redirected1", "");
}
