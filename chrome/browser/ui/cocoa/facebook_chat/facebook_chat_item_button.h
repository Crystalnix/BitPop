// Copyright (c) 2012 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2012 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_FACEBOOK_CHAT_FACEBOOK_CHAT_ITEM_BUTTON_H_
#define CHROME_BROWSER_UI_COCOA_FACEBOOK_CHAT_FACEBOOK_CHAT_ITEM_BUTTON_H_

#import <Cocoa/Cocoa.h>

@class FacebookChatItemController;

@interface FacebookChatItemButton : NSButton {
  IBOutlet FacebookChatItemController* controller_;
}

@end

#endif // CHROME_BROWSER_UI_COCOA_FACEBOOK_CHAT_FACEBOOK_CHAT_ITEM_BUTTON_H_

