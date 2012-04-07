// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/format_macros.h"
#include "base/string_util.h"
#include "base/stringprintf.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/autocomplete/autocomplete.h"
#include "chrome/browser/autocomplete/autocomplete_edit.h"
#include "chrome/browser/autocomplete/autocomplete_match.h"
#include "chrome/browser/autocomplete/autocomplete_popup_model.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url.h"
#include "chrome/browser/search_engines/template_url_service.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/omnibox/location_bar.h"
#include "chrome/browser/ui/omnibox/omnibox_view.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/notification_service.h"

#if defined(TOOLKIT_GTK)
#include "chrome/browser/ui/gtk/browser_window_gtk.h"
#endif

namespace {

string16 AutocompleteResultAsString(const AutocompleteResult& result) {
  std::string output(base::StringPrintf("{%" PRIuS "} ", result.size()));
  for (size_t i = 0; i < result.size(); ++i) {
    AutocompleteMatch match = result.match_at(i);
    std::string provider_name = match.provider->name();
    output.append(base::StringPrintf("[\"%s\" by \"%s\"] ",
                                     UTF16ToUTF8(match.contents).c_str(),
                                     provider_name.c_str()));
  }
  return UTF8ToUTF16(output);
}

}  // namespace

class OmniboxApiTest : public ExtensionApiTest {
 protected:
  LocationBar* GetLocationBar(Browser* browser) const {
    return browser->window()->GetLocationBar();
  }

  AutocompleteController* GetAutocompleteController(Browser* browser) const {
    return GetLocationBar(browser)->location_entry()->model()->popup_model()->
        autocomplete_controller();
  }

  void WaitForTemplateURLServiceToLoad() {
    ui_test_utils::WindowedNotificationObserver loaded_observer(
        chrome::NOTIFICATION_TEMPLATE_URL_SERVICE_LOADED,
        content::NotificationService::AllSources());
    TemplateURLService* model =
        TemplateURLServiceFactory::GetForProfile(browser()->profile());
    model->Load();
    if (!model->loaded())
      loaded_observer.Wait();
  }

  // TODO(phajdan.jr): Get rid of this wait-in-a-loop pattern.
  void WaitForAutocompleteDone(AutocompleteController* controller) {
    while (!controller->done()) {
      ui_test_utils::WindowedNotificationObserver ready_observer(
          chrome::NOTIFICATION_AUTOCOMPLETE_CONTROLLER_RESULT_READY,
          content::Source<AutocompleteController>(controller));
      ready_observer.Wait();
    }
  }
};

