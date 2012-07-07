// Copyright (c) 2012 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2012 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/facebook_chat/chat_item_view.h"

#include <string>

#include "base/location.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/facebook_chat/facebook_chat_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/views/facebook_chat/chatbar_view.h"
#include "chrome/browser/ui/views/facebook_chat/chat_notification_popup.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/infobars/infobar_button_border.h"
#include "chrome/common/badge_util.h"
#include "chrome/common/url_constants.h"
#include "googleurl/src/gurl.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "grit/theme_resources_standard.h"
#include "grit/ui_resources.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "third_party/skia/include/core/SkTypeface.h"
#include "third_party/skia/include/effects/SkGradientShader.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/canvas_skia.h"
#include "ui/gfx/skia_util.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/text_button.h"
#include "ui/views/controls/label.h"

using views::CustomButton;
using views::Label;
using views::View;

namespace {

const gfx::Size kChatButtonSize = gfx::Size(158, 25);

const int kCloseButtonRightPadding = 3;

const int kNotificationMessageDelaySec = 10;

const int kNotifyIconDimX = 16;
const int kNotifyIconDimY = 11;

const float kTextSize = 10;
const int kBottomMargin = 0;
const int kPadding = 2;
// The padding between the top of the badge and the top of the text.
const int kTopTextPadding = -1;
const int kBadgeHeight = 11;
const int kMaxTextWidth = 23;
// The minimum width for center-aligning the badge.
const int kCenterAlignThreshold = 20;

// duplicate methods (ui/gfx/canvas_skia.cc)
bool IntersectsClipRectInt(const SkCanvas& canvas, int x, int y, int w, int h) {
  SkRect clip;
  return canvas.getClipBounds(&clip) &&
      clip.intersect(SkIntToScalar(x), SkIntToScalar(y), SkIntToScalar(x + w),
                     SkIntToScalar(y + h));
}

bool ClipRectInt(SkCanvas& canvas, int x, int y, int w, int h) {
  SkRect new_clip;
  new_clip.set(SkIntToScalar(x), SkIntToScalar(y),
               SkIntToScalar(x + w), SkIntToScalar(y + h));
  return canvas.clipRect(new_clip);
}

void TileImageInt(SkCanvas& canvas, const SkBitmap& bitmap,
                  int src_x, int src_y,
                  int dest_x, int dest_y, int w, int h) {
  if (!IntersectsClipRectInt(canvas, dest_x, dest_y, w, h))
    return;

  SkPaint paint;

  SkShader* shader = SkShader::CreateBitmapShader(bitmap,
                                                  SkShader::kRepeat_TileMode,
                                                  SkShader::kRepeat_TileMode);
  paint.setShader(shader);
  paint.setXfermodeMode(SkXfermode::kSrcOver_Mode);

  // CreateBitmapShader returns a Shader with a reference count of one, we
  // need to unref after paint takes ownership of the shader.
  shader->unref();
  canvas.save();
  canvas.translate(SkIntToScalar(dest_x - src_x), SkIntToScalar(dest_y - src_y));
  ClipRectInt(canvas, src_x, src_y, w, h);
  canvas.drawPaint(paint);
  canvas.restore();
}

void TileImageInt(SkCanvas& canvas, const SkBitmap& bitmap,
                  int x, int y, int w, int h) {
  TileImageInt(canvas, bitmap, 0, 0, x, y, w, h);
}

}

class OverOutTextButton : public views::TextButton {
public:
  OverOutTextButton(ChatItemView* owner, const std::wstring& text)
    : views::TextButton(owner, text),
      owner_(owner) {
  }

  virtual void OnMouseEntered(const views::MouseEvent& event) OVERRIDE {
    owner_->OnMouseEntered(event);
  }

  virtual void OnMouseExited(const views::MouseEvent& event) OVERRIDE {
    owner_->OnMouseExited(event);
  }
private:
  ChatItemView *owner_;
};

ChatItemView::ChatItemView(FacebookChatItem *model, ChatbarView *chatbar)
  : model_(model),
    chatbar_(chatbar),
    close_button_bg_color_(0),
    chat_popup_(NULL),
    notification_popup_(NULL),
    isMouseOverNotification_(false),
    notification_icon_(NULL) {

  model->AddObserver(this);

  ResourceBundle& rb = ResourceBundle::GetSharedInstance();

  openChatButton_ = new OverOutTextButton(this, UTF8ToWide(model->username()));
  //openChatButton_->SetNormalHasBorder(true);
  openChatButton_->set_icon_placement(views::TextButton::ICON_ON_LEFT);
  openChatButton_->set_border(new InfoBarButtonBorder);
  //openChatButton_->SetNormalHasBorder(true);
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
    chat_popup_->GetWidget()->RemoveObserver(this);
    chat_popup_->GetWidget()->Close();
    delete chat_popup_;
  }
  if (notification_popup_) {
    notification_popup_->GetWidget()->RemoveObserver(this);
    notification_popup_->GetWidget()->Close();
    delete notification_popup_;
  }
  if (notification_icon_)
    delete notification_icon_;

  for (TimerList::iterator it = timers_.begin(); it != timers_.end(); it++) {
    if (*it && (*it)->IsRunning())
      (*it)->Stop();
    delete *it;
    *it = NULL;
  }
}

