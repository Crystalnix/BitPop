// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/facebook_chat/facebook_sidebar_controller.h"

#include <Cocoa/Cocoa.h>

#include "base/memory/scoped_nsobject.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/prefs/pref_service.h"
#import "chrome/browser/ui/cocoa/view_id_util.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/web_contents.h"

using content::WebContents;

namespace {

// Width of the facebook friends sidebar is constant and cannot be manipulated
// by user. When time comes we may change this decision.
const int kFriendsSidebarWidth = 186;

}  // end namespace


@interface FacebookSidebarController (Private)
//- (void)resizeSidebarToNewWidth:(CGFloat)width;
- (void)showSidebarContents:(WebContents*)sidebarContents;
@end


@implementation FacebookSidebarController

- (id)initWithContents:(WebContents*)contents {
  if ((self = [super initWithNibName:nil bundle:nil])) {
    web_contents_ = contents;
    sidebarVisible_ = NO;
    NSRect rc = [self view].frame;
    rc.size.width = 0;
    [[self view] setFrame:rc];
  }
  return self;
}

- (void)loadView {
  scoped_nsobject<NSView> view([[NSView alloc] initWithFrame:NSZeroRect]);
  [view setAutoresizingMask:NSViewMinXMargin | NSViewHeightSizable];
  [view setAutoresizesSubviews:NO];
  [self setView:view];
}

- (void)dealloc {
  [super dealloc];
}

- (BOOL)isSidebarVisible {
  return sidebarVisible_;
}

- (void)updateFriendsForTabContents:(WebContents*)contents {
  WebContents* sidebarContents = contents;

  WebContents* oldSidebarContents = web_contents_;
  if (oldSidebarContents == sidebarContents)
    return;

  // Adjust sidebar view.
  [self showSidebarContents:sidebarContents];
 }

- (void)showSidebarContents:(WebContents*)sidebarContents {
  NSRect rc = [[self view] frame];

  if (web_contents_) {
    [web_contents_->GetNativeView() removeFromSuperview];
  }

  if (sidebarContents) {
    // Native view is a TabContentsViewCocoa object, whose ViewID was
    // set to VIEW_ID_TAB_CONTAINER initially, so change it to
    // VIEW_ID_SIDE_BAR_CONTAINER here.
    view_id_util::SetID(
        sidebarContents->GetNativeView(),
        VIEW_ID_FACEBOOK_FRIENDS_SIDE_BAR_CONTAINER);

    rc.size.width = kFriendsSidebarWidth;
    sidebarVisible_ = YES;


    web_contents_ = sidebarContents;
    [[self view] setFrame:rc];

    [[self view] addSubview:sidebarContents->GetNativeView()];
    [self sizeUpdated];

  } else {
    rc.size.width = 0;
    sidebarVisible_ = NO;

    web_contents_ = NULL;
    [[self view] setFrame:rc];
  }
}

- (CGFloat)maxWidth {
  return kFriendsSidebarWidth;
}

- (void)sizeUpdated {
  if (web_contents_) {
    [web_contents_->GetNativeView() setFrame:[[self view] bounds]];
  }
}

@end