IN_PROC_BROWSER_TEST_F(OmniboxApiTest, Basic) {
#if defined(TOOLKIT_GTK)
  // Disable the timer because, on Lucid at least, it triggers resize/move
  // behavior in the browser window, which dismisses the autocomplete popup
  // before the results can be read.
  static_cast<BrowserWindowGtk*>(
      browser()->window())->DisableDebounceTimerForTests(true);
#endif

  ASSERT_TRUE(RunExtensionTest("omnibox")) << message_;

  // The results depend on the TemplateURLService being loaded. Make sure it is
  // loaded so that the autocomplete results are consistent.
  WaitForTemplateURLServiceToLoad();

  LocationBar* location_bar = GetLocationBar(browser());
  AutocompleteController* autocomplete_controller =
      GetAutocompleteController(browser());

  // Test that our extension's keyword is suggested to us when we partially type
  // it.
  {
    autocomplete_controller->Start(
        ASCIIToUTF16("keywor"), string16(), true, false, true,
        AutocompleteInput::ALL_MATCHES);

    WaitForAutocompleteDone(autocomplete_controller);
    EXPECT_TRUE(autocomplete_controller->done());
    EXPECT_EQ(string16(), location_bar->GetInputString());
    EXPECT_EQ(string16(), location_bar->location_entry()->GetText());
    EXPECT_TRUE(location_bar->location_entry()->IsSelectAll());

    // First result should be to search for what was typed, second should be to
    // enter "extension keyword" mode.
    const AutocompleteResult& result = autocomplete_controller->result();
    ASSERT_EQ(2U, result.size()) << AutocompleteResultAsString(result);
    AutocompleteMatch match = result.match_at(0);
    EXPECT_EQ(AutocompleteMatch::SEARCH_WHAT_YOU_TYPED, match.type);
    EXPECT_FALSE(match.deletable);

    match = result.match_at(1);
    ASSERT_TRUE(match.template_url);
    EXPECT_TRUE(match.template_url->IsExtensionKeyword());
    EXPECT_EQ(ASCIIToUTF16("keyword"), match.template_url->keyword());
  }

  // Test that our extension can send suggestions back to us.
  {
    autocomplete_controller->Start(
        ASCIIToUTF16("keyword suggestio"), string16(), true, false, true,
        AutocompleteInput::ALL_MATCHES);

    WaitForAutocompleteDone(autocomplete_controller);
    EXPECT_TRUE(autocomplete_controller->done());

    // First result should be to invoke the keyword with what we typed, 2-4
    // should be to invoke with suggestions from the extension, and the last
    // should be to search for what we typed.
    const AutocompleteResult& result = autocomplete_controller->result();
    ASSERT_EQ(5U, result.size()) << AutocompleteResultAsString(result);

    ASSERT_TRUE(result.match_at(0).template_url);
    EXPECT_EQ(ASCIIToUTF16("keyword suggestio"),
              result.match_at(0).fill_into_edit);
    EXPECT_EQ(ASCIIToUTF16("keyword suggestion1"),
              result.match_at(1).fill_into_edit);
    EXPECT_EQ(ASCIIToUTF16("keyword suggestion2"),
              result.match_at(2).fill_into_edit);
    EXPECT_EQ(ASCIIToUTF16("keyword suggestion3"),
              result.match_at(3).fill_into_edit);

    string16 description =
        ASCIIToUTF16("Description with style: <match>, [dim], (url till end)");
    EXPECT_EQ(description, result.match_at(1).contents);
    ASSERT_EQ(6u, result.match_at(1).contents_class.size());

    EXPECT_EQ(0u,
              result.match_at(1).contents_class[0].offset);
    EXPECT_EQ(ACMatchClassification::NONE,
              result.match_at(1).contents_class[0].style);

    EXPECT_EQ(description.find('<'),
              result.match_at(1).contents_class[1].offset);
    EXPECT_EQ(ACMatchClassification::MATCH,
              result.match_at(1).contents_class[1].style);

    EXPECT_EQ(description.find('>') + 1u,
              result.match_at(1).contents_class[2].offset);
    EXPECT_EQ(ACMatchClassification::NONE,
              result.match_at(1).contents_class[2].style);

    EXPECT_EQ(description.find('['),
              result.match_at(1).contents_class[3].offset);
    EXPECT_EQ(ACMatchClassification::DIM,
              result.match_at(1).contents_class[3].style);

    EXPECT_EQ(description.find(']') + 1u,
              result.match_at(1).contents_class[4].offset);
    EXPECT_EQ(ACMatchClassification::NONE,
              result.match_at(1).contents_class[4].style);

    EXPECT_EQ(description.find('('),
              result.match_at(1).contents_class[5].offset);
    EXPECT_EQ(ACMatchClassification::URL,
              result.match_at(1).contents_class[5].style);

    AutocompleteMatch match = result.match_at(4);
    EXPECT_EQ(AutocompleteMatch::SEARCH_WHAT_YOU_TYPED, match.type);
    EXPECT_FALSE(match.deletable);
  }

  {
    ResultCatcher catcher;
    autocomplete_controller->Start(
        ASCIIToUTF16("keyword command"), string16(), true, false, true,
        AutocompleteInput::ALL_MATCHES);
    location_bar->AcceptInput();
    EXPECT_TRUE(catcher.GetNextResult()) << catcher.message();
  }
}

