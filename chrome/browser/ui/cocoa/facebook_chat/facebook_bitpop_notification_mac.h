// Copyright (c) 2012 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2012 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_FACEBOK_CHAT_FACEBOOK_BITPOP_NOTIFICATION_MAC_H_
#define CHROME_BROWSER_UI_COCOA_FACEBOK_CHAT_FACEBOOK_BITPOP_NOTIFICATION_MAC_H_

#pragma once

#include "chrome/browser/facebook_chat/facebook_bitpop_notification.h"

class FacebookProfileImageFetcherDelegate;
class Profile;

class FacebookBitpopNotificationMac : public FacebookBitpopNotification {
public:
  FacebookBitpopNotificationMac(Profile *profile);
  virtual ~FacebookBitpopNotificationMac();

  Profile* profile() const { return profile_; }

  virtual void ClearNotification();
  virtual void NotifyUnreadMessagesWithLastUser(int num_unread,
                                                std::string user_id);

private:
  Profile* const profile_;
  FacebookProfileImageFetcherDelegate *delegate_;
};

#endif  // CHROME_BROWSER_UI_COCOA_FACEBOK_CHAT_FACEBOOK_BITPOP_NOTIFICATION_MAC_H_
