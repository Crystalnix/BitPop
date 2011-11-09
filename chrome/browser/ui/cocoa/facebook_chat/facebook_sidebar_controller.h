// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_FACEBOOK_SIDEBAR_CONTROLLER_H_
#define CHROME_BROWSER_UI_COCOA_FACEBOOK_SIDEBAR_CONTROLLER_H_
#pragma once

#import <Foundation/Foundation.h>

#include "base/memory/scoped_nsobject.h"
#import "chrome/browser/ui/cocoa/tab_contents/tab_contents_controller.h"

@class NSSplitView;
@class NSView;

class TabContents;

// A class that handles updates of the sidebar view within a browser window.
// It swaps in the relevant sidebar contents for a given TabContents or removes
// the vew, if there's no sidebar contents to show.
@interface FacebookSidebarController : TabContentsController {
 @private
  // A view hosting sidebar contents.
  scoped_nsobject<NSSplitView> splitView_;

  // Manages currently displayed sidebar contents.
  scoped_nsobject<TabContentsController> contentsController_;

  BOOL sidebarVisible_;
}

- (id)initWithDelegate:(id<TabContentsControllerDelegate>)delegate;

// Depending on |contents|'s state, decides whether the sidebar
// should be shown or hidden and adjusts its width (|delegate_| handles
// the actual resize).
- (void)updateFriendsForTabContents:(TabContents*)contents;
// - (void)showSidebarContents:(TabContents*)sidebarContents;

- (BOOL)isSidebarVisible;

- (CGFloat)maxWidth;
@end

#endif  // CHROME_BROWSER_UI_COCOA_FACEBOOK_SIDEBAR_CONTROLLER_H_
