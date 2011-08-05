// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VIEWS_CONTROLS_FOCUSABLE_BORDER_H_
#define VIEWS_CONTROLS_FOCUSABLE_BORDER_H_
#pragma once

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "views/border.h"
#include "views/view.h"

namespace gfx {
class Canvas;
class Insets;
}

namespace views {

// A Border class to draw a focused border around a field (e.g textfield).
class FocusableBorder : public Border {
 public:
  FocusableBorder();

  // Sets the insets of the border.
  void SetInsets(int top, int left, int bottom, int right);

  // Sets the focus state.
  void set_has_focus(bool has_focus) {
    has_focus_ = has_focus;
  }

  // Overridden from Border:
  virtual void Paint(const View& view, gfx::Canvas* canvas) const OVERRIDE;
  virtual void GetInsets(gfx::Insets* insets) const OVERRIDE;

 private:
  bool has_focus_;
  gfx::Insets insets_;

  DISALLOW_COPY_AND_ASSIGN(FocusableBorder);
};

}  // namespace views

#endif  // VIEWS_CONTROLS_FOCUSABLE_BORDER_H_
