// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/setting_level_bubble.h"

#include <gdk/gdk.h>

#include "base/timer.h"
#include "chrome/browser/chromeos/login/background_view.h"
#include "chrome/browser/chromeos/login/login_utils.h"
#include "chrome/browser/chromeos/setting_level_bubble_view.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/views/bubble/bubble.h"
#include "views/widget/root_view.h"

namespace {

const int kBubbleShowTimeoutSec = 2;
const int kAnimationDurationMs = 200;

// Horizontal relative position: 0 - leftmost, 0.5 - center, 1 - rightmost.
const double kBubbleXRatio = 0.5;

// Vertical gap from the bottom of the screen in pixels.
const int kBubbleBottomGap = 30;

int LimitPercent(int percent) {
  if (percent < 0)
    percent = 0;
  else if (percent > 100)
    percent = 100;
  return percent;
}

}  // namespace

namespace chromeos {

// Temporary helper routine. Tries to first return the widget from the
// most-recently-focused normal browser window, then from a login
// background, and finally NULL if both of those fail.
// TODO(glotov): remove this in favor of enabling Bubble class act
// without |parent| specified. crosbug.com/4025
static views::Widget* GetToplevelWidget() {
  GtkWindow* window = NULL;

  // We just use the default profile here -- this gets overridden as needed
  // in Chrome OS depending on whether the user is logged in or not.
  Browser* browser =
      BrowserList::FindBrowserWithType(
          ProfileManager::GetDefaultProfile(),
          Browser::TYPE_NORMAL,
          true);  // match_incognito
  if (browser) {
    window = GTK_WINDOW(browser->window()->GetNativeHandle());
  } else {
    // Otherwise, see if there's a background window that we can use.
    BackgroundView* background = LoginUtils::Get()->GetBackgroundView();
    if (background)
      window = GTK_WINDOW(background->GetNativeWindow());
  }

  if (!window)
    return NULL;

  views::NativeWidget* native_widget =
      views::NativeWidget::GetNativeWidgetForNativeWindow(window);
  return native_widget->GetWidget();
}

SettingLevelBubble::SettingLevelBubble(SkBitmap* increase_icon,
                                       SkBitmap* decrease_icon,
                                       SkBitmap* zero_icon)
    : previous_percent_(-1),
      current_percent_(-1),
      increase_icon_(increase_icon),
      decrease_icon_(decrease_icon),
      zero_icon_(zero_icon),
      bubble_(NULL),
      view_(NULL),
      animation_(this) {
  animation_.SetSlideDuration(kAnimationDurationMs);
  animation_.SetTweenType(ui::Tween::LINEAR);
}

void SettingLevelBubble::ShowBubble(int percent) {
  percent = LimitPercent(percent);
  if (previous_percent_ == -1)
    previous_percent_ = percent;
  current_percent_ = percent;

  SkBitmap* icon = increase_icon_;
  if (current_percent_ == 0)
    icon = zero_icon_;
  else if (current_percent_ < previous_percent_)
    icon = decrease_icon_;

  if (!bubble_) {
    views::Widget* widget = GetToplevelWidget();
    if (widget == NULL)
      return;
    DCHECK(view_ == NULL);
    view_ = new SettingLevelBubbleView;
    view_->Init(icon, previous_percent_);
    // Calculate position of the bubble.
    gfx::Rect bounds = widget->GetClientAreaScreenBounds();
    const gfx::Size view_size = view_->GetPreferredSize();
    // Note that (x, y) is the point of the center of the bubble.
    const int x = view_size.width() / 2 +
        kBubbleXRatio * (bounds.width() - view_size.width());
    const int y = bounds.height() - view_size.height() / 2 - kBubbleBottomGap;
    bubble_ = Bubble::ShowFocusless(widget,  // parent
                                    gfx::Rect(x, y, 0, 20),
                                    BubbleBorder::FLOAT,
                                    view_,  // contents
                                    this,   // delegate
                                    true);  // show while screen is locked
  } else {
    DCHECK(view_);
    timeout_timer_.Stop();
    view_->SetIcon(icon);
  }
  if (animation_.is_animating())
    animation_.End();
  animation_.Reset();
  animation_.Show();
  timeout_timer_.Start(base::TimeDelta::FromSeconds(kBubbleShowTimeoutSec),
                       this, &SettingLevelBubble::OnTimeout);
}

void SettingLevelBubble::HideBubble() {
  if (bubble_)
    bubble_->Close();
}

void SettingLevelBubble::UpdateWithoutShowingBubble(int percent) {
  percent = LimitPercent(percent);

  previous_percent_ =
      animation_.is_animating() ?
      animation_.GetCurrentValue() :
      current_percent_;
  if (previous_percent_ < 0)
    previous_percent_ = percent;
  current_percent_ = percent;

  if (animation_.is_animating())
    animation_.End();
  animation_.Reset();
  animation_.Show();
}

void SettingLevelBubble::OnTimeout() {
  HideBubble();
}

void SettingLevelBubble::BubbleClosing(Bubble* bubble, bool) {
  DCHECK(bubble == bubble_);
  timeout_timer_.Stop();
  animation_.Stop();
  bubble_ = NULL;
  view_ = NULL;
}

void SettingLevelBubble::AnimationEnded(const ui::Animation* animation) {
  previous_percent_ = current_percent_;
}

void SettingLevelBubble::AnimationProgressed(const ui::Animation* animation) {
  if (view_) {
    view_->Update(
        ui::Tween::ValueBetween(animation->GetCurrentValue(),
                                previous_percent_,
                                current_percent_));
  }
}

}  // namespace chromeos
