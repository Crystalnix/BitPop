// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/tabpose_window.h"

#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/cocoa/cocoa_profile_test.h"
#include "chrome/browser/ui/tab_contents/tab_contents.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/site_instance.h"
#include "ipc/ipc_message.h"
#include "testing/gtest/include/gtest/gtest.h"

using content::SiteInstance;

class TabposeWindowTest : public CocoaProfileTest {
 public:
  virtual void SetUp() {
    CocoaProfileTest::SetUp();
    ASSERT_TRUE(profile());

    site_instance_ = SiteInstance::Create(profile());
  }

  void AppendTabToStrip() {
    TabContents* tab_contents = chrome::TabContentsFactory(
        profile(), site_instance_, MSG_ROUTING_NONE, NULL, NULL);
    browser()->tab_strip_model()->AppendTabContents(
        tab_contents, /*foreground=*/true);
  }

  scoped_refptr<SiteInstance> site_instance_;
};

// Check that this doesn't leak.
TEST_F(TabposeWindowTest, TestShow) {
  NSWindow* parent = browser()->window()->GetNativeWindow();

  [parent orderFront:nil];
  EXPECT_TRUE([parent isVisible]);

  // Add a few tabs to the tab strip model.
  for (int i = 0; i < 3; ++i)
    AppendTabToStrip();

  base::mac::ScopedNSAutoreleasePool pool;
  TabposeWindow* window =
      [TabposeWindow openTabposeFor:parent
                               rect:NSMakeRect(10, 20, 250, 160)
                              slomo:NO
                      tabStripModel:browser()->tab_strip_model()];

  // Should release the window.
  [window mouseDown:nil];
}

TEST_F(TabposeWindowTest, TestModelObserver) {
  NSWindow* parent = browser()->window()->GetNativeWindow();
  [parent orderFront:nil];

  // Add a few tabs to the tab strip model.
  for (int i = 0; i < 3; ++i)
    AppendTabToStrip();

  base::mac::ScopedNSAutoreleasePool pool;
  TabposeWindow* window =
      [TabposeWindow openTabposeFor:parent
                               rect:NSMakeRect(10, 20, 250, 160)
                              slomo:NO
                      tabStripModel:browser()->tab_strip_model()];

  // Exercise all the model change events.
  TabStripModel* model = browser()->tab_strip_model();
  DCHECK_EQ([window thumbnailLayerCount], 3u);
  DCHECK_EQ([window selectedIndex], 2);

  model->MoveTabContentsAt(0, 2, /*select_after_move=*/false);
  DCHECK_EQ([window thumbnailLayerCount], 3u);
  DCHECK_EQ([window selectedIndex], 1);

  model->MoveTabContentsAt(2, 0, /*select_after_move=*/false);
  DCHECK_EQ([window thumbnailLayerCount], 3u);
  DCHECK_EQ([window selectedIndex], 2);

  [window selectTileAtIndexWithoutAnimation:0];
  DCHECK_EQ([window selectedIndex], 0);

  model->MoveTabContentsAt(0, 2, /*select_after_move=*/false);
  DCHECK_EQ([window selectedIndex], 2);

  model->MoveTabContentsAt(2, 0, /*select_after_move=*/false);
  DCHECK_EQ([window selectedIndex], 0);

  delete model->DetachTabContentsAt(0);
  DCHECK_EQ([window thumbnailLayerCount], 2u);
  DCHECK_EQ([window selectedIndex], 0);

  AppendTabToStrip();
  DCHECK_EQ([window thumbnailLayerCount], 3u);
  DCHECK_EQ([window selectedIndex], 0);

  model->CloseTabContentsAt(0, TabStripModel::CLOSE_NONE);
  DCHECK_EQ([window thumbnailLayerCount], 2u);
  DCHECK_EQ([window selectedIndex], 0);

  [window selectTileAtIndexWithoutAnimation:1];
  model->CloseTabContentsAt(0, TabStripModel::CLOSE_NONE);
  DCHECK_EQ([window thumbnailLayerCount], 1u);
  DCHECK_EQ([window selectedIndex], 0);

  // Should release the window.
  [window mouseDown:nil];
}
