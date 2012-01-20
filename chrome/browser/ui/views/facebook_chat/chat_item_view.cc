// Copyright (c) 2012 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2012 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/facebook_chat/chat_item_view.h"

#include <string>

#include "base/string_util.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/views/facebook_chat/chatbar_view.h"
#include "chrome/browser/ui/views/facebook_chat/chat_notification_popup.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/infobars/infobar_button_border.h"
#include "chrome/common/url_constants.h"
#include "googleurl/src/gurl.h"
#include "grit/app_resources.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "grit/theme_resources_standard.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/canvas_skia.h"
#include "ui/gfx/skia_util.h"
#include "views/background.h"
#include "views/controls/button/image_button.h"
#include "views/controls/button/text_button.h"
#include "views/controls/label.h"

using views::CustomButton;
using views::Label;
using views::View;

namespace {

const gfx::Size kChatButtonSize = gfx::Size(158, 25);

const int kCloseButtonRightPadding = 3;

//class ChatButtonBackground : public views::Background {
//  public:
//    ChatButtonBackground() {
//    }
//
//    virtual void Paint(gfx::Canvas* canvas, View* view) const {
//      CustomButton::ButtonState state =
//        (view->GetClassName() == views::Label::kViewClassName) ?
//        CustomButton::BS_NORMAL : static_cast<CustomButton*>(view)->state();
//      int w = view->width();
//      int h = view->height();
//
//      canvas->FillRectInt(background_color(state), 1, 1, w - 2, h - 2);
//      canvas->FillRectInt(border_color(state), 2, 0, w - 4, 1);
//      canvas->FillRectInt(border_color(state), 1, 1, 1, 1);
//      canvas->FillRectInt(border_color(state), 0, 2, 1, h - 4);
//      canvas->FillRectInt(border_color(state), 1, h - 2, 1, 1);
//      canvas->FillRectInt(border_color(state), 2, h - 1, w - 4, 1);
//      canvas->FillRectInt(border_color(state), w - 2, 1, 1, 1);
//      canvas->FillRectInt(border_color(state), w - 1, 2, 1, h - 4);
//      canvas->FillRectInt(border_color(state), w - 2, h - 2, 1, 1);
//      canvas->
//
//    }
//
//  private:
//    static SkColor border_color(CustomButton::ButtonState state) {
//      switch (state) {
//        case CustomButton::BS_HOT:    return kHotBorderColor;
//        case CustomButton::BS_PUSHED: return kPushedBorderColor;
//        default:                      return kBorderColor;
//      }
//    }
//
//    static SkColor background_color(CustomButton::ButtonState state) {
//      switch (state) {
//        case CustomButton::BS_HOT:    return kHotBackgroundColor;
//        case CustomButton::BS_PUSHED: return kPushedBackgroundColor;
//        default:                      return kBackgroundColor;
//      }
//    }
//
//    DISALLOW_COPY_AND_ASSIGN(ChatButtonBackground);
//};

}

ChatItemView::ChatItemView(FacebookChatItem *model, ChatbarView *chatbar)
  : model_(model),
    chatbar_(chatbar),
    close_button_bg_color_(0),
    chat_popup_(NULL) {
  
  model->AddObserver(this);

  ResourceBundle& rb = ResourceBundle::GetSharedInstance();

  openChatButton_ = new views::TextButton(this, UTF8ToWide(model->username()));
  //openChatButton_->SetNormalHasBorder(true);
  openChatButton_->set_icon_placement(views::TextButton::ICON_ON_LEFT);
  openChatButton_->set_border(new InfoBarButtonBorder);
  openChatButton_->SetNormalHasBorder(true);
  openChatButton_->SetAnimationDuration(0);
  openChatButton_->SetEnabledColor(SK_ColorBLACK);
  openChatButton_->SetHighlightColor(SK_ColorBLACK);
  openChatButton_->SetHoverColor(SK_ColorBLACK);
  openChatButton_->SetFont(rb.GetFont(ResourceBundle::BaseFont));

  StatusChanged();  // sets button icon
  AddChildView(openChatButton_);

  // Add the Close Button.
  close_button_ = new views::ImageButton(this);
  close_button_->SetImage(views::CustomButton::BS_NORMAL,
                          rb.GetBitmapNamed(IDR_TAB_CLOSE));
  close_button_->SetImage(views::CustomButton::BS_HOT,
                          rb.GetBitmapNamed(IDR_TAB_CLOSE_H));
  close_button_->SetImage(views::CustomButton::BS_PUSHED,
                          rb.GetBitmapNamed(IDR_TAB_CLOSE_P));
  
  //close_button_->SetTooltipText(
  //    UTF16ToWide(l10n_util::GetStringUTF16(IDS_TOOLTIP_CLOSE_TAB)));
  //close_button_->SetAccessibleName(
  //    l10n_util::GetStringUTF16(IDS_ACCNAME_CLOSE));
  // Disable animation so that the red danger sign shows up immediately
  // to help avoid mis-clicks.
  close_button_->SetAnimationDuration(0);
  AddChildView(close_button_);
}

