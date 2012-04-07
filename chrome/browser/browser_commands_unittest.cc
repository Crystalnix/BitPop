// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/test/test_browser_thread.h"

typedef BrowserWithTestWindowTest BrowserCommandsTest;

using content::OpenURLParams;
using content::Referrer;
using content::WebContents;

// Tests IDC_SELECT_TAB_0, IDC_SELECT_NEXT_TAB, IDC_SELECT_PREVIOUS_TAB and
// IDC_SELECT_LAST_TAB.
TEST_F(BrowserCommandsTest, TabNavigationAccelerators) {
  GURL about_blank(chrome::kAboutBlankURL);

  // Create three tabs.
  AddTab(browser(), about_blank);
  AddTab(browser(), about_blank);
  AddTab(browser(), about_blank);

  // Select the second tab.
  browser()->ActivateTabAt(1, false);

  // Navigate to the first tab using an accelerator.
  browser()->ExecuteCommand(IDC_SELECT_TAB_0);
  ASSERT_EQ(0, browser()->active_index());

  // Navigate to the second tab using the next accelerators.
  browser()->ExecuteCommand(IDC_SELECT_NEXT_TAB);
  ASSERT_EQ(1, browser()->active_index());

  // Navigate back to the first tab using the previous accelerators.
  browser()->ExecuteCommand(IDC_SELECT_PREVIOUS_TAB);
  ASSERT_EQ(0, browser()->active_index());

  // Navigate to the last tab using the select last accelerator.
  browser()->ExecuteCommand(IDC_SELECT_LAST_TAB);
  ASSERT_EQ(2, browser()->active_index());
}

// Tests IDC_DUPLICATE_TAB.
TEST_F(BrowserCommandsTest, DuplicateTab) {
  GURL url1("http://foo/1");
  GURL url2("http://foo/2");
  GURL url3("http://foo/3");

  // Navigate to the three urls, then go back.
  AddTab(browser(), url1);
  NavigateAndCommitActiveTab(url2);
  NavigateAndCommitActiveTab(url3);

  size_t initial_window_count = BrowserList::size();

  // Duplicate the tab.
  browser()->ExecuteCommand(IDC_DUPLICATE_TAB);

  // The duplicated tab should not end up in a new window.
  size_t window_count = BrowserList::size();
  ASSERT_EQ(initial_window_count, window_count);

  // And we should have a newly duplicated tab.
  ASSERT_EQ(2, browser()->tab_count());

  // Verify the stack of urls.
  content::NavigationController& controller =
      browser()->GetWebContentsAt(1)->GetController();
  ASSERT_EQ(3, controller.GetEntryCount());
  ASSERT_EQ(2, controller.GetCurrentEntryIndex());
  ASSERT_TRUE(url1 == controller.GetEntryAtIndex(0)->GetURL());
  ASSERT_TRUE(url2 == controller.GetEntryAtIndex(1)->GetURL());
  ASSERT_TRUE(url3 == controller.GetEntryAtIndex(2)->GetURL());
}

TEST_F(BrowserCommandsTest, BookmarkCurrentPage) {
  // We use profile() here, since it's a TestingProfile.
  profile()->CreateBookmarkModel(true);
  profile()->BlockUntilBookmarkModelLoaded();

  // Navigate to a url.
  GURL url1("http://foo/1");
  AddTab(browser(), url1);
  browser()->OpenURL(OpenURLParams(
      url1, Referrer(), CURRENT_TAB, content::PAGE_TRANSITION_TYPED, false));

  // TODO(beng): remove this once we can use TabContentses directly in testing
  //             instead of the TestTabContents which causes this command not to
  //             be enabled when the tab is added (and selected).
  browser()->command_updater()->UpdateCommandEnabled(IDC_BOOKMARK_PAGE, true);

  // Star it.
  browser()->ExecuteCommand(IDC_BOOKMARK_PAGE);

  // It should now be bookmarked in the bookmark model.
  EXPECT_EQ(profile(), browser()->profile());
  EXPECT_TRUE(browser()->profile()->GetBookmarkModel()->IsBookmarked(url1));
}

