// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_FACEBOOK_CHAT_FACEBOOK_CHAT_ITEM_CONTROLLER_H_
#define CHROME_BROWSER_UI_COCOA_FACEBOOK_CHAT_FACEBOOK_CHAT_ITEM_CONTROLLER_H_

#pragma once

#import <Cocoa/Cocoa.h>

#include "base/memory/scoped_ptr.h"
#include "chrome/browser/ui/cocoa/facebook_chat/facebook_chat_item_mac.h"

@class FacebookChatbarController;
@class HoverButton;
class GURL;

@interface FacebookChatItemController : NSViewController {
@private
  IBOutlet NSButton* button_;
  IBOutlet HoverButton* hoverCloseButton_;

  scoped_ptr<FacebookChatItemMac> bridge_;
  FacebookChatbarController *chatbarController_;
}

// Takes ownership of |downloadModel|.
- (id)initWithModel:(FacebookChatItem*)downloadModel
            chatbar:(FacebookChatbarController*)chatbar;

- (IBAction)openChatWindow:(id)sender;
- (IBAction)remove:(id)sender;

- (NSSize)preferredSize;

- (NSPoint)popupPointForChatWindow;
- (GURL)getPopupURL;

- (FacebookChatItem*)chatItem;
@end

#endif    // CHROME_BROWSER_UI_COCOA_FACEBOOK_CHAT_FACEBOOK_CHAT_ITEM_CONTROLLER_H_