void ChatItemView::ButtonPressed(views::Button* sender, const views::Event& event) {
  if (sender == close_button_) {
    Close(true);
  } else if (sender == openChatButton_) {
    if (!chat_popup_)
      ActivateChat();
  }
}

void ChatItemView::Layout() {
  gfx::Rect bounds;
  bounds.set_x(0);
  bounds.set_y(0);
  gfx::Size sz = GetPreferredSize();
  bounds.set_size(sz);

  openChatButton_->SetBoundsRect(bounds);

  gfx::Size closeButtonSize = close_button_->GetPreferredSize();
  close_button_->SetBounds(bounds.width() - closeButtonSize.width() - kCloseButtonRightPadding,
                            bounds.height() / 2 - closeButtonSize.height() / 2,
                            closeButtonSize.width(),
                            closeButtonSize.height());

  if (notification_popup_) {
    notification_popup_->SizeToContents();
  }

  if (chat_popup_) {
    chat_popup_->SizeToContents();
  }
}

gfx::Size ChatItemView::GetPreferredSize() {
  return kChatButtonSize;
}

void ChatItemView::OnChatUpdated(FacebookChatItem *source) {
  DCHECK(source == model_);
  switch (source->state()) {
  case FacebookChatItem::REMOVING:
    Close(false);
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
  } else if (model_->status() != FacebookChatItem::COMPOSING)
    UpdateNotificationIcon();

  if (model_->status() == FacebookChatItem::COMPOSING)
    openChatButton_->SetIcon(*rb.GetBitmapNamed(IDR_FACEBOOK_COMPOSING_ICON_14));
}

void ChatItemView::Close(bool should_animate) {
  if (notification_popup_)
    notification_popup_->GetWidget()->Close();
  chatbar_->Remove(this, should_animate);
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
      notification_popup_->GetWidget()->Close();

  model_->ClearUnreadMessages();
  StatusChanged();  // restore status icon
  SchedulePaint();

  // open popup
  std::string urlString(chrome::kFacebookChatExtensionPrefixURL);
  urlString += chrome::kFacebookChatExtensionChatPage;
  urlString += "#";
  urlString += model_->jid() + "&" +
    chatbar_->browser()->profile()->GetFacebookChatManager()->global_my_uid();

  chat_popup_ = ChatPopup::ShowPopup(GURL(urlString), chatbar_->browser(),
                                this, BitpopBubbleBorder::BOTTOM_CENTER);
  chat_popup_->GetWidget()->AddObserver(this);
}

const FacebookChatItem* ChatItemView::GetModel() const {
  return model_;
}

void ChatItemView::OnWidgetClosing(views::Widget* bubble) {
  if (chat_popup_ && bubble == chat_popup_->GetWidget()) {
    bubble->RemoveObserver(this);
    chat_popup_ = NULL;
  }

  if (notification_popup_ && bubble == notification_popup_->GetWidget()) {
    bubble->RemoveObserver(this);
    notification_popup_ = NULL;

    for (TimerList::iterator it = timers_.begin(); it != timers_.end(); it++) {
      if (*it && (*it)->IsRunning())
        (*it)->Stop();
    }
  }
}

void ChatItemView::NotifyUnread() {
  if (model_->num_notifications() > 0) {
    if (!notification_popup_) {
      notification_popup_ = ChatNotificationPopup::Show(this, BitpopBubbleBorder::BOTTOM_CENTER);
      notification_popup_->GetWidget()->AddObserver(this);
    }

    notification_popup_->PushMessage(model_->GetMessageAtIndex(model_->num_notifications() - 1));

    ChatTimer *timer = NULL;
    for (TimerList::iterator it = timers_.begin(); it != timers_.end(); it++) {
      if (!(*it)->IsRunning()) {
        timer = *it;
        break;
      }
    }
    if (timer == NULL) {
      timer = new ChatTimer();
      timers_.push_back(timer);
    }
    timer->Start(FROM_HERE, base::TimeDelta::FromSeconds(kNotificationMessageDelaySec), this, &ChatItemView::TimerFired);

    if (!visible())
      chatbar_->PlaceFirstInOrder(this);

    UpdateNotificationIcon();
    openChatButton_->SchedulePaint();
  }
}

