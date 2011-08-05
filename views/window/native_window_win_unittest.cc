// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views/window/native_window_win.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/rect.h"

namespace views {

TEST(NativeWindowWinTest, EnsureRectIsVisibleInRect) {
  gfx::Rect parent_rect(0, 0, 500, 400);

  {
    // Child rect x < 0
    gfx::Rect child_rect(-50, 20, 100, 100);
    internal::EnsureRectIsVisibleInRect(parent_rect, &child_rect, 10);
    EXPECT_EQ(gfx::Rect(10, 20, 100, 100), child_rect);
  }

  {
    // Child rect y < 0
    gfx::Rect child_rect(20, -50, 100, 100);
    internal::EnsureRectIsVisibleInRect(parent_rect, &child_rect, 10);
    EXPECT_EQ(gfx::Rect(20, 10, 100, 100), child_rect);
  }

  {
    // Child rect right > parent_rect.right
    gfx::Rect child_rect(450, 20, 100, 100);
    internal::EnsureRectIsVisibleInRect(parent_rect, &child_rect, 10);
    EXPECT_EQ(gfx::Rect(390, 20, 100, 100), child_rect);
  }

  {
    // Child rect bottom > parent_rect.bottom
    gfx::Rect child_rect(20, 350, 100, 100);
    internal::EnsureRectIsVisibleInRect(parent_rect, &child_rect, 10);
    EXPECT_EQ(gfx::Rect(20, 290, 100, 100), child_rect);
  }

  {
    // Child rect width > parent_rect.width
    gfx::Rect child_rect(20, 20, 700, 100);
    internal::EnsureRectIsVisibleInRect(parent_rect, &child_rect, 10);
    EXPECT_EQ(gfx::Rect(20, 20, 480, 100), child_rect);
  }

  {
    // Child rect height > parent_rect.height
    gfx::Rect child_rect(20, 20, 100, 700);
    internal::EnsureRectIsVisibleInRect(parent_rect, &child_rect, 10);
    EXPECT_EQ(gfx::Rect(20, 20, 100, 380), child_rect);
  }
}

}  // namespace views
