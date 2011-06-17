// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_BUBBLE_BORDER_H_
#define CHROME_BROWSER_UI_VIEWS_BUBBLE_BORDER_H_
#pragma once

#include "third_party/skia/include/core/SkColor.h"
#include "views/background.h"
#include "views/border.h"

class SkBitmap;

// Renders a border, with optional arrow, and a custom dropshadow.
// This can be used to produce floating "bubble" objects with rounded corners.
class BubbleBorder : public views::Border {
 public:
  // Possible locations for the (optional) arrow.
  // 0 bit specifies left or right.
  // 1 bit specifies top or bottom.
  // 2 bit specifies horizontal or vertical.
  enum ArrowLocation {
    TOP_LEFT     = 0,
    TOP_RIGHT    = 1,
    BOTTOM_LEFT  = 2,
    BOTTOM_RIGHT = 3,
    LEFT_TOP     = 4,
    RIGHT_TOP    = 5,
    LEFT_BOTTOM  = 6,
    RIGHT_BOTTOM = 7,
    NONE  = 8,  // No arrow. Positioned under the supplied rect.
    FLOAT = 9   // No arrow. Centered over the supplied rect.
  };

  explicit BubbleBorder(ArrowLocation arrow_location)
      : override_arrow_offset_(0),
        arrow_location_(arrow_location),
        background_color_(SK_ColorWHITE) {
    InitClass();
  }

  // Returns the radius of the corner of the border.
  static int GetCornerRadius() {
    // We can't safely calculate a border radius by comparing the sizes of the
    // side and corner images, because either may have been extended in various
    // directions in order to do more subtle dropshadow fading or other effects.
    // So we hardcode the most accurate value.
    return 4;
  }

  // Sets the location for the arrow.
  void set_arrow_location(ArrowLocation arrow_location) {
    arrow_location_ = arrow_location;
  }
  ArrowLocation arrow_location() const { return arrow_location_; }

  static ArrowLocation horizontal_mirror(ArrowLocation loc) {
    return loc >= NONE ? loc : static_cast<ArrowLocation>(loc ^ 1);
  }

  static ArrowLocation vertical_mirror(ArrowLocation loc) {
    return loc >= NONE ? loc : static_cast<ArrowLocation>(loc ^ 2);
  }

  static bool has_arrow(ArrowLocation loc) {
    return loc >= NONE ? false : true;
  }

  static bool is_arrow_on_left(ArrowLocation loc) {
    return loc >= NONE ? false : !(loc & 1);
  }

  static bool is_arrow_on_top(ArrowLocation loc) {
    return loc >= NONE ? false : !(loc & 2);
  }

  static bool is_arrow_on_horizontal(ArrowLocation loc) {
    return loc >= NONE ? false : !(loc & 4);
  }

  // Sets the background color for the arrow body.  This is irrelevant if you do
  // not also set the arrow location to something other than NONE.
  void set_background_color(SkColor background_color) {
    background_color_ = background_color;
  }
  SkColor background_color() const { return background_color_; }

  // For borders with an arrow, gives the desired bounds (in screen coordinates)
  // given the rect to point to and the size of the contained contents.  This
  // depends on the arrow location, so if you change that, you should call this
  // again to find out the new coordinates.
  gfx::Rect GetBounds(const gfx::Rect& position_relative_to,
                      const gfx::Size& contents_size) const;

  // Sets a fixed offset for the arrow from the beginning of corresponding edge.
  // The arrow will still point to the same location but the bubble will shift
  // location to make that happen. Returns actuall arrow offset, in case of
  // overflow it differ from desired.
  int SetArrowOffset(int offset, const gfx::Size& contents_size);

  // Overridden from views::Border:
  virtual void GetInsets(gfx::Insets* insets) const;

 private:
  // Loads images if necessary.
  static void InitClass();

  virtual ~BubbleBorder() { }

  // Overridden from views::Border:
  virtual void Paint(const views::View& view, gfx::Canvas* canvas) const;

  void DrawEdgeWithArrow(gfx::Canvas* canvas,
                         bool is_horizontal,
                         SkBitmap* edge,
                         SkBitmap* arrow,
                         int start_x,
                         int start_y,
                         int before_arrow,
                         int after_arrow,
                         int offset) const;

  void DrawArrowInterior(gfx::Canvas* canvas,
                         bool is_horizontal,
                         int tip_x,
                         int tip_y,
                         int shift_x,
                         int shift_y) const;

  // Border graphics.
  static SkBitmap* left_;
  static SkBitmap* top_left_;
  static SkBitmap* top_;
  static SkBitmap* top_right_;
  static SkBitmap* right_;
  static SkBitmap* bottom_right_;
  static SkBitmap* bottom_;
  static SkBitmap* bottom_left_;
  static SkBitmap* left_arrow_;
  static SkBitmap* top_arrow_;
  static SkBitmap* right_arrow_;
  static SkBitmap* bottom_arrow_;

  // Minimal offset of the arrow from the closet edge of bounding rect.
  static int arrow_offset_;

  // If specified, overrides the pre-calculated |arrow_offset_| of the arrow.
  int override_arrow_offset_;

  ArrowLocation arrow_location_;
  SkColor background_color_;

  DISALLOW_COPY_AND_ASSIGN(BubbleBorder);
};

// A Background that clips itself to the specified BubbleBorder and uses
// the background color of the BubbleBorder.
class BubbleBackground : public views::Background {
 public:
  explicit BubbleBackground(BubbleBorder* border) : border_(border) {}

  // Background overrides.
  virtual void Paint(gfx::Canvas* canvas, views::View* view) const;

 private:
  BubbleBorder* border_;

  DISALLOW_COPY_AND_ASSIGN(BubbleBackground);
};

#endif  // CHROME_BROWSER_UI_VIEWS_BUBBLE_BORDER_H_
