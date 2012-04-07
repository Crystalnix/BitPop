// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/bind.h"
#include "base/callback.h"
#include "base/memory/scoped_ptr.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/favicon/favicon_service.h"
#include "chrome/browser/intents/web_intents_registry.h"
#include "chrome/browser/intents/web_intents_registry_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/intents/web_intent_picker.h"
#include "chrome/browser/ui/intents/web_intent_picker_controller.h"
#include "chrome/browser/ui/intents/web_intent_picker_model.h"
#include "chrome/browser/ui/intents/web_intent_picker_model_observer.h"
#include "chrome/browser/ui/tab_contents/tab_contents_wrapper.h"
#include "chrome/browser/webdata/web_data_service.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_intents_dispatcher.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "webkit/glue/web_intent_service_data.h"

namespace {

const string16 kAction1(ASCIIToUTF16("http://www.example.com/share"));
const string16 kAction2(ASCIIToUTF16("http://www.example.com/foobar"));
const string16 kType(ASCIIToUTF16("image/png"));
const GURL kServiceURL1("http://www.google.com");
const GURL kServiceURL2("http://www.chromium.org");

}  // namespace

class WebIntentPickerMock : public WebIntentPicker,
                            public WebIntentPickerModelObserver {
 public:
  WebIntentPickerMock()
      : num_items_(0),
        num_icons_changed_(0),
        message_loop_started_(false),
        pending_async_completed_(false) {
  }

  virtual void OnModelChanged(WebIntentPickerModel* model) OVERRIDE {
    num_items_ = static_cast<int>(model->GetItemCount());
  }

  virtual void OnFaviconChanged(
      WebIntentPickerModel* model, size_t index) OVERRIDE {
    num_icons_changed_++;
  }

  virtual void OnInlineDisposition(WebIntentPickerModel* model) OVERRIDE {}
  virtual void Close() OVERRIDE {}

  virtual void OnPendingAsyncCompleted() OVERRIDE {
    pending_async_completed_ = true;

    if (message_loop_started_)
      MessageLoop::current()->Quit();
  }

  void WaitForPendingAsync() {
    if (!pending_async_completed_) {
      message_loop_started_ = true;
      ui_test_utils::RunMessageLoop();
    }
  }

  int num_items_;
  int num_icons_changed_;
  bool message_loop_started_;
  bool pending_async_completed_;
};

class IntentsDispatcherMock : public content::WebIntentsDispatcher {
 public:
  explicit IntentsDispatcherMock(const webkit_glue::WebIntentData& intent)
      : intent_(intent),
        dispatched_(false) {}

  virtual const webkit_glue::WebIntentData& GetIntent() OVERRIDE {
    return intent_;
  }

  virtual void DispatchIntent(content::WebContents* web_contents) OVERRIDE {
    dispatched_ = true;
  }

  virtual void SendReplyMessage(webkit_glue::WebIntentReplyType reply_type,
                                const string16& data) OVERRIDE {
  }

  virtual void RegisterReplyNotification(
      const base::Callback<void(webkit_glue::WebIntentReplyType)>&) OVERRIDE {
  }

  webkit_glue::WebIntentData intent_;
  bool dispatched_;
};

class WebIntentPickerControllerBrowserTest : public InProcessBrowserTest {
 protected:
  typedef WebIntentPickerModel::Disposition Disposition;

  WebIntentPickerControllerBrowserTest() {}

  virtual void SetUpOnMainThread() OVERRIDE {
    web_data_service_ =
        browser()->profile()->GetWebDataService(Profile::EXPLICIT_ACCESS);
    favicon_service_ =
        browser()->profile()->GetFaviconService(Profile::EXPLICIT_ACCESS);
    controller_ = browser()->
        GetSelectedTabContentsWrapper()->web_intent_picker_controller();

    controller_->set_picker(&picker_);
    controller_->set_model_observer(&picker_);
  }

