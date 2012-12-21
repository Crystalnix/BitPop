// Copyright (c) 2012 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2012 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_FACEBOK_CHAT_FACEBOOK_BITPOP_NOTIFICATION_H_
#define CHROME_BROWSER_FACEBOK_CHAT_FACEBOOK_BITPOP_NOTIFICATION_H_

#pragma once

#include <string>
#include "chrome/browser/profiles/profile_keyed_service.h"

class FacebookBitpopNotification : public ProfileKeyedService {
public:
  virtual ~FacebookBitpopNotification();

  virtual void ClearNotification() = 0;
  virtual void NotifyUnreadMessagesWithLastUser(int num_unread,
                                                const std::string& user_id) = 0;
};

#endif  // CHROME_BROWSER_FACEBOK_CHAT_FACEBOOK_BITPOP_NOTIFICATION_H_
