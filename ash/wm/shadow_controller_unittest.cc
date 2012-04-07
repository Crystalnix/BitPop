// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/shadow_controller.h"

#include <algorithm>
#include <vector>

#include "ash/shell.h"
#include "ash/test/aura_shell_test_base.h"
#include "ash/wm/shadow.h"
#include "ash/wm/shadow_types.h"
#include "ash/wm/window_properties.h"
#include "base/memory/scoped_ptr.h"
#include "ui/aura/root_window.h"
#include "ui/aura/window.h"
#include "ui/gfx/compositor/layer.h"

namespace ash {
namespace test {

typedef ash::test::AuraShellTestBase ShadowControllerTest;

// Tests that various methods in Window update the Shadow object as expected.
TEST_F(ShadowControllerTest, Shadow) {
  scoped_ptr<aura::Window> window(new aura::Window(NULL));
  window->SetType(aura::client::WINDOW_TYPE_NORMAL);
  window->Init(ui::Layer::LAYER_TEXTURED);
  window->SetParent(NULL);

  // We should create the shadow before the window is visible (the shadow's
  // layer won't get drawn yet since it's a child of the window's layer).
  internal::ShadowController::TestApi api(
      ash::Shell::GetInstance()->shadow_controller());
  const internal::Shadow* shadow = api.GetShadowForWindow(window.get());
  ASSERT_TRUE(shadow != NULL);
  EXPECT_TRUE(shadow->layer()->visible());

  // The shadow should remain visible after window visibility changes.
  window->Show();
  EXPECT_TRUE(shadow->layer()->visible());
  window->Hide();
  EXPECT_TRUE(shadow->layer()->visible());

  // If the shadow is disabled, it should be hidden.
  internal::SetShadowType(window.get(), internal::SHADOW_TYPE_NONE);
  window->Show();
  EXPECT_FALSE(shadow->layer()->visible());
  internal::SetShadowType(window.get(), internal::SHADOW_TYPE_RECTANGULAR);
  EXPECT_TRUE(shadow->layer()->visible());

  // The shadow's layer should be a child of the window's layer.
  EXPECT_EQ(window->layer(), shadow->layer()->parent());

  window->parent()->RemoveChild(window.get());
  aura::Window* window_ptr = window.get();
  window.reset();
  EXPECT_TRUE(api.GetShadowForWindow(window_ptr) == NULL);
}

// Tests that the window's shadow's bounds are updated correctly.
TEST_F(ShadowControllerTest, ShadowBounds) {
  scoped_ptr<aura::Window> window(new aura::Window(NULL));
  window->SetType(aura::client::WINDOW_TYPE_NORMAL);
  window->Init(ui::Layer::LAYER_TEXTURED);
  window->SetParent(NULL);
  window->Show();

  const gfx::Rect kOldBounds(20, 30, 400, 300);
  window->SetBounds(kOldBounds);

  // When the shadow is first created, it should use the window's size (but
  // remain at the origin, since it's a child of the window's layer).
  internal::SetShadowType(window.get(), internal::SHADOW_TYPE_RECTANGULAR);
  internal::ShadowController::TestApi api(
      ash::Shell::GetInstance()->shadow_controller());
  const internal::Shadow* shadow = api.GetShadowForWindow(window.get());
  ASSERT_TRUE(shadow != NULL);
  EXPECT_EQ(gfx::Rect(kOldBounds.size()).ToString(),
            shadow->content_bounds().ToString());

  // When we change the window's bounds, the shadow's should be updated too.
  gfx::Rect kNewBounds(50, 60, 500, 400);
  window->SetBounds(kNewBounds);
  EXPECT_EQ(gfx::Rect(kNewBounds.size()).ToString(),
            shadow->content_bounds().ToString());
}

}  // namespace test
}  // namespace ash
