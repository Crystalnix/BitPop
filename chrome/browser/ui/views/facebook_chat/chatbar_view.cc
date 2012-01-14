// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/facebook_chat/chatbar_view.h"

#include "chrome/browser/facebook_chat/facebook_chat_item.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/view_ids.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "ui/base/animation/slide_animation.h"
#include "ui/gfx/canvas.h"

namespace {

// Max number of chat buttons we'll contain. Any time a view is added and
// we already have this many chat item views, one is removed.
static const size_t kMaxChatItemViews = 15;

// Padding from left edge and first chat item view.
static const int kLeftPadding = 2;

// Padding from right edge and close button link.
static const int kRightPadding = 10;

// Padding between the chat item views.
static const int kChatItemPadding = 10;

// Padding between the top/bottom and the content.
static const int kTopBottomPadding = 2;

static const SkColor kBorderColor = SkColorSetRGB(214, 214, 214);

// Bar show/hide speed.
static const int kBarAnimationDurationMs = 120;

}


ChatbarView::ChatbarView(Browser* browser, BrowserView* parent)
  : browser_(browser),
    parent_(parent) {
  SetID(VIEW_ID_FACEBOOK_CHATBAR);
  parent->AddChildView(this);
  
  bar_animation_.reset(new ui::SlideAnimation(this));
  bar_animation_->SetSlideDuration(kBarAnimationDurationMs);
  Show();
}

ChatbarView::~ChatbarView() {
  parent_->RemoveChildView(this);
}

gfx::Size ChatbarView::GetPreferredSize() {
  gfx::Size prefsize(kRightPadding + kLeftPadding + kClosePadding, 0);
  AdjustSize(close_button_, &prefsize);
 
  // TODO: switch to chat items
  //// Add one download view to the preferred size.
  //if (!download_views_.empty()) {
  //  AdjustSize(*download_views_.begin(), &prefsize);
  //  prefsize.Enlarge(kDownloadPadding, 0);
  //}
  prefsize.Enlarge(0, kTopBottomPadding + kTopBottomPadding);
  if (bar_animation_->is_animating()) {
    prefsize.set_height(static_cast<int>(
        static_cast<double>(prefsize.height()) *
                            bar_animation_->GetCurrentValue()));
  }
  return prefsize;
}

void ChatbarView::OnPaintBorder(gfx::Canvas* canvas) {
  canvas->FillRectInt(kBorderColor, 0, 0, width(), 1);
}

void ChatbarView::AddChatItem(FacebookChatItem *chat_item) {
}

void ChatbarView::Show() {
  bar_animation_->Show();
}

void ChatbarView::Hide() {
  bar_animation_->Hide();
}

Browser *ChatbarView::browser() const {
  return browser_;
}

bool ChatbarView::IsShowing() const {
  return bar_animation_->IsShowing();
}

bool ChatbarView::IsClosing() const {
  return bar_animation_->IsClosing();
}

void ChatbarView::AnimationProgressed(const ui::Animation *animation) {
  if (animation == bar_animation_.get()) {
    // Force a re-layout of the parent, which will call back into
    // GetPreferredSize, where we will do our animation. In the case where the
    // animation is hiding, we do a full resize - the fast resizing would
    // otherwise leave blank white areas where the shelf was and where the
    // user's eye is. Thankfully bottom-resizing is a lot faster than
    // top-resizing.
    parent_->ToolbarSizeChanged(bar_animation_->IsShowing());
  }
}

void ChatbarView::AnimationEnded(const ui::Animation *animation) {
  if (animation == bar_animation_.get()) {
    parent_->SetChatbarVisible(bar_animation_->IsShowing());
    if (!bar_animation_->IsShowing())
      Closed();
  }
}

void ChatbarView::Closed() {
}