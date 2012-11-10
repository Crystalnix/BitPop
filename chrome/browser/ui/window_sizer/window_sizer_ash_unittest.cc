// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/window_sizer/window_sizer.h"

#include <vector>

#include "ash/shell.h"
#include "ash/shell_window_ids.h"
#include "ash/test/ash_test_base.h"
#include "ash/test/test_shell_delegate.h"
#include "ash/wm/window_resizer.h"
#include "base/compiler_specific.h"
#include "base/logging.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/window_sizer/window_sizer_common_unittest.h"
#include "chrome/test/base/testing_profile.h"
#include "chrome/test/base/test_browser_window.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/test/render_view_test.h"
#include "content/public/test/test_browser_thread.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebKit.h"
#include "ui/aura/env.h"
#include "ui/aura/root_window.h"
#include "ui/aura/test/test_windows.h"

namespace {

typedef ash::test::AshTestBase WindowSizerTest;

// A special test class for use with browser creation - it will create a
// browser thread and deletes it after all other things have been destroyed.
class WindowSizerTestWithBrowser : public WindowSizerTest {
 public:
  WindowSizerTestWithBrowser();
  virtual ~WindowSizerTestWithBrowser();

 private:
  // Note: It is important to delete the thread after the browser instances got
  // deleted. For this we transfer the thread here.
  scoped_ptr<content::TestBrowserThread> ui_thread_;

  DISALLOW_COPY_AND_ASSIGN(WindowSizerTestWithBrowser);
};

WindowSizerTestWithBrowser::WindowSizerTestWithBrowser() {
  // Set up a UI message thread.
  MessageLoopForUI* ui_loop = message_loop();
  ui_thread_.reset(
      new content::TestBrowserThread(content::BrowserThread::UI, ui_loop));
}

WindowSizerTestWithBrowser::~WindowSizerTestWithBrowser() {
}

// A browser window proxy which is able to associate an aura native window with
// it.
class TestBrowserWindowAura : public TestBrowserWindow {
 public:
  explicit TestBrowserWindowAura(aura::Window* native_window);
  virtual ~TestBrowserWindowAura();

  virtual gfx::NativeWindow GetNativeWindow() OVERRIDE {
    return native_window_;
  }

 private:
  gfx::NativeWindow native_window_;

  DISALLOW_COPY_AND_ASSIGN(TestBrowserWindowAura);
};

} // namespace

TestBrowserWindowAura::TestBrowserWindowAura(aura::Window *native_window)
    : native_window_(native_window) {
}

TestBrowserWindowAura::~TestBrowserWindowAura() {}

