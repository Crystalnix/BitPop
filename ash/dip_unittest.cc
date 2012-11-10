// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <vector>

#include "ash/launcher/launcher.h"
#include "ash/shell.h"
#include "ash/test/ash_test_base.h"
#include "ash/wm/shadow.h"
#include "ash/wm/shadow_controller.h"
#include "ash/wm/shadow_types.h"
#include "ash/wm/window_properties.h"
#include "ash/wm/window_util.h"
#include "base/memory/scoped_ptr.h"
#include "ui/aura/client/activation_client.h"
#include "ui/aura/root_window.h"
#include "ui/aura/window.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/display.h"
#include "ui/gfx/insets.h"
#include "ui/gfx/screen.h"
#include "ui/views/widget/widget.h"

namespace ash {

typedef ash::test::AshTestBase DIPTest;

#if defined(OS_WIN)
// Windows/Aura doesn't have DIP support in display yet.
#define MAYBE_WorkArea DISABLED_WorkArea
#else
#define MAYBE_WorkArea WorkArea
#endif

// Test if the WM sets correct work area under different density.
TEST_F(DIPTest, MAYBE_WorkArea) {
  ChangeDisplayConfig(1.0f, gfx::Rect(0, 0, 1000, 900));

  aura::RootWindow* root = Shell::GetPrimaryRootWindow();
  const gfx::Display display = gfx::Screen::GetDisplayNearestWindow(root);

  EXPECT_EQ("0,0 1000x900", display.bounds().ToString());
  gfx::Rect work_area = display.work_area();
  EXPECT_EQ("0,0 1000x852", work_area.ToString());
  EXPECT_EQ("0,0,48,0", display.bounds().InsetsFrom(work_area).ToString());

  ChangeDisplayConfig(2.0f, gfx::Rect(0, 0, 2000, 1800));

  const gfx::Display display_2x = gfx::Screen::GetDisplayNearestWindow(root);

  // The |bounds_in_pixel()| should report bounds in pixel coordinate.
  EXPECT_EQ("0,0 2000x1800", display_2x.bounds_in_pixel().ToString());

  // Aura and views coordinates are in DIP, so they their bounds do not change.
  EXPECT_EQ("0,0 1000x900", display_2x.bounds().ToString());
  work_area = display_2x.work_area();
  EXPECT_EQ("0,0 1000x852", work_area.ToString());
  EXPECT_EQ("0,0,48,0", display_2x.bounds().InsetsFrom(work_area).ToString());

  // Sanity check if the workarea's inset hight is same as
  // the launcher's height.
  Launcher* launcher = Shell::GetInstance()->launcher();
  EXPECT_EQ(
      display_2x.bounds().InsetsFrom(work_area).height(),
      launcher->widget()->GetNativeView()->layer()->bounds().height());
}

}  // namespace ash
