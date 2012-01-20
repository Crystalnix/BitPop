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
#include "views/controls/button/button.h"
#include "views/controls/label.h"

namespace views {
  class ImageButton;
}

class ChatNotificationPopup : public Bubble,
                              public views::ButtonListener {
public:
  static ChatNotificationPopup* Show(views::Widget* parent,
                     const gfx::Rect& position_relative_to,
                     BubbleBorder::ArrowLocation arrow_location,
                     BubbleDelegate* delegate);

  void PushMessage(const std::string& message);
  std::string PopMessage();
  int num_messages_remaining() const { return messages_.size(); }

  const std::vector<std::string>& GetMessages() const;

  // views::ButtonListener protocol
  virtual void ButtonPressed(views::Button* sender, const views::Event& event) OVERRIDE;
  
  views::View* container_view() { return container_view_; }
protected:
  // OVERRIDE Bubble functionality
  virtual void OnActivate(UINT action, BOOL minimized, HWND window) OVERRIDE;
private:

  ChatNotificationPopup();

  std::vector<std::string> messages_;
  views::View* container_view_;
};

#endif  // CHROME_BROWSER_UI_VIEWS_FACEBOOK_CHAT_CHAT_NOTIFICATION_POPUP_H_