// Test that the window is sized appropriately for the first run experience
// where the default window bounds calculation is invoked.
TEST_F(WindowSizerTest, DefaultSizeCase) {
  int grid = ash::Shell::GetInstance()->GetGridSize();
  EXPECT_EQ(WindowSizer::kDesktopBorderSize, grid);
  { // 4:3 monitor case, 1024x768, no taskbar
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(), gfx::Rect(),
                    gfx::Rect(), DEFAULT, &window_bounds, NULL, gfx::Rect());
    EXPECT_EQ(gfx::Rect(WindowSizer::kDesktopBorderSize,
                        WindowSizer::kDesktopBorderSize,
                        1024 - WindowSizer::kDesktopBorderSize * 2,
                        768 - WindowSizer::kDesktopBorderSize),
              window_bounds);
  }

  { // 4:3 monitor case, 1024x768, taskbar on bottom
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, taskbar_bottom_work_area, gfx::Rect(),
                    gfx::Rect(), gfx::Rect(), DEFAULT, &window_bounds, NULL,
                    gfx::Rect());
    EXPECT_EQ(gfx::Rect(WindowSizer::kDesktopBorderSize,
                        WindowSizer::kDesktopBorderSize,
                        1024 - WindowSizer::kDesktopBorderSize * 2,
                        ash::WindowResizer::AlignToGridRoundDown(
                            taskbar_bottom_work_area.height() -
                            WindowSizer::kDesktopBorderSize, grid)),
                        window_bounds);
  }

  { // 4:3 monitor case, 1024x768, taskbar on right
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, taskbar_right_work_area, gfx::Rect(),
                    gfx::Rect(), gfx::Rect(), DEFAULT, &window_bounds, NULL,
                    gfx::Rect());
    EXPECT_EQ(gfx::Rect(WindowSizer::kDesktopBorderSize,
                        WindowSizer::kDesktopBorderSize,
                        ash::WindowResizer::AlignToGridRoundDown(
                            taskbar_right_work_area.width() -
                            WindowSizer::kDesktopBorderSize * 2, grid),
                        768 - WindowSizer::kDesktopBorderSize),
              window_bounds);
  }

  { // 4:3 monitor case, 1024x768, taskbar on left
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, taskbar_left_work_area, gfx::Rect(),
                    gfx::Rect(), gfx::Rect(), DEFAULT, &window_bounds, NULL,
                    gfx::Rect());
    EXPECT_EQ(gfx::Rect(taskbar_left_work_area.x() +
                          WindowSizer::kDesktopBorderSize,
                        WindowSizer::kDesktopBorderSize,
                        ash::WindowResizer::AlignToGridRoundDown(
                            taskbar_left_work_area.width() -
                        WindowSizer::kDesktopBorderSize * 2, grid),
                            ash::WindowResizer::AlignToGridRoundDown(
                            taskbar_left_work_area.height() -
                            WindowSizer::kDesktopBorderSize, grid)),
              window_bounds);
  }

  { // 4:3 monitor case, 1024x768, taskbar on top
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, taskbar_top_work_area, gfx::Rect(),
                    gfx::Rect(), gfx::Rect(), DEFAULT, &window_bounds, NULL,
                    gfx::Rect());
    EXPECT_EQ(gfx::Rect(WindowSizer::kDesktopBorderSize,
                        taskbar_top_work_area.y() +
                          WindowSizer::kDesktopBorderSize,
                        1024 - WindowSizer::kDesktopBorderSize * 2,
                        ash::WindowResizer::AlignToGridRoundDown(
                            taskbar_top_work_area.height() -
                            WindowSizer::kDesktopBorderSize, grid)),
              window_bounds);
  }

  { // 4:3 monitor case, 1280x1024
    gfx::Rect window_bounds;
    GetWindowBounds(twelveeighty, twelveeighty, gfx::Rect(), gfx::Rect(),
                    gfx::Rect(), DEFAULT, &window_bounds, NULL, gfx::Rect());
    EXPECT_EQ(gfx::Rect(WindowSizer::kDesktopBorderSize,
                        WindowSizer::kDesktopBorderSize,
                        1280 - 2 * WindowSizer::kDesktopBorderSize,
                        1024 - WindowSizer::kDesktopBorderSize),
              window_bounds);
  }

  { // 4:3 monitor case, 1600x1200
    gfx::Rect window_bounds;
    GetWindowBounds(sixteenhundred, sixteenhundred, gfx::Rect(), gfx::Rect(),
                    gfx::Rect(), DEFAULT, &window_bounds, NULL, gfx::Rect());
    EXPECT_EQ(gfx::Rect((1600 - 1280) / 2, WindowSizer::kDesktopBorderSize,
                        1280,
                        1200 - WindowSizer::kDesktopBorderSize),
              window_bounds);
  }

  { // 16:10 monitor case, 1680x1050
    gfx::Rect window_bounds;
    GetWindowBounds(sixteeneighty, sixteeneighty, gfx::Rect(), gfx::Rect(),
                    gfx::Rect(), DEFAULT, &window_bounds, NULL, gfx::Rect());
    EXPECT_EQ(gfx::Rect((1680 - 1280) / 2, WindowSizer::kDesktopBorderSize,
                        1280,
                        ash::WindowResizer::AlignToGridRoundDown(
                            1050 - WindowSizer::kDesktopBorderSize,
                            grid)),
              window_bounds);
  }

  { // 16:10 monitor case, 1920x1200
    gfx::Rect window_bounds;
    GetWindowBounds(nineteentwenty, nineteentwenty, gfx::Rect(), gfx::Rect(),
                    gfx::Rect(), DEFAULT, &window_bounds, NULL, gfx::Rect());
    EXPECT_EQ(gfx::Rect((1920 - 1280) / 2, WindowSizer::kDesktopBorderSize,
                        1280,
                        1200 - WindowSizer::kDesktopBorderSize),
              window_bounds);
  }
}

