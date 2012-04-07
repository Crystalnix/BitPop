// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/scoped_nsobject.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/ui/cocoa/cocoa_profile_test.h"
#import "chrome/browser/ui/cocoa/toolbar/toolbar_controller.h"
#include "chrome/browser/ui/cocoa/run_loop_testing.h"
#import "chrome/browser/ui/cocoa/view_resizer_pong.h"
#import "chrome/browser/ui/cocoa/wrench_menu/wrench_menu_controller.h"
#include "chrome/browser/ui/toolbar/wrench_menu_model.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/platform_test.h"

namespace {

class MockWrenchMenuModel : public WrenchMenuModel {
 public:
  MockWrenchMenuModel() : WrenchMenuModel() {}
  ~MockWrenchMenuModel() {
    // This dirty, ugly hack gets around a bug in the test. In
    // ~WrenchMenuModel(), there's a call to TabstripModel::RemoveObserver(this)
    // which mysteriously leads to this crash: http://crbug.com/49206 .  It
    // seems that the vector of observers is getting hosed somewhere between
    // |-[ToolbarController dealloc]| and ~MockWrenchMenuModel(). This line
    // short-circuits the parent destructor to avoid this crash.
    tabstrip_model_ = NULL;
  }
  MOCK_METHOD1(ExecuteCommand, void(int command_id));
};

class WrenchMenuControllerTest : public CocoaProfileTest {
 public:
  virtual void SetUp() {
    CocoaProfileTest::SetUp();
    ASSERT_TRUE(browser());

    controller_.reset([[WrenchMenuController alloc] initWithBrowser:browser()]);
  }

  WrenchMenuController* controller() {
    return controller_.get();
  }

  MockWrenchMenuModel fake_model_;
  scoped_nsobject<WrenchMenuController> controller_;
};

TEST_F(WrenchMenuControllerTest, Initialized) {
  EXPECT_TRUE([controller() menu]);
  EXPECT_GE([[controller() menu] numberOfItems], 5);
}

TEST_F(WrenchMenuControllerTest, DispatchSimple) {
  scoped_nsobject<NSButton> button([[NSButton alloc] init]);
  [button setTag:IDC_ZOOM_PLUS];

  // Set fake model to test dispatching.
  EXPECT_CALL(fake_model_, ExecuteCommand(IDC_ZOOM_PLUS));
  [controller() setModel:&fake_model_];

  [controller() dispatchWrenchMenuCommand:button.get()];
  chrome::testing::NSRunLoopRunAllPending();
}

}  // namespace
