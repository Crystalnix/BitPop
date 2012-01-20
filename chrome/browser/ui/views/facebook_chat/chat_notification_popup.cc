// Copyright (c) 2012 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2012 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/facebook_chat/chat_notification_popup.h"

#include "base/utf_string_conversions.h"
#include "chrome/browser/ui/views/bubble/border_contents.h"
#include "chrome/browser/ui/views/bubble/border_widget_win.h"
#include "grit/app_resources.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "grit/theme_resources_standard.h"
#include "ui/base/resource/resource_bundle.h"
#include "views/controls/button/image_button.h"

using views::View;

namespace {
  static const int kMaxNotifications = 20;

  static const int kNotificationLabelWidth = 200;

  static const int kNotificationLabelMaxHeight = 600;

  static const SkColor kNotificationPopupBackgroundColor = SkColorSetRGB(176, 246, 255);
}

class NotificationPopupContent : public views::Label {
public:
  NotificationPopupContent(ChatNotificationPopup *owner)
    : views::Label(),
      owner_(owner) {
    SetMultiLine(true);
    SetAllowCharacterBreak(true);
    SetHorizontalAlignment(views::Label::ALIGN_LEFT);
    set_background(views::Background::CreateSolidBackground(kNotificationPopupBackgroundColor));

  }
  
  virtual gfx::Size GetPreferredSize() {
    int height = GetHeightForWidth(kNotificationLabelWidth);
    if (height > kNotificationLabelMaxHeight) {
      height = kNotificationLabelMaxHeight;  
    }
    gfx::Size prefsize(kNotificationLabelWidth, height);
    gfx::Insets insets = GetInsets();
    prefsize.Enlarge(insets.width(), insets.height());
    return prefsize;
  }

  void UpdateOwnText() {
    const std::vector<std::string>& msgs = owner_->GetMessages();
    std::string concat("");
    for (int i = 0; i < (int)msgs.size(); ++i) {
      concat += msgs.at(i);
      if (i != (int)msgs.size() - 1)
        concat += "\n\n";
    }
    SetText(UTF8ToWide(concat));
    owner_->SizeToContents();
  }

private:
  ChatNotificationPopup* owner_;
};


class NotificationContainerView : public View {
public:
  NotificationContainerView(ChatNotificationPopup *owner) 
    : View(),
      owner_(owner),
      label_(new NotificationPopupContent(owner)),
      close_button_(new views::ImageButton(owner)),
      close_button_bg_color_(0) {

    AddChildView(label_);

    // Add the Close Button.
    ResourceBundle& rb = ResourceBundle::GetSharedInstance();

    close_button_->SetImage(views::CustomButton::BS_NORMAL,
                            rb.GetBitmapNamed(IDR_TAB_CLOSE));
    close_button_->SetImage(views::CustomButton::BS_HOT,
                            rb.GetBitmapNamed(IDR_TAB_CLOSE_H));
    close_button_->SetImage(views::CustomButton::BS_PUSHED,
                            rb.GetBitmapNamed(IDR_TAB_CLOSE_P));
  
    // Disable animation so that the red danger sign shows up immediately
    // to help avoid mis-clicks.
    close_button_->SetAnimationDuration(0);
  
    AddChildView(close_button_);
  }

  virtual gfx::Size GetPreferredSize() {
    return label_->GetPreferredSize();
  }

  virtual void Layout() {
    gfx::Rect ourBounds = bounds();
    ourBounds.set_x(0);
    ourBounds.set_y(0);

    label_->SetBounds(ourBounds.x(), ourBounds.y(), ourBounds.width(), ourBounds.height());

    gfx::Size prefsize = close_button_->GetPreferredSize();
    close_button_->SetBounds(ourBounds.width() - 16, 0, prefsize.width(), prefsize.height());
  }

  virtual void OnPaint(gfx::Canvas* canvas) {
    views::View::OnPaint(canvas);

    ResourceBundle &rb = ResourceBundle::GetSharedInstance();
    SkColor bgColor = kNotificationPopupBackgroundColor;

    if (bgColor != close_button_bg_color_) {
      close_button_bg_color_ = bgColor;
      close_button_->SetBackground(SkColorSetRGB(0,0,0),
          rb.GetBitmapNamed(IDR_TAB_CLOSE),
          rb.GetBitmapNamed(IDR_TAB_CLOSE_MASK));
    }
  }

  NotificationPopupContent* GetLabelView() { return label_; }

private:
  ChatNotificationPopup* owner_;
  NotificationPopupContent* label_;
  views::ImageButton* close_button_;
  SkColor close_button_bg_color_;
};

// static
ChatNotificationPopup* ChatNotificationPopup::Show(views::Widget* parent,
                     const gfx::Rect& position_relative_to,
                     BubbleBorder::ArrowLocation arrow_location,
                     BubbleDelegate* delegate) {
  ChatNotificationPopup* popup = new ChatNotificationPopup();
  popup->InitBubble(parent, position_relative_to, arrow_location, popup->container_view(), delegate);
  popup->border_->border_contents()->SetBackgroundColor(kNotificationPopupBackgroundColor);

  return popup;
}

ChatNotificationPopup::ChatNotificationPopup() 
  : Bubble() {
  container_view_ = new NotificationContainerView(this);
}

void ChatNotificationPopup::PushMessage(const std::string& message) {
  if (messages_.size() >= kMaxNotifications)
    messages_.erase(messages_.begin());
  
  messages_.push_back(message);
  static_cast<NotificationContainerView*>(container_view_)->GetLabelView()->UpdateOwnText();
}

std::string ChatNotificationPopup::PopMessage() {
  std::string res = messages_.front();
  messages_.erase(messages_.begin());
  static_cast<NotificationContainerView*>(container_view_)->GetLabelView()->UpdateOwnText();
  return res;
}

const std::vector<std::string>& ChatNotificationPopup::GetMessages() const {
  return this->messages_;
}

void ChatNotificationPopup::ButtonPressed(views::Button* sender, const views::Event& event) {
  //DCHECK(sender == close_button_);
  Close();
}

void ChatNotificationPopup::OnActivate(UINT action, BOOL minimized, HWND window) {
  // The popup should close when it is deactivated.
  if (action == WA_ACTIVE) {
    DCHECK(GetWidget()->GetRootView()->has_children());
    GetWidget()->GetRootView()->GetChildViewAt(0)->RequestFocus();
  }
}