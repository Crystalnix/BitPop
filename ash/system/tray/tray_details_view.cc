// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/tray/tray_details_view.h"

#include "ash/system/tray/tray_constants.h"
#include "ash/system/tray/tray_views.h"
#include "ui/gfx/canvas.h"
#include "ui/views/background.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/grid_layout.h"

namespace ash {
namespace internal {

class ScrollBorder : public views::Border {
 public:
  ScrollBorder() {}
  virtual ~ScrollBorder() {}

  void set_visible(bool visible) { visible_ = visible; }

 private:
  // Overridden from views::Border.
  virtual void Paint(const views::View& view,
                     gfx::Canvas* canvas) const OVERRIDE {
    if (!visible_)
      return;
    canvas->FillRect(gfx::Rect(0, view.height() - 1, view.width(), 1),
                     kBorderLightColor);
  }

  virtual void GetInsets(gfx::Insets* insets) const OVERRIDE {
    insets->Set(0, 0, 1, 0);
  }

  bool visible_;

  DISALLOW_COPY_AND_ASSIGN(ScrollBorder);
};

TrayDetailsView::TrayDetailsView()
    : footer_(NULL),
      scroller_(NULL),
      scroll_content_(NULL),
      scroll_border_(NULL) {
  SetLayoutManager(new views::BoxLayout(views::BoxLayout::kVertical,
      0, 0, 0));
  set_background(views::Background::CreateSolidBackground(kBackgroundColor));
}

TrayDetailsView::~TrayDetailsView() {
}

void TrayDetailsView::CreateSpecialRow(int string_id,
                                       ViewClickListener* listener) {
  DCHECK(!footer_);
  footer_ = new SpecialPopupRow();
  footer_->SetTextLabel(string_id, listener);
  AddChildViewAt(footer_, child_count());
}

void TrayDetailsView::CreateScrollableList() {
  DCHECK(!scroller_);
  scroll_content_ = new views::View;
  scroll_content_->SetLayoutManager(new views::BoxLayout(
      views::BoxLayout::kVertical, 0, 0, 1));
  scroller_ = new FixedSizedScrollView;
  scroller_->SetContentsView(scroll_content_);

  // Note: |scroller_| takes ownership of |scroll_border_|.
  scroll_border_ = new ScrollBorder;
  scroller_->set_border(scroll_border_);

  AddChildView(scroller_);
}

void TrayDetailsView::Reset() {
  RemoveAllChildViews(true);
  footer_ = NULL;
  scroller_ = NULL;
  scroll_content_ = NULL;
}

void TrayDetailsView::Layout() {
  if (!scroller_ || !footer_ || bounds().IsEmpty()) {
    views::View::Layout();
    return;
  }

  scroller_->SetFixedSize(gfx::Size());
  gfx::Size size = GetPreferredSize();
  if (size.height() > height()) {
    // The available size is smaller than the requested size. Squeeze the
    // scroller so that everything fits in the available size.
    gfx::Size scroller_size = scroll_content_->GetPreferredSize();
    scroller_->SetFixedSize(gfx::Size(
        width() + scroller_->GetScrollBarWidth(),
        scroller_size.height() - (size.height() - height())));
  }
  views::View::Layout();
  // Always make sure the footer element is bottom aligned.
  gfx::Rect fbounds = footer_->bounds();
  fbounds.set_y(height() - footer_->height());
  footer_->SetBoundsRect(fbounds);
}

void TrayDetailsView::OnPaintBorder(gfx::Canvas* canvas) {
  if (scroll_border_) {
    int index = GetIndexOf(scroller_);
    if (index < child_count() - 1 && child_at(index + 1) != footer_)
      scroll_border_->set_visible(true);
    else
      scroll_border_->set_visible(false);
  }

  views::View::OnPaintBorder(canvas);
}

}  // namespace internal
}  // namespace ash
