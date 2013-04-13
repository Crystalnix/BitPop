// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/facebook_chat/facebook_chatbar_mac.h"

#import "chrome/browser/ui/cocoa/facebook_chat/facebook_chatbar_controller.h"
#include "chrome/browser/ui/browser.h"

FacebookChatbarMac::FacebookChatbarMac(Browser *browser,
                                       FacebookChatbarController *controller)
                                       : browser_(browser),
                                         controller_(controller) {
}

void FacebookChatbarMac::AddChatItem(FacebookChatItem *chat_item) {
  [controller_ addChatItem: chat_item];
  Show();
}

void FacebookChatbarMac::Show() {
  [controller_ show:nil];
}

void FacebookChatbarMac::Hide() {
  [controller_ hide:nil];
}

Browser *FacebookChatbarMac::browser() const {
  return browser_;
}

void FacebookChatbarMac::RemoveAll() {
  [controller_ removeAll];
  Hide();
}
