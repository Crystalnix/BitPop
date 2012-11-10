// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_GFX_COMPOSITOR_TEST_TEST_COMPOSITOR_HOST_H_
#define UI_GFX_COMPOSITOR_TEST_TEST_COMPOSITOR_HOST_H_

#include "base/message_loop.h"

namespace gfx {
class Rect;
}

namespace ui {

class Compositor;

class TestCompositorHost {
 public:
  virtual ~TestCompositorHost() {}

  // Creates a new TestCompositorHost. The caller owns the returned value.
  static TestCompositorHost* Create(const gfx::Rect& bounds);

  // Shows the TestCompositorHost.
  virtual void Show() = 0;

  virtual ui::Compositor* GetCompositor() = 0;
};

}  // namespace ui

#endif  // UI_GFX_COMPOSITOR_TEST_TEST_COMPOSITOR_HOST_H_
