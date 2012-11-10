// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/bubble/bubble_border_2.h"

#include <algorithm>  // for std::max

#include "base/logging.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/path.h"
#include "ui/gfx/rect.h"
#include "ui/gfx/screen.h"
#include "ui/gfx/skia_util.h"

namespace {

// Bubble border corner radius.
const int kCornerRadius = 2;

// Arrow width and height.
const int kArrowHeight = 10;
const int kArrowWidth = 20;

const int kBorderSize = 1;
const SkColor kBorderColor = SkColorSetARGB(0x26, 0, 0, 0);
const SkColor kBackgroundColor = SK_ColorWHITE;

const int kShadowOffsetX = 0;
const int kShadowOffsetY = 5;
const double kShadowBlur = 30;
const SkColor kShadowColor = SkColorSetARGB(0x72, 0, 0, 0);

// Builds a bubble shape for given |bounds|.
void BuildShape(const gfx::Rect& bounds,
                views::BubbleBorder::ArrowLocation arrow_location,
                SkScalar arrow_offset,
                SkScalar padding,
                SkPath* path,
                int corner_radius_int,
                int arrow_height_int,
                int arrow_width_int) {
  const SkScalar corner_radius = SkIntToScalar(corner_radius_int);

  const SkScalar left = SkIntToScalar(bounds.x()) + padding;
  const SkScalar top = SkIntToScalar(bounds.y()) + padding;
  const SkScalar right = SkIntToScalar(bounds.right()) - padding;
  const SkScalar bottom = SkIntToScalar(bounds.bottom()) - padding;

  const SkScalar center_x = SkIntToScalar((bounds.x() + bounds.right()) / 2);
  const SkScalar center_y = SkIntToScalar((bounds.y() + bounds.bottom()) / 2);

  const SkScalar half_arrow_width =
      (SkIntToScalar(arrow_width_int) - padding) / 2;
  const SkScalar arrow_height = SkIntToScalar(arrow_height_int) - padding;

  path->reset();
  path->incReserve(12);

  switch (arrow_location) {
    case views::BubbleBorder::TOP_LEFT:
    case views::BubbleBorder::TOP_RIGHT:
      path->moveTo(center_x, bottom);
      path->arcTo(right, bottom, right, center_y, corner_radius);
      path->arcTo(right, top, center_x  - half_arrow_width, top,
                  corner_radius);
      path->lineTo(center_x + arrow_offset + half_arrow_width, top);
      path->lineTo(center_x + arrow_offset, top - arrow_height);
      path->lineTo(center_x + arrow_offset - half_arrow_width, top);
      path->arcTo(left, top, left, center_y, corner_radius);
      path->arcTo(left, bottom, center_x, bottom, corner_radius);
      break;
    case views::BubbleBorder::BOTTOM_LEFT:
    case views::BubbleBorder::BOTTOM_RIGHT:
      path->moveTo(center_x, top);
      path->arcTo(left, top, left, center_y, corner_radius);
      path->arcTo(left, bottom, center_x  - half_arrow_width, bottom,
                  corner_radius);
      path->lineTo(center_x + arrow_offset - half_arrow_width, bottom);
      path->lineTo(center_x + arrow_offset, bottom + arrow_height);
      path->lineTo(center_x + arrow_offset + half_arrow_width, bottom);
      path->arcTo(right, bottom, right, center_y, corner_radius);
      path->arcTo(right, top, center_x, top, corner_radius);
      break;
    case views::BubbleBorder::LEFT_TOP:
    case views::BubbleBorder::LEFT_BOTTOM:
      path->moveTo(right, center_y);
      path->arcTo(right, top, center_x, top, corner_radius);
      path->arcTo(left, top, left, center_y + arrow_offset - half_arrow_width,
                  corner_radius);
      path->lineTo(left, center_y + arrow_offset - half_arrow_width);
      path->lineTo(left - arrow_height, center_y + arrow_offset);
      path->lineTo(left, center_y + arrow_offset + half_arrow_width);
      path->arcTo(left, bottom, center_x, bottom, corner_radius);
      path->arcTo(right, bottom, right, center_y, corner_radius);
      break;
    case views::BubbleBorder::RIGHT_TOP:
    case views::BubbleBorder::RIGHT_BOTTOM:
      path->moveTo(left, center_y);
      path->arcTo(left, bottom, center_x, bottom, corner_radius);
      path->arcTo(right, bottom,
                  right, center_y + arrow_offset + half_arrow_width,
                  corner_radius);
      path->lineTo(right, center_y + arrow_offset + half_arrow_width);
      path->lineTo(right + arrow_height, center_y + arrow_offset);
      path->lineTo(right, center_y + arrow_offset - half_arrow_width);
      path->arcTo(right, top, center_x, top, corner_radius);
      path->arcTo(left, top, left, center_y, corner_radius);
      break;
    default:
      // No arrows.
      path->addRoundRect(gfx::RectToSkRect(bounds),
                         corner_radius,
                         corner_radius);
      break;
  }

  path->close();
}

}  // namespace

