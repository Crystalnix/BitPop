// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/compiler_specific.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/content_settings/host_content_settings_map.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/fullscreen/fullscreen_controller.h"
#include "chrome/browser/ui/fullscreen/fullscreen_controller_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/url_constants.h"
#include "content/public/test/test_navigation_observer.h"
#if defined(OS_MACOSX)
#include "base/mac/mac_util.h"
#endif

using content::WebContents;

namespace {

const FilePath::CharType* kSimpleFile = FILE_PATH_LITERAL("simple.html");

}  // namespace

class FullscreenControllerBrowserTest: public FullscreenControllerTest {
 protected:
  void TestFullscreenMouseLockContentSettings();
};

#if defined(OS_MACOSX)
// http://crbug.com/104265
#define MAYBE_TestNewTabExitsFullscreen DISABLED_TestNewTabExitsFullscreen
#elif defined(OS_LINUX)
// http://crbug.com/137657
#define MAYBE_TestNewTabExitsFullscreen DISABLED_TestNewTabExitsFullscreen
#else
#define MAYBE_TestNewTabExitsFullscreen TestNewTabExitsFullscreen
#endif

// Tests that while in fullscreen creating a new tab will exit fullscreen.
IN_PROC_BROWSER_TEST_F(FullscreenControllerTest,
                       MAYBE_TestNewTabExitsFullscreen) {
  ASSERT_TRUE(test_server()->Start());

  AddTabAtIndexAndWait(
      0, GURL(chrome::kAboutBlankURL), content::PAGE_TRANSITION_TYPED);

  ASSERT_NO_FATAL_FAILURE(ToggleTabFullscreen(true));

  {
    FullscreenNotificationObserver fullscreen_observer;
    AddTabAtIndexAndWait(
        1, GURL(chrome::kAboutBlankURL), content::PAGE_TRANSITION_TYPED);
    fullscreen_observer.Wait();
    ASSERT_FALSE(browser()->window()->IsFullscreen());
  }
}

#if defined(OS_MACOSX)
// http://crbug.com/100467
#define MAYBE_TestTabExitsItselfFromFullscreen \
        FAILS_TestTabExitsItselfFromFullscreen
#else
#define MAYBE_TestTabExitsItselfFromFullscreen TestTabExitsItselfFromFullscreen
#endif

// Tests a tab exiting fullscreen will bring the browser out of fullscreen.
IN_PROC_BROWSER_TEST_F(FullscreenControllerTest,
                       MAYBE_TestTabExitsItselfFromFullscreen) {
  ASSERT_TRUE(test_server()->Start());

  AddTabAtIndexAndWait(
      0, GURL(chrome::kAboutBlankURL), content::PAGE_TRANSITION_TYPED);

  ASSERT_NO_FATAL_FAILURE(ToggleTabFullscreen(true));
  ASSERT_NO_FATAL_FAILURE(ToggleTabFullscreen(false));
}

// Tests entering fullscreen and then requesting mouse lock results in
// buttons for the user, and that after confirming the buttons are dismissed.
IN_PROC_BROWSER_TEST_F(FullscreenControllerTest,
                       TestFullscreenBubbleMouseLockState) {
  ASSERT_TRUE(test_server()->Start());

  AddTabAtIndexAndWait(0, GURL(chrome::kAboutBlankURL),
                content::PAGE_TRANSITION_TYPED);
  AddTabAtIndexAndWait(1, GURL(chrome::kAboutBlankURL),
                content::PAGE_TRANSITION_TYPED);

  ASSERT_NO_FATAL_FAILURE(ToggleTabFullscreen(true));

  // Request mouse lock and verify the bubble is waiting for user confirmation.
  RequestToLockMouse(true, false);
  ASSERT_TRUE(IsMouseLockPermissionRequested());

  // Accept mouse lock and verify bubble no longer shows confirmation buttons.
  AcceptCurrentFullscreenOrMouseLockRequest();
  ASSERT_FALSE(IsFullscreenBubbleDisplayingButtons());
}

