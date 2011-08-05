// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "base/stringprintf.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/autocomplete/autocomplete_edit.h"
#include "chrome/browser/content_settings/host_content_settings_map.h"
#include "chrome/browser/instant/instant_controller.h"
#include "chrome/browser/instant/instant_loader.h"
#include "chrome/browser/instant/instant_loader_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url.h"
#include "chrome/browser/search_engines/template_url_model.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/omnibox/location_bar.h"
#include "chrome/browser/ui/omnibox/omnibox_view.h"
#include "chrome/browser/ui/tab_contents/tab_contents_wrapper.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/in_process_browser_test.h"
#include "chrome/test/ui_test_utils.h"
#include "content/browser/renderer_host/render_view_host.h"
#include "content/browser/renderer_host/render_widget_host_view.h"
#include "content/browser/tab_contents/tab_contents.h"

#define EXPECT_STR_EQ(ascii, utf16) \
  EXPECT_EQ(ASCIIToWide(ascii), UTF16ToWide(utf16))

class InstantTest : public InProcessBrowserTest {
 public:
  InstantTest()
      : location_bar_(NULL),
        preview_(NULL) {
    set_show_window(true);
    EnableDOMAutomation();
  }

  void EnableInstant() {
    InstantController::Enable(browser()->profile());
  }

  void SetupInstantProvider(const std::string& page) {
    TemplateURLModel* model = browser()->profile()->GetTemplateURLModel();
    ASSERT_TRUE(model);

    if (!model->loaded()) {
      model->Load();
      ui_test_utils::WaitForNotification(
          NotificationType::TEMPLATE_URL_MODEL_LOADED);
    }

    ASSERT_TRUE(model->loaded());

    // TemplateURLModel takes ownership of this.
    TemplateURL* template_url = new TemplateURL();

    std::string url = StringPrintf(
        "http://%s:%d/files/instant/%s?q={searchTerms}",
        test_server()->host_port_pair().host().c_str(),
        test_server()->host_port_pair().port(),
        page.c_str());
    template_url->SetURL(url, 0, 0);
    template_url->SetInstantURL(url, 0, 0);
    template_url->set_keyword(ASCIIToUTF16("foo"));
    template_url->set_short_name(ASCIIToUTF16("foo"));

    model->Add(template_url);
    model->SetDefaultSearchProvider(template_url);
  }

  void FindLocationBar() {
    if (location_bar_)
      return;
    location_bar_ = browser()->window()->GetLocationBar();
    ASSERT_TRUE(location_bar_);
  }

  TabContentsWrapper* GetPendingPreviewContents() {
    return browser()->instant()->GetPendingPreviewContents();
  }

  // Type a character to get instant to trigger.
  void SetupLocationBar() {
    FindLocationBar();
    // "a" triggers the "about:" provider.  "b" begins the "bing.com" keyword.
    // "c" might someday trigger a "chrome:" provider.
    location_bar_->location_entry()->SetUserText(ASCIIToUTF16("d"));
  }

  // Waits for preview to be shown.
  void WaitForPreviewToNavigate(bool use_current) {
    InstantController* instant = browser()->instant();
    ASSERT_TRUE(instant);
    TabContentsWrapper* tab = use_current ?
        instant->GetPreviewContents() : GetPendingPreviewContents();
    ASSERT_TRUE(tab);
    preview_ = tab->tab_contents();
    ASSERT_TRUE(preview_);
    ui_test_utils::WaitForNavigation(&preview_->controller());
  }

  // Wait for instant to load and ensure it is in the state we expect.
  void SetupPreview() {
    // Wait for the preview to navigate.
    WaitForPreviewToNavigate(true);

    ASSERT_TRUE(browser()->instant()->IsShowingInstant());
    ASSERT_FALSE(browser()->instant()->is_displayable());
    ASSERT_TRUE(browser()->instant()->is_active());

    // When the page loads, the initial searchBox values are set and only a
    // resize will have been sent.
    ASSERT_EQ("true 0 0 0 1 d false d false 1 1",
              GetSearchStateAsString(preview_, false));
  }

  void SetLocationBarText(const std::string& text) {
    ASSERT_NO_FATAL_FAILURE(FindLocationBar());
    location_bar_->location_entry()->SetUserText(UTF8ToUTF16(text));
    ui_test_utils::WaitForNotification(
        NotificationType::INSTANT_CONTROLLER_SHOWN);
  }

  const string16& GetSuggestion() const {
    return browser()->instant()->loader_manager_->
        current_loader()->complete_suggested_text_;
  }

  void SendKey(ui::KeyboardCode key) {
    ASSERT_TRUE(ui_test_utils::SendKeyPressSync(
        browser(), key, false, false, false, false));
  }

  void SetSuggestionsJavascriptArgument(TabContents* tab_contents,
                                        const std::string& argument) {
    std::string script = StringPrintf(
        "window.setSuggestionsArgument = %s;", argument.c_str());
    ASSERT_TRUE(ui_test_utils::ExecuteJavaScript(
        tab_contents->render_view_host(),
        std::wstring(),
        UTF8ToWide(script)));
  }

