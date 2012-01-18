// Copyright (c) 2012 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2012 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FACEBOOK_CHAT_CHAT_NOTIFICATION_POPUP_H_
#define CHROME_BROWSER_UI_VIEWS_FACEBOOK_CHAT_CHAT_NOTIFICATION_POPUP_H_
#pragma once

#include <vector>
#include <string>

#include "chrome/browser/ui/views/bubble/bubble.h"

class ChatNotificationPopup : public Bubble,
                              public BubbleDelegate {
public:
  static ChatNotificationPopup* Show(views::Widget* parent,
                     const gfx::Rect& position_relative_to,
                     BubbleBorder::ArrowLocation arrow_location,
                     BubbleDelegate* delegate);

  const std::vector<std::string>& GetMessages() const;
private:
  std::vector<std::string> messages_;
};

#endif  // CHROME_BROWSER_UI_VIEWS_FACEBOOK_CHAT_CHAT_NOTIFICATION_POPUP_H_