// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/frame/bubble_frame_view.h"

#include <algorithm>

#include "base/utf_string_conversions.h"
#include "chrome/browser/chromeos/frame/bubble_window.h"
#include "chrome/browser/chromeos/login/helper.h"
#include "grit/ui_resources_standard.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "ui/base/hit_test.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/canvas_skia.h"
#include "ui/gfx/font.h"
#include "ui/gfx/insets.h"
#include "ui/gfx/path.h"
#include "ui/gfx/rect.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/throbber.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"

namespace {

const int kTitleTopPadding = 10;
const int kTitleContentPadding = 10;
const int kHorizontalPadding = 10;

// Title font size correction.
#if defined(CROS_FONTS_USING_BCI)
const int kTitleFontSizeDelta = 0;
#else
const int kTitleFontSizeDelta = 1;
#endif

}  // namespace

namespace chromeos {

BubbleFrameView::BubbleFrameView(views::WidgetDelegate* widget_delegate,
                                 DialogStyle style)
    : style_(style),
      title_(NULL),
      close_button_(NULL),
      throbber_(NULL) {
  if (widget_delegate->ShouldShowWindowTitle()) {
    title_ = new views::Label(widget_delegate->GetWindowTitle());
    title_->SetHorizontalAlignment(views::Label::ALIGN_LEFT);
    title_->SetFont(title_->font().DeriveFont(kFontSizeCorrectionDelta,
                                              gfx::Font::BOLD));
    AddChildView(title_);
  }

  if (style_ & STYLE_XBAR) {
    ResourceBundle& rb = ResourceBundle::GetSharedInstance();
    close_button_ = new views::ImageButton(this);
    close_button_->SetImage(views::CustomButton::BS_NORMAL,
        rb.GetBitmapNamed(IDR_CLOSE_BAR));
    close_button_->SetImage(views::CustomButton::BS_HOT,
        rb.GetBitmapNamed(IDR_CLOSE_BAR_H));
    close_button_->SetImage(views::CustomButton::BS_PUSHED,
        rb.GetBitmapNamed(IDR_CLOSE_BAR_P));
    AddChildView(close_button_);
  }

  if (style_ & STYLE_THROBBER) {
    throbber_ = CreateDefaultSmoothedThrobber();
    AddChildView(throbber_);
  }
}

BubbleFrameView::~BubbleFrameView() {
}

void BubbleFrameView::StartThrobber() {
  DCHECK(throbber_ != NULL);
  if (title_)
    title_->SetText(string16());
  throbber_->Start();
}

void BubbleFrameView::StopThrobber() {
  DCHECK(throbber_ != NULL);
  throbber_->Stop();
  if (title_)
    title_->SetText(GetWidget()->widget_delegate()->GetWindowTitle());
}

gfx::Rect BubbleFrameView::GetBoundsForClientView() const {
  return client_view_bounds_;
}

gfx::Rect BubbleFrameView::GetWindowBoundsForClientBounds(
    const gfx::Rect& client_bounds) const {
  gfx::Insets insets = GetInsets();

  gfx::Size title_size;
  if (title_)
    title_size = title_->GetPreferredSize();
  gfx::Size close_button_size;
  if (close_button_)
    close_button_size = close_button_->GetPreferredSize();
  gfx::Size throbber_size;
  if (throbber_)
    throbber_size = throbber_->GetPreferredSize();

  int top_height = insets.top();
  if (title_size.height() > 0 ||
      close_button_size.height() > 0 ||
      throbber_size.height() > 0) {
    top_height += kTitleContentPadding + std::max(
        std::max(title_size.height(), close_button_size.height()),
        throbber_size.height());
  }
  return gfx::Rect(std::max(0, client_bounds.x() - insets.left()),
                   std::max(0, client_bounds.y() - top_height),
                   client_bounds.width() + insets.width(),
                   client_bounds.height() + top_height + insets.bottom());
}

int BubbleFrameView::NonClientHitTest(const gfx::Point& point) {
  return HTNOWHERE;
}

void BubbleFrameView::GetWindowMask(const gfx::Size& size,
                                    gfx::Path* window_mask) {
}

void BubbleFrameView::ResetWindowControls() {
}

void BubbleFrameView::UpdateWindowIcon() {
}

gfx::Insets BubbleFrameView::GetInsets() const {
  return (style_ & STYLE_FLUSH || style_ & STYLE_FLUSH_CONTENT) ?
         gfx::Insets() :
         gfx::Insets(kTitleTopPadding,
                     kHorizontalPadding,
                     0,
                     kHorizontalPadding);
}

gfx::Size BubbleFrameView::GetPreferredSize() {
  gfx::Size pref = GetWidget()->client_view()->GetPreferredSize();
  gfx::Rect bounds(0, 0, pref.width(), pref.height());
  return GetWidget()->non_client_view()->GetWindowBoundsForClientBounds(
      bounds).size();
}

void BubbleFrameView::Layout() {
  gfx::Insets insets = GetInsets();

  gfx::Size title_size;
  if (title_)
    title_size = title_->GetPreferredSize();
  gfx::Size close_button_size;
  if (close_button_)
    close_button_size = close_button_->GetPreferredSize();
  gfx::Size throbber_size;
  if (throbber_)
    throbber_size = throbber_->GetPreferredSize();

  // Need to center elements which are shorter.
  int max_height = std::max(title_size.height(),
                            std::max(close_button_size.height(),
                                     throbber_size.height()));

  gfx::Insets title_insets = gfx::Insets();
  // Need to insert title padding for STYLE_FLUSH_CONTENT.
  if (style_ & STYLE_FLUSH_CONTENT)
    title_insets = gfx::Insets(kTitleTopPadding,
                               kHorizontalPadding,
                               0,
                               kHorizontalPadding);

  if (title_) {
    title_->SetBounds(
        insets.left() + title_insets.left(),
        insets.top() + title_insets.top() +
            (max_height - title_size.height())/2, // Center.
        std::max(0, width() - insets.width() - title_insets.width() -
            close_button_size.width()),
        title_size.height());
  }

  if (close_button_) {
    close_button_->SetBounds(
        width() - insets.right() - title_insets.right() -
            close_button_size.width(),
        insets.top() + title_insets.top() +
            (max_height - close_button_size.height())/2,
        close_button_size.width(),
        close_button_size.height());
  }

  if (throbber_) {
    throbber_->SetBounds(
        insets.left() + title_insets.left(),
        insets.top() + title_insets.top() +
            (max_height - throbber_size.height())/2,
        std::min(throbber_size.width(), width()),
        throbber_size.height());
  }

  int top_height = insets.top() + title_insets.top();
  if (title_size.height() > 0 ||
      close_button_size.height() > 0 ||
      throbber_size.height() > 0) {
    top_height += kTitleContentPadding + std::max(
        std::max(title_size.height(), close_button_size.height()),
        throbber_size.height());
  }
  client_view_bounds_.SetRect(insets.left(), top_height,
      std::max(0, width() - insets.width()),
      std::max(0, height() - top_height - insets.bottom()));
}

void BubbleFrameView::OnPaint(gfx::Canvas* canvas) {
  SkPaint paint;
  paint.setStyle(SkPaint::kFill_Style);
  paint.setColor(kBubbleWindowBackgroundColor);
  gfx::Path path;
  gfx::Rect bounds(GetContentsBounds());
  SkRect rect;
  rect.set(SkIntToScalar(bounds.x()), SkIntToScalar(bounds.y()),
           SkIntToScalar(bounds.right()), SkIntToScalar(bounds.bottom()));
  path.addRect(rect);
  canvas->GetSkCanvas()->drawPath(path, paint);
}

void BubbleFrameView::ButtonPressed(views::Button* sender,
                                    const views::Event& event) {
  if (close_button_ != NULL && sender == close_button_)
    GetWidget()->Close();
}

}  // namespace chromeos
