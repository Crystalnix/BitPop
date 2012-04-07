// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_APP_LIST_DROP_SHADOW_LABEL_H_
#define ASH_APP_LIST_DROP_SHADOW_LABEL_H_
#pragma once

#include "ui/gfx/font.h"
#include "ui/views/controls/label.h"

namespace ash {

/////////////////////////////////////////////////////////////////////////////
//
// DropShadowLabel class
//
// A drop shadow label is a view subclass that can display a string
// with a drop shadow.
//
/////////////////////////////////////////////////////////////////////////////
class DropShadowLabel : public views::Label  {
 public:
  DropShadowLabel();

  // Sets the size of the drop shadow drawn under the text.
  // Defaults to two.  Note that this is a really simplistic drop
  // shadow -- it gets more expensive to draw the larger it gets,
  // since it simply draws more copies of the string.  For instance,
  // for a value of two, the string is draw seven times.  In general,
  // it is drawn three extra times for each increment of |size|.
  void SetDropShadowSize(int size);

  // Return the size of the drop shadow in pixels.
  int drop_shadow_size() const { return drop_shadow_size_; }

  // Overridden to paint the text differently from the base class.
  virtual void PaintText(gfx::Canvas* canvas,
                         const string16& text,
                         const gfx::Rect& text_bounds,
                         int flags) OVERRIDE;

 protected:
  virtual gfx::Size GetTextSize() const OVERRIDE;

 private:
  int drop_shadow_size_;

  DISALLOW_COPY_AND_ASSIGN(DropShadowLabel);
};

}  // namespace ash

#endif  // ASH_APP_LIST_DROP_SHADOW_LABEL_H_
