// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/scoped_nsobject.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/notifications/balloon.h"
#include "chrome/browser/notifications/balloon_collection.h"
#include "chrome/browser/notifications/notification.h"
#include "chrome/browser/notifications/notification_object_proxy.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/cocoa/cocoa_test_helper.h"
#include "chrome/browser/ui/cocoa/notifications/balloon_controller.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "chrome/test/base/test_browser_window.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/test_browser_thread.h"

using content::BrowserThread;

// Subclass balloon controller and mock out the initialization of the RVH.
@interface TestBalloonController : BalloonController {
}
- (void)initializeHost;
@end

@implementation TestBalloonController
- (void)initializeHost {}
@end

namespace {

// Use a dummy balloon collection for testing.
class MockBalloonCollection : public BalloonCollection {
  virtual void Add(const Notification& notification,
                   Profile* profile) {}
  virtual bool RemoveById(const std::string& id) { return false; }
  virtual bool RemoveBySourceOrigin(const GURL& origin) { return false; }
  virtual bool RemoveByProfile(Profile* profile) { return false; }
  virtual void RemoveAll() {}
  virtual bool HasSpace() const { return true; }
  virtual void ResizeBalloon(Balloon* balloon, const gfx::Size& size) {}
  virtual void DisplayChanged() {}
  virtual void SetPositionPreference(PositionPreference preference) {}
  virtual void OnBalloonClosed(Balloon* source) {};
  virtual const Balloons& GetActiveBalloons() {
    NOTREACHED();
    return balloons_;
  }
 private:
  Balloons balloons_;
};

class BalloonControllerTest : public ChromeRenderViewHostTestHarness {
 public:
  BalloonControllerTest() :
      ui_thread_(BrowserThread::UI, MessageLoop::current()),
      file_user_blocking_thread_(
            BrowserThread::FILE_USER_BLOCKING, MessageLoop::current()),
      io_thread_(BrowserThread::IO, MessageLoop::current()) {
  }

  virtual void SetUp() {
    ChromeRenderViewHostTestHarness::SetUp();
    CocoaTest::BootstrapCocoa();
    profile()->CreateRequestContext();
    browser_.reset(chrome::CreateBrowserWithTestWindowForProfile(profile()));
    collection_.reset(new MockBalloonCollection());
  }

  virtual void TearDown() {
    collection_.reset();
    browser_.reset();
    MessageLoop::current()->RunUntilIdle();
    ChromeRenderViewHostTestHarness::TearDown();
  }

 protected:
  content::TestBrowserThread ui_thread_;
  content::TestBrowserThread file_user_blocking_thread_;
  content::TestBrowserThread io_thread_;
  scoped_ptr<Browser> browser_;
  scoped_ptr<BalloonCollection> collection_;
};

TEST_F(BalloonControllerTest, ShowAndCloseTest) {
  Notification n(GURL("http://www.google.com"), GURL("http://www.google.com"),
      ASCIIToUTF16("http://www.google.com"), string16(),
      new NotificationObjectProxy(-1, -1, -1, false));
  scoped_ptr<Balloon> balloon(
      new Balloon(n, profile(), collection_.get()));
  balloon->SetPosition(gfx::Point(1, 1), false);
  balloon->set_content_size(gfx::Size(100, 100));

  BalloonController* controller =
      [[TestBalloonController alloc] initWithBalloon:balloon.get()];

  [controller showWindow:nil];
  [controller closeBalloon:YES];
}

TEST_F(BalloonControllerTest, SizesTest) {
  Notification n(GURL("http://www.google.com"), GURL("http://www.google.com"),
      ASCIIToUTF16("http://www.google.com"), string16(),
      new NotificationObjectProxy(-1, -1, -1, false));
  scoped_ptr<Balloon> balloon(
      new Balloon(n, profile(), collection_.get()));
  balloon->SetPosition(gfx::Point(1, 1), false);
  balloon->set_content_size(gfx::Size(100, 100));

  BalloonController* controller =
      [[TestBalloonController alloc] initWithBalloon:balloon.get()];

  [controller showWindow:nil];

  EXPECT_TRUE([controller desiredTotalWidth] > 100);
  EXPECT_TRUE([controller desiredTotalHeight] > 100);

  [controller closeBalloon:YES];
}

}
