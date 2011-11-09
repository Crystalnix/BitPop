// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_FACEBOOK_CHAT_FACEBOOK_CHATBAR_H_
#define CHROME_BROWSER_FACEBOOK_CHAT_FACEBOOK_CHATBAR_H_
#pragma once

class FacebookChatItem;
class Browser;

class FacebookChatbar {
  public:
    virtual ~FacebookChatbar() {}

    virtual void AddChatItem(FacebookChatItem *chat_item) = 0;

    virtual void Show() = 0;
    virtual void Hide() = 0;

    virtual Browser *browser() const = 0;
};

#endif  // CHROME_BROWSER_FACEBOOK_CHAT_FACEBOOK_CHATBAR_H_