// Helper method to be called by multiple tests.
// Tests Fullscreen and Mouse Lock with varying content settings ALLOW & BLOCK.
void FullscreenControllerBrowserTest::TestFullscreenMouseLockContentSettings() {
  GURL url = test_server()->GetURL("simple.html");
  AddTabAtIndexAndWait(0, url, content::PAGE_TRANSITION_TYPED);

  // Validate that going fullscreen for a URL defaults to asking permision.
  ASSERT_FALSE(IsFullscreenPermissionRequested());
  ASSERT_NO_FATAL_FAILURE(ToggleTabFullscreen(true));
  ASSERT_TRUE(IsFullscreenPermissionRequested());
  ASSERT_NO_FATAL_FAILURE(ToggleTabFullscreen(false));

  // Add content setting to ALLOW fullscreen.
  HostContentSettingsMap* settings_map =
      browser()->profile()->GetHostContentSettingsMap();
  ContentSettingsPattern pattern =
      ContentSettingsPattern::FromURL(url);
  settings_map->SetContentSetting(
      pattern, ContentSettingsPattern::Wildcard(),
      CONTENT_SETTINGS_TYPE_FULLSCREEN, std::string(),
      CONTENT_SETTING_ALLOW);

  // Now, fullscreen should not prompt for permission.
  ASSERT_FALSE(IsFullscreenPermissionRequested());
  ASSERT_NO_FATAL_FAILURE(ToggleTabFullscreen(true));
  ASSERT_FALSE(IsFullscreenPermissionRequested());

  // Leaving tab in fullscreen, now test mouse lock ALLOW:

  // Validate that mouse lock defaults to asking permision.
  ASSERT_FALSE(IsMouseLockPermissionRequested());
  RequestToLockMouse(true, false);
  ASSERT_TRUE(IsMouseLockPermissionRequested());
  LostMouseLock();

  // Add content setting to ALLOW mouse lock.
  settings_map->SetContentSetting(
      pattern, ContentSettingsPattern::Wildcard(),
      CONTENT_SETTINGS_TYPE_MOUSELOCK, std::string(),
      CONTENT_SETTING_ALLOW);

  // Now, mouse lock should not prompt for permission.
  ASSERT_FALSE(IsMouseLockPermissionRequested());
  RequestToLockMouse(true, false);
  ASSERT_FALSE(IsMouseLockPermissionRequested());
  LostMouseLock();

  // Leaving tab in fullscreen, now test mouse lock BLOCK:

  // Add content setting to BLOCK mouse lock.
  settings_map->SetContentSetting(
      pattern, ContentSettingsPattern::Wildcard(),
      CONTENT_SETTINGS_TYPE_MOUSELOCK, std::string(),
      CONTENT_SETTING_BLOCK);

  // Now, mouse lock should not be pending.
  ASSERT_FALSE(IsMouseLockPermissionRequested());
  RequestToLockMouse(true, false);
  ASSERT_FALSE(IsMouseLockPermissionRequested());
}

#if defined(OS_MACOSX) || defined(OS_LINUX)
// http://crbug.com/133831
#define MAYBE_FullscreenMouseLockContentSettings \
    FLAKY_FullscreenMouseLockContentSettings
#else
#define MAYBE_FullscreenMouseLockContentSettings \
    FullscreenMouseLockContentSettings
#endif

// Tests fullscreen and Mouse Lock with varying content settings ALLOW & BLOCK.
IN_PROC_BROWSER_TEST_F(FullscreenControllerBrowserTest,
                       MAYBE_FullscreenMouseLockContentSettings) {
  TestFullscreenMouseLockContentSettings();
}

// Tests fullscreen and Mouse Lock with varying content settings ALLOW & BLOCK,
// but with the browser initiated in fullscreen mode first.
IN_PROC_BROWSER_TEST_F(FullscreenControllerBrowserTest,
                       BrowserFullscreenMouseLockContentSettings) {
  // Enter browser fullscreen first.
  ASSERT_NO_FATAL_FAILURE(ToggleBrowserFullscreen(true));
  TestFullscreenMouseLockContentSettings();
  ASSERT_NO_FATAL_FAILURE(ToggleBrowserFullscreen(false));
}

// Tests Fullscreen entered in Browser, then Tab mode, then exited via Browser.
IN_PROC_BROWSER_TEST_F(FullscreenControllerTest, BrowserFullscreenExit) {
  // Enter browser fullscreen.
  ASSERT_NO_FATAL_FAILURE(ToggleBrowserFullscreen(true));

  // Enter tab fullscreen.
  AddTabAtIndexAndWait(0, GURL(chrome::kAboutBlankURL),
                content::PAGE_TRANSITION_TYPED);
  ASSERT_NO_FATAL_FAILURE(ToggleTabFullscreen(true));

  // Exit browser fullscreen.
  ASSERT_NO_FATAL_FAILURE(ToggleBrowserFullscreen(false));
  ASSERT_FALSE(browser()->window()->IsFullscreen());
}

