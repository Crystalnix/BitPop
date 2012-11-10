// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/tray/tray_background_view.h"

#include "ash/launcher/background_animator.h"
#include "ash/shell.h"
#include "ash/shell_window_ids.h"
#include "ash/system/status_area_widget.h"
#include "ash/system/status_area_widget_delegate.h"
#include "ash/system/tray/tray_constants.h"
#include "ui/aura/window.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/skia_util.h"
#include "ui/views/background.h"
#include "ui/views/layout/box_layout.h"

namespace {

const SkColor kTrayBackgroundAlpha = 100;
const SkColor kTrayBackgroundHoverAlpha = 150;

// Adjust the size of TrayContainer with additional padding.
const int kTrayContainerVerticalPaddingBottomAlignment  = 1;
const int kTrayContainerHorizontalPaddingBottomAlignment  = 1;
const int kTrayContainerVerticalPaddingVerticalAlignment  = 1;
const int kTrayContainerHorizontalPaddingVerticalAlignment = 1;

}  // namespace

namespace ash {
namespace internal {

class TrayBackground : public views::Background {
 public:
  TrayBackground() : alpha_(kTrayBackgroundAlpha) {}
  virtual ~TrayBackground() {}

  void set_alpha(int alpha) { alpha_ = alpha; }

 private:
  // Overridden from views::Background.
  virtual void Paint(gfx::Canvas* canvas, views::View* view) const OVERRIDE {
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setStyle(SkPaint::kFill_Style);
    paint.setColor(SkColorSetARGB(alpha_, 0, 0, 0));
    SkPath path;
    gfx::Rect bounds(view->GetLocalBounds());
    SkScalar radius = SkIntToScalar(kTrayRoundedBorderRadius);
    path.addRoundRect(gfx::RectToSkRect(bounds), radius, radius);
    canvas->DrawPath(path, paint);
  }

  int alpha_;

