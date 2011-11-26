// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_FACEBOOK_CHAT_FACEBOOK_CHAT_ITEM_CONTROLLER_H_
#define CHROME_BROWSER_UI_COCOA_FACEBOOK_CHAT_FACEBOOK_CHAT_ITEM_CONTROLLER_H_

#pragma once

#import <Cocoa/Cocoa.h>

#include "base/memory/scoped_ptr.h"
#include "base/memory/scoped_nsobject.h"
#include "chrome/browser/ui/cocoa/facebook_chat/facebook_chat_item_mac.h"

@class FacebookChatbarController;
@class FacebookNotificationController;
@class HoverButton;
class GURL;

@interface FacebookChatItemController : NSViewController {
@private
  IBOutlet NSButton* button_;
  IBOutlet HoverButton* hoverCloseButton_;

  scoped_nsobject<NSTrackingArea> buttonTrackingArea_;
  BOOL showMouseEntered_;

  scoped_ptr<FacebookChatItemMac> bridge_;
  scoped_nsobject<FacebookNotificationController> notificationController_;
  FacebookChatbarController *chatbarController_;

  NSImage *numNotificationsImage_;

  BOOL active_;
}

// Takes ownership of |downloadModel|.
- (id)initWithModel:(FacebookChatItem*)downloadModel
            chatbar:(FacebookChatbarController*)chatbar;

- (IBAction)activateItemAction:(id)sender;
- (IBAction)removeAction:(id)sender;

- (void)openChatWindow;
- (void)chatWindowWillClose:(NSNotification*)notification;

- (NSSize)preferredSize;

- (NSPoint)popupPointForChatWindow;
- (NSPoint)popupPointForNotificationWindow;

- (GURL)getPopupURL;

- (FacebookChatItem*)chatItem;

//- (void)chatItemUpdated:(FacebookChatItem*)source;
- (void)remove;
- (void)setUnreadMessagesNumber:(int)number;

+ (NSImage*)imageForNotificationBadgeWithNumber:(int)number;

- (BOOL)active;
- (void)setActive:(BOOL)active;

- (void)layedOutAfterAddingToChatbar;

- (void)viewFrameDidChange:(NSNotification*)notification;

- (void)switchParentWindow:(NSWindow*)window;
- (void)layoutChildWindows;
@end

#endif    // CHROME_BROWSER_UI_COCOA_FACEBOOK_CHAT_FACEBOOK_CHAT_ITEM_CONTROLLER_H_