namespace views {

BubbleBorder2::BubbleBorder2(ArrowLocation arrow_location)
    : BubbleBorder(arrow_location, views::BubbleBorder::NO_SHADOW),
      corner_radius_(kCornerRadius),
      border_size_(kBorderSize),
      arrow_height_(kArrowHeight),
      arrow_width_(kArrowWidth),
      background_color_(kBackgroundColor),
      border_color_(kBorderColor) {
  SetShadow(gfx::ShadowValue(gfx::Point(kShadowOffsetX, kShadowOffsetY),
      kShadowBlur, kShadowColor));
}

BubbleBorder2::~BubbleBorder2() {}

gfx::Rect BubbleBorder2::ComputeOffsetAndUpdateBubbleRect(
    gfx::Rect bubble_rect,
    const gfx::Rect& anchor_view_rect) {
  offset_ = gfx::Point();

  gfx::Rect monitor_rect = gfx::Screen::GetDisplayNearestPoint(
      anchor_view_rect.CenterPoint()).bounds();
  if (monitor_rect.IsEmpty() || monitor_rect.Contains(bubble_rect))
    return bubble_rect;

  gfx::Point offset;

  if (has_arrow(arrow_location())) {
    if (is_arrow_on_horizontal(arrow_location())) {
      if (bubble_rect.x() < monitor_rect.x())
        offset.set_x(monitor_rect.x() - bubble_rect.x());
      else if (bubble_rect.right() > monitor_rect.right())
        offset.set_x(monitor_rect.right() - bubble_rect.right());
    } else {
      if (bubble_rect.y() < monitor_rect.y())
        offset.set_y(monitor_rect.y() - bubble_rect.y());
      else if (bubble_rect.bottom() > monitor_rect.bottom())
        offset.set_y(monitor_rect.bottom() - bubble_rect.bottom());
    }
  }

  bubble_rect.Offset(offset);
  set_offset(offset);

  return bubble_rect;
}

void BubbleBorder2::GetMask(const gfx::Rect& bounds,
                            gfx::Path* mask) const {
  gfx::Insets insets;
  GetInsets(&insets);

  gfx::Rect content_bounds(bounds);
  content_bounds.Inset(insets);

  BuildShape(content_bounds,
             arrow_location(),
             SkIntToScalar(GetArrowOffset()),
             SkIntToScalar(kBorderSize),
             mask,
             corner_radius_,
             arrow_height_,
             arrow_width_);
}

void BubbleBorder2::SetShadow(gfx::ShadowValue shadow) {
  shadows_.clear();
  shadows_.push_back(shadow);
}

int BubbleBorder2::GetBorderThickness() const {
  return 0;
}

void BubbleBorder2::PaintBackground(gfx::Canvas* canvas,
                                    const gfx::Rect& bounds) const {
  canvas->FillRect(bounds, background_color_);
}

int BubbleBorder2::GetArrowOffset() const {
  if (has_arrow(arrow_location())) {
    if (is_arrow_on_horizontal(arrow_location())) {
      // Picks x offset and moves bubble arrow in the opposite direction.
      // i.e. If bubble bounds is moved to right (positive offset), we need to
      // move arrow to left so that it points to the same position.
      return -offset_.x();
    } else {
      // Picks y offset and moves bubble arrow in the opposite direction.
      return -offset_.y();
    }
  }

  // Other style does not have an arrow, so return 0.
  return 0;
}

void BubbleBorder2::GetInsets(gfx::Insets* insets) const {
  // Negate to change from outer margin to inner padding.
  gfx::Insets shadow_padding(-gfx::ShadowValue::GetMargin(shadows_));

  if (arrow_location() == views::BubbleBorder::TOP_LEFT ||
      arrow_location() == views::BubbleBorder::TOP_RIGHT) {
    // Arrow at top.
    insets->Set(shadow_padding.top() + arrow_height_,
                shadow_padding.left(),
                shadow_padding.bottom(),
                shadow_padding.right());
  } else if (arrow_location() == views::BubbleBorder::BOTTOM_LEFT ||
      arrow_location() == views::BubbleBorder::BOTTOM_RIGHT) {
    // Arrow at bottom.
    insets->Set(shadow_padding.top(),
                shadow_padding.left(),
                shadow_padding.bottom() + arrow_height_,
                shadow_padding.right());
  } else if (arrow_location() == views::BubbleBorder::LEFT_TOP ||
      arrow_location() == views::BubbleBorder::LEFT_BOTTOM) {
    // Arrow on left.
    insets->Set(shadow_padding.top(),
                shadow_padding.left() + arrow_height_,
                shadow_padding.bottom(),
                shadow_padding.right());
  } else if (arrow_location() == views::BubbleBorder::RIGHT_TOP ||
      arrow_location() == views::BubbleBorder::RIGHT_BOTTOM) {
    // Arrow on right.
    insets->Set(shadow_padding.top(),
                shadow_padding.left(),
                shadow_padding.bottom(),
                shadow_padding.right() + arrow_height_);
  }
}

gfx::Rect BubbleBorder2::GetBounds(const gfx::Rect& position_relative_to,
                                   const gfx::Size& contents_size) const {
  gfx::Size border_size(contents_size);
  gfx::Insets insets;
  GetInsets(&insets);
  border_size.Enlarge(insets.width(), insets.height());

  // Negate to change from outer margin to inner padding.
  gfx::Insets shadow_padding(-gfx::ShadowValue::GetMargin(shadows_));

  // Anchor center that arrow aligns with.
  const int anchor_center_x =
      (position_relative_to.x() + position_relative_to.right()) / 2;
  const int anchor_center_y =
      (position_relative_to.y() + position_relative_to.bottom()) / 2;

  // Arrow position relative to top-left of bubble. |arrow_tip_x| is used for
  // arrow at the top or bottom and |arrow_tip_y| is used for arrow on left or
  // right. The 1px offset for |arrow_tip_y| is needed because the app list grid
  // icon start at a different position (1px earlier) compared with bottom
  // launcher bar.
  // TODO(xiyuan): Remove 1px offset when app list icon image asset is updated.
  int arrow_tip_x = insets.left() + contents_size.width() / 2 +
      GetArrowOffset();
  int arrow_tip_y = insets.top() + contents_size.height() / 2 +
      GetArrowOffset() + 1;

  if (arrow_location() == views::BubbleBorder::TOP_LEFT ||
      arrow_location() == views::BubbleBorder::TOP_RIGHT) {
    // Arrow at top.
    return gfx::Rect(
        gfx::Point(anchor_center_x - arrow_tip_x,
                   position_relative_to.bottom() - shadow_padding.top()),
        border_size);
  } else if (arrow_location() == views::BubbleBorder::BOTTOM_LEFT ||
      arrow_location() == views::BubbleBorder::BOTTOM_RIGHT) {
    // Arrow at bottom.
    return gfx::Rect(
        gfx::Point(anchor_center_x - arrow_tip_x,
                   position_relative_to.y() - border_size.height() +
                       shadow_padding.bottom()),
        border_size);
  } else if (arrow_location() == views::BubbleBorder::LEFT_TOP ||
      arrow_location() == views::BubbleBorder::LEFT_BOTTOM) {
    // Arrow on left.
    return gfx::Rect(
        gfx::Point(position_relative_to.right() - shadow_padding.left(),
                   anchor_center_y - arrow_tip_y),
        border_size);
  } else if (arrow_location() == views::BubbleBorder::RIGHT_TOP ||
      arrow_location() == views::BubbleBorder::RIGHT_BOTTOM) {
    // Arrow on right.
    return gfx::Rect(
        gfx::Point(position_relative_to.x() - border_size.width() +
                       shadow_padding.right(),
                   anchor_center_y - arrow_tip_y),
        border_size);
  }

  // No arrow bubble, center align with anchor.
  return position_relative_to.Center(border_size);
}

void BubbleBorder2::GetInsetsForArrowLocation(gfx::Insets* insets,
                                              ArrowLocation arrow_loc) const {
  int top = border_size_;
  int bottom = border_size_;
  int left = border_size_;
  int right = border_size_;
  switch (arrow_loc) {
    case TOP_LEFT:
    case TOP_RIGHT:
      top = std::max(top, arrow_height_);
      break;

    case BOTTOM_LEFT:
    case BOTTOM_RIGHT:
      bottom = std::max(bottom, arrow_height_);
      break;

    case LEFT_TOP:
    case LEFT_BOTTOM:
      left = std::max(left, arrow_height_);
      break;

    case RIGHT_TOP:
    case RIGHT_BOTTOM:
      right = std::max(right, arrow_height_);
      break;

    case NONE:
    case FLOAT:
      // Nothing to do.
      break;
  }
  insets->Set(top, left, bottom, right);
}

void BubbleBorder2::Paint(const views::View& view, gfx::Canvas* canvas) const {
  gfx::Insets insets;
  GetInsets(&insets);

  gfx::Rect content_bounds = view.bounds();
  content_bounds.Inset(insets);

  SkPath path;
  // Pads with 0.5 pixel since anti alias is used.
  BuildShape(content_bounds,
             arrow_location(),
             SkIntToScalar(GetArrowOffset()),
             SkDoubleToScalar(0.5),
             &path,
             corner_radius_,
             arrow_height_,
             arrow_width_);

  // Draw border and shadow. Note fill is needed to generate enough shadow.
  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setStyle(SkPaint::kStrokeAndFill_Style);
  paint.setStrokeWidth(SkIntToScalar(border_size_));
  paint.setColor(border_color_);
  SkSafeUnref(paint.setLooper(gfx::CreateShadowDrawLooper(shadows_)));
  canvas->DrawPath(path, paint);

  // Pads with |border_size_| pixels to leave space for border lines.
  BuildShape(content_bounds,
             arrow_location(),
             SkIntToScalar(GetArrowOffset()),
             SkIntToScalar(border_size_),
             &path,
             corner_radius_,
             arrow_height_,
             arrow_width_);
  canvas->Save();
  canvas->ClipPath(path);

  // Use full bounds so that arrow is also painted.
  const gfx::Rect& bounds = view.bounds();
  PaintBackground(canvas, bounds);

  canvas->Restore();
}

}  // namespace views