// Tests that the autocomplete popup doesn't reopen after accepting input for
// a given query.
// http://crbug.com/88552
IN_PROC_BROWSER_TEST_F(OmniboxApiTest, PopupStaysClosed) {
#if defined(TOOLKIT_GTK)
  // Disable the timer because, on Lucid at least, it triggers resize/move
  // behavior in the browser window, which dismisses the autocomplete popup
  // before the results can be read.
  static_cast<BrowserWindowGtk*>(
    browser()->window())->DisableDebounceTimerForTests(true);
#endif

  ASSERT_TRUE(RunExtensionTest("omnibox")) << message_;

  // The results depend on the TemplateURLService being loaded. Make sure it is
  // loaded so that the autocomplete results are consistent.
  WaitForTemplateURLServiceToLoad();

  LocationBar* location_bar = GetLocationBar(browser());
  AutocompleteController* autocomplete_controller =
      GetAutocompleteController(browser());
  AutocompletePopupModel* popup_model =
      GetLocationBar(browser())->location_entry()->model()->popup_model();

  // Input a keyword query and wait for suggestions from the extension.
  autocomplete_controller->Start(
      ASCIIToUTF16("keyword comman"), string16(), true, false, true,
      AutocompleteInput::ALL_MATCHES);
  WaitForAutocompleteDone(autocomplete_controller);
  EXPECT_TRUE(autocomplete_controller->done());
  EXPECT_TRUE(popup_model->IsOpen());

  // Quickly type another query and accept it before getting suggestions back
  // for the query. The popup will close after accepting input - ensure that it
  // does not reopen when the extension returns its suggestions.
  ResultCatcher catcher;
  autocomplete_controller->Start(
      ASCIIToUTF16("keyword command"), string16(), true, false, true,
      AutocompleteInput::ALL_MATCHES);
  location_bar->AcceptInput();
  WaitForAutocompleteDone(autocomplete_controller);
  EXPECT_TRUE(autocomplete_controller->done());
  EXPECT_TRUE(catcher.GetNextResult()) << catcher.message();
  EXPECT_FALSE(popup_model->IsOpen());
}

// Tests that we get suggestions from and send input to the incognito context
// of an incognito split mode extension.
// http://crbug.com/100927
// Test flaky on linux: http://crbug.com/101219
IN_PROC_BROWSER_TEST_F(OmniboxApiTest, DISABLED_IncognitoSplitMode) {
#if defined(TOOLKIT_GTK)
  // Disable the timer because, on Lucid at least, it triggers resize/move
  // behavior in the browser window, which dismisses the autocomplete popup
  // before the results can be read.
  static_cast<BrowserWindowGtk*>(
    browser()->window())->DisableDebounceTimerForTests(true);
#endif

  ResultCatcher catcher_incognito;
  catcher_incognito.RestrictToProfile(
      browser()->profile()->GetOffTheRecordProfile());

  ASSERT_TRUE(RunExtensionTestIncognito("omnibox")) << message_;

  // Open an incognito window and wait for the incognito extension process to
  // respond.
  Browser* incognito_browser = CreateIncognitoBrowser();
  ASSERT_TRUE(catcher_incognito.GetNextResult()) << catcher_incognito.message();

  // The results depend on the TemplateURLService being loaded. Make sure it is
  // loaded so that the autocomplete results are consistent.
  WaitForTemplateURLServiceToLoad();

  LocationBar* location_bar = GetLocationBar(incognito_browser);
  AutocompleteController* autocomplete_controller =
      GetAutocompleteController(incognito_browser);

  // Test that we get the incognito-specific suggestions.
  {
    autocomplete_controller->Start(
        ASCIIToUTF16("keyword suggestio"), string16(), true, false, true,
        AutocompleteInput::ALL_MATCHES);

    WaitForAutocompleteDone(autocomplete_controller);
    EXPECT_TRUE(autocomplete_controller->done());

    // First result should be to invoke the keyword with what we typed, 2-4
    // should be to invoke with suggestions from the extension, and the last
    // should be to search for what we typed.
    const AutocompleteResult& result = autocomplete_controller->result();
    ASSERT_EQ(5U, result.size()) << AutocompleteResultAsString(result);
    ASSERT_TRUE(result.match_at(0).template_url);
    EXPECT_EQ(ASCIIToUTF16("keyword suggestion3 incognito"),
              result.match_at(3).fill_into_edit);
  }

  // Test that our input is sent to the incognito context. The test will do a
  // text comparison and succeed only if "command incognito" is sent to the
  // incognito context.
  {
    ResultCatcher catcher;
    autocomplete_controller->Start(
        ASCIIToUTF16("keyword command incognito"), string16(),
        true, false, true, AutocompleteInput::ALL_MATCHES);
    location_bar->AcceptInput();
    EXPECT_TRUE(catcher.GetNextResult()) << catcher.message();
  }
}
