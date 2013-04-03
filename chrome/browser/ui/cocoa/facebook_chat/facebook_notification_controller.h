// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_FACEBOOK_CHAT_FACEBOOK_NOTIFICATION_CONTROLLER_H_
#define CHROME_BROWSER_UI_COCOA_FACEBOOK_CHAT_FACEBOOK_NOTIFICATION_CONTROLLER_H_

#pragma once

#import <Cocoa/Cocoa.h>

#import "base/mac/cocoa_protocols.h"
#include "base/memory/scoped_nsobject.h"

@class FacebookNotificationView;
@class HoverButton;

@interface FacebookNotificationController : NSWindowController<NSWindowDelegate> {
  NSWindow *parentWindow_;
  NSPoint anchor_;
  NSPoint oldAnchor_;

  scoped_nsobject<FacebookNotificationView> bubble_;
  scoped_nsobject<HoverButton> hoverCloseButton_;

  NSRect notificationFrame_;
}

@property (nonatomic, assign) NSPoint anchor;

- (id)initWithParentWindow:(NSWindow*)parentWindow
                anchoredAt:(NSPoint)anchorPoint;

- (void)messageReceived:(NSString*)message;

- (void)hideWindow;

- (BOOL)isClosing;

- (void)parentControllerWillDie;

- (void)reparentWindowTo:(NSWindow*)window;
@end

#endif  // CHROME_BROWSER_UI_COCOA_FACEBOOK_CHAT_FACEBOOK_NOTIFICATION_CONTROLLER_H_
