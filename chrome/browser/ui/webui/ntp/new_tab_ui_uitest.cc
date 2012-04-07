// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/ui/ui_test.h"

#include "base/test/test_timeouts.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/prefs/pref_value_store.h"
#include "chrome/browser/signin/signin_manager.h"
#include "chrome/browser/ui/webui/ntp/new_tab_ui.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/json_pref_store.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/automation/automation_proxy.h"
#include "chrome/test/automation/browser_proxy.h"
#include "chrome/test/automation/tab_proxy.h"
#include "chrome/test/automation/window_proxy.h"
#include "chrome/test/base/testing_pref_service.h"

class NewTabUITest : public UITest {
 public:
  NewTabUITest() {
    dom_automation_enabled_ = true;
    // Set home page to the empty string so that we can set the home page using
    // preferences.
    set_homepage("");

    // Setup the DEFAULT_THEME profile (has fake history entries).
    set_template_user_data(UITest::ComputeTypicalUserDataSource(
        UITestBase::DEFAULT_THEME));
  }
};

#if defined(OS_WIN)
// Bug 87200: Disable NTPHasThumbnails for Windows
#define MAYBE_NTPHasThumbnails DISABLED_NTPHasThumbnails
#elif defined(OS_LINUX)
// This test is flaky on Linux and CrOS: http://crbug/
#define MAYBE_NTPHasThumbnails FLAKY_NTPHasThumbnails
#else
#define MAYBE_NTPHasThumbnails NTPHasThumbnails
#endif
TEST_F(NewTabUITest, MAYBE_NTPHasThumbnails) {
  // Switch to the "new tab" tab, which should be any new tab after the
  // first (the first is about:blank).
  scoped_refptr<BrowserProxy> window(automation()->GetBrowserWindow(0));
  ASSERT_TRUE(window.get());

  // Bring up a new tab page.
  ASSERT_TRUE(window->RunCommand(IDC_NEW_TAB));

  scoped_refptr<TabProxy> tab = window->GetActiveTab();
  ASSERT_TRUE(tab.get());

  // TopSites should return at least 3 non-filler pages.
  // 8 - 3 = max 5 filler pages.
  ASSERT_TRUE(WaitUntilJavaScriptCondition(tab, L"",
      L"window.domAutomationController.send("
      L"document.getElementsByClassName('filler').length <= 5)",
      TestTimeouts::action_max_timeout_ms()));
}

// Sometimes hangs: http://crbug.com/70157
TEST_F(NewTabUITest, DISABLED_NTPHasLoginName) {
  scoped_refptr<BrowserProxy> window(automation()->GetBrowserWindow(0));
  ASSERT_TRUE(window.get());

  ASSERT_TRUE(window->SetStringPreference(prefs::kGoogleServicesUsername,
                                          "user@gmail.com"));
  // Bring up a new tab page.
  ASSERT_TRUE(window->RunCommand(IDC_NEW_TAB));

  scoped_refptr<TabProxy> tab = window->GetActiveTab();
  ASSERT_TRUE(tab.get());

  std::wstring displayed_username;
  // The login span should be eventually populated and have the
  // correct value.
  ASSERT_TRUE(WaitUntilJavaScriptCondition(tab, L"",
      L"window.domAutomationController.send("
      L"document.getElementById('login-username').innerText.length > 0)",
      TestTimeouts::action_max_timeout_ms()));

  ASSERT_TRUE(tab->ExecuteAndExtractString(
      L"",
      L"window.domAutomationController.send("
      L"document.getElementById('login-username').innerText)",
      &displayed_username));

  EXPECT_EQ(L"user@gmail.com", displayed_username);
}

// Bug 87200: Disable ChromeHangInNTP for Windows
#if defined(OS_WIN)
#define MAYBE_ChromeHangInNTP DISABLED_ChromeHangInNTP
#elif defined(OS_CHROMEOS)
#define MAYBE_ChromeHangInNTP FLAKY_ChromeHangInNTP
#else
#define MAYBE_ChromeHangInNTP ChromeHangInNTP
#endif
// Loads chrome://hang/ into two NTP tabs, ensuring we don't crash.
// See http://crbug.com/59859.
TEST_F(NewTabUITest, MAYBE_ChromeHangInNTP) {
  scoped_refptr<BrowserProxy> window(automation()->GetBrowserWindow(0));
  ASSERT_TRUE(window.get());

  // Bring up a new tab page.
  ASSERT_TRUE(window->RunCommand(IDC_NEW_TAB));
  scoped_refptr<TabProxy> tab = window->GetActiveTab();
  ASSERT_TRUE(tab.get());

  // Navigate to chrome://hang/ to stall the process.
  ASSERT_TRUE(tab->NavigateToURLAsync(GURL(chrome::kChromeUIHangURL)));

  // Visit chrome://hang/ again in another NTP. Don't bother waiting for the
  // NTP to load, because it's hung.
  ASSERT_TRUE(window->RunCommandAsync(IDC_NEW_TAB));
  scoped_refptr<TabProxy> tab2 = window->GetActiveTab();
  ASSERT_TRUE(tab2.get());
  ASSERT_TRUE(tab2->NavigateToURLAsync(GURL(chrome::kChromeUIHangURL)));
}

// Allows testing NTP in process-per-tab mode.
class NewTabUIProcessPerTabTest : public NewTabUITest {
 public:
  NewTabUIProcessPerTabTest() : NewTabUITest() {}

 protected:
  virtual void SetUp() {
    launch_arguments_.AppendSwitch(switches::kProcessPerTab);
    UITest::SetUp();
  }
};

// Bug 87200: Disable NavBeforeNTPCommits for Windows
#if defined(OS_WIN)
#define MAYBE_NavBeforeNTPCommits DISABLED_NavBeforeNTPCommits
#else
#define MAYBE_NavBeforeNTPCommits NavBeforeNTPCommits
#endif
// Navigates away from NTP before it commits, in process-per-tab mode.
// Ensures that we don't load the normal page in the NTP process (and thus
// crash), as in http://crbug.com/69224.
TEST_F(NewTabUIProcessPerTabTest, MAYBE_NavBeforeNTPCommits) {
  scoped_refptr<BrowserProxy> window(automation()->GetBrowserWindow(0));
  ASSERT_TRUE(window.get());

  // Bring up a new tab page.
  ASSERT_TRUE(window->RunCommand(IDC_NEW_TAB));
  scoped_refptr<TabProxy> tab = window->GetActiveTab();
  ASSERT_TRUE(tab.get());

  // Navigate to chrome://hang/ to stall the process.
  ASSERT_TRUE(tab->NavigateToURLAsync(GURL(chrome::kChromeUIHangURL)));

  // Visit a normal URL in another NTP that hasn't committed.
  ASSERT_TRUE(window->RunCommandAsync(IDC_NEW_TAB));
  scoped_refptr<TabProxy> tab2 = window->GetActiveTab();
  ASSERT_TRUE(tab2.get());
  ASSERT_TRUE(tab2->NavigateToURL(GURL("data:text/html,hello world")));
}