  bool GetStringFromJavascript(TabContents* tab_contents,
                               const std::string& function,
                               std::string* result) {
    std::string script = StringPrintf(
        "window.domAutomationController.send(%s)", function.c_str());
    return ui_test_utils::ExecuteJavaScriptAndExtractString(
        tab_contents->render_view_host(),
        std::wstring(), UTF8ToWide(script), result);
  }

  bool GetIntFromJavascript(TabContents* tab_contents,
                            const std::string& function,
                            int* result) {
    std::string script = StringPrintf(
        "window.domAutomationController.send(%s)", function.c_str());
    return ui_test_utils::ExecuteJavaScriptAndExtractInt(
        tab_contents->render_view_host(),
        std::wstring(), UTF8ToWide(script), result);
  }

  bool GetBoolFromJavascript(TabContents* tab_contents,
                             const std::string& function,
                             bool* result) {
    std::string script = StringPrintf(
        "window.domAutomationController.send(%s)", function.c_str());
    return ui_test_utils::ExecuteJavaScriptAndExtractBool(
        tab_contents->render_view_host(),
        std::wstring(), UTF8ToWide(script), result);
  }

  // Returns the state of the search box as a string. This consists of the
  // following:
  // window.chrome.sv
  // window.onsubmitcalls
  // window.oncancelcalls
  // window.onchangecalls
  // window.onresizecalls
  // window.beforeLoadSearchBox.value
  // window.beforeLoadSearchBox.verbatim
  // window.chrome.searchBox.value
  // window.chrome.searchBox.verbatim
  // window.chrome.searchBox.selectionStart
  // window.chrome.searchBox.selectionEnd
  // If determining any of the values fails, the value is 'fail'.
  //
  // If |use_last| is true, then the last searchBox values are used instead of
  // the current. Set |use_last| to true when testing OnSubmit/OnCancel.
  std::string GetSearchStateAsString(TabContents* tab_contents,
                                     bool use_last) {
    bool sv = false;
    int onsubmitcalls = 0;
    int oncancelcalls = 0;
    int onchangecalls = 0;
    int onresizecalls = 0;
    int selection_start = 0;
    int selection_end = 0;
    std::string before_load_value;
    bool before_load_verbatim = false;
    std::string value;
    bool verbatim = false;

    if (!GetBoolFromJavascript(tab_contents, "window.chrome.sv", &sv) ||
        !GetIntFromJavascript(tab_contents, "window.onsubmitcalls",
                              &onsubmitcalls) ||
        !GetIntFromJavascript(tab_contents, "window.oncancelcalls",
                              &oncancelcalls) ||
        !GetIntFromJavascript(tab_contents, "window.onchangecalls",
                              &onchangecalls) ||
        !GetIntFromJavascript(tab_contents, "window.onresizecalls",
                              &onresizecalls) ||
        !GetStringFromJavascript(
            tab_contents, "window.beforeLoadSearchBox.value",
            &before_load_value) ||
        !GetBoolFromJavascript(
            tab_contents, "window.beforeLoadSearchBox.verbatim",
            &before_load_verbatim)) {
      return "fail";
    }

    if (use_last &&
        (!GetStringFromJavascript(tab_contents, "window.lastSearchBox.value",
                                  &value) ||
         !GetBoolFromJavascript(tab_contents, "window.lastSearchBox.verbatim",
                                &verbatim) ||
         !GetIntFromJavascript(tab_contents,
                               "window.lastSearchBox.selectionStart",
                               &selection_start) ||
         !GetIntFromJavascript(tab_contents,
                               "window.lastSearchBox.selectionEnd",
                               &selection_end))) {
      return "fail";
    }

    if (!use_last &&
        (!GetStringFromJavascript(tab_contents, "window.searchBox.value",
                                  &value) ||
         !GetBoolFromJavascript(tab_contents, "window.searchBox.verbatim",
                                &verbatim) ||
         !GetIntFromJavascript(tab_contents,
                               "window.searchBox.selectionStart",
                               &selection_start) ||
         !GetIntFromJavascript(tab_contents,
                               "window.searchBox.selectionEnd",
                               &selection_end))) {
      return "fail";
    }

    return StringPrintf("%s %d %d %d %d %s %s %s %s %d %d",
                        sv ? "true" : "false",
                        onsubmitcalls,
                        oncancelcalls,
                        onchangecalls,
                        onresizecalls,
                        before_load_value.c_str(),
                        before_load_verbatim ? "true" : "false",
                        value.c_str(),
                        verbatim ? "true" : "false",
                        selection_start,
                        selection_end);
  }

  void CheckStringValueFromJavascript(
      const std::string& expected,
      const std::string& function,
      TabContents* tab_contents) {
    std::string result;
    ASSERT_TRUE(GetStringFromJavascript(tab_contents, function, &result));
    ASSERT_EQ(expected, result);
  }

  void CheckBoolValueFromJavascript(
      bool expected,
      const std::string& function,
      TabContents* tab_contents) {
    bool result;
    ASSERT_TRUE(GetBoolFromJavascript(tab_contents, function, &result));
    ASSERT_EQ(expected, result);
  }

  void CheckIntValueFromJavascript(
      int expected,
      const std::string& function,
      TabContents* tab_contents) {
    int result;
    ASSERT_TRUE(GetIntFromJavascript(tab_contents, function, &result));
    ASSERT_EQ(expected, result);
  }

