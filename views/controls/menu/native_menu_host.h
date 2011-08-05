// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VIEWS_CONTROLS_MENU_NATIVE_MENU_HOST_H_
#define VIEWS_CONTROLS_MENU_NATIVE_MENU_HOST_H_

#include "ui/gfx/native_widget_types.h"

namespace gfx {
class Rect;
}

namespace views {
class NativeWidget;
namespace internal {
class NativeMenuHostDelegate;
}

class NativeMenuHost {
 public:
  virtual ~NativeMenuHost() {}

  static NativeMenuHost* CreateNativeMenuHost(
      internal::NativeMenuHostDelegate* delegate);

  // Starts capturing input events.
  virtual void StartCapturing() = 0;

  virtual NativeWidget* AsNativeWidget() = 0;
};

}  // namespace views

#endif  // VIEWS_CONTROLS_MENU_NATIVE_MENU_HOST_H_
