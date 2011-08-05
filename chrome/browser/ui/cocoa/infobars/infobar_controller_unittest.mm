// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import <Cocoa/Cocoa.h>

#include "base/memory/scoped_nsobject.h"
#include "base/string_util.h"
#include "base/sys_string_conversions.h"
#include "chrome/browser/tab_contents/confirm_infobar_delegate.h"
#import "chrome/browser/ui/cocoa/cocoa_test_helper.h"
#import "chrome/browser/ui/cocoa/infobars/infobar_container_controller.h"
#import "chrome/browser/ui/cocoa/infobars/infobar_controller.h"
#include "chrome/browser/ui/cocoa/infobars/mock_confirm_infobar_delegate.h"
#include "chrome/browser/ui/cocoa/infobars/mock_link_infobar_delegate.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/platform_test.h"

@interface InfoBarController (ExposedForTesting)
- (NSString*)labelString;
- (NSRect)labelFrame;
@end

@implementation InfoBarController (ExposedForTesting)
- (NSString*)labelString {
  return [label_.get() string];
}
- (NSRect)labelFrame {
  return [label_.get() frame];
}
@end


// Calls to removeDelegate: normally start an animation, which removes the
// infobar completely when finished.  For unittesting purposes, we create a mock
// container which calls close: immediately, rather than kicking off an
// animation.
@interface InfoBarContainerTest : NSObject<InfoBarContainer> {
  InfoBarController* controller_;
}
- (id)initWithController:(InfoBarController*)controller;
- (void)removeDelegate:(InfoBarDelegate*)delegate;
- (void)willRemoveController:(InfoBarController*)controller;
- (void)removeController:(InfoBarController*)controller;
@end

@implementation InfoBarContainerTest
- (id)initWithController:(InfoBarController*)controller {
  if ((self = [super init])) {
    controller_ = controller;
  }
  return self;
}

- (void)removeDelegate:(InfoBarDelegate*)delegate {
  [controller_ close];
}

- (void)willRemoveController:(InfoBarController*)controller {
}

- (void)removeController:(InfoBarController*)controller {
  DCHECK(controller_ == controller);
  controller_ = nil;
}
@end