void ChatItemView::TimerFired() {
  if (notification_popup_)
    (void)notification_popup_->PopMessage();
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

void ChatItemView::OnMouseEntered(const views::MouseEvent& event) {
  if (!notification_popup_ && model_->num_notifications() > 0) {

    notification_popup_ = ChatNotificationPopup::Show(this, BitpopBubbleBorder::BOTTOM_CENTER);
    notification_popup_->GetWidget()->AddObserver(this);
    notification_popup_->PushMessage(model_->GetMessageAtIndex(model_->num_notifications() - 1));
    isMouseOverNotification_ = true;
  }
}

void ChatItemView::OnMouseExited(const views::MouseEvent& event) {
  if (isMouseOverNotification_ && notification_popup_) {
    //notification_popup_->set_fade_away_on_close(false);
    notification_popup_->GetWidget()->Close();
  }
}

void ChatItemView::UpdateNotificationIcon() {
  if (notification_icon_) {
    delete notification_icon_;
    notification_icon_ = NULL;
  }

  if (model_->num_notifications() > 0) {
    notification_icon_ = new SkBitmap();
    notification_icon_->setConfig(SkBitmap::kARGB_8888_Config, kNotifyIconDimX, kNotifyIconDimY);
    notification_icon_->allocPixels();

    SkCanvas canvas(*notification_icon_);
    canvas.clear(SkColorSetARGB(0, 0, 0, 0));

    // ----------------------------------------------------------------------
    gfx::Rect bounds(0, 0, kNotifyIconDimX, kNotifyIconDimY);

    char text_s[4] = { '\0', '\0', '\0', '\0' };
    char *p = text_s;
    int num = model_->num_notifications();
    if (num > 99)
      num = 99;
    if (num > 9)
      *p++ = num / 10 + '0';
    *p = num % 10 + '0';

    std::string text(text_s);
    if (text.empty())
      return;

    SkColor text_color = SK_ColorWHITE;
    SkColor background_color = SkColorSetARGB(255, 218, 0, 24);

    //canvas->Save();

    SkPaint* text_paint = badge_util::GetBadgeTextPaintSingleton();
    text_paint->setTextSize(SkFloatToScalar(kTextSize));
    text_paint->setColor(text_color);

    // Calculate text width. We clamp it to a max size.
    SkScalar text_width = text_paint->measureText(text.c_str(), text.size());
    text_width = SkIntToScalar(
        std::min(kMaxTextWidth, SkScalarFloor(text_width)));

    // Calculate badge size. It is clamped to a min width just because it looks
    // silly if it is too skinny.
    int badge_width = SkScalarFloor(text_width) + kPadding * 2;
    int icon_width = kNotifyIconDimX;
    // Force the pixel width of badge to be either odd (if the icon width is odd)
    // or even otherwise. If there is a mismatch you get http://crbug.com/26400.
    if (icon_width != 0 && (badge_width % 2 != kNotifyIconDimX % 2))
      badge_width += 1;
    badge_width = std::max(kBadgeHeight, badge_width);

    // Paint the badge background color in the right location. It is usually
    // right-aligned, but it can also be center-aligned if it is large.
    SkRect rect;
    rect.fBottom = SkIntToScalar(bounds.bottom() - kBottomMargin);
    rect.fTop = rect.fBottom - SkIntToScalar(kBadgeHeight);
    if (badge_width >= kCenterAlignThreshold) {
      rect.fLeft = SkIntToScalar(
                       SkScalarFloor(SkIntToScalar(bounds.x()) +
                                     SkIntToScalar(bounds.width()) / 2 -
                                     SkIntToScalar(badge_width) / 2));
      rect.fRight = rect.fLeft + SkIntToScalar(badge_width);
    } else {
      rect.fRight = SkIntToScalar(bounds.right());
      rect.fLeft = rect.fRight - badge_width;
    }

    SkPaint rect_paint;
    rect_paint.setStyle(SkPaint::kFill_Style);
    rect_paint.setAntiAlias(true);
    rect_paint.setColor(background_color);
    canvas.drawRoundRect(rect, SkIntToScalar(2),
                                          SkIntToScalar(2), rect_paint);

    // Overlay the gradient. It is stretchy, so we do this in three parts.
    ResourceBundle& resource_bundle = ResourceBundle::GetSharedInstance();
    SkBitmap* gradient_left = resource_bundle.GetBitmapNamed(
        IDR_BROWSER_ACTION_BADGE_LEFT);
    SkBitmap* gradient_right = resource_bundle.GetBitmapNamed(
        IDR_BROWSER_ACTION_BADGE_RIGHT);
    SkBitmap* gradient_center = resource_bundle.GetBitmapNamed(
        IDR_BROWSER_ACTION_BADGE_CENTER);

    canvas.drawBitmap(*gradient_left, rect.fLeft, rect.fTop);

    TileImageInt(canvas,
        *gradient_center,
        SkScalarFloor(rect.fLeft) + gradient_left->width(),
        SkScalarFloor(rect.fTop),
        SkScalarFloor(rect.width()) - gradient_left->width() -
                      gradient_right->width(),
        SkScalarFloor(rect.height()));
    canvas.drawBitmap(*gradient_right,
        rect.fRight - SkIntToScalar(gradient_right->width()), rect.fTop);

    // Finally, draw the text centered within the badge. We set a clip in case the
    // text was too large.
    rect.fLeft += kPadding;
    rect.fRight -= kPadding;
    canvas.clipRect(rect);
    canvas.drawText(text.c_str(), text.size(),
                                     rect.fLeft + (rect.width() - text_width) / 2,
                                     rect.fTop + kTextSize + kTopTextPadding,
                                     *text_paint);

    openChatButton_->SetIcon(*notification_icon_);
  }
}
