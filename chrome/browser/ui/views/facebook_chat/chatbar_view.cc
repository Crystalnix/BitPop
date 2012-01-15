// Copyright (c) 2011 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/facebook_chat/chatbar_view.h"

#include "chrome/browser/facebook_chat/facebook_chat_item.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/view_ids.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "grit/theme_resources_standard.h"
#include "ui/base/animation/slide_animation.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/canvas.h"
#include "views/controls/button/image_button.h"

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

// Sets size->width() to view's preferred width + size->width().s
// Sets size->height() to the max of the view's preferred height and
// size->height();
void AdjustSize(views::View* view, gfx::Size* size) {
  gfx::Size view_preferred = view->GetPreferredSize();
  size->Enlarge(view_preferred.width(), 0);
  size->set_height(std::max(view_preferred.height(), size->height()));
}

int CenterPosition(int size, int target_size) {
  return std::max((target_size - size) / 2, kTopBottomPadding);
}

}  // namespace

ChatbarView::ChatbarView(Browser* browser, BrowserView* parent)
  : browser_(browser),
    parent_(parent) {
  SetID(VIEW_ID_FACEBOOK_CHATBAR);
  parent->AddChildView(this);
  
  ResourceBundle &rb = ResourceBundle::GetSharedInstance();

  close_button_ = new views::ImageButton(this);
  close_button_->SetImage(views::CustomButton::BS_NORMAL,
                          rb.GetBitmapNamed(IDR_CLOSE_BAR));
  close_button_->SetImage(views::CustomButton::BS_HOT,
                          rb.GetBitmapNamed(IDR_CLOSE_BAR_H));
  close_button_->SetImage(views::CustomButton::BS_PUSHED,
                          rb.GetBitmapNamed(IDR_CLOSE_BAR_P));
  close_button_->SetAccessibleName(
      l10n_util::GetStringUTF16(IDS_ACCNAME_CLOSE));
  UpdateButtonColors();
  AddChildView(close_button_);

  bar_animation_.reset(new ui::SlideAnimation(this));
  bar_animation_->SetSlideDuration(kBarAnimationDurationMs);
  Show();
}

ChatbarView::~ChatbarView() {
  parent_->RemoveChildView(this);
}

gfx::Size ChatbarView::GetPreferredSize() {
  gfx::Size prefsize(kRightPadding + kLeftPadding, 0);
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

void ChatbarView::Layout() {
  // Now that we know we have a parent, we can safely set our theme colors.
  set_background(views::Background::CreateSolidBackground(
      GetThemeProvider()->GetColor(ThemeService::COLOR_TOOLBAR)));

  // Let our base class layout our child views
  views::View::Layout();

  gfx::Size close_button_size = close_button_->GetPreferredSize();
  // If the window is maximized, we want to expand the hitbox of the close
  // button to the right and bottom to make it easier to click.
  bool is_maximized = browser_->window()->IsMaximized();
  int next_x = width() - kRightPadding - close_button_size.width();
  int y = CenterPosition(close_button_size.height(), height());
  close_button_->SetBounds(next_x, y,
      is_maximized ? width() - next_x : close_button_size.width(),
      is_maximized ? height() - y : close_button_size.height());
}

void ChatbarView::OnPaintBorder(gfx::Canvas* canvas) {
  canvas->FillRectInt(kBorderColor, 0, 0, width(), 1);
}

void ChatbarView::AddChatItem(FacebookChatItem *chat_item) {
  if (!this->IsVisible())
    Show();
}

void ChatbarView::RemoveAll() {
}

void ChatbarView::Show() {
  this->SetVisible(true);
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

void ChatbarView::ButtonPressed(views::Button* button, const views::Event& event) {
  Hide();
}

void ChatbarView::Closed() {
  //parent_->RemoveChildView(this);
  //this->SetVisible(false);
}

void ChatbarView::UpdateButtonColors() {
  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  if (GetThemeProvider()) {
    close_button_->SetBackground(
        GetThemeProvider()->GetColor(ThemeService::COLOR_TAB_TEXT),
        rb.GetBitmapNamed(IDR_CLOSE_BAR),
        rb.GetBitmapNamed(IDR_CLOSE_BAR_MASK));
  }
}