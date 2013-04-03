// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_FACEBOOK_SIDEBAR_CONTROLLER_H_
#define CHROME_BROWSER_UI_COCOA_FACEBOOK_SIDEBAR_CONTROLLER_H_
#pragma once

#import <Cocoa/Cocoa.h>

#include "base/memory/scoped_ptr.h"
#include "content/public/browser/notification_registrar.h"

namespace extensions {
  class ExtensionHost;
}

class Browser;
class SidebarExtensionContainer;
class SidebarExtensionNotificationBridge;

// A class that handles updates of the sidebar view within a browser window.
// It swaps in the relevant sidebar contents for a given TabContents or removes
// the vew, if there's no sidebar contents to show.
@interface FacebookSidebarController : NSViewController {
 @private
  BOOL sidebarVisible_;
  content::NotificationRegistrar registrar_;
  scoped_ptr<extensions::ExtensionHost> extension_host_;
  scoped_ptr<SidebarExtensionNotificationBridge> notification_bridge_;
  scoped_ptr<SidebarExtensionContainer> extension_container_;
  Browser* browser_;
}

@property(nonatomic, assign) BOOL visible;

- (id)initWithBrowser:(Browser*)browser;

- (CGFloat)maxWidth;

- (void)removeAllChildViews;

- (extensions::ExtensionHost*)extension_host;

@end

#endif  // CHROME_BROWSER_UI_COCOA_FACEBOOK_SIDEBAR_CONTROLLER_H_
