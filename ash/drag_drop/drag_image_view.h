// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_DRAG_DROP_DRAG_IMAGE_VIEW_H_
#define ASH_DRAG_DROP_DRAG_IMAGE_VIEW_H_
#pragma once

#include "ui/views/controls/image_view.h"

namespace views {
class Widget;
}

namespace ash {
namespace internal {

class DragImageView : public views::ImageView {
 public:
  DragImageView();
  virtual ~DragImageView();

  // Sets the bounds of the native widget.
  void SetScreenBounds(const gfx::Rect& bounds);

  // Sets the position of the native widget.
  void SetScreenPosition(const gfx::Point& position);

  // Sets the visibility of the native widget.
  void SetWidgetVisible(bool visible);

 private:
  scoped_ptr<views::Widget> widget_;

  DISALLOW_COPY_AND_ASSIGN(DragImageView);
};

}  // namespace internal
}  // namespace ash

#endif  // ASH_DRAG_DROP_DRAG_IMAGE_VIEW_H_
