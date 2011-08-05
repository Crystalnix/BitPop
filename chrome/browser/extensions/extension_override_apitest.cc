// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_web_ui.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/prefs/scoped_user_pref_update.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/ui_test_utils.h"
#include "content/browser/tab_contents/tab_contents.h"

class ExtensionOverrideTest : public ExtensionApiTest {
 protected:
  bool CheckHistoryOverridesContainsNoDupes() {
    // There should be no duplicate entries in the preferences.
    const DictionaryValue* overrides =
        browser()->profile()->GetPrefs()->GetDictionary(
            ExtensionWebUI::kExtensionURLOverrides);

    ListValue* values = NULL;
    if (!overrides->GetList("history", &values))
      return false;

    std::set<std::string> seen_overrides;
    for (size_t i = 0; i < values->GetSize(); ++i) {
      std::string value;
      if (!values->GetString(i, &value))
        return false;

      if (seen_overrides.find(value) != seen_overrides.end())
        return false;

      seen_overrides.insert(value);
    }

    return true;
  }

#if defined(TOUCH_UI)
  // Navigate to the keyboard page, and ensure we have arrived at an
  // extension URL.
  void NavigateToKeyboard() {
    ui_test_utils::NavigateToURL(browser(), GURL("chrome://keyboard/"));
    TabContents* tab = browser()->GetSelectedTabContents();
    ASSERT_TRUE(tab->controller().GetActiveEntry());
    EXPECT_TRUE(tab->controller().GetActiveEntry()->url().
                SchemeIs(chrome::kExtensionScheme));
  }
#endif
};

IN_PROC_BROWSER_TEST_F(ExtensionOverrideTest, OverrideNewtab) {
  ASSERT_TRUE(RunExtensionTest("override/newtab")) << message_;
  {
    ResultCatcher catcher;
    // Navigate to the new tab page.  The overridden new tab page
    // will call chrome.test.notifyPass() .
    ui_test_utils::NavigateToURL(browser(), GURL("chrome://newtab/"));
    TabContents* tab = browser()->GetSelectedTabContents();
    ASSERT_TRUE(tab->controller().GetActiveEntry());
    EXPECT_TRUE(tab->controller().GetActiveEntry()->url().
                SchemeIs(chrome::kExtensionScheme));

    ASSERT_TRUE(catcher.GetNextResult());
  }

  // TODO(erikkay) Load a second extension with the same override.
  // Verify behavior, then unload the first and verify behavior, etc.
}

#if defined(OS_MACOSX)
// Hangy: http://crbug.com/70511
#define MAYBE_OverrideNewtabIncognito DISABLED_OverrideNewtabIncognito
#else
#define MAYBE_OverrideNewtabIncognito OverrideNewtabIncognito
#endif
IN_PROC_BROWSER_TEST_F(ExtensionOverrideTest, MAYBE_OverrideNewtabIncognito) {
  ASSERT_TRUE(RunExtensionTest("override/newtab")) << message_;

  // Navigate an incognito tab to the new tab page.  We should get the actual
  // new tab page because we can't load chrome-extension URLs in incognito.
  ui_test_utils::OpenURLOffTheRecord(browser()->profile(),
                                     GURL("chrome://newtab/"));
  Browser* otr_browser = BrowserList::FindTabbedBrowser(
      browser()->profile()->GetOffTheRecordProfile(), false);
  TabContents* tab = otr_browser->GetSelectedTabContents();
  ASSERT_TRUE(tab->controller().GetActiveEntry());
  EXPECT_FALSE(tab->controller().GetActiveEntry()->url().
               SchemeIs(chrome::kExtensionScheme));
}

// Times out consistently on Win, http://crbug.com/45173.
#if defined(OS_WIN)
#define MAYBE_OverrideHistory DISABLED_OverrideHistory
#else
#define MAYBE_OverrideHistory OverrideHistory
#endif  // defined(OS_WIN)

IN_PROC_BROWSER_TEST_F(ExtensionOverrideTest, MAYBE_OverrideHistory) {
  ASSERT_TRUE(RunExtensionTest("override/history")) << message_;
  {
    ResultCatcher catcher;
    // Navigate to the history page.  The overridden history page
    // will call chrome.test.notifyPass() .
    ui_test_utils::NavigateToURL(browser(), GURL("chrome://history/"));
    ASSERT_TRUE(catcher.GetNextResult());
  }
}

// Regression test for http://crbug.com/41442.
IN_PROC_BROWSER_TEST_F(ExtensionOverrideTest, ShouldNotCreateDuplicateEntries) {
  ASSERT_TRUE(LoadExtension(test_data_dir_.AppendASCII("override/history")));

  // Simulate several LoadExtension() calls happening over the lifetime of
  // a preferences file without corresponding UnloadExtension() calls.
  for (size_t i = 0; i < 3; ++i) {
    ExtensionWebUI::RegisterChromeURLOverrides(
        browser()->profile(),
        browser()->profile()->GetExtensionService()->extensions()->back()->
            GetChromeURLOverrides());
  }

  ASSERT_TRUE(CheckHistoryOverridesContainsNoDupes());
}

IN_PROC_BROWSER_TEST_F(ExtensionOverrideTest, ShouldCleanUpDuplicateEntries) {
  // Simulate several LoadExtension() calls happening over the lifetime of
  // a preferences file without corresponding UnloadExtension() calls. This is
  // the same as the above test, except for that it is testing the case where
  // the file already contains dupes when an extension is loaded.
  ListValue* list = new ListValue();
  for (size_t i = 0; i < 3; ++i)
    list->Append(Value::CreateStringValue("http://www.google.com/"));

  {
    DictionaryPrefUpdate update(browser()->profile()->GetPrefs(),
                                ExtensionWebUI::kExtensionURLOverrides);
    update.Get()->Set("history", list);
  }

  ASSERT_FALSE(CheckHistoryOverridesContainsNoDupes());

  ASSERT_TRUE(LoadExtension(test_data_dir_.AppendASCII("override/history")));

  ASSERT_TRUE(CheckHistoryOverridesContainsNoDupes());
}

#if defined(TOUCH_UI)
IN_PROC_BROWSER_TEST_F(ExtensionOverrideTest, OverrideKeyboard) {
  ASSERT_TRUE(RunExtensionTest("override/keyboard")) << message_;
  {
    ResultCatcher catcher;
    NavigateToKeyboard();
    ASSERT_TRUE(catcher.GetNextResult());
  }

  // Load the failing version.  This should take precedence.
  ASSERT_TRUE(LoadExtension(
      test_data_dir_.AppendASCII("override").AppendASCII("keyboard_fails")));
  {
    ResultCatcher catcher;
    NavigateToKeyboard();
    ASSERT_FALSE(catcher.GetNextResult());
  }

  // Unload the failing version.  We should be back to passing now.
  const ExtensionList *extensions =
      browser()->profile()->GetExtensionService()->extensions();
  UnloadExtension((*extensions->rbegin())->id());
  {
    ResultCatcher catcher;
    NavigateToKeyboard();
    ASSERT_TRUE(catcher.GetNextResult());
  }
}
#endif
