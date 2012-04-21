// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_FACEBOOK_SIDEBAR_CONTROLLER_H_
#define CHROME_BROWSER_UI_COCOA_FACEBOOK_SIDEBAR_CONTROLLER_H_
#pragma once

#import <Cocoa/Cocoa.h>

namespace content {
  class WebContents;
}

// A class that handles updates of the sidebar view within a browser window.
// It swaps in the relevant sidebar contents for a given TabContents or removes
// the vew, if there's no sidebar contents to show.
@interface FacebookSidebarController : NSViewController {
 @private
  content::WebContents* web_contents_;

  BOOL sidebarVisible_;
}

- (id)initWithContents:(content::WebContents*)contents;

// Depending on |contents|'s state, decides whether the sidebar
// should be shown or hidden and adjusts its width (|delegate_| handles
// the actual resize).
- (void)updateFriendsForTabContents:(content::WebContents*)contents;
// - (void)showSidebarContents:(TabContents*)sidebarContents;

- (void)sizeUpdated;

- (BOOL)isSidebarVisible;

- (CGFloat)maxWidth;
@end

#endif  // CHROME_BROWSER_UI_COCOA_FACEBOOK_SIDEBAR_CONTROLLER_H_
