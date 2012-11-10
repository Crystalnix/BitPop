// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_GFX_SCREEN_IMPL_H_
#define UI_GFX_SCREEN_IMPL_H_

#include "ui/gfx/display.h"
#include "ui/gfx/native_widget_types.h"

namespace gfx {
class Display;
class Point;
class Rect;

// A class that provides |gfx::Screen|'s implementation on aura.
class UI_EXPORT ScreenImpl {
 public:
  virtual ~ScreenImpl() {}

  virtual gfx::Point GetCursorScreenPoint() = 0;
  virtual gfx::NativeWindow GetWindowAtCursorScreenPoint() = 0;

  virtual int GetNumDisplays() = 0;
  virtual gfx::Display GetDisplayNearestWindow(
      gfx::NativeView window) const = 0;
  virtual gfx::Display GetDisplayNearestPoint(
      const gfx::Point& point) const = 0;
  virtual gfx::Display GetDisplayMatching(
      const gfx::Rect& match_rect) const = 0;
  virtual gfx::Display GetPrimaryDisplay() const = 0;
};

}  // namespace gfx

#endif  // UI_GFX_SCREEN_IMPL_H_
