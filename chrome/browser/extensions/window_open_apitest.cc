// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/test/ui_test_utils.h"
#include "net/base/mock_host_resolver.h"

// Disabled, http://crbug.com/64899.
IN_PROC_BROWSER_TEST_F(ExtensionApiTest, DISABLED_WindowOpen) {
  CommandLine::ForCurrentProcess()->AppendSwitch(
      switches::kEnableExperimentalExtensionApis);

  ResultCatcher catcher;
  ASSERT_TRUE(LoadExtensionIncognito(test_data_dir_
      .AppendASCII("window_open").AppendASCII("spanning")));
  EXPECT_TRUE(catcher.GetNextResult()) << catcher.message();
}

void WaitForTabsAndPopups(Browser* browser, int num_tabs, int num_popups) {
  // We start with one tab and one browser already open.
  ++num_tabs;
  size_t num_browsers = static_cast<size_t>(num_popups) + 1;

  const base::TimeDelta kWaitTime = base::TimeDelta::FromSeconds(15);
  base::TimeTicks end_time = base::TimeTicks::Now() + kWaitTime;
  while (base::TimeTicks::Now() < end_time) {
    if (BrowserList::GetBrowserCount(browser->profile()) >= num_browsers &&
        browser->tab_count() >= num_tabs)
      break;

    MessageLoopForUI::current()->RunAllPending();
  }

  EXPECT_EQ(num_browsers, BrowserList::GetBrowserCount(browser->profile()));
  EXPECT_EQ(num_tabs, browser->tab_count());

  for (BrowserList::const_iterator iter = BrowserList::begin();
       iter != BrowserList::end(); ++iter) {
    if (*iter == browser)
      continue;

    // Check for TYPE_POPUP or TYPE_PANEL.
    EXPECT_TRUE((*iter)->is_type_popup() || (*iter)->is_type_panel());
  }
}

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, BrowserIsApp) {
  host_resolver()->AddRule("a.com", "127.0.0.1");
  ASSERT_TRUE(StartTestServer());
  ASSERT_TRUE(LoadExtension(
      test_data_dir_.AppendASCII("window_open").AppendASCII("browser_is_app")));

  WaitForTabsAndPopups(browser(), 0, 2);

  for (BrowserList::const_iterator iter = BrowserList::begin();
       iter != BrowserList::end(); ++iter) {
    if (*iter == browser())
      ASSERT_FALSE((*iter)->is_app());
    else
      ASSERT_TRUE((*iter)->is_app());
  }
}

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, WindowOpenPopupDefault) {
  ASSERT_TRUE(StartTestServer());
  ASSERT_TRUE(LoadExtension(
      test_data_dir_.AppendASCII("window_open").AppendASCII("popup")));

  const int num_tabs = 1;
  const int num_popups = 0;
  WaitForTabsAndPopups(browser(), num_tabs, num_popups);
}

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, WindowOpenPopupLarge) {
  ASSERT_TRUE(StartTestServer());
  ASSERT_TRUE(LoadExtension(
      test_data_dir_.AppendASCII("window_open").AppendASCII("popup_large")));

#if defined(OS_CHROMEOS)
  // On ChromeOS this should open a new tab.
  const int num_tabs = 1;
  const int num_popups = 0;
#else
  // On other systems this should open a new popup window.
  const int num_tabs = 0;
  const int num_popups = 1;
#endif
  WaitForTabsAndPopups(browser(), num_tabs, num_popups);
}

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, WindowOpenPopupSmall) {
  ASSERT_TRUE(StartTestServer());
  ASSERT_TRUE(LoadExtension(
      test_data_dir_.AppendASCII("window_open").AppendASCII("popup_small")));

  // On ChromeOS this should open a new panel (acts like a new popup window).
  // On other systems this should open a new popup window.
  const int num_tabs = 0;
  const int num_popups = 1;
  WaitForTabsAndPopups(browser(), num_tabs, num_popups);
}

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, PopupBlockingExtension) {
  host_resolver()->AddRule("*", "127.0.0.1");
  ASSERT_TRUE(StartTestServer());

  ASSERT_TRUE(LoadExtension(
      test_data_dir_.AppendASCII("window_open").AppendASCII("popup_blocking")
      .AppendASCII("extension")));

  WaitForTabsAndPopups(browser(), 5, 3);
}

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, PopupBlockingHostedApp) {
  host_resolver()->AddRule("*", "127.0.0.1");
  ASSERT_TRUE(test_server()->Start());

  ASSERT_TRUE(LoadExtension(
      test_data_dir_.AppendASCII("window_open").AppendASCII("popup_blocking")
      .AppendASCII("hosted_app")));

  // The app being tested owns the domain a.com .  The test URLs we navigate
  // to below must be within that domain, so that they fall within the app's
  // web extent.
  GURL::Replacements replace_host;
  std::string a_dot_com = "a.com";
  replace_host.SetHostStr(a_dot_com);

  const std::string popup_app_contents_path(
    "files/extensions/api_test/window_open/popup_blocking/hosted_app/");

  GURL open_tab =
      test_server()->GetURL(popup_app_contents_path + "open_tab.html")
          .ReplaceComponents(replace_host);
  GURL open_popup =
      test_server()->GetURL(popup_app_contents_path + "open_popup.html")
          .ReplaceComponents(replace_host);

  browser()->OpenURL(open_tab, GURL(), NEW_FOREGROUND_TAB,
                     PageTransition::TYPED);
  browser()->OpenURL(open_popup, GURL(), NEW_FOREGROUND_TAB,
                     PageTransition::TYPED);

  WaitForTabsAndPopups(browser(), 3, 1);
}

#if defined(OS_MACOSX) || defined(OS_WIN)
// Focus test fails if there is no window manager on Linux.
IN_PROC_BROWSER_TEST_F(ExtensionApiTest, WindowOpenFocus) {
  ASSERT_TRUE(RunExtensionTest("window_open/focus")) << message_;
}
#endif

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, WindowArgumentsOverflow) {
  ASSERT_TRUE(RunExtensionTest("window_open/argument_overflow")) << message_;
}

class WindowOpenPanelTest : public ExtensionApiTest {
  virtual void SetUpCommandLine(CommandLine* command_line) {
    ExtensionApiTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch(switches::kEnableExperimentalExtensionApis);
    command_line->AppendSwitch(switches::kEnablePanels);
  }
};

// TODO(jianli): Enable it when Panel::CreateNativePanel is implemented.
IN_PROC_BROWSER_TEST_F(WindowOpenPanelTest, DISABLED_WindowOpenPanel) {
  ASSERT_TRUE(RunExtensionTest("window_open/panel")) << message_;
}
