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
#include "ui/base/animation/slide_animation.h"
#include "ui/base/resource/resource_bundle.h"
#include "views/controls/button/image_button.h"

using views::View;

namespace {
  static const int kMaxNotifications = 20;

  static const int kNotificationLabelWidth = 180;

  static const int kNotificationLabelMaxHeight = 600;

  static const SkColor kNotificationPopupBackgroundColor = SkColorSetRGB(0xc2, 0xec, 0xfc);

  static const int kNotificationBubbleAlpha = 200;
}

class NotificationPopupContent : public views::Label {
public:
  NotificationPopupContent(ChatNotificationPopup *owner)
    : views::Label(),
      owner_(owner) {
    SetMultiLine(true);
    SetAllowCharacterBreak(true);
    SetHorizontalAlignment(views::Label::ALIGN_LEFT);
    SkColor labelBgr = SkColorSetA(kNotificationPopupBackgroundColor, 0);
    set_background(views::Background::CreateSolidBackground(0, 0, 0, 0));

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
    const ChatNotificationPopup::MessageContainer& msgs = owner_->GetMessages();
    std::string concat = "";
    int i = 0;
    for (ChatNotificationPopup::MessageContainer::const_iterator it = msgs.begin(); it != msgs.end(); it++, i++) {
      concat += *it;
      if (i != (int)msgs.size() - 1)
        concat += "\n\n";
    }
    SetText(L"");
    owner_->SizeToContents();  // dirty hack to force the window redraw
    SetText(UTF8ToWide(concat));
    owner_->SizeToContents();
  }

private:
  ChatNotificationPopup* owner_;
};


class NotificationContainerView : public View {
public:
  NotificationContainerView(ChatNotificationPopup *owner) 
    : owner_(owner),
      label_(new NotificationPopupContent(owner)),
      close_button_(new views::ImageButton(owner)),
      close_button_bg_color_(0) {

    AddChildView(label_);

    // Add the Close Button.
    ResourceBundle& rb = ResourceBundle::GetSharedInstance();

    close_button_->SetImage(views::CustomButton::BS_NORMAL,
                            rb.GetBitmapNamed(IDR_CLOSE_BAR));
    close_button_->SetImage(views::CustomButton::BS_HOT,
                            rb.GetBitmapNamed(IDR_CLOSE_BAR_H));
    close_button_->SetImage(views::CustomButton::BS_PUSHED,
                            rb.GetBitmapNamed(IDR_CLOSE_BAR_P));
  
    // Disable animation so that the red danger sign shows up immediately
    // to help avoid mis-clicks.
    //close_button_->SetAnimationDuration(0);
    AddChildView(close_button_);

    set_background(views::Background::CreateSolidBackground(kNotificationPopupBackgroundColor));
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
    close_button_->SetBounds(ourBounds.width() - prefsize.width(), 0, prefsize.width(), prefsize.height());
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
  popup->container_view()->parent()->set_background(views::Background::CreateSolidBackground(176, 246, 255, 0));
  popup->border_->border_contents()->SetBackgroundColor(kNotificationPopupBackgroundColor);

  return popup;
}

ChatNotificationPopup::ChatNotificationPopup() 
  : Bubble() {
  container_view_ = new NotificationContainerView(this);
}

void ChatNotificationPopup::PushMessage(const std::string& message) {
  if (messages_.size() >= kMaxNotifications)
    messages_.pop_front();
  
  messages_.push_back(message);
  static_cast<NotificationContainerView*>(container_view_)->GetLabelView()->UpdateOwnText();
}

std::string ChatNotificationPopup::PopMessage() {
  std::string res = messages_.front();
  messages_.pop_front();
  if (messages_.size() == 0)
    Close();
  else 
    static_cast<NotificationContainerView*>(container_view_)->GetLabelView()->UpdateOwnText();
  return res;
}

const ChatNotificationPopup::MessageContainer& ChatNotificationPopup::GetMessages() {
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

// Overridden from ui::AnimationDelegate:
void ChatNotificationPopup::AnimationEnded(const ui::Animation* animation) {
  Bubble::AnimationEnded(animation);
}

void ChatNotificationPopup::AnimationProgressed(const ui::Animation* animation) {
  Bubble::AnimationProgressed(animation);
//  #if defined(OS_WIN)
//  // Set the opacity for the main contents window.
//  unsigned char opacity = static_cast<unsigned char>(
//      animation_->GetCurrentValue() * 255);
//  SetLayeredWindowAttributes(GetNativeView(), 0,
//      static_cast<byte>(opacity), LWA_ALPHA);
//  contents_->SchedulePaint();
//
//  // Also fade in/out the bubble border window.
//  border_->SetOpacity(opacity);
//  border_->border_contents()->SchedulePaint();
//#else
//  NOTIMPLEMENTED();
//#endif
}