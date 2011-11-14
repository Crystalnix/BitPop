// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/facebook_chat/facebook_popup_controller.h"

#include <algorithm>

#include "chrome/browser/debugger/devtools_manager.h"
#include "chrome/browser/extensions/extension_host.h"
#include "chrome/browser/extensions/extension_process_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#import "chrome/browser/ui/cocoa/browser_window_cocoa.h"
#import "chrome/browser/ui/cocoa/extensions/extension_view_mac.h"
#import "chrome/browser/ui/cocoa/info_bubble_window.h"
#include "content/common/notification_details.h"
#include "content/common/notification_registrar.h"
#include "content/common/notification_source.h"

namespace {
// There should only be one extension popup showing at one time. Keep a
// reference to it here.
static FacebookPopupController* gPopup;
}

@interface FacebookPopupController(Private)
// Callers should be using the public static method for initialization.
// NOTE: This takes ownership of |host|.
- (id)initWithHost:(ExtensionHost*)host
      parentWindow:(NSWindow*)parentWindow
        anchoredAt:(NSPoint)anchoredAt
     arrowLocation:(info_bubble::BubbleArrowLocation)arrowLocation
           devMode:(BOOL)devMode;

// Called when the extension's hosted NSView has been resized.
- (void)extensionViewFrameChanged;
@end

@implementation FacebookPopupController

+ (FacebookPopupController*)showURL:(GURL)url
                          inBrowser:(Browser*)browser
                         anchoredAt:(NSPoint)anchoredAt
                      arrowLocation:(info_bubble::BubbleArrowLocation)
                                        arrowLocation
                            devMode:(BOOL)devMode {
  DCHECK([NSThread isMainThread]);
  DCHECK(browser);
  if (!browser)
    return nil;

  ExtensionProcessManager* manager =
      browser->profile()->GetExtensionProcessManager();
  DCHECK(manager);
  if (!manager)
    return nil;

  ExtensionHost* host = manager->CreatePopupHost(url, browser);
  DCHECK(host);
  if (!host)
    return nil;

  // Make absolutely sure that no popups are leaked.
  if (gPopup) {
    if ([[gPopup window] isVisible])
      [gPopup close];

    [gPopup autorelease];
    gPopup = nil;
  }
  DCHECK(!gPopup);

  // Takes ownership of |host|. Also will autorelease itself when the popup is
  // closed, so no need to do that here.
  gPopup = [[FacebookPopupController alloc]
      initWithHost:host
      parentWindow:browser->window()->GetNativeHandle()
        anchoredAt:anchoredAt
     arrowLocation:arrowLocation
           devMode:devMode];
  return gPopup;
}

+ (FacebookPopupController*)popup {
  return gPopup;
}

- (void)windowDidResignKey:(NSNotification *)notification {
  NSWindow* window = [self window];
  DCHECK_EQ([notification object], window);

  // If the window isn't visible, it is already closed, and this notification
  // has been sent as part of the closing operation, so no need to close.
  if ([[NSApplication sharedApplication] keyWindow] != nil && 
      [window isVisible] && ![self beingInspected]) {
    [self close];
  }
}

@end