// Tests Browser Fullscreen remains active after Tab mode entered and exited.
IN_PROC_BROWSER_TEST_F(FullscreenControllerTest,
                       BrowserFullscreenAfterTabFSExit) {
  // Enter browser fullscreen.
  ASSERT_NO_FATAL_FAILURE(ToggleBrowserFullscreen(true));

  // Enter and then exit tab fullscreen.
  AddTabAtIndexAndWait(0, GURL(chrome::kAboutBlankURL),
                content::PAGE_TRANSITION_TYPED);
  ASSERT_NO_FATAL_FAILURE(ToggleTabFullscreen(true));
  ASSERT_NO_FATAL_FAILURE(ToggleTabFullscreen(false));

  // Verify browser fullscreen still active.
  ASSERT_TRUE(IsFullscreenForBrowser());
}

// Tests fullscreen entered without permision prompt for file:// urls.
IN_PROC_BROWSER_TEST_F(FullscreenControllerTest, FullscreenFileURL) {
  ui_test_utils::NavigateToURL(browser(),
      ui_test_utils::GetTestUrl(FilePath(FilePath::kCurrentDirectory),
                                FilePath(kSimpleFile)));

  // Validate that going fullscreen for a file does not ask permision.
  ASSERT_FALSE(IsFullscreenPermissionRequested());
  ASSERT_NO_FATAL_FAILURE(ToggleTabFullscreen(true));
  ASSERT_FALSE(IsFullscreenPermissionRequested());
  ASSERT_NO_FATAL_FAILURE(ToggleTabFullscreen(false));
}

// Tests fullscreen is exited on page navigation.
// (Similar to mouse lock version in FullscreenControllerInteractiveTest)
IN_PROC_BROWSER_TEST_F(FullscreenControllerTest,
                       TestTabExitsFullscreenOnNavigation) {
  ASSERT_TRUE(test_server()->Start());

  ui_test_utils::NavigateToURL(browser(), GURL("about:blank"));
  ASSERT_NO_FATAL_FAILURE(ToggleTabFullscreen(true));
  ui_test_utils::NavigateToURL(browser(), GURL("chrome://newtab"));

  ASSERT_FALSE(browser()->window()->IsFullscreen());
}

// Tests fullscreen is exited when navigating back.
// (Similar to mouse lock version in FullscreenControllerInteractiveTest)
IN_PROC_BROWSER_TEST_F(FullscreenControllerTest,
                       TestTabExitsFullscreenOnGoBack) {
  ASSERT_TRUE(test_server()->Start());

  ui_test_utils::NavigateToURL(browser(), GURL("about:blank"));
  ui_test_utils::NavigateToURL(browser(), GURL("chrome://newtab"));

  ASSERT_NO_FATAL_FAILURE(ToggleTabFullscreen(true));

  GoBack();

  ASSERT_FALSE(browser()->window()->IsFullscreen());
}

// Tests fullscreen is not exited on sub frame navigation.
// (Similar to mouse lock version in FullscreenControllerInteractiveTest)
IN_PROC_BROWSER_TEST_F(FullscreenControllerTest,
                       TestTabDoesntExitFullscreenOnSubFrameNavigation) {
  ASSERT_TRUE(test_server()->Start());

  GURL url(ui_test_utils::GetTestUrl(FilePath(FilePath::kCurrentDirectory),
                                     FilePath(kSimpleFile)));
  GURL url_with_fragment(url.spec() + "#fragment");

  ui_test_utils::NavigateToURL(browser(), url);
  ASSERT_NO_FATAL_FAILURE(ToggleTabFullscreen(true));
  ui_test_utils::NavigateToURL(browser(), url_with_fragment);
  ASSERT_TRUE(IsFullscreenForTabOrPending());
}

// Tests tab fullscreen exits, but browser fullscreen remains, on navigation.
IN_PROC_BROWSER_TEST_F(FullscreenControllerTest,
                       TestFullscreenFromTabWhenAlreadyInBrowserFullscreenWorks) {
  ASSERT_TRUE(test_server()->Start());

  ui_test_utils::NavigateToURL(browser(), GURL("about:blank"));
  ui_test_utils::NavigateToURL(browser(), GURL("chrome://newtab"));

  ASSERT_NO_FATAL_FAILURE(ToggleBrowserFullscreen(true));
  ASSERT_NO_FATAL_FAILURE(ToggleTabFullscreen(true));

  GoBack();

  ASSERT_TRUE(IsFullscreenForBrowser());
  ASSERT_FALSE(IsFullscreenForTabOrPending());
}