  void AddWebIntentService(const string16& action, const GURL& service_url) {
    webkit_glue::WebIntentServiceData service;
    service.action = action;
    service.type = kType;
    service.service_url = service_url;
    web_data_service_->AddWebIntentService(service);
  }

  void OnSendReturnMessage(
    webkit_glue::WebIntentReplyType reply_type) {
    controller_->OnSendReturnMessage(reply_type);
  }

  void OnServiceChosen(size_t index, Disposition disposition) {
    controller_->OnServiceChosen(index, disposition);
  }

  void OnCancelled() {
    controller_->OnCancelled();
  }

  WebIntentPickerMock picker_;
  WebDataService* web_data_service_;
  FaviconService* favicon_service_;
  WebIntentPickerController* controller_;
};

IN_PROC_BROWSER_TEST_F(WebIntentPickerControllerBrowserTest, ChooseService) {
  AddWebIntentService(kAction1, kServiceURL1);
  AddWebIntentService(kAction1, kServiceURL2);

  controller_->ShowDialog(browser(), kAction1, kType);
  picker_.WaitForPendingAsync();
  EXPECT_EQ(2, picker_.num_items_);
  EXPECT_EQ(0, picker_.num_icons_changed_);

  webkit_glue::WebIntentData intent;
  intent.action = ASCIIToUTF16("a");
  intent.type = ASCIIToUTF16("b");
  IntentsDispatcherMock dispatcher(intent);
  controller_->SetIntentsDispatcher(&dispatcher);

  OnServiceChosen(1, WebIntentPickerModel::DISPOSITION_WINDOW);
  ASSERT_EQ(2, browser()->tab_count());
  EXPECT_EQ(GURL(kServiceURL2),
            browser()->GetSelectedWebContents()->GetURL());

  EXPECT_TRUE(dispatcher.dispatched_);

  OnSendReturnMessage(webkit_glue::WEB_INTENT_REPLY_SUCCESS);
  ASSERT_EQ(1, browser()->tab_count());
}

IN_PROC_BROWSER_TEST_F(WebIntentPickerControllerBrowserTest, OpenCancelOpen) {
  AddWebIntentService(kAction1, kServiceURL1);
  AddWebIntentService(kAction1, kServiceURL2);

  controller_->ShowDialog(browser(), kAction1, kType);
  picker_.WaitForPendingAsync();
  OnCancelled();

  controller_->ShowDialog(browser(), kAction1, kType);
  OnCancelled();
}

IN_PROC_BROWSER_TEST_F(WebIntentPickerControllerBrowserTest,
                       CloseTargetTabReturnToSource) {
  AddWebIntentService(kAction1, kServiceURL1);

  GURL original = browser()->GetSelectedWebContents()->GetURL();

  // Open a new page, but keep focus on original.
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL(chrome::kChromeUINewTabURL), NEW_BACKGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  ASSERT_EQ(2, browser()->tab_count());
  EXPECT_EQ(original, browser()->GetSelectedWebContents()->GetURL());

  controller_->ShowDialog(browser(), kAction1, kType);
  picker_.WaitForPendingAsync();
  EXPECT_EQ(1, picker_.num_items_);

  webkit_glue::WebIntentData intent;
  intent.action = ASCIIToUTF16("a");
  intent.type = ASCIIToUTF16("b");
  IntentsDispatcherMock dispatcher(intent);
  controller_->SetIntentsDispatcher(&dispatcher);

  OnServiceChosen(0, WebIntentPickerModel::DISPOSITION_WINDOW);
  ASSERT_EQ(3, browser()->tab_count());
  EXPECT_EQ(GURL(kServiceURL1),
            browser()->GetSelectedWebContents()->GetURL());

  EXPECT_TRUE(dispatcher.dispatched_);

  OnSendReturnMessage(webkit_glue::WEB_INTENT_REPLY_SUCCESS);
  ASSERT_EQ(2, browser()->tab_count());
  EXPECT_EQ(original, browser()->GetSelectedWebContents()->GetURL());
}
