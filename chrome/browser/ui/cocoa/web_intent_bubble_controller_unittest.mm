// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/basictypes.h"
#include "base/message_loop.h"
#import "chrome/browser/ui/cocoa/cocoa_test_helper.h"
#import "chrome/browser/ui/cocoa/hyperlink_button_cell.h"
#import "chrome/browser/ui/cocoa/info_bubble_window.h"
#import "chrome/browser/ui/cocoa/web_intent_bubble_controller.h"
#include "chrome/browser/ui/cocoa/web_intent_picker_cocoa.h"
#include "chrome/browser/ui/intents/web_intent_picker_delegate.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace {

class MockIntentPickerDelegate : public WebIntentPickerDelegate {
 public:
  virtual ~MockIntentPickerDelegate() {}

  MOCK_METHOD2(OnServiceChosen, void(size_t index, Disposition disposition));
  MOCK_METHOD1(OnInlineDispositionWebContentsCreated,
      void(content::WebContents* web_contents));
  MOCK_METHOD0(OnCancelled, void());
  MOCK_METHOD0(OnClosing, void());
};

}  // namespace

class WebIntentBubbleControllerTest : public CocoaTest {
 public:
  virtual ~WebIntentBubbleControllerTest() {
    message_loop_.RunAllPending();
  }
  virtual void TearDown() {
    // Do not animate out because that is hard to test around.
    if (window_)
      [window_ setDelayOnClose:NO];

    if (picker_.get()) {
      EXPECT_CALL(delegate_, OnCancelled());
      EXPECT_CALL(delegate_, OnClosing());

      [controller_ close];
      // Closing |controller_| destroys |picker_|.
      ignore_result(picker_.release());
    }
    CocoaTest::TearDown();
  }

  void CreatePicker() {
    picker_.reset(new WebIntentPickerCocoa());
    picker_->delegate_ = &delegate_;
    picker_->model_ = &model_;
    window_ = nil;
    controller_ = nil;
  }

  void CreateBubble() {
    CreatePicker();
    NSPoint anchor=NSMakePoint(0, 0);

    controller_ =
        [[WebIntentBubbleController alloc] initWithPicker:picker_.get()
                                             parentWindow:test_window()
                                               anchoredAt:anchor];
    window_ = static_cast<InfoBubbleWindow*>([controller_ window]);
    [controller_ showWindow:nil];
  }

  // Checks the controller's window for the requisite subviews and icons.
  void CheckWindow(size_t icon_count) {
    NSArray* flip_views = [[window_ contentView] subviews];

    // Expect 1 subview - the flip view.
    ASSERT_EQ(1U, [flip_views count]);

    NSArray* views = [[flip_views objectAtIndex:0] subviews];

    // 3 + |icon_count| subviews - Icon, Header text, |icon_count| buttons,
    // and a CWS link.
    ASSERT_EQ(3U + icon_count, [views count]);

    ASSERT_TRUE([[views objectAtIndex:0] isKindOfClass:[NSTextField class]]);
    ASSERT_TRUE([[views objectAtIndex:1] isKindOfClass:[NSImageView class]]);
    for(NSUInteger i = 0; i < icon_count; ++i) {
      ASSERT_TRUE([[views objectAtIndex:2 + i] isKindOfClass:[NSButton class]]);
    }

    // Verify the Chrome Web Store button.
    NSButton* button = static_cast<NSButton*>([views lastObject]);
    ASSERT_TRUE([button isKindOfClass:[NSButton class]]);
    EXPECT_TRUE([[button cell] isKindOfClass:[HyperlinkButtonCell class]]);
    CheckButton(button, @selector(showChromeWebStore:));

    // Verify buttons pointing to services.
    for(NSUInteger i = 0; i < icon_count; ++i) {
      NSButton* button = [views objectAtIndex:2 + i];
      CheckServiceButton(button, i);
    }

    EXPECT_EQ([window_ delegate], controller_);
  }

  // Checks that a service button is hooked up correctly.
  void CheckServiceButton(NSButton* button, NSUInteger service_index) {
    CheckButton(button, @selector(invokeService:));
    EXPECT_EQ(NSInteger(service_index), [button tag]);
  }
  // Checks that a button is hooked up correctly.
  void CheckButton(id button, SEL action) {
    EXPECT_TRUE([button isKindOfClass:[NSButton class]] ||
      [button isKindOfClass:[NSButtonCell class]]);
    EXPECT_EQ(action, [button action]);
    EXPECT_EQ(controller_, [button target]);
    EXPECT_TRUE([button stringValue]);
  }

  WebIntentBubbleController* controller_;  // Weak, owns self.
  InfoBubbleWindow* window_;  // Weak, owned by controller.
  scoped_ptr<WebIntentPickerCocoa> picker_;
  MockIntentPickerDelegate delegate_;
  MessageLoopForUI message_loop_;
  WebIntentPickerModel model_;  // The model used by the picker
};

TEST_F(WebIntentBubbleControllerTest, EmptyBubble) {
  CreateBubble();

  CheckWindow(/*icon_count=*/0);
}

TEST_F(WebIntentBubbleControllerTest, PopulatedBubble) {
  CreateBubble();

  WebIntentPickerModel model;
  model.AddItem(string16(), GURL(), WebIntentPickerModel::DISPOSITION_WINDOW);
  model.AddItem(string16(), GURL(), WebIntentPickerModel::DISPOSITION_WINDOW);

  [controller_ performLayoutWithModel:&model];

  CheckWindow(/*icon_count=*/2);
}

TEST_F(WebIntentBubbleControllerTest, OnCancelledWillSignalClose) {
  CreatePicker();

  EXPECT_CALL(delegate_, OnCancelled());
  EXPECT_CALL(delegate_, OnClosing());
  picker_->OnCancelled();

  ignore_result(picker_.release());  // Closing |picker_| will self-destruct it.
}

TEST_F(WebIntentBubbleControllerTest, CloseWillClose) {
  CreateBubble();

  EXPECT_CALL(delegate_, OnCancelled());
  EXPECT_CALL(delegate_, OnClosing());
  picker_->Close();

  ignore_result(picker_.release());  // Closing |picker_| will self-destruct it.
}

TEST_F(WebIntentBubbleControllerTest, DontCancelAfterServiceInvokation) {
  CreateBubble();
  model_.AddItem(string16(), GURL(), WebIntentPickerModel::DISPOSITION_WINDOW);

  EXPECT_CALL(delegate_, OnServiceChosen(
      0, WebIntentPickerModel::DISPOSITION_WINDOW));
  EXPECT_CALL(delegate_, OnCancelled()).Times(0);
  EXPECT_CALL(delegate_, OnClosing());

  picker_->OnServiceChosen(0);
  picker_->Close();

  ignore_result(picker_.release());  // Closing |picker_| will self-destruct it.
}