#if defined(OS_MACOSX)
// http://crbug.com/100467
IN_PROC_BROWSER_TEST_F(
    FullscreenControllerTest, FAILS_TabEntersPresentationModeFromWindowed) {
  ASSERT_TRUE(test_server()->Start());

  AddTabAtIndexAndWait(
      0, GURL(chrome::kAboutBlankURL), content::PAGE_TRANSITION_TYPED);

  WebContents* tab = chrome::GetActiveWebContents(browser());

  {
    FullscreenNotificationObserver fullscreen_observer;
    EXPECT_FALSE(browser()->window()->IsFullscreen());
    EXPECT_FALSE(browser()->window()->InPresentationMode());
    browser()->ToggleFullscreenModeForTab(tab, true);
    fullscreen_observer.Wait();
    ASSERT_TRUE(browser()->window()->IsFullscreen());
    ASSERT_TRUE(browser()->window()->InPresentationMode());
  }

  {
    FullscreenNotificationObserver fullscreen_observer;
    browser()->TogglePresentationMode();
    fullscreen_observer.Wait();
    ASSERT_FALSE(browser()->window()->IsFullscreen());
    ASSERT_FALSE(browser()->window()->InPresentationMode());
  }

  if (base::mac::IsOSLionOrLater()) {
    // Test that tab fullscreen mode doesn't make presentation mode the default
    // on Lion.
    FullscreenNotificationObserver fullscreen_observer;
    chrome::ToggleFullscreenMode(browser());
    fullscreen_observer.Wait();
    ASSERT_TRUE(browser()->window()->IsFullscreen());
    ASSERT_FALSE(browser()->window()->InPresentationMode());
  }
}
#endif

IN_PROC_BROWSER_TEST_F(FullscreenControllerTest,
                       PendingMouseLockExitsOnTabSwitch) {
  AddTabAtIndexAndWait(0, GURL(chrome::kAboutBlankURL),
                       content::PAGE_TRANSITION_TYPED);
  AddTabAtIndexAndWait(0, GURL(chrome::kAboutBlankURL),
                       content::PAGE_TRANSITION_TYPED);
  WebContents* tab1 = chrome::GetActiveWebContents(browser());

  // Request mouse lock. Bubble is displayed.
  RequestToLockMouse(true, false);
  ASSERT_TRUE(IsFullscreenBubbleDisplayed());

  // Activate current tab. Mouse lock bubble remains.
  chrome::ActivateTabAt(browser(), 0, true);
  ASSERT_TRUE(IsFullscreenBubbleDisplayed());

  // Activate second tab. Mouse lock bubble clears.
  {
    MouseLockNotificationObserver mouse_lock_observer;
    chrome::ActivateTabAt(browser(), 1, true);
    mouse_lock_observer.Wait();
  }
  ASSERT_FALSE(IsFullscreenBubbleDisplayed());

  // Now, test that closing an unrelated tab does not disturb a request.

  // Request mouse lock. Bubble is displayed.
  RequestToLockMouse(true, false);
  ASSERT_TRUE(IsFullscreenBubbleDisplayed());

  // Close first tab while second active. Mouse lock bubble remains.
  chrome::CloseWebContents(browser(), tab1);
  ASSERT_TRUE(IsFullscreenBubbleDisplayed());
}

IN_PROC_BROWSER_TEST_F(FullscreenControllerTest,
                       PendingMouseLockExitsOnTabClose) {
  // Add more tabs.
  AddTabAtIndexAndWait(0, GURL(chrome::kAboutBlankURL),
                       content::PAGE_TRANSITION_TYPED);
  AddTabAtIndexAndWait(0, GURL(chrome::kAboutBlankURL),
                       content::PAGE_TRANSITION_TYPED);

  // Request mouse lock. Bubble is displayed.
  RequestToLockMouse(true, false);
  ASSERT_TRUE(IsFullscreenBubbleDisplayed());

  // Close tab. Bubble is cleared.
  {
    MouseLockNotificationObserver mouse_lock_observer;
    chrome::CloseTab(browser());
    mouse_lock_observer.Wait();
  }
  ASSERT_FALSE(IsFullscreenBubbleDisplayed());
}
