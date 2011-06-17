// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/string_util.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/theme_installed_infobar_delegate.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/test/ui_test_utils.h"
#include "content/browser/tab_contents/tab_contents.h"

namespace {

}  // namespace

class ExtensionInstallUIBrowserTest : public ExtensionBrowserTest {
 public:
  // Checks that a theme info bar is currently visible and issues an undo to
  // revert to the previous theme.
  void VerifyThemeInfoBarAndUndoInstall() {
    TabContents* tab_contents = browser()->GetSelectedTabContents();
    ASSERT_TRUE(tab_contents);
    ASSERT_EQ(1U, tab_contents->infobar_count());
    ConfirmInfoBarDelegate* delegate =
        tab_contents->GetInfoBarDelegateAt(0)->AsConfirmInfoBarDelegate();
    ASSERT_TRUE(delegate);
    delegate->Cancel();
    ASSERT_EQ(0U, tab_contents->infobar_count());
  }

  const Extension* GetTheme() const {
    return ThemeServiceFactory::GetThemeForProfile(browser()->profile());
  }
};

IN_PROC_BROWSER_TEST_F(ExtensionInstallUIBrowserTest,
                       TestThemeInstallUndoResetsToDefault) {
  // Install theme once and undo to verify we go back to default theme.
  FilePath theme_crx = PackExtension(test_data_dir_.AppendASCII("theme"));
  ASSERT_TRUE(InstallExtensionWithUI(theme_crx, 1));
  const Extension* theme = GetTheme();
  ASSERT_TRUE(theme);
  std::string theme_id = theme->id();
  VerifyThemeInfoBarAndUndoInstall();
  ASSERT_EQ(NULL, GetTheme());

  // Set the same theme twice and undo to verify we go back to default theme.
  // We set the |expected_change| to zero in these 'InstallExtensionWithUI'
  // calls since the theme has already been installed above and this is an
  // overinstall to set the active theme.
  ASSERT_TRUE(InstallExtensionWithUI(theme_crx, 0));
  theme = GetTheme();
  ASSERT_TRUE(theme);
  ASSERT_EQ(theme_id, theme->id());
  ASSERT_TRUE(InstallExtensionWithUI(theme_crx, 0));
  theme = GetTheme();
  ASSERT_TRUE(theme);
  ASSERT_EQ(theme_id, theme->id());
  VerifyThemeInfoBarAndUndoInstall();
  ASSERT_EQ(NULL, GetTheme());
}

IN_PROC_BROWSER_TEST_F(ExtensionInstallUIBrowserTest,
                       TestThemeInstallUndoResetsToPreviousTheme) {
  // Install first theme.
  FilePath theme_path = test_data_dir_.AppendASCII("theme");
  ASSERT_TRUE(InstallExtensionWithUI(theme_path, 1));
  const Extension* theme = GetTheme();
  ASSERT_TRUE(theme);
  std::string theme_id = theme->id();

  // Then install second theme.
  FilePath theme_path2 = test_data_dir_.AppendASCII("theme2");
  ASSERT_TRUE(InstallExtensionWithUI(theme_path2, 1));
  const Extension* theme2 = GetTheme();
  ASSERT_TRUE(theme2);
  EXPECT_FALSE(theme_id == theme2->id());

  // Undo second theme will revert to first theme.
  VerifyThemeInfoBarAndUndoInstall();
  EXPECT_EQ(theme, GetTheme());
}

IN_PROC_BROWSER_TEST_F(ExtensionInstallUIBrowserTest,
                       AppInstallConfirmation) {
  int num_tabs = browser()->tab_count();

  FilePath app_dir = test_data_dir_.AppendASCII("app");
  ASSERT_TRUE(InstallExtensionWithUIAutoConfirm(app_dir, 1,
                                                browser()->profile()));

  EXPECT_EQ(num_tabs + 1, browser()->tab_count());
  TabContents* tab_contents = browser()->GetSelectedTabContents();
  ASSERT_TRUE(tab_contents);
  EXPECT_TRUE(StartsWithASCII(tab_contents->GetURL().spec(),
                              "chrome://newtab/#app-id=",  // id changes
                              false));
}

IN_PROC_BROWSER_TEST_F(ExtensionInstallUIBrowserTest,
                       AppInstallConfirmation_Incognito) {
  Profile* incognito_profile = browser()->profile()->GetOffTheRecordProfile();
  Browser* incognito_browser = Browser::GetOrCreateTabbedBrowser(
      incognito_profile);

  int num_incognito_tabs = incognito_browser->tab_count();
  int num_normal_tabs = browser()->tab_count();

  FilePath app_dir = test_data_dir_.AppendASCII("app");
  ASSERT_TRUE(InstallExtensionWithUIAutoConfirm(app_dir, 1,
                                                incognito_profile));

  EXPECT_EQ(num_incognito_tabs, incognito_browser->tab_count());
  EXPECT_EQ(num_normal_tabs + 1, browser()->tab_count());
  TabContents* tab_contents = browser()->GetSelectedTabContents();
  ASSERT_TRUE(tab_contents);
  EXPECT_TRUE(StartsWithASCII(tab_contents->GetURL().spec(),
                              "chrome://newtab/#app-id=",  // id changes
                              false));
}