ChatItemView::~ChatItemView() {
  if (model_)
    model_->RemoveObserver(this);
  if (close_button_)
    delete close_button_;
  if (openChatButton_)
    delete openChatButton_;
  if (chat_popup_) {
    chat_popup_->Close();
  }
}

void ChatItemView::ButtonPressed(views::Button* sender, const views::Event& event) {
  if (sender == close_button_) {
    Close();
  } else if (sender == openChatButton_) {
    ActivateChat();
  }
}

void ChatItemView::Layout() {
  gfx::Rect bounds = this->bounds();
  bounds.set_x(0);
  bounds.set_y(0);

  openChatButton_->SetBoundsRect(bounds);

  gfx::Size closeButtonSize = close_button_->GetPreferredSize();
  close_button_->SetBounds(bounds.width() - closeButtonSize.width() - kCloseButtonRightPadding,
                           bounds.height() / 2 - closeButtonSize.height() / 2,
                           closeButtonSize.width(),
                           closeButtonSize.height());

  if (notification_popup_) {
    notification_popup_->SetPositionRelativeTo(RectForNotificationPopup());
  }
}

gfx::Size ChatItemView::GetPreferredSize() {
  return kChatButtonSize;
}

void ChatItemView::OnChatUpdated(FacebookChatItem *source) {
  DCHECK(source == model_);
  switch (source->state()) {
  case FacebookChatItem::REMOVING:
    Close();
    break;
  case FacebookChatItem::NUM_NOTIFICATIONS_CHANGED:
    NotifyUnread();
    break;
  case FacebookChatItem::STATUS_CHANGED:
    StatusChanged();
    break;
  }
}

void ChatItemView::AnimationProgressed(const ui::Animation* animation) {
}

void ChatItemView::StatusChanged() {
  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  if (model_->num_notifications() == 0) {
    if (model_->status() == FacebookChatItem::AVAILABLE)
      openChatButton_->SetIcon(*rb.GetBitmapNamed(IDR_FACEBOOK_ONLINE_ICON_14));
    else if (model_->status() == FacebookChatItem::IDLE)
      openChatButton_->SetIcon(*rb.GetBitmapNamed(IDR_FACEBOOK_IDLE_ICON_14));
    else
      openChatButton_->SetIcon(SkBitmap::SkBitmap());
  }
}

void ChatItemView::Close() {
  if (notification_popup_)
    notification_popup_->Close();
  chatbar_->Remove(this);
}

void ChatItemView::OnPaint(gfx::Canvas* canvas) {
  views::View::OnPaint(canvas);

  ResourceBundle &rb = ResourceBundle::GetSharedInstance();
  SkColor bgColor = GetThemeProvider()->GetColor(ThemeService::COLOR_TAB_TEXT);

  if (bgColor != close_button_bg_color_) {
    close_button_bg_color_ = bgColor;
    close_button_->SetBackground(close_button_bg_color_,
        rb.GetBitmapNamed(IDR_TAB_CLOSE),
        rb.GetBitmapNamed(IDR_TAB_CLOSE_MASK));
  }
}

void ChatItemView::ActivateChat() {
  if (notification_popup_)
      notification_popup_->Close();

  model_->ClearUnreadMessages();

  // open popup
  std::string urlString(chrome::kFacebookChatExtensionPrefixURL);
  urlString += chrome::kFacebookChatExtensionChatPage;
  urlString += "#";
  urlString += model_->jid();
  
  chat_popup_ = ChatPopup::Show(GURL(urlString), chatbar_->browser(), RectForChatPopup(), BubbleBorder::BOTTOM_CENTER, this);
}

const FacebookChatItem* ChatItemView::GetModel() const {
  return model_;
}

void ChatItemView::ChatPopupIsClosing(ChatPopup* popup) {
  if (popup == chat_popup_)
    chat_popup_ = NULL;
}

void ChatItemView::BubbleClosing(Bubble* bubble, bool closed_by_escape) {
  DCHECK(bubble == notification_popup_);
  notification_popup_ = NULL;
}

void ChatItemView::NotifyUnread() {
  if (model_->num_notifications() > 0) {
    views::Widget* frame = BrowserView::GetBrowserViewForNativeWindow(
      chatbar_->browser()->window()->GetNativeHandle())->GetWidget();
    
    if (!notification_popup_)
      //notification_popup_->Close();
      notification_popup_ = ChatNotificationPopup::Show(frame, RectForNotificationPopup(), BubbleBorder::BOTTOM_LEFT, this);

    notification_popup_->PushMessage(model_->GetMessageAtIndex(model_->num_notifications() - 1));
  }
}

gfx::Rect ChatItemView::RectForChatPopup() {
  View* reference_view = openChatButton_;
  gfx::Point origin;
  View::ConvertPointToScreen(reference_view, &origin);
  gfx::Rect rect = reference_view->bounds();
  rect.set_origin(origin);

  return rect;
}

gfx::Rect ChatItemView::RectForNotificationPopup() {
  View* reference_view = openChatButton_;
  gfx::Point origin;
  View::ConvertPointToScreen(reference_view, &origin);
  gfx::Rect rect = reference_view->bounds();
  rect.set_origin(origin);
  rect.set_width(20);

  return rect; 
}