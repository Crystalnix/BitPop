// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_FACEBOOK_CHAT_FACEBOOK_CHAT_ITEM_MAC_H_
#define CHROME_BROWSER_UI_COCOA_FACEBOOK_CHAT_FACEBOOK_CHAT_ITEM_MAC_H_

#pragma once

#include "chrome/browser/facebook_chat/facebook_chat_item.h"

@class FacebookChatItemController;

class FacebookChatItemMac : public FacebookChatItem::Observer {
public:
  FacebookChatItemMac(FacebookChatItem *model,
                      FacebookChatItemController *controller);
  virtual ~FacebookChatItemMac();

  virtual void OnChatUpdated(FacebookChatItem *source) OVERRIDE;

  FacebookChatItem* chat() const;
private:
  FacebookChatItem *model_;
  FacebookChatItemController *controller_;
};

#endif  // CHROME_BROWSER_UI_COCOA_FACEBOOK_CHAT_FACEBOOK_CHAT_ITEM_MAC_H_

