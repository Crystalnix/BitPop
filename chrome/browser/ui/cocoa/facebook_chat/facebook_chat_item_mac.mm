// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/cocoa/facebook_chat/facebook_chat_item_mac.h"

#import "chrome/browser/ui/cocoa/facebook_chat/facebook_chat_item_controller.h"

FacebookChatItemMac::FacebookChatItemMac(FacebookChatItem *model,
                                         FacebookChatItemController *controller)
                                         : model_(model),
                                           controller_(controller) {
}

FacebookChatItemMac::~FacebookChatItemMac() {
}

void FacebookChatItemMac::OnChatUpdated(FacebookChatItem *source) {
}

FacebookChatItem* FacebookChatItemMac::chat() const {
  return model_;
}