namespace {

///////////////////////////////////////////////////////////////////////////
// Test fixtures

class LinkInfoBarControllerTest : public CocoaTest,
                                  public MockLinkInfoBarDelegate::Owner {
 public:
  virtual void SetUp() {
    CocoaTest::SetUp();

    delegate_ = new MockLinkInfoBarDelegate(this);
    controller_.reset(
        [[LinkInfoBarController alloc] initWithDelegate:delegate_]);
    container_.reset(
        [[InfoBarContainerTest alloc] initWithController:controller_]);
    [controller_ setContainerController:container_];
    [[test_window() contentView] addSubview:[controller_ view]];
    closed_delegate_link_clicked_ = false;
  }

  virtual void TearDown() {
    if (delegate_)
      delete delegate_;
    CocoaTest::TearDown();
  }

 protected:
  // Hopefully-obvious: If this returns true, you must not deref |delegate_|!
  bool delegate_closed() const { return delegate_ == NULL; }

  MockLinkInfoBarDelegate* delegate_;  // Owns itself.
  scoped_nsobject<id> container_;
  scoped_nsobject<LinkInfoBarController> controller_;
  bool closed_delegate_link_clicked_;

 private:
  virtual void OnInfoBarDelegateClosed() {
    closed_delegate_link_clicked_ = delegate_->link_clicked();
    delegate_ = NULL;
  }
};

class ConfirmInfoBarControllerTest : public CocoaTest,
                                     public MockConfirmInfoBarDelegate::Owner {
 public:
  virtual void SetUp() {
    CocoaTest::SetUp();

    delegate_ = new MockConfirmInfoBarDelegate(this);
    controller_.reset(
        [[ConfirmInfoBarController alloc] initWithDelegate:delegate_]);
    container_.reset(
        [[InfoBarContainerTest alloc] initWithController:controller_]);
    [controller_ setContainerController:container_];
    [[test_window() contentView] addSubview:[controller_ view]];
    closed_delegate_ok_clicked_ = false;
    closed_delegate_cancel_clicked_ = false;
    closed_delegate_link_clicked_ = false;
  }

  virtual void TearDown() {
    if (delegate_)
      delete delegate_;
    CocoaTest::TearDown();
  }

 protected:
  // Hopefully-obvious: If this returns true, you must not deref |delegate_|!
  bool delegate_closed() const { return delegate_ == NULL; }

  MockConfirmInfoBarDelegate* delegate_;  // Owns itself.
  scoped_nsobject<id> container_;
  scoped_nsobject<ConfirmInfoBarController> controller_;
  bool closed_delegate_ok_clicked_;
  bool closed_delegate_cancel_clicked_;
  bool closed_delegate_link_clicked_;

 private:
  virtual void OnInfoBarDelegateClosed() {
    closed_delegate_ok_clicked_ = delegate_->ok_clicked();
    closed_delegate_cancel_clicked_ = delegate_->cancel_clicked();
    closed_delegate_link_clicked_ = delegate_->link_clicked();
    delegate_ = NULL;
  }
};


////////////////////////////////////////////////////////////////////////////
// Tests

TEST_VIEW(LinkInfoBarControllerTest, [controller_ view]);

TEST_F(LinkInfoBarControllerTest, ShowAndDismiss) {
  // Make sure someone looked at the message, link, and icon.
  EXPECT_TRUE(delegate_->message_text_accessed());
  EXPECT_TRUE(delegate_->link_text_accessed());
  EXPECT_TRUE(delegate_->icon_accessed());

  // Check that dismissing the infobar deletes the delegate.
  [controller_ dismiss:nil];
  ASSERT_TRUE(delegate_closed());
  EXPECT_FALSE(closed_delegate_link_clicked_);
}

TEST_F(LinkInfoBarControllerTest, ShowAndClickLink) {
  // Check that clicking on the link calls LinkClicked() on the
  // delegate.  It should also close the infobar.
  [controller_ linkClicked];
  ASSERT_TRUE(delegate_closed());
  EXPECT_TRUE(closed_delegate_link_clicked_);
}

TEST_F(LinkInfoBarControllerTest, ShowAndClickLinkWithoutClosing) {
  delegate_->set_dont_close_on_action();

  // Check that clicking on the link calls LinkClicked() on the
  // delegate.  It should not close the infobar.
  [controller_ linkClicked];
  ASSERT_FALSE(delegate_closed());
  EXPECT_TRUE(delegate_->link_clicked());
}

TEST_F(LinkInfoBarControllerTest, DeallocController) {
  // Test that dealloc'ing the controller does not delete the delegate.
  controller_.reset(nil);
  ASSERT_FALSE(delegate_closed());
}

TEST_VIEW(ConfirmInfoBarControllerTest, [controller_ view]);

TEST_F(ConfirmInfoBarControllerTest, ShowAndDismiss) {
  // Make sure someone looked at the message, link, and icon.
  EXPECT_TRUE(delegate_->message_text_accessed());
  EXPECT_TRUE(delegate_->link_text_accessed());
  EXPECT_TRUE(delegate_->icon_accessed());

  // Check to make sure the infobar message was set properly.
  EXPECT_EQ(MockConfirmInfoBarDelegate::kMessage,
            base::SysNSStringToUTF8([controller_.get() labelString]));

  // Check that dismissing the infobar deletes the delegate.
  [controller_ dismiss:nil];
  ASSERT_TRUE(delegate_closed());
  EXPECT_FALSE(closed_delegate_ok_clicked_);
  EXPECT_FALSE(closed_delegate_cancel_clicked_);
  EXPECT_FALSE(closed_delegate_link_clicked_);
}

TEST_F(ConfirmInfoBarControllerTest, ShowAndClickOK) {
  // Check that clicking the OK button calls Accept() and then closes
  // the infobar.
  [controller_ ok:nil];
  ASSERT_TRUE(delegate_closed());
  EXPECT_TRUE(closed_delegate_ok_clicked_);
  EXPECT_FALSE(closed_delegate_cancel_clicked_);
  EXPECT_FALSE(closed_delegate_link_clicked_);
}

TEST_F(ConfirmInfoBarControllerTest, ShowAndClickOKWithoutClosing) {
  delegate_->set_dont_close_on_action();

  // Check that clicking the OK button calls Accept() but does not close
  // the infobar.
  [controller_ ok:nil];
  ASSERT_FALSE(delegate_closed());
  EXPECT_TRUE(delegate_->ok_clicked());
  EXPECT_FALSE(delegate_->cancel_clicked());
  EXPECT_FALSE(delegate_->link_clicked());
}

TEST_F(ConfirmInfoBarControllerTest, ShowAndClickCancel) {
  // Check that clicking the cancel button calls Cancel() and closes
  // the infobar.
  [controller_ cancel:nil];
  ASSERT_TRUE(delegate_closed());
  EXPECT_FALSE(closed_delegate_ok_clicked_);
  EXPECT_TRUE(closed_delegate_cancel_clicked_);
  EXPECT_FALSE(closed_delegate_link_clicked_);
}

TEST_F(ConfirmInfoBarControllerTest, ShowAndClickCancelWithoutClosing) {
  delegate_->set_dont_close_on_action();

  // Check that clicking the cancel button calls Cancel() but does not close
  // the infobar.
  [controller_ cancel:nil];
  ASSERT_FALSE(delegate_closed());
  EXPECT_FALSE(delegate_->ok_clicked());
  EXPECT_TRUE(delegate_->cancel_clicked());
  EXPECT_FALSE(delegate_->link_clicked());
}

TEST_F(ConfirmInfoBarControllerTest, ShowAndClickLink) {
  // Check that clicking on the link calls LinkClicked() on the
  // delegate.  It should also close the infobar.
  [controller_ linkClicked];
  ASSERT_TRUE(delegate_closed());
  EXPECT_FALSE(closed_delegate_ok_clicked_);
  EXPECT_FALSE(closed_delegate_cancel_clicked_);
  EXPECT_TRUE(closed_delegate_link_clicked_);
}

TEST_F(ConfirmInfoBarControllerTest, ShowAndClickLinkWithoutClosing) {
  delegate_->set_dont_close_on_action();

  // Check that clicking on the link calls LinkClicked() on the
  // delegate.  It should not close the infobar.
  [controller_ linkClicked];
  ASSERT_FALSE(delegate_closed());
  EXPECT_FALSE(delegate_->ok_clicked());
  EXPECT_FALSE(delegate_->cancel_clicked());
  EXPECT_TRUE(delegate_->link_clicked());
}

TEST_F(ConfirmInfoBarControllerTest, ResizeView) {
  NSRect originalLabelFrame = [controller_ labelFrame];

  // Expand the view by 20 pixels and make sure the label frame changes
  // accordingly.
  const CGFloat width = 20;
  NSRect newViewFrame = [[controller_ view] frame];
  newViewFrame.size.width += width;
  [[controller_ view] setFrame:newViewFrame];

  NSRect newLabelFrame = [controller_ labelFrame];
  EXPECT_EQ(NSWidth(newLabelFrame), NSWidth(originalLabelFrame) + width);
}

}  // namespace