// Test that the next opened window is positioned appropriately given the
// bounds of an existing window of the same type.
TEST_F(WindowSizerTest, LastWindowBoundsCase) {
  { // normal, in the middle of the screen somewhere.
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(WindowSizer::kDesktopBorderSize,
                              WindowSizer::kDesktopBorderSize, 500, 400),
                    gfx::Rect(), LAST_ACTIVE, &window_bounds, NULL,
                    gfx::Rect());
    EXPECT_EQ(gfx::Rect(kWindowTilePixels + WindowSizer::kDesktopBorderSize,
                        kWindowTilePixels + WindowSizer::kDesktopBorderSize,
                        500, 400),
              window_bounds);
  }

  { // taskbar on top.
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, taskbar_top_work_area, gfx::Rect(),
                    gfx::Rect(WindowSizer::kDesktopBorderSize,
                              WindowSizer::kDesktopBorderSize, 500, 400),
                    gfx::Rect(), LAST_ACTIVE, &window_bounds, NULL,
                    gfx::Rect());
    EXPECT_EQ(gfx::Rect(kWindowTilePixels + WindowSizer::kDesktopBorderSize,
                        std::max(kWindowTilePixels +
                                 WindowSizer::kDesktopBorderSize,
                                 34 /* toolbar height */),
                        500, 400), window_bounds);
  }

  { // Too small to satisify the minimum visibility condition.
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(WindowSizer::kDesktopBorderSize,
                              WindowSizer::kDesktopBorderSize, 29, 29),
                    gfx::Rect(), LAST_ACTIVE, &window_bounds, NULL,
                    gfx::Rect());
    EXPECT_EQ(gfx::Rect(kWindowTilePixels + WindowSizer::kDesktopBorderSize,
                        kWindowTilePixels + WindowSizer::kDesktopBorderSize,
                        30 /* not 29 */,
                        30 /* not 29 */),
              window_bounds);
  }


  { // Normal.
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(WindowSizer::kDesktopBorderSize,
                              WindowSizer::kDesktopBorderSize, 500, 400),
                    gfx::Rect(), LAST_ACTIVE, &window_bounds, NULL,
                    gfx::Rect());
    EXPECT_EQ(gfx::Rect(kWindowTilePixels + WindowSizer::kDesktopBorderSize,
                        kWindowTilePixels + WindowSizer::kDesktopBorderSize,
                        500, 400),
              window_bounds);
  }
}