// Tests back/forward in new tab (Control + Back/Forward button in the UI).
TEST_F(BrowserCommandsTest, BackForwardInNewTab) {
  GURL url1("http://foo/1");
  GURL url2("http://foo/2");

  // Make a tab with the two pages navigated in it.
  AddTab(browser(), url1);
  NavigateAndCommitActiveTab(url2);

  // Go back in a new background tab.
  browser()->GoBack(NEW_BACKGROUND_TAB);
  EXPECT_EQ(0, browser()->active_index());
  ASSERT_EQ(2, browser()->tab_count());

  // The original tab should be unchanged.
  WebContents* zeroth = browser()->GetWebContentsAt(0);
  EXPECT_EQ(url2, zeroth->GetURL());
  EXPECT_TRUE(zeroth->GetController().CanGoBack());
  EXPECT_FALSE(zeroth->GetController().CanGoForward());

  // The new tab should be like the first one but navigated back.
  WebContents* first = browser()->GetWebContentsAt(1);
  EXPECT_EQ(url1, browser()->GetWebContentsAt(1)->GetURL());
  EXPECT_FALSE(first->GetController().CanGoBack());
  EXPECT_TRUE(first->GetController().CanGoForward());

  // Select the second tab and make it go forward in a new background tab.
  browser()->ActivateTabAt(1, true);
  // TODO(brettw) bug 11055: It should not be necessary to commit the load here,
  // but because of this bug, it will assert later if we don't. When the bug is
  // fixed, one of the three commits here related to this bug should be removed
  // (to test both codepaths).
  CommitPendingLoad(&first->GetController());
  EXPECT_EQ(1, browser()->active_index());
  browser()->GoForward(NEW_BACKGROUND_TAB);

  // The previous tab should be unchanged and still in the foreground.
  EXPECT_EQ(url1, first->GetURL());
  EXPECT_FALSE(first->GetController().CanGoBack());
  EXPECT_TRUE(first->GetController().CanGoForward());
  EXPECT_EQ(1, browser()->active_index());

  // There should be a new tab navigated forward.
  ASSERT_EQ(3, browser()->tab_count());
  WebContents* second = browser()->GetWebContentsAt(2);
  EXPECT_EQ(url2, second->GetURL());
  EXPECT_TRUE(second->GetController().CanGoBack());
  EXPECT_FALSE(second->GetController().CanGoForward());

  // Now do back in a new foreground tab. Don't bother re-checking every sngle
  // thing above, just validate that it's opening properly.
  browser()->ActivateTabAt(2, true);
  // TODO(brettw) bug 11055: see the comment above about why we need this.
  CommitPendingLoad(&second->GetController());
  browser()->GoBack(NEW_FOREGROUND_TAB);
  ASSERT_EQ(3, browser()->active_index());
  ASSERT_EQ(url1, browser()->GetSelectedWebContents()->GetURL());

  // Same thing again for forward.
  // TODO(brettw) bug 11055: see the comment above about why we need this.
  CommitPendingLoad(&browser()->GetSelectedWebContents()->GetController());
  browser()->GoForward(NEW_FOREGROUND_TAB);
  ASSERT_EQ(4, browser()->active_index());
  ASSERT_EQ(url2, browser()->GetSelectedWebContents()->GetURL());
}

// Tests IDC_SEARCH (the Search key on Chrome OS devices).
#if defined(OS_CHROMEOS)
TEST_F(BrowserCommandsTest, Search) {
  // Load a non-NTP URL.
  GURL non_ntp_url("http://foo/");
  AddTab(browser(), non_ntp_url);
  ASSERT_EQ(1, browser()->tab_count());
  EXPECT_EQ(non_ntp_url, browser()->GetSelectedWebContents()->GetURL());

  // Pressing the Search key should open a new tab containing the NTP.
  browser()->Search();
  ASSERT_EQ(2, browser()->tab_count());
  ASSERT_EQ(1, browser()->active_index());
  GURL current_url = browser()->GetSelectedWebContents()->GetURL();
  EXPECT_TRUE(current_url.SchemeIs(chrome::kChromeUIScheme));
  EXPECT_EQ(chrome::kChromeUINewTabHost, current_url.host());

  // Pressing it a second time while the NTP is open shouldn't change anything.
  browser()->Search();
  ASSERT_EQ(2, browser()->tab_count());
  ASSERT_EQ(1, browser()->active_index());
  current_url = browser()->GetSelectedWebContents()->GetURL();
  EXPECT_TRUE(current_url.SchemeIs(chrome::kChromeUIScheme));
  EXPECT_EQ(chrome::kChromeUINewTabHost, current_url.host());
}
#endif
