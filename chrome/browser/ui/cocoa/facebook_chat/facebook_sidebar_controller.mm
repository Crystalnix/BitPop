// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/facebook_chat/facebook_sidebar_controller.h"

#include <algorithm>

#include <Cocoa/Cocoa.h>

#include "chrome/browser/browser_process.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/sidebar/sidebar_manager.h"
#import "chrome/browser/ui/cocoa/view_id_util.h"
#include "chrome/common/pref_names.h"
#include "content/browser/tab_contents/tab_contents.h"

namespace {

// Width of the facebook friends sidebar is constant and cannot be manipulated
// by user. When time comes we may change this decision.
const int kFriendsSidebarWidth = 186;

}  // end namespace


@interface FacebookSidebarController (Private)
//- (void)resizeSidebarToNewWidth:(CGFloat)width;
- (void)showSidebarContents:(TabContents*)sidebarContents;
@end


@implementation FacebookSidebarController

- (id)initWithDelegate:(id<TabContentsControllerDelegate>)delegate {
  if ((self = [super initWithContents:NULL delegate:delegate])) {
    sidebarVisible_ = NO;
    NSRect rc = [self view].frame;
    rc.size.width = 0;
    [[self view] setFrame:rc];
    [[self view] setAutoresizingMask:NSViewMinXMargin | NSViewHeightSizable];
  }
  return self;
}

- (void)dealloc {
  [super dealloc];
}

- (BOOL)isSidebarVisible {
  return sidebarVisible_;
}

- (void)updateFriendsForTabContents:(TabContents*)contents {
  TabContents* sidebarContents = contents;

  TabContents* oldSidebarContents = [self tabContents];
  if (oldSidebarContents == sidebarContents)
    return;

  // Adjust sidebar view.
  [self showSidebarContents:sidebarContents];

  // // Notify extensions.
  // SidebarManager::GetInstance()->NotifyStateChanges(
  //     oldSidebarContents, sidebarContents);
}

- (void)showSidebarContents:(TabContents*)sidebarContents {
  [self ensureContentsSizeDoesNotChange];

  NSRect rc = [[self view] frame];
  if (sidebarContents) {
    // Native view is a TabContentsViewCocoa object, whose ViewID was
    // set to VIEW_ID_TAB_CONTAINER initially, so change it to
    // VIEW_ID_SIDE_BAR_CONTAINER here.
    view_id_util::SetID(
        sidebarContents->GetNativeView(),
        VIEW_ID_FACEBOOK_FRIENDS_SIDE_BAR_CONTAINER);
    
    rc.size.width = kFriendsSidebarWidth;
    sidebarVisible_ = YES;
  } else {
    rc.size.width = 0;
    sidebarVisible_ = NO;
  }

  [[self view] setFrame:rc];
  [self changeTabContents:sidebarContents];
}

- (CGFloat)maxWidth {
  return kFriendsSidebarWidth;
}

@end
