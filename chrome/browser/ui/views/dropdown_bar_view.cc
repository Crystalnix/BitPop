// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/dropdown_bar_view.h"

#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/ui/view_ids.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources_standard.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkRect.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/canvas_skia.h"
#include "ui/views/border.h"
#include "ui/views/painter.h"
#include "ui/views/background.h"
#include "ui/views/widget/widget.h"

namespace {

// When we are animating, we draw only the top part of the left and right
// edges to give the illusion that the find dialog is attached to the
// window during this animation; this is the height of the items we draw.
const int kAnimatingEdgeHeight = 5;

// Background to paint toolbar background with rounded corners.
class DropdownBackground : public views::Background {
 public:
  explicit DropdownBackground(BrowserView* browser,
                              const SkBitmap* left_alpha_mask,
                              const SkBitmap* right_alpha_mask);
  virtual ~DropdownBackground() {}

  // Overridden from views::Background.
  virtual void Paint(gfx::Canvas* canvas, views::View* view) const OVERRIDE;

 private:
  BrowserView* browser_view_;
  const SkBitmap* left_alpha_mask_;
  const SkBitmap* right_alpha_mask_;

  DISALLOW_COPY_AND_ASSIGN(DropdownBackground);
};

DropdownBackground::DropdownBackground(BrowserView* browser_view,
                                     const SkBitmap* left_alpha_mask,
                                     const SkBitmap* right_alpha_mask)
    : browser_view_(browser_view),
      left_alpha_mask_(left_alpha_mask),
      right_alpha_mask_(right_alpha_mask) {
}

void DropdownBackground::Paint(gfx::Canvas* canvas, views::View* view) const {
  // Find the offset from which to tile the toolbar background image.
  // First, get the origin with respect to the screen.
  gfx::Point origin = view->GetWidget()->GetWindowScreenBounds().origin();
  // Now convert from screen to parent coordinates.
  view->ConvertPointToView(NULL, browser_view_, &origin);
  // Finally, calculate the background image tiling offset.
  origin = browser_view_->OffsetPointForToolbarBackgroundImage(origin);

  ui::ThemeProvider* tp = view->GetThemeProvider();
  SkBitmap background = *tp->GetBitmapNamed(IDR_THEME_TOOLBAR);

  int left_edge_width = left_alpha_mask_->width();
  int right_edge_width = right_alpha_mask_->width();
  int mask_height = left_alpha_mask_->height();
  int height = view->bounds().height();

  // Stretch the middle background to cover the entire area.
  canvas->TileImageInt(background, origin.x(), origin.y(),
      0, 0, view->bounds().width(), height);

  SkPaint paint;
  paint.setXfermodeMode(SkXfermode::kDstIn_Mode);
  // Draw left edge.
  canvas->DrawBitmapInt(*left_alpha_mask_, 0, 0, left_edge_width, mask_height,
      0, 0, left_edge_width, height, false, paint);

  // Draw right edge.
  int x_right_edge = view->bounds().width() - right_edge_width;
  canvas->DrawBitmapInt(*right_alpha_mask_, 0, 0, right_edge_width,
      mask_height, x_right_edge, 0, right_edge_width, height, false, paint);
}

}  // namespace

DropdownBarView::DropdownBarView(DropdownBarHost* host)
    : host_(host),
      animation_offset_(0) {
}

DropdownBarView::~DropdownBarView() {
}

////////////////////////////////////////////////////////////////////////////////
// DropDownBarView, public:

void DropdownBarView::SetAnimationOffset(int offset) {
  animation_offset_ = offset;
  set_clip_insets(gfx::Insets(animation_offset_, 0, 0, 0));
}

// DropDownBarView, views::View overrides:
void DropdownBarView::OnPaint(gfx::Canvas* canvas) {
  OnPaintBackground(canvas);
  OnPaintBorder(canvas);

  if (animation_offset() > 0) {
     gfx::CanvasSkia animating_edges(
         gfx::Size(bounds().width(), kAnimatingEdgeHeight), false);
     canvas->Translate(bounds().origin());
     OnPaintBackground(&animating_edges);
     OnPaintBorder(&animating_edges);
     canvas->DrawBitmapInt(animating_edges.ExtractBitmap(), bounds().x(),
         animation_offset());
  }
}

////////////////////////////////////////////////////////////////////////////////
// DropDownBarView, protected:

void DropdownBarView::SetBackground(const SkBitmap* left_alpha_mask,
                                    const SkBitmap* right_alpha_mask) {
  set_background(new DropdownBackground(host()->browser_view(), left_alpha_mask,
      right_alpha_mask));
}

void DropdownBarView::SetBorder(int left_border_bitmap_id,
                                int middle_border_bitmap_id,
                                int right_border_bitmap_id) {
  int border_bitmap_ids[3] = {left_border_bitmap_id, middle_border_bitmap_id,
      right_border_bitmap_id};
  set_border(views::Border::CreateBorderPainter(
      new views::HorizontalPainter(border_bitmap_ids)));
}
