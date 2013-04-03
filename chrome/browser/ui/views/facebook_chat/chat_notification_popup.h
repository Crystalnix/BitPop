// Copyright (c) 2012 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2012 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FACEBOOK_CHAT_CHAT_NOTIFICATION_POPUP_H_
#define CHROME_BROWSER_UI_VIEWS_FACEBOOK_CHAT_CHAT_NOTIFICATION_POPUP_H_
#pragma once

#include <deque>
#include <string>

#include "chrome/browser/ui/views/facebook_chat/bubble/bubble_delegate.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/label.h"
#include "ui/views/focus/widget_focus_manager.h"

namespace views {
  class ImageButton;
}

using views::BitpopBubbleDelegateView;
using views::BitpopBubbleBorder;

class ChatNotificationPopup : public BitpopBubbleDelegateView,
                              public views::ButtonListener /*,
                              public views::WidgetFocusChangeListener */ {
public:
  static ChatNotificationPopup* Show(views::View* anchor_view,
      BitpopBubbleBorder::ArrowLocation arrow_location);

  void PushMessage(const std::string& message);
  std::string PopMessage();
  int num_messages_remaining() const { return messages_.size(); }

  typedef std::deque<std::string> MessageContainer;
  const MessageContainer& GetMessages();

  // views::ButtonListener protocol
  virtual void ButtonPressed(views::Button* sender, const views::Event& event) OVERRIDE;

  // Overridden from ui::AnimationDelegate:
  //virtual void AnimationEnded(const ui::Animation* animation) OVERRIDE;
  //virtual void AnimationProgressed(const ui::Animation* animation) OVERRIDE;

  virtual gfx::Size GetPreferredSize() OVERRIDE;

  views::View* container_view() { return container_view_; }

private:

  ChatNotificationPopup();

  MessageContainer messages_;
  views::View* container_view_;
};

#endif  // CHROME_BROWSER_UI_VIEWS_FACEBOOK_CHAT_CHAT_NOTIFICATION_POPUP_H_
