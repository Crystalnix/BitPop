// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/options/core_options_handler.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/ui_test_utils.h"
#include "content/browser/webui/web_ui_browsertest.h"
#include "googleurl/src/gurl.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::StrictMock;
using ::testing::_;

MATCHER_P(Eq_ListValue, inList, "") {
  return arg->Equals(inList);
}

class MockCoreOptionsHandler : public CoreOptionsHandler {
 public:
  MOCK_METHOD1(HandleInitialize,
      void(const ListValue* args));
  MOCK_METHOD1(HandleFetchPrefs,
      void(const ListValue* args));
  MOCK_METHOD1(HandleObservePrefs,
      void(const ListValue* args));
  MOCK_METHOD1(HandleSetBooleanPref,
      void(const ListValue* args));
  MOCK_METHOD1(HandleSetIntegerPref,
      void(const ListValue* args));
  MOCK_METHOD1(HandleSetDoublePref,
      void(const ListValue* args));
  MOCK_METHOD1(HandleSetStringPref,
      void(const ListValue* args));
  MOCK_METHOD1(HandleSetObjectPref,
      void(const ListValue* args));
  MOCK_METHOD1(HandleClearPref,
      void(const ListValue* args));
  MOCK_METHOD1(HandleUserMetricsAction,
      void(const ListValue* args));

  virtual void RegisterMessages() {
    web_ui_->RegisterMessageCallback("coreOptionsInitialize",
        NewCallback(this, &MockCoreOptionsHandler ::HandleInitialize));
    web_ui_->RegisterMessageCallback("fetchPrefs",
        NewCallback(this, &MockCoreOptionsHandler ::HandleFetchPrefs));
    web_ui_->RegisterMessageCallback("observePrefs",
        NewCallback(this, &MockCoreOptionsHandler ::HandleObservePrefs));
    web_ui_->RegisterMessageCallback("setBooleanPref",
        NewCallback(this, &MockCoreOptionsHandler ::HandleSetBooleanPref));
    web_ui_->RegisterMessageCallback("setIntegerPref",
        NewCallback(this, &MockCoreOptionsHandler ::HandleSetIntegerPref));
    web_ui_->RegisterMessageCallback("setDoublePref",
        NewCallback(this, &MockCoreOptionsHandler ::HandleSetDoublePref));
    web_ui_->RegisterMessageCallback("setStringPref",
        NewCallback(this, &MockCoreOptionsHandler ::HandleSetStringPref));
    web_ui_->RegisterMessageCallback("setObjectPref",
        NewCallback(this, &MockCoreOptionsHandler ::HandleSetObjectPref));
    web_ui_->RegisterMessageCallback("clearPref",
        NewCallback(this, &MockCoreOptionsHandler ::HandleClearPref));
    web_ui_->RegisterMessageCallback("coreOptionsUserMetricsAction",
        NewCallback(this, &MockCoreOptionsHandler ::HandleUserMetricsAction));
  }
};

class SettingsWebUITest : public WebUIBrowserTest {
 protected:
  virtual void SetUpInProcessBrowserTestFixture() {
    WebUIBrowserTest::SetUpInProcessBrowserTestFixture();
    AddLibrary(FILE_PATH_LITERAL("settings.js"));
  }

  virtual void SetUpOnMainThread() {
    mock_core_options_handler_.reset(new StrictMock<MockCoreOptionsHandler>());
  }

  virtual void CleanUpOnMainThread() {
    mock_core_options_handler_.reset();
  }

  virtual WebUIMessageHandler* GetMockMessageHandler() {
    return mock_core_options_handler_.get();
  }

  scoped_ptr<StrictMock<MockCoreOptionsHandler> > mock_core_options_handler_;
};

// Test the end to end js to WebUI handler code path for
// the message setBooleanPref.
// TODO(dtseng): add more EXPECT_CALL's when updating js test.

// Crashes on Mac only. See http://crbug.com/79181
#if defined(OS_MACOSX)
#define MAYBE_TestSetBooleanPrefTriggers DISABLED_TestSetBooleanPrefTriggers
#else
#define MAYBE_TestSetBooleanPrefTriggers TestSetBooleanPrefTriggers
#endif
IN_PROC_BROWSER_TEST_F(SettingsWebUITest, MAYBE_TestSetBooleanPrefTriggers) {
  // This serves as an example of a very constrained test.
  ListValue true_list_value;
  true_list_value.Append(Value::CreateStringValue("browser.show_home_button"));
  true_list_value.Append(Value::CreateBooleanValue(true));
  true_list_value.Append(
      Value::CreateStringValue("Options_Homepage_HomeButton"));
  ui_test_utils::NavigateToURL(browser(), GURL(chrome::kChromeUISettingsURL));
  EXPECT_CALL(*mock_core_options_handler_,
      HandleSetBooleanPref(Eq_ListValue(&true_list_value)));
  ASSERT_TRUE(RunJavascriptTest("testSetBooleanPrefTriggers"));
}
