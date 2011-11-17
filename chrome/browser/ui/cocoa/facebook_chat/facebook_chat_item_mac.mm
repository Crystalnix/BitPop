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
  model_->AddObserver(this);
}

FacebookChatItemMac::~FacebookChatItemMac() {
  model_->RemoveObserver(this);
}

void FacebookChatItemMac::OnChatUpdated(FacebookChatItem *source) {
  DCHECK(source == model_);
  switch (source->state()) {
  // case FacebookChatItem::ACTIVE_STATUS_CHANGED:
  //   if (source->active())
  //     [controller_ openChatWindow];
  //   break;
  case FacebookChatItem::REMOVING:
    [controller_ remove];
    break;
  case FacebookChatItem::NUM_NOTIFICATIONS_CHANGED:
    [controller_ setUnreadMessagesNumber:source->num_notifications()];
    break;
  default:
    break;
  }
}

FacebookChatItem* FacebookChatItemMac::chat() const {
  return model_;
}