// Test that the window opened is sized appropriately given persisted sizes.
TEST_F(WindowSizerTest, PersistedBoundsCase) {
  { // normal, in the middle of the screen somewhere.
    gfx::Rect initial_bounds(WindowSizer::kDesktopBorderSize,
                             WindowSizer::kDesktopBorderSize, 500, 400);

    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(), initial_bounds,
                    gfx::Rect(), PERSISTED, &window_bounds, NULL, gfx::Rect());
    EXPECT_EQ(initial_bounds, window_bounds);
  }

  { // Normal.
    gfx::Rect initial_bounds(0, 0, 1024, 768);

    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(), initial_bounds,
                    gfx::Rect(), PERSISTED, &window_bounds, NULL, gfx::Rect());
    EXPECT_EQ(initial_bounds, window_bounds);
  }

  { // normal, on non-primary monitor in negative coords.
    gfx::Rect initial_bounds(-600, 10, 500, 400);

    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, left_nonprimary,
                    initial_bounds, gfx::Rect(), PERSISTED, &window_bounds,
                    NULL, gfx::Rect());
    EXPECT_EQ(initial_bounds, window_bounds);
  }

  { // normal, on non-primary monitor in negative coords.
    gfx::Rect initial_bounds(-1024, 0, 1024, 768);

    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, left_nonprimary,
                    initial_bounds, gfx::Rect(), PERSISTED, &window_bounds,
                    NULL, gfx::Rect());
    EXPECT_EQ(initial_bounds, window_bounds);
  }

  { // Non-primary monitor resoultion has changed, but the monitor still
    // completely contains the window.

    gfx::Rect initial_bounds(1074, 50, 600, 500);

    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(1024, 0, 800, 600),
                    initial_bounds, right_nonprimary, PERSISTED,
                    &window_bounds, NULL, gfx::Rect());
    EXPECT_EQ(initial_bounds, window_bounds);
  }

  { // Non-primary monitor resoultion has changed, and the window is partially
    // off-screen.

    gfx::Rect initial_bounds(1274, 50, 600, 500);

    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(1024, 0, 800, 600),
                    initial_bounds, right_nonprimary, PERSISTED,
                    &window_bounds, NULL, gfx::Rect());
    EXPECT_EQ(gfx::Rect(1224, 50, 600, 500), window_bounds);
  }

  { // Non-primary monitor resoultion has changed, and the window is now too
    // large for the monitor.

    gfx::Rect initial_bounds(1274, 50, 900, 700);

    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(1024, 0, 800, 600),
                    initial_bounds, right_nonprimary, PERSISTED,
                    &window_bounds, NULL, gfx::Rect());
    EXPECT_EQ(gfx::Rect(1024, 0, 800, 600), window_bounds);
  }

  { // width and height too small
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(WindowSizer::kDesktopBorderSize,
                              WindowSizer::kDesktopBorderSize, 29, 29),
                    gfx::Rect(), PERSISTED, &window_bounds, NULL, gfx::Rect());
    EXPECT_EQ(gfx::Rect(WindowSizer::kDesktopBorderSize,
                        WindowSizer::kDesktopBorderSize,
                        30 /* not 29 */, 30 /* not 29 */),
              window_bounds);
  }
}

//////////////////////////////////////////////////////////////////////////////
// The following unittests have different results on Mac/non-Mac because we
// reposition windows aggressively on Mac.  The *WithAggressiveReposition tests
// are run on Mac, and the *WithNonAggressiveRepositioning tests are run on
// other platforms.

TEST_F(WindowSizerTest, LastWindowOffscreenWithNonAggressiveRepositioning) {
  { // taskbar on left.
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, taskbar_left_work_area, gfx::Rect(),
                    gfx::Rect(WindowSizer::kDesktopBorderSize,
                              WindowSizer::kDesktopBorderSize, 500, 400),
                    gfx::Rect(), LAST_ACTIVE, &window_bounds, NULL,
                    gfx::Rect());
    EXPECT_EQ(gfx::Rect(kWindowTilePixels + WindowSizer::kDesktopBorderSize,
                        kWindowTilePixels + WindowSizer::kDesktopBorderSize,
                        500, 400),
              window_bounds);
  }

  { // offset would put the new window offscreen at the bottom but the minimum
    // visibility condition is barely satisfied without relocation.
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(10, 728, 500, 400), gfx::Rect(), LAST_ACTIVE,
                    &window_bounds, NULL, gfx::Rect());
    EXPECT_EQ(gfx::Rect(10 + kWindowTilePixels, 738,
                        500, 400), window_bounds);
  }

  { // offset would put the new window offscreen at the bottom and the minimum
    // visibility condition is satisified by relocation.
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(10, 729, 500, 400), gfx::Rect(), LAST_ACTIVE,
                    &window_bounds, NULL, gfx::Rect());
    EXPECT_EQ(gfx::Rect(10 + kWindowTilePixels, 738 /* not 739 */, 500, 400),
              window_bounds);
  }

  { // offset would put the new window offscreen at the right but the minimum
    // visibility condition is barely satisfied without relocation.
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(984, 10, 500, 400), gfx::Rect(), LAST_ACTIVE,
                    &window_bounds, NULL, gfx::Rect());
    EXPECT_EQ(gfx::Rect(994, 10 + kWindowTilePixels, 500, 400), window_bounds);
  }

  { // offset would put the new window offscreen at the right and the minimum
    // visibility condition is satisified by relocation.
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(985, 10, 500, 400), gfx::Rect(), LAST_ACTIVE,
                    &window_bounds, NULL, gfx::Rect());
    EXPECT_EQ(gfx::Rect(994 /* not 995 */, 10 + kWindowTilePixels,
                        500, 400), window_bounds);
  }

  { // offset would put the new window offscreen at the bottom right and the
    // minimum visibility condition is satisified by relocation.
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(985, 729, 500, 400), gfx::Rect(), LAST_ACTIVE,
                    &window_bounds, NULL, gfx::Rect());
    EXPECT_EQ(gfx::Rect(994 /* not 995 */, 738 /* not 739 */, 500, 400),
              window_bounds);
  }
}

