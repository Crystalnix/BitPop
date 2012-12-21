// Copyright (c) 2012 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2012 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FACEBOOK_CHAT_FACEBOOK_BITPOP_NOTIFICATION_WIN_H_
#define CHROME_BROWSER_UI_VIEWS_FACEBOOK_CHAT_FACEBOOK_BITPOP_NOTIFICATION_WIN_H_

#pragma once

#include "base/compiler_specific.h"
#include "base/win/win_util.h"
#include "chrome/browser/facebook_chat/facebook_bitpop_notification.h"

class Profile;

class FacebookBitpopNotificationWin : public FacebookBitpopNotification {
public:
  FacebookBitpopNotificationWin(Profile* profile);
  virtual ~FacebookBitpopNotificationWin();

  virtual void ClearNotification() OVERRIDE;
  virtual void NotifyUnreadMessagesWithLastUser(int num_unread,
                                                const std::string& user_id) OVERRIDE;

  virtual void Shutdown();
protected:
  Profile *profile_;
  HWND notified_hwnd_;
};

#endif  // CHROME_BROWSER_UI_VIEWS_FACEBOOK_CHAT_FACEBOOK_BITPOP_NOTIFICATION_WIN_H_