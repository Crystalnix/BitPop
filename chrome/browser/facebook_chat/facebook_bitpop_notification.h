// Copyright (c) 2012 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2012 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_FACEBOK_CHAT_FACEBOOK_BITPOP_NOTIFICATION_H_
#define CHROME_BROWSER_FACEBOK_CHAT_FACEBOOK_BITPOP_NOTIFICATION_H_

#pragma once

#include <string>

class FacebookBitpopNotification {
public:
  virtual ~FacebookBitpopNotification();

  virtual void ClearNotification();
  virtual void NotifyUnreadMessagesWithLastUser(int num_unread,
                                                std::string user_id);
};

#endif  // CHROME_BROWSER_FACEBOK_CHAT_FACEBOOK_BITPOP_NOTIFICATION_H_