  // Sends a message to the renderer and waits for the response to come back to
  // the browser.
  void WaitForMessageToBeProcessedByRenderer(TabContentsWrapper* tab) {
    ASSERT_NO_FATAL_FAILURE(
        CheckBoolValueFromJavascript(true, "true", tab->tab_contents()));
  }

 protected:
  LocationBar* location_bar_;
  TabContents* preview_;
};

// TODO(tonyg): Add the following tests:
// - Test that the search box API is not populated for pages other than the
//   default search provider.
// - Test resize events.

// Verify that the onchange event is dispatched upon typing in the box.
// DISABLED http://crbug.com/80118
#if defined(OS_LINUX)
IN_PROC_BROWSER_TEST_F(InstantTest, DISABLED_OnChangeEvent) {
#else
IN_PROC_BROWSER_TEST_F(InstantTest, OnChangeEvent) {
#endif  // OS_LINUX
  ASSERT_TRUE(test_server()->Start());
  EnableInstant();
  ASSERT_NO_FATAL_FAILURE(SetupInstantProvider("search.html"));
  ASSERT_NO_FATAL_FAILURE(SetupLocationBar());
  ASSERT_NO_FATAL_FAILURE(SetupPreview());

  ASSERT_NO_FATAL_FAILURE(SetLocationBarText("def"));

  ASSERT_EQ(ASCIIToUTF16("defghi"), location_bar_->location_entry()->GetText());

  // Make sure the url that will get committed when we press enter matches that
  // of the default search provider.
  const TemplateURL* default_turl =
      browser()->profile()->GetTemplateURLModel()->GetDefaultSearchProvider();
  ASSERT_TRUE(default_turl);
  ASSERT_TRUE(default_turl->url());
  EXPECT_EQ(default_turl->url()->ReplaceSearchTerms(
                *default_turl, ASCIIToUTF16("defghi"), 0, string16()),
            browser()->instant()->GetCurrentURL().spec());

  // Check that the value is reflected and onchange is called.
  EXPECT_EQ("true 0 0 1 2 d false def false 3 3",
            GetSearchStateAsString(preview_, true));
}

// DISABLED http://crbug.com/80118
#if defined(OS_LINUX)
IN_PROC_BROWSER_TEST_F(InstantTest, DISABLED_SetSuggestionsArrayOfStrings) {
#else
IN_PROC_BROWSER_TEST_F(InstantTest, SetSuggestionsArrayOfStrings) {
#endif  // OS_LINUX
  ASSERT_TRUE(test_server()->Start());
  EnableInstant();
  ASSERT_NO_FATAL_FAILURE(SetupInstantProvider("search.html"));
  ASSERT_NO_FATAL_FAILURE(SetupLocationBar());
  ASSERT_NO_FATAL_FAILURE(SetupPreview());

  SetSuggestionsJavascriptArgument(preview_, "['defgh', 'unused']");
  ASSERT_NO_FATAL_FAILURE(SetLocationBarText("def"));
  EXPECT_STR_EQ("defgh", GetSuggestion());
}

// DISABLED http://crbug.com/80118
#if defined(OS_LINUX)
IN_PROC_BROWSER_TEST_F(InstantTest, DISABLED_SetSuggestionsEmptyArray) {
#else
IN_PROC_BROWSER_TEST_F(InstantTest, SetSuggestionsEmptyArray) {
#endif  // OS_LINUX
  ASSERT_TRUE(test_server()->Start());
  EnableInstant();
  ASSERT_NO_FATAL_FAILURE(SetupInstantProvider("search.html"));
  ASSERT_NO_FATAL_FAILURE(SetupLocationBar());
  ASSERT_NO_FATAL_FAILURE(SetupPreview());

  SetSuggestionsJavascriptArgument(preview_, "[]");
  ASSERT_NO_FATAL_FAILURE(SetLocationBarText("def"));
  EXPECT_STR_EQ("", GetSuggestion());
}

// DISABLED http://crbug.com/80118
#if defined(OS_LINUX)
IN_PROC_BROWSER_TEST_F(InstantTest, DISABLED_SetSuggestionsValidJson) {
#else
IN_PROC_BROWSER_TEST_F(InstantTest, SetSuggestionsValidJson) {
#endif  // OS_LINUX
  ASSERT_TRUE(test_server()->Start());
  EnableInstant();
  ASSERT_NO_FATAL_FAILURE(SetupInstantProvider("search.html"));
  ASSERT_NO_FATAL_FAILURE(SetupLocationBar());
  ASSERT_NO_FATAL_FAILURE(SetupPreview());

  SetSuggestionsJavascriptArgument(
      preview_,
      "{suggestions:[{value:'defghij'},{value:'unused'}]}");
  ASSERT_NO_FATAL_FAILURE(SetLocationBarText("def"));
  EXPECT_STR_EQ("defghij", GetSuggestion());
}

// DISABLED http://crbug.com/80118
#if defined(OS_LINUX)
IN_PROC_BROWSER_TEST_F(InstantTest, DISABLED_SetSuggestionsInvalidSuggestions) {
#else
IN_PROC_BROWSER_TEST_F(InstantTest, SetSuggestionsInvalidSuggestions) {
#endif  // OS_LINUX
  ASSERT_TRUE(test_server()->Start());
  EnableInstant();
  ASSERT_NO_FATAL_FAILURE(SetupInstantProvider("search.html"));
  ASSERT_NO_FATAL_FAILURE(SetupLocationBar());
  ASSERT_NO_FATAL_FAILURE(SetupPreview());

  SetSuggestionsJavascriptArgument(
      preview_,
      "{suggestions:{value:'defghi'}}");
  ASSERT_NO_FATAL_FAILURE(SetLocationBarText("def"));
  EXPECT_STR_EQ("", GetSuggestion());
}

// DISABLED http://crbug.com/80118
#if defined(OS_LINUX)
IN_PROC_BROWSER_TEST_F(InstantTest, DISABLED_SetSuggestionsEmptyJson) {
#else
IN_PROC_BROWSER_TEST_F(InstantTest, SetSuggestionsEmptyJson) {
#endif  // OS_LINUX
  ASSERT_TRUE(test_server()->Start());
  EnableInstant();
  ASSERT_NO_FATAL_FAILURE(SetupInstantProvider("search.html"));
  ASSERT_NO_FATAL_FAILURE(SetupLocationBar());
  ASSERT_NO_FATAL_FAILURE(SetupPreview());

  SetSuggestionsJavascriptArgument(preview_, "{}");
  ASSERT_NO_FATAL_FAILURE(SetLocationBarText("def"));
  EXPECT_STR_EQ("", GetSuggestion());
}

// DISABLED http://crbug.com/80118
#if defined(OS_LINUX)
IN_PROC_BROWSER_TEST_F(InstantTest, DISABLED_SetSuggestionsEmptySuggestions) {
#else
IN_PROC_BROWSER_TEST_F(InstantTest, SetSuggestionsEmptySuggestions) {
#endif  // OS_LINUX
  ASSERT_TRUE(test_server()->Start());
  EnableInstant();
  ASSERT_NO_FATAL_FAILURE(SetupInstantProvider("search.html"));
  ASSERT_NO_FATAL_FAILURE(SetupLocationBar());
  ASSERT_NO_FATAL_FAILURE(SetupPreview());

  SetSuggestionsJavascriptArgument(preview_, "{suggestions:[]}");
  ASSERT_NO_FATAL_FAILURE(SetLocationBarText("def"));
  EXPECT_STR_EQ("", GetSuggestion());
}

// DISABLED http://crbug.com/80118
#if defined(OS_LINUX)
IN_PROC_BROWSER_TEST_F(InstantTest, DISABLED_SetSuggestionsEmptySuggestion) {
#else
IN_PROC_BROWSER_TEST_F(InstantTest, SetSuggestionsEmptySuggestion) {
#endif  // OS_LINUX
  ASSERT_TRUE(test_server()->Start());
  EnableInstant();
  ASSERT_NO_FATAL_FAILURE(SetupInstantProvider("search.html"));
  ASSERT_NO_FATAL_FAILURE(SetupLocationBar());
  ASSERT_NO_FATAL_FAILURE(SetupPreview());

  SetSuggestionsJavascriptArgument(preview_, "{suggestions:[{}]}");
  ASSERT_NO_FATAL_FAILURE(SetLocationBarText("def"));
  EXPECT_STR_EQ("", GetSuggestion());
}

// Verify instant preview is shown correctly for a non-search query.
// DISABLED http://crbug.com/80118
#if defined(OS_LINUX)
IN_PROC_BROWSER_TEST_F(InstantTest, DISABLED_ShowPreviewNonSearch) {
#else
IN_PROC_BROWSER_TEST_F(InstantTest, ShowPreviewNonSearch) {
#endif  // OS_LINUX
  ASSERT_TRUE(test_server()->Start());
  EnableInstant();
  GURL url(test_server()->GetURL("files/instant/empty.html"));
  ASSERT_NO_FATAL_FAILURE(SetLocationBarText(url.spec()));
  // The preview should be active and showing.
  ASSERT_TRUE(browser()->instant()->is_active());
  ASSERT_TRUE(browser()->instant()->is_displayable());
  ASSERT_TRUE(browser()->instant()->IsCurrent());
  ASSERT_TRUE(browser()->instant()->GetPreviewContents());
  RenderWidgetHostView* rwhv =
      browser()->instant()->GetPreviewContents()->tab_contents()->
      GetRenderWidgetHostView();
  ASSERT_TRUE(rwhv);
  ASSERT_TRUE(rwhv->IsShowing());
}

// Transition from non-search to search and make sure everything is shown
// correctly.
// DISABLED http://crbug.com/80118
#if defined(OS_LINUX)
IN_PROC_BROWSER_TEST_F(InstantTest, DISABLED_NonSearchToSearch) {
#else
IN_PROC_BROWSER_TEST_F(InstantTest, NonSearchToSearch) {
#endif  // OS_LINUX
  ASSERT_TRUE(test_server()->Start());
  EnableInstant();
  GURL url(test_server()->GetURL("files/instant/empty.html"));
  ASSERT_NO_FATAL_FAILURE(SetLocationBarText(url.spec()));
  // The preview should be active and showing.
  ASSERT_TRUE(browser()->instant()->is_active());
  ASSERT_TRUE(browser()->instant()->is_displayable());
  TabContentsWrapper* initial_tab = browser()->instant()->GetPreviewContents();
  ASSERT_TRUE(initial_tab);
  RenderWidgetHostView* rwhv =
      initial_tab->tab_contents()->GetRenderWidgetHostView();
  ASSERT_TRUE(rwhv);
  ASSERT_TRUE(rwhv->IsShowing());

  // Now type in some search text.
  ASSERT_NO_FATAL_FAILURE(SetupInstantProvider("search.html"));
  location_bar_->location_entry()->SetUserText(ASCIIToUTF16("def"));

  // Wait for the preview to navigate.
  ASSERT_NO_FATAL_FAILURE(WaitForPreviewToNavigate(false));

  // The controller is still determining if the provider really supports
  // instant. As a result the tabcontents should not have changed.
  TabContentsWrapper* current_tab = browser()->instant()->GetPreviewContents();
  ASSERT_EQ(current_tab, initial_tab);
  // The preview should still be showing.
  rwhv = current_tab->tab_contents()->GetRenderWidgetHostView();
  ASSERT_TRUE(rwhv);
  ASSERT_TRUE(rwhv->IsShowing());

  // Use MightSupportInstant as the controller is still determining if the
  // page supports instant and hasn't actually commited yet.
  EXPECT_TRUE(browser()->instant()->MightSupportInstant());

  // Instant should still be active.
  EXPECT_TRUE(browser()->instant()->is_active());
  EXPECT_TRUE(browser()->instant()->is_displayable());

  // Because we're waiting on the page, instant isn't current.
  ASSERT_FALSE(browser()->instant()->IsCurrent());

  // Bounce a message to the renderer so that we know the instant has gotten a
  // response back from the renderer as to whether the page supports instant.
  ASSERT_NO_FATAL_FAILURE(
      WaitForMessageToBeProcessedByRenderer(GetPendingPreviewContents()));

  // Reset the user text so that the page is told the text changed. We should be
  // able to nuke this once 66104 is fixed.
  location_bar_->location_entry()->SetUserText(ASCIIToUTF16("defg"));

  // Wait for the renderer to process it.
  ASSERT_NO_FATAL_FAILURE(
      WaitForMessageToBeProcessedByRenderer(GetPendingPreviewContents()));

  // We should have gotten a response back from the renderer that resulted in
  // committing.
  ASSERT_FALSE(GetPendingPreviewContents());
  ASSERT_TRUE(browser()->instant()->is_active());
  ASSERT_TRUE(browser()->instant()->is_displayable());
  TabContentsWrapper* new_tab = browser()->instant()->GetPreviewContents();
  ASSERT_TRUE(new_tab);
  ASSERT_NE(new_tab, initial_tab);
  RenderWidgetHostView* new_rwhv =
      new_tab->tab_contents()->GetRenderWidgetHostView();
  ASSERT_TRUE(new_rwhv);
  ASSERT_NE(new_rwhv, rwhv);
  ASSERT_TRUE(new_rwhv->IsShowing());
}

// Makes sure that if the server doesn't support the instant API we don't show
// anything.
#if defined(OS_MACOSX) || defined(OS_LINUX)
// Showing as flaky on Mac and Linux.
// http://crbug.com/70860
#define MAYBE_SearchServerDoesntSupportInstant \
    DISABLED_SearchServerDoesntSupportInstant
#else
#define MAYBE_SearchServerDoesntSupportInstant \
    SearchServerDoesntSupportInstant
#endif
IN_PROC_BROWSER_TEST_F(InstantTest, MAYBE_SearchServerDoesntSupportInstant) {
  ASSERT_TRUE(test_server()->Start());
  EnableInstant();
  ASSERT_NO_FATAL_FAILURE(SetupInstantProvider("empty.html"));
  ASSERT_NO_FATAL_FAILURE(FindLocationBar());
  location_bar_->location_entry()->SetUserText(ASCIIToUTF16("d"));
  ASSERT_TRUE(browser()->instant());
  // Because we typed in a search string we should think we're showing instant
  // results.
  EXPECT_TRUE(browser()->instant()->IsShowingInstant());
  // But because we're waiting to determine if the page really supports instant
  // we shouldn't be showing the preview.
  EXPECT_FALSE(browser()->instant()->is_displayable());
  // But instant should still be active.
  EXPECT_TRUE(browser()->instant()->is_active());

  // When the response comes back that the page doesn't support instant the tab
  // should be closed.
  ui_test_utils::WaitForNotification(NotificationType::TAB_CLOSED);
  EXPECT_FALSE(browser()->instant()->IsShowingInstant());
  EXPECT_FALSE(browser()->instant()->is_displayable());
  EXPECT_TRUE(browser()->instant()->is_active());
  EXPECT_FALSE(browser()->instant()->IsCurrent());
}

// Verifies transitioning from loading a non-search string to a search string
// with the provider not supporting instant works (meaning we don't display
// anything).
#if defined(OS_MACOSX) || defined(OS_LINUX)
// Showing as flaky on Mac and Linux/ChromeOS
// http://crbug.com/70810
#define MAYBE_NonSearchToSearchDoesntSupportInstant \
    DISABLED_NonSearchToSearchDoesntSupportInstant
#else
#define MAYBE_NonSearchToSearchDoesntSupportInstant \
    NonSearchToSearchDoesntSupportInstant
#endif
IN_PROC_BROWSER_TEST_F(InstantTest,
                       MAYBE_NonSearchToSearchDoesntSupportInstant) {
  ASSERT_TRUE(test_server()->Start());
  EnableInstant();
  ASSERT_NO_FATAL_FAILURE(SetupInstantProvider("empty.html"));
  GURL url(test_server()->GetURL("files/instant/empty.html"));
  ASSERT_NO_FATAL_FAILURE(SetLocationBarText(url.spec()));
  // The preview should be active and showing.
  ASSERT_TRUE(browser()->instant()->is_displayable());
  ASSERT_TRUE(browser()->instant()->is_active());
  TabContentsWrapper* initial_tab = browser()->instant()->GetPreviewContents();
  ASSERT_TRUE(initial_tab);
  RenderWidgetHostView* rwhv =
      initial_tab->tab_contents()->GetRenderWidgetHostView();
  ASSERT_TRUE(rwhv);
  ASSERT_TRUE(rwhv->IsShowing());

  // Now type in some search text.
  location_bar_->location_entry()->SetUserText(ASCIIToUTF16("d"));

  // Instant should still be live.
  ASSERT_TRUE(browser()->instant()->is_displayable());
  ASSERT_TRUE(browser()->instant()->is_active());
  // Because we typed in a search string we should think we're showing instant
  // results.
  EXPECT_TRUE(browser()->instant()->MightSupportInstant());
  // Instant should not be current (it's still loading).
  EXPECT_FALSE(browser()->instant()->IsCurrent());

  // When the response comes back that the page doesn't support instant the tab
  // should be closed.
  ui_test_utils::WaitForNotification(NotificationType::TAB_CLOSED);
  EXPECT_FALSE(browser()->instant()->IsShowingInstant());
  EXPECT_FALSE(browser()->instant()->is_displayable());
  // But because the omnibox is still open, instant should be active.
  ASSERT_TRUE(browser()->instant()->is_active());
}

// Verifies the page was told a non-zero height.
// DISABLED http://crbug.com/80118
#if defined(OS_LINUX)
IN_PROC_BROWSER_TEST_F(InstantTest, DISABLED_ValidHeight) {
#else
IN_PROC_BROWSER_TEST_F(InstantTest, ValidHeight) {
#endif  // OS_LINUX
  ASSERT_TRUE(test_server()->Start());
  EnableInstant();
  ASSERT_NO_FATAL_FAILURE(SetupInstantProvider("search.html"));
  ASSERT_NO_FATAL_FAILURE(SetupLocationBar());
  ASSERT_NO_FATAL_FAILURE(SetupPreview());

  ASSERT_NO_FATAL_FAILURE(SetLocationBarText("def"));

  int height;

  // searchBox height is not yet set during initial load.
  ASSERT_TRUE(GetIntFromJavascript(preview_,
      "window.beforeLoadSearchBox.height",
      &height));
  EXPECT_EQ(0, height);

  // searchBox height is available by the time the page loads.
  ASSERT_TRUE(GetIntFromJavascript(preview_,
      "window.chrome.searchBox.height",
      &height));
  EXPECT_GT(height, 0);
}

// Verifies that if the server returns a 403 we don't show the preview and
// query the host again.
// DISABLED http://crbug.com/80118
#if defined(OS_LINUX)
IN_PROC_BROWSER_TEST_F(InstantTest, DISABLED_HideOn403) {
#else
IN_PROC_BROWSER_TEST_F(InstantTest, HideOn403) {
#endif  // OS_LINUX
  ASSERT_TRUE(test_server()->Start());
  EnableInstant();
  GURL url(test_server()->GetURL("files/instant/403.html"));
  ASSERT_NO_FATAL_FAILURE(FindLocationBar());
  location_bar_->location_entry()->SetUserText(UTF8ToUTF16(url.spec()));
  // The preview shouldn't be showing, but it should be loading.
  ASSERT_TRUE(browser()->instant()->GetPreviewContents());
  ASSERT_TRUE(browser()->instant()->is_active());
  ASSERT_FALSE(browser()->instant()->is_displayable());

  // When instant sees the 403, it should close the tab.
  ui_test_utils::WaitForNotification(NotificationType::TAB_CLOSED);
  ASSERT_FALSE(browser()->instant()->GetPreviewContents());
  ASSERT_TRUE(browser()->instant()->is_active());
  ASSERT_FALSE(browser()->instant()->is_displayable());

  // Try loading another url on the server. Instant shouldn't create a new tab
  // as the server returned 403.
  GURL url2(test_server()->GetURL("files/instant/empty.html"));
  location_bar_->location_entry()->SetUserText(UTF8ToUTF16(url2.spec()));
  ASSERT_FALSE(browser()->instant()->GetPreviewContents());
  ASSERT_TRUE(browser()->instant()->is_active());
  ASSERT_FALSE(browser()->instant()->is_displayable());
}

// Verify that the onsubmit event is dispatched upon pressing enter.
// DISABLED http://crbug.com/80118
#if defined(OS_LINUX)
IN_PROC_BROWSER_TEST_F(InstantTest, DISABLED_OnSubmitEvent) {
#else
IN_PROC_BROWSER_TEST_F(InstantTest, OnSubmitEvent) {
#endif  // OS_LINUX
  ASSERT_TRUE(test_server()->Start());
  EnableInstant();
  ASSERT_NO_FATAL_FAILURE(SetupInstantProvider("search.html"));

  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
  ASSERT_NO_FATAL_FAILURE(SetupLocationBar());
  ASSERT_NO_FATAL_FAILURE(SetupPreview());

  ASSERT_NO_FATAL_FAILURE(SetLocationBarText("def"));
  ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_RETURN));

  // Check that the preview contents have been committed.
  ASSERT_FALSE(browser()->instant()->GetPreviewContents());
  ASSERT_FALSE(browser()->instant()->is_active());
  TabContents* contents = browser()->GetSelectedTabContents();
  ASSERT_TRUE(contents);

  // Check that the value is reflected and onsubmit is called.
  EXPECT_EQ("true 1 0 1 2 d false defghi true 3 3",
            GetSearchStateAsString(preview_, true));

  // Make sure the searchbox values were reset.
  EXPECT_EQ("true 1 0 1 2 d false  false 0 0",
            GetSearchStateAsString(preview_, false));
}

// Verify that the oncancel event is dispatched upon losing focus.
// DISABLED http://crbug.com/80118
#if defined(OS_LINUX)
IN_PROC_BROWSER_TEST_F(InstantTest, DISABLED_OnCancelEvent) {
#else
IN_PROC_BROWSER_TEST_F(InstantTest, OnCancelEvent) {
#endif  // OS_LINUX
  ASSERT_TRUE(test_server()->Start());
  EnableInstant();
  ASSERT_NO_FATAL_FAILURE(SetupInstantProvider("search.html"));

  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
  ASSERT_NO_FATAL_FAILURE(SetupLocationBar());
  ASSERT_NO_FATAL_FAILURE(SetupPreview());

  ASSERT_NO_FATAL_FAILURE(SetLocationBarText("def"));
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::ClickOnView(browser(),
                                                     VIEW_ID_TAB_CONTAINER));

  // Check that the preview contents have been committed.
  ASSERT_FALSE(browser()->instant()->GetPreviewContents());
  ASSERT_FALSE(browser()->instant()->is_active());
  TabContents* contents = browser()->GetSelectedTabContents();
  ASSERT_TRUE(contents);

  // Check that the value is reflected and oncancel is called.
  EXPECT_EQ("true 0 1 1 2 d false def false 3 3",
            GetSearchStateAsString(preview_, true));

  // Make sure the searchbox values were reset.
  EXPECT_EQ("true 0 1 1 2 d false  false 0 0",
            GetSearchStateAsString(preview_, false));
}

// Make sure about:crash is shown.
// DISABLED http://crbug.com/80118
#if defined(OS_LINUX)
IN_PROC_BROWSER_TEST_F(InstantTest, DISABLED_ShowAboutCrash) {
#else
IN_PROC_BROWSER_TEST_F(InstantTest, ShowAboutCrash) {
#endif  // OS_LINUX
  ASSERT_TRUE(test_server()->Start());
  EnableInstant();

  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

  ASSERT_NO_FATAL_FAILURE(SetLocationBarText(chrome::kAboutCrashURL));

  // If we get here it means the preview was shown. If we time out, it means the
  // preview was never shown.
}

// DISABLED http://crbug.com/80118
#if defined(OS_LINUX)
IN_PROC_BROWSER_TEST_F(InstantTest, DISABLED_InstantCompleteNever) {
#else
IN_PROC_BROWSER_TEST_F(InstantTest, InstantCompleteNever) {
#endif  // OS_LINUX
  ASSERT_TRUE(test_server()->Start());
  EnableInstant();
  ASSERT_NO_FATAL_FAILURE(SetupInstantProvider("search.html"));
  ASSERT_NO_FATAL_FAILURE(SetupLocationBar());
  ASSERT_NO_FATAL_FAILURE(SetupPreview());

  SetSuggestionsJavascriptArgument(
      preview_,
      "{suggestions:[{value:'defghij'}],complete_behavior:'never'}");
  ASSERT_NO_FATAL_FAILURE(SetLocationBarText("def"));
  EXPECT_STR_EQ("defghij", GetSuggestion());
  AutocompleteEditModel* edit_model = location_bar_->location_entry()->model();
  EXPECT_EQ(INSTANT_COMPLETE_NEVER, edit_model->instant_complete_behavior());
  ASSERT_EQ(ASCIIToUTF16("def"), location_bar_->location_entry()->GetText());
}

// DISABLED http://crbug.com/80118
#if defined(OS_LINUX)
IN_PROC_BROWSER_TEST_F(InstantTest, DISABLED_InstantCompleteDelayed) {
#else
IN_PROC_BROWSER_TEST_F(InstantTest, InstantCompleteDelayed) {
#endif  // OS_LINUX
  ASSERT_TRUE(test_server()->Start());
  EnableInstant();
  ASSERT_NO_FATAL_FAILURE(SetupInstantProvider("search.html"));
  ASSERT_NO_FATAL_FAILURE(SetupLocationBar());
  ASSERT_NO_FATAL_FAILURE(SetupPreview());

  SetSuggestionsJavascriptArgument(
      preview_,
      "{suggestions:[{value:'defghij'}],complete_behavior:'delayed'}");
  ASSERT_NO_FATAL_FAILURE(SetLocationBarText("def"));
  EXPECT_STR_EQ("defghij", GetSuggestion());
  AutocompleteEditModel* edit_model = location_bar_->location_entry()->model();
  EXPECT_EQ(INSTANT_COMPLETE_DELAYED, edit_model->instant_complete_behavior());
  ASSERT_EQ(ASCIIToUTF16("def"), location_bar_->location_entry()->GetText());
}

// Make sure the renderer doesn't crash if javascript is blocked.
// DISABLED http://crbug.com/80118
#if defined(OS_LINUX)
IN_PROC_BROWSER_TEST_F(InstantTest, DISABLED_DontCrashOnBlockedJS) {
#else
IN_PROC_BROWSER_TEST_F(InstantTest, DontCrashOnBlockedJS) {
#endif  // OS_LINUX
  browser()->profile()->GetHostContentSettingsMap()->SetDefaultContentSetting(
      CONTENT_SETTINGS_TYPE_JAVASCRIPT, CONTENT_SETTING_BLOCK);
  ASSERT_TRUE(test_server()->Start());
  EnableInstant();
  ASSERT_NO_FATAL_FAILURE(SetupInstantProvider("search.html"));
  ASSERT_NO_FATAL_FAILURE(SetupLocationBar());
  // Wait for notification that the instant API has been determined.
  ui_test_utils::WaitForNotification(
      NotificationType::INSTANT_SUPPORT_DETERMINED);
  // As long as we get the notification we're good (the renderer didn't crash).
}

// DISABLED http://crbug.com/80118
#if defined(OS_LINUX)
IN_PROC_BROWSER_TEST_F(InstantTest, DISABLED_DownloadOnEnter) {
#else
IN_PROC_BROWSER_TEST_F(InstantTest, DownloadOnEnter) {
#endif  // OS_LINUX
  ASSERT_TRUE(test_server()->Start());
  EnableInstant();
  // Make sure the browser window is the front most window.
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
  ASSERT_NO_FATAL_FAILURE(SetupInstantProvider("search.html"));
  ASSERT_NO_FATAL_FAILURE(FindLocationBar());
  GURL url(test_server()->GetURL("files/instant/empty.html"));
  location_bar_->location_entry()->SetUserText(UTF8ToUTF16(url.spec()));
  printf("0\n");
  ASSERT_NO_FATAL_FAILURE(WaitForPreviewToNavigate(true));
  url = test_server()->GetURL("files/instant/download.zip");
  location_bar_->location_entry()->SetUserText(UTF8ToUTF16(url.spec()));
  // Wait for the load to fail (because instant disables downloads).
  printf("1\n");
  ui_test_utils::WaitForNotification(
      NotificationType::FAIL_PROVISIONAL_LOAD_WITH_ERROR);

  printf("2\n");
  ui_test_utils::WindowedNotificationObserver download_observer(
      NotificationType::DOWNLOAD_INITIATED,
      NotificationService::AllSources());
  ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_RETURN));
  printf("3\n");
  download_observer.Wait();
  printf("4\n");

  // And we should end up at about:blank.
  TabContents* contents = browser()->GetSelectedTabContents();
  ASSERT_TRUE(contents);
  EXPECT_EQ("about:blank",
            contents->controller().GetLastCommittedEntry()->url().spec());
  if (contents->controller().pending_entry()) {
    // If there is a pending entry, the url should correspond to the download.
    EXPECT_EQ(url.spec(),
              contents->controller().pending_entry()->url().spec());
  }
}

// Makes sure window.chrome.searchbox doesn't persist when a new page is loaded.
// DISABLED http://crbug.com/80118
#if defined(OS_LINUX)
IN_PROC_BROWSER_TEST_F(InstantTest, DISABLED_DontPersistSearchbox) {
#else
IN_PROC_BROWSER_TEST_F(InstantTest, DontPersistSearchbox) {
#endif  // OS_LINUX
  ASSERT_TRUE(test_server()->Start());
  EnableInstant();
  ASSERT_NO_FATAL_FAILURE(SetupInstantProvider("search.html"));

  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
  ASSERT_NO_FATAL_FAILURE(SetupLocationBar());
  ASSERT_NO_FATAL_FAILURE(SetupPreview());

  ASSERT_NO_FATAL_FAILURE(SetLocationBarText("def"));
  ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_RETURN));

  // Check that the preview contents have been committed.
  ASSERT_FALSE(browser()->instant()->GetPreviewContents());
  ASSERT_FALSE(browser()->instant()->is_active());

  TabContents* contents = browser()->GetSelectedTabContents();
  ASSERT_TRUE(contents);

  // Navigate to a new URL. This should reset the searchbox values.
  ui_test_utils::NavigateToURL(
      browser(),
      GURL(test_server()->GetURL("files/instant/empty.html")));
  bool result;
  ASSERT_TRUE(GetBoolFromJavascript(
                  browser()->GetSelectedTabContents(),
                  "window.chrome.searchBox.value.length == 0",
                  &result));
  EXPECT_TRUE(result);
}