TEST_F(WindowSizerTest,
       PersistedWindowOffscreenWithNonAggressiveRepositioning) {
  { // off the left but the minimum visibility condition is barely satisfied
    // without relocaiton.
    gfx::Rect initial_bounds(-470, 50, 500, 400);

    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    initial_bounds, gfx::Rect(), PERSISTED,
                    &window_bounds, NULL, gfx::Rect());
    EXPECT_EQ(initial_bounds, window_bounds);
  }

  { // off the left and the minimum visibility condition is satisfied by
    // relocation.
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(-471, 50, 500, 400), gfx::Rect(), PERSISTED,
                    &window_bounds, NULL, gfx::Rect());
    EXPECT_EQ(gfx::Rect(-470 /* not -471 */, 50, 500, 400), window_bounds);
  }

  { // off the top
    gfx::Rect initial_bounds(50, -370, 500, 400);

    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(50, -370, 500, 400), gfx::Rect(), PERSISTED,
                    &window_bounds, NULL, gfx::Rect());
    EXPECT_EQ(gfx::Rect(50, 0, 500, 400), window_bounds);
  }

  { // off the right but the minimum visibility condition is barely satisified
    // without relocation.
    gfx::Rect initial_bounds(994, 50, 500, 400);

    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    initial_bounds, gfx::Rect(), PERSISTED,
                    &window_bounds, NULL, gfx::Rect());
    EXPECT_EQ(initial_bounds, window_bounds);
  }

  { // off the right and the minimum visibility condition is satisified by
    // relocation.
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(995, 50, 500, 400), gfx::Rect(), PERSISTED,
                    &window_bounds, NULL, gfx::Rect());
    EXPECT_EQ(gfx::Rect(994 /* not 995 */, 50, 500, 400), window_bounds);
  }

  { // off the bottom but the minimum visibility condition is barely satisified
    // without relocation.
    gfx::Rect initial_bounds(50, 738, 500, 400);

    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    initial_bounds, gfx::Rect(), PERSISTED,
                    &window_bounds, NULL, gfx::Rect());
    EXPECT_EQ(initial_bounds, window_bounds);
  }

  { // off the bottom and the minimum visibility condition is satisified by
    // relocation.
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(50, 739, 500, 400), gfx::Rect(), PERSISTED,
                    &window_bounds, NULL, gfx::Rect());
    EXPECT_EQ(gfx::Rect(50, 738 /* not 739 */, 500, 400), window_bounds);
  }

  { // off the topleft
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(-471, -371, 500, 400), gfx::Rect(), PERSISTED,
                    &window_bounds, NULL, gfx::Rect());
    EXPECT_EQ(gfx::Rect(-470 /* not -471 */, 0, 500, 400),
              window_bounds);
  }

  { // off the topright and the minimum visibility condition is satisified by
    // relocation.
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(995, -371, 500, 400), gfx::Rect(), PERSISTED,
                    &window_bounds, NULL, gfx::Rect());
    EXPECT_EQ(gfx::Rect(994 /* not 995 */, 0, 500, 400),
                        window_bounds);
  }

  { // off the bottomleft and the minimum visibility condition is satisified by
    // relocation.
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(-471, 739, 500, 400), gfx::Rect(), PERSISTED,
                    &window_bounds, NULL, gfx::Rect());
    EXPECT_EQ(gfx::Rect(-470 /* not -471 */, 738 /* not 739 */, 500, 400),
                        window_bounds);
  }

  { // off the bottomright and the minimum visibility condition is satisified by
    // relocation.
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(995, 739, 500, 400), gfx::Rect(), PERSISTED,
                    &window_bounds, NULL, gfx::Rect());
    EXPECT_EQ(gfx::Rect(994 /* not 995 */, 738 /* not 739 */, 500, 400),
                        window_bounds);
  }

  { // entirely off left
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(-700, 50, 500, 400), gfx::Rect(), PERSISTED,
                    &window_bounds, NULL, gfx::Rect());
    EXPECT_EQ(gfx::Rect(-470 /* not -700 */, 50, 500, 400), window_bounds);
  }

  { // entirely off left (monitor was detached since last run)
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(-700, 50, 500, 400), left_nonprimary, PERSISTED,
                    &window_bounds, NULL, gfx::Rect());
    EXPECT_EQ(gfx::Rect(0, 50, 500, 400), window_bounds);
  }

  { // entirely off top
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(50, -500, 500, 400), gfx::Rect(), PERSISTED,
                    &window_bounds, NULL, gfx::Rect());
    EXPECT_EQ(gfx::Rect(50, 0, 500, 400), window_bounds);
  }

  { // entirely off top (monitor was detached since last run)
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(50, -500, 500, 400), top_nonprimary,
                    PERSISTED, &window_bounds, NULL, gfx::Rect());
    EXPECT_EQ(gfx::Rect(50, 0, 500, 400), window_bounds);
  }

  { // entirely off right
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(1200, 50, 500, 400), gfx::Rect(), PERSISTED,
                    &window_bounds, NULL, gfx::Rect());
    EXPECT_EQ(gfx::Rect(994 /* not 1200 */, 50, 500, 400), window_bounds);
  }

  { // entirely off right (monitor was detached since last run)
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(1200, 50, 500, 400), right_nonprimary,
                    PERSISTED, &window_bounds, NULL, gfx::Rect());
    EXPECT_EQ(gfx::Rect(524, 50, 500, 400), window_bounds);
  }

  { // entirely off bottom
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(50, 800, 500, 400), gfx::Rect(), PERSISTED,
                    &window_bounds, NULL, gfx::Rect());
    EXPECT_EQ(gfx::Rect(50, 738 /* not 800 */, 500, 400), window_bounds);
  }

  { // entirely off bottom (monitor was detached since last run)
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(50, 800, 500, 400), bottom_nonprimary,
                    PERSISTED, &window_bounds, NULL, gfx::Rect());
    EXPECT_EQ(gfx::Rect(50, 368, 500, 400), window_bounds);
  }
}