  DISALLOW_COPY_AND_ASSIGN(TrayBackground);
};

TrayBackgroundView::TrayContainer::TrayContainer(ShelfAlignment alignment)
    : alignment_(alignment) {
  UpdateLayout();
}

void TrayBackgroundView::TrayContainer::SetAlignment(ShelfAlignment alignment) {
  if (alignment_ == alignment)
    return;
  alignment_ = alignment;
  UpdateLayout();
}

gfx::Size TrayBackgroundView::TrayContainer::GetPreferredSize() {
  if (size_.IsEmpty())
    return views::View::GetPreferredSize();
  return size_;
}

void TrayBackgroundView::TrayContainer::ChildPreferredSizeChanged(
    views::View* child) {
  PreferredSizeChanged();
}

void TrayBackgroundView::TrayContainer::ChildVisibilityChanged(View* child) {
  PreferredSizeChanged();
}

void TrayBackgroundView::TrayContainer::ViewHierarchyChanged(bool is_add,
                                                             View* parent,
                                                             View* child) {
  if (parent == this)
    PreferredSizeChanged();
}

void TrayBackgroundView::TrayContainer::UpdateLayout() {
  // Adjust the size of status tray dark background by adding additional
  // empty border.
  if (alignment_ == SHELF_ALIGNMENT_BOTTOM) {
    set_border(views::Border::CreateEmptyBorder(
        kTrayContainerVerticalPaddingBottomAlignment,
        kTrayContainerHorizontalPaddingBottomAlignment,
        kTrayContainerVerticalPaddingBottomAlignment,
        kTrayContainerHorizontalPaddingBottomAlignment));
    views::BoxLayout* layout =
        new views::BoxLayout(views::BoxLayout::kHorizontal, 0, 0, 0);
    layout->set_spread_blank_space(true);
    views::View::SetLayoutManager(layout);
  } else {
    set_border(views::Border::CreateEmptyBorder(
        kTrayContainerVerticalPaddingVerticalAlignment,
        kTrayContainerHorizontalPaddingVerticalAlignment,
        kTrayContainerVerticalPaddingVerticalAlignment,
        kTrayContainerHorizontalPaddingVerticalAlignment));
    views::BoxLayout* layout =
        new views::BoxLayout(views::BoxLayout::kVertical, 0, 0, 0);
    layout->set_spread_blank_space(true);
    views::View::SetLayoutManager(layout);
  }
  PreferredSizeChanged();
}

////////////////////////////////////////////////////////////////////////////////
// TrayBackgroundView

TrayBackgroundView::TrayBackgroundView(
    internal::StatusAreaWidget* status_area_widget)
    : status_area_widget_(status_area_widget),
      tray_container_(NULL),
      shelf_alignment_(SHELF_ALIGNMENT_BOTTOM),
      background_(NULL),
      ALLOW_THIS_IN_INITIALIZER_LIST(hide_background_animator_(
          this, 0, kTrayBackgroundAlpha)),
      ALLOW_THIS_IN_INITIALIZER_LIST(hover_background_animator_(
          this, 0, kTrayBackgroundHoverAlpha - kTrayBackgroundAlpha)) {
  set_notify_enter_exit_on_child(true);

  // Initially we want to paint the background, but without the hover effect.
  SetPaintsBackground(true, internal::BackgroundAnimator::CHANGE_IMMEDIATE);
  hover_background_animator_.SetPaintsBackground(false,
      internal::BackgroundAnimator::CHANGE_IMMEDIATE);

  tray_container_ = new TrayContainer(shelf_alignment_);
  SetContents(tray_container_);
}

TrayBackgroundView::~TrayBackgroundView() {
}

void TrayBackgroundView::OnMouseEntered(const views::MouseEvent& event) {
  hover_background_animator_.SetPaintsBackground(true,
      internal::BackgroundAnimator::CHANGE_ANIMATE);
}

void TrayBackgroundView::OnMouseExited(const views::MouseEvent& event) {
  hover_background_animator_.SetPaintsBackground(false,
      internal::BackgroundAnimator::CHANGE_ANIMATE);
}

void TrayBackgroundView::ChildPreferredSizeChanged(views::View* child) {
  PreferredSizeChanged();
}

bool TrayBackgroundView::PerformAction(const views::Event& event) {
  return false;
}

void TrayBackgroundView::UpdateBackground(int alpha) {
  if (background_) {
    background_->set_alpha(hide_background_animator_.alpha() +
                           hover_background_animator_.alpha());
  }
  SchedulePaint();
}

void TrayBackgroundView::SetContents(views::View* contents) {
  background_ = new internal::TrayBackground;
  contents->set_background(background_);

  SetLayoutManager(new views::BoxLayout(views::BoxLayout::kVertical, 0, 0, 0));
  AddChildView(contents);
}

void TrayBackgroundView::SetPaintsBackground(
      bool value,
      internal::BackgroundAnimator::ChangeType change_type) {
  hide_background_animator_.SetPaintsBackground(value, change_type);
}

void TrayBackgroundView::SetShelfAlignment(ShelfAlignment alignment) {
  shelf_alignment_ = alignment;
  SetBorder();
  tray_container_->SetAlignment(alignment);
}

void TrayBackgroundView::SetBorder() {
  views::View* parent = status_area_widget_->status_area_widget_delegate();
  int child_count = parent->child_count();
  DCHECK(child_count > 0);
  int on_edge = (this == parent->child_at(child_count-1));
  // Change the border padding for different shelf alignment.
  if (shelf_alignment() == SHELF_ALIGNMENT_BOTTOM) {
    set_border(views::Border::CreateEmptyBorder(
        0, 0, kPaddingFromBottomOfScreenBottomAlignment,
        on_edge ? kPaddingFromRightEdgeOfScreenBottomAlignment : 0));
  } else if (shelf_alignment() == SHELF_ALIGNMENT_LEFT) {
    set_border(views::Border::CreateEmptyBorder(
        0, kPaddingFromOuterEdgeOfLauncherVerticalAlignment,
        on_edge ? kPaddingFromBottomOfScreenVerticalAlignment : 0,
        kPaddingFromInnerEdgeOfLauncherVerticalAlignment));
  } else {
    set_border(views::Border::CreateEmptyBorder(
        0, kPaddingFromInnerEdgeOfLauncherVerticalAlignment,
        on_edge ? kPaddingFromBottomOfScreenVerticalAlignment : 0,
        kPaddingFromOuterEdgeOfLauncherVerticalAlignment));
  }
}

}  // namespace internal
}  // namespace ash