// Test that a newly created window gets positioned over a previously created
// window.
TEST_F(WindowSizerTestWithBrowser, PlaceNewWindowOverOldWindow) {
  // Create a dummy window.
  aura::Window* default_container =
      ash::Shell::GetContainer(
          ash::Shell::GetPrimaryRootWindow(),
          ash::internal::kShellWindowId_DefaultContainer);
  scoped_ptr<aura::Window> window(
      aura::test::CreateTestWindowWithId(0, default_container));
  window->SetBounds(gfx::Rect(16, 32, 640, 320));

  scoped_ptr<aura::Window> popup(
      aura::test::CreateTestWindowWithId(1, default_container));
  popup->SetBounds(gfx::Rect(16, 32, 128, 256));

  scoped_ptr<aura::Window> panel(
      aura::test::CreateTestWindowWithId(2, default_container));
  panel->SetBounds(gfx::Rect(32, 48, 256, 512));

  // Create a browser which we can use to pass into the GetWindowBounds
  // function.
  scoped_ptr<TestingProfile> profile(new TestingProfile());
  // Creating a popup handler here to make sure it does not interfere with the
  // existing windows.
  scoped_ptr<Browser> browser(
      chrome::CreateBrowserWithTestWindowForProfile(profile.get()));

  // Creating a popup handler here to make sure it does not interfere with the
  // existing windows.
  scoped_ptr<BrowserWindow> browser_window(
      new TestBrowserWindowAura(window.get()));
  Browser::CreateParams window_params(profile.get());
  window_params.window = browser_window.get();
  scoped_ptr<Browser> window_owning_browser(new Browser(window_params));

  // Creating a popup to make sure it does not interfere with the positioning.
  scoped_ptr<BrowserWindow> browser_popup(
      new TestBrowserWindowAura(popup.get()));
  Browser::CreateParams popup_params(Browser::TYPE_POPUP, profile.get());
  popup_params.window = browser_popup.get();
  scoped_ptr<Browser> popup_owning_browser(new Browser(popup_params));

  // Creating a panel to make sure it does not interfere with the positioning.
  scoped_ptr<BrowserWindow> browser_panel(
      new TestBrowserWindowAura(panel.get()));
  Browser::CreateParams panel_params(Browser::TYPE_POPUP, profile.get());
  panel_params.window = browser_panel.get();
  scoped_ptr<Browser> panel_owning_browser(new Browser(panel_params));

  window->Show();
  { // With a shown window it's size should get returned.
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(50, 100, 300, 150), bottom_nonprimary,
                    PERSISTED, &window_bounds, browser.get(), gfx::Rect());
    EXPECT_EQ(gfx::Rect(16, 32, 640, 320), window_bounds);
  }

  { // Make sure that popups do not get changed.
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(50, 100, 300, 150), bottom_nonprimary,
                    PERSISTED, &window_bounds, popup_owning_browser.get(),
                    gfx::Rect());
    EXPECT_EQ(gfx::Rect(50, 100, 300, 150), window_bounds);
  }

  window->SetBounds(gfx::Rect(816, 720, 640, 320));
  // Verifies newly created windows appear on screen.
  {
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(50, 100, 300, 150), bottom_nonprimary,
                    PERSISTED, &window_bounds, browser.get(), gfx::Rect());
    EXPECT_EQ("384,448 640x320", window_bounds.ToString());
  }

  window->Hide();
  { // If a window is there but not shown the default should be returned.
    // The existing popup should not have any impact as well.
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(50, 100, 300, 150), bottom_nonprimary,
                    PERSISTED, &window_bounds, browser.get(), gfx::Rect());
    EXPECT_EQ(gfx::Rect(WindowSizer::kDesktopBorderSize,
                        WindowSizer::kDesktopBorderSize,
                        1024 - 2 * WindowSizer::kDesktopBorderSize,
                        768 - WindowSizer::kDesktopBorderSize),
                        window_bounds);
  }
}

// Test that the window is sized appropriately for the first run experience
// where the default window bounds calculation is invoked.
TEST_F(WindowSizerTest, AdjustFitSize) {
  { // Check that the window gets resized to the screen.
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(), gfx::Rect(),
                    gfx::Rect(), DEFAULT, &window_bounds, NULL,
                    gfx::Rect(-10, -10, 1024 + 20, 768 + 20));
    EXPECT_EQ(gfx::Rect(0, 0, 1024, 768), window_bounds);
  }

  { // Check that a window which hangs out of the screen get moved back in.
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(), gfx::Rect(),
                    gfx::Rect(), DEFAULT, &window_bounds, NULL,
                    gfx::Rect(1020, 700, 100, 100));
    EXPECT_EQ(gfx::Rect(924, 668, 100, 100), window_bounds);
  }
}
