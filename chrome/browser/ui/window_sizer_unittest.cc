// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/window_sizer.h"

#include <vector>

#include "base/compiler_specific.h"
#include "base/logging.h"
#include "testing/gtest/include/gtest/gtest.h"

// Some standard monitor sizes (no task bar).
static const gfx::Rect tentwentyfour(0, 0, 1024, 768);
static const gfx::Rect twelveeighty(0, 0, 1280, 1024);
static const gfx::Rect sixteenhundred(0, 0, 1600, 1200);
static const gfx::Rect sixteeneighty(0, 0, 1680, 1050);
static const gfx::Rect nineteentwenty(0, 0, 1920, 1200);

// Represents a 1024x768 monitor that is not the primary monitor, arranged to
// the immediate left of the primary 1024x768 monitor.
static const gfx::Rect left_nonprimary(-1024, 0, 1024, 768);

// Represents a 1024x768 monitor that is not the primary monitor, arranged to
// the immediate right of the primary 1024x768 monitor.
static const gfx::Rect right_nonprimary(1024, 0, 1024, 768);

// Represents a 1024x768 monitor that is not the primary monitor, arranged to
// the immediate top of the primary 1024x768 monitor.
static const gfx::Rect top_nonprimary(0, -768, 1024, 768);

// Represents a 1024x768 monitor that is not the primary monitor, arranged to
// the immediate bottom of the primary 1024x768 monitor.
static const gfx::Rect bottom_nonprimary(0, 768, 1024, 768);

// The work area for 1024x768 monitors with different taskbar orientations.
static const gfx::Rect taskbar_bottom_work_area(0, 0, 1024, 734);
static const gfx::Rect taskbar_top_work_area(0, 34, 1024, 734);
static const gfx::Rect taskbar_left_work_area(107, 0, 917, 768);
static const gfx::Rect taskbar_right_work_area(0, 0, 917, 768);

static int kWindowTilePixels = WindowSizer::kWindowTilePixels;

// Testing implementation of WindowSizer::MonitorInfoProvider that we can use
// to fake various monitor layouts and sizes.
class TestMonitorInfoProvider : public MonitorInfoProvider {
 public:
  TestMonitorInfoProvider() {}
  virtual ~TestMonitorInfoProvider() {}

  void AddMonitor(const gfx::Rect& bounds, const gfx::Rect& work_area) {
    DCHECK(bounds.Contains(work_area));
    monitor_bounds_.push_back(bounds);
    work_areas_.push_back(work_area);
  }

  // Overridden from WindowSizer::MonitorInfoProvider:
  virtual gfx::Rect GetPrimaryMonitorWorkArea() const OVERRIDE {
    return work_areas_[0];
  }

  virtual gfx::Rect GetPrimaryMonitorBounds() const OVERRIDE {
    return monitor_bounds_[0];
  }

  virtual gfx::Rect GetMonitorWorkAreaMatching(
      const gfx::Rect& match_rect) const OVERRIDE {
    return work_areas_[GetMonitorIndexMatchingBounds(match_rect)];
  }

 private:
  size_t GetMonitorIndexMatchingBounds(const gfx::Rect& match_rect) const {
    int max_area = 0;
    size_t max_area_index = 0;
    // Loop through all the monitors, finding the one that intersects the
    // largest area of the supplied match rect.
    for (size_t i = 0; i < work_areas_.size(); ++i) {
      gfx::Rect overlap(match_rect.Intersect(work_areas_[i]));
      int area = overlap.width() * overlap.height();
      if (area > max_area) {
        max_area = area;
        max_area_index = i;
      }
    }
    return max_area_index;
  }

  std::vector<gfx::Rect> monitor_bounds_;
  std::vector<gfx::Rect> work_areas_;

  DISALLOW_COPY_AND_ASSIGN(TestMonitorInfoProvider);
};

// Testing implementation of WindowSizer::StateProvider that we use to fake
// persistent storage and existing windows.
class TestStateProvider : public WindowSizer::StateProvider {
 public:
  TestStateProvider()
    : has_persistent_data_(false),
      has_last_active_data_(false) {
  }
  virtual ~TestStateProvider() {}

  void SetPersistentState(const gfx::Rect& bounds,
                          const gfx::Rect& work_area,
                          bool has_persistent_data) {
    persistent_bounds_ = bounds;
    persistent_work_area_ = work_area;
    has_persistent_data_ = has_persistent_data;
  }

  void SetLastActiveState(const gfx::Rect& bounds, bool has_last_active_data) {
    last_active_bounds_ = bounds;
    has_last_active_data_ = has_last_active_data;
  }

  // Overridden from WindowSizer::StateProvider:
  virtual bool GetPersistentState(gfx::Rect* bounds,
                                  gfx::Rect* saved_work_area) const {
    *bounds = persistent_bounds_;
    *saved_work_area = persistent_work_area_;
    return has_persistent_data_;
  }

  virtual bool GetLastActiveWindowState(gfx::Rect* bounds) const {
    *bounds = last_active_bounds_;
    return has_last_active_data_;
  }

 private:
  gfx::Rect persistent_bounds_;
  gfx::Rect persistent_work_area_;
  bool has_persistent_data_;

  gfx::Rect last_active_bounds_;
  bool has_last_active_data_;

  DISALLOW_COPY_AND_ASSIGN(TestStateProvider);
};

// A convenience function to read the window bounds from the window sizer
// according to the specified configuration.
enum Source { DEFAULT, LAST_ACTIVE, PERSISTED };
static void GetWindowBounds(const gfx::Rect& monitor1_bounds,
                            const gfx::Rect& monitor1_work_area,
                            const gfx::Rect& monitor2_bounds,
                            const gfx::Rect& state,
                            const gfx::Rect& work_area,
                            Source source,
                            gfx::Rect* out_bounds) {
  TestMonitorInfoProvider* mip = new TestMonitorInfoProvider;
  mip->AddMonitor(monitor1_bounds, monitor1_work_area);
  if (!monitor2_bounds.IsEmpty())
    mip->AddMonitor(monitor2_bounds, monitor2_bounds);
  TestStateProvider* sp = new TestStateProvider;
  if (source == PERSISTED)
    sp->SetPersistentState(state, work_area, true);
  else if (source == LAST_ACTIVE)
    sp->SetLastActiveState(state, true);
  WindowSizer sizer(sp, mip);
  sizer.DetermineWindowBounds(gfx::Rect(), out_bounds);
}

// Test that the window is sized appropriately for the first run experience
// where the default window bounds calculation is invoked.
TEST(WindowSizerTest, DefaultSizeCase) {
  { // 4:3 monitor case, 1024x768, no taskbar
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(), gfx::Rect(),
                    gfx::Rect(), DEFAULT, &window_bounds);
    EXPECT_EQ(gfx::Rect(kWindowTilePixels, kWindowTilePixels,
                        1024 - kWindowTilePixels * 2,
                        768 - kWindowTilePixels * 2),
              window_bounds);
  }

  { // 4:3 monitor case, 1024x768, taskbar on bottom
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, taskbar_bottom_work_area, gfx::Rect(),
                    gfx::Rect(), gfx::Rect(), DEFAULT, &window_bounds);
    EXPECT_EQ(gfx::Rect(kWindowTilePixels, kWindowTilePixels,
                        1024 - kWindowTilePixels * 2,
                        (taskbar_bottom_work_area.height() -
                         kWindowTilePixels * 2)),
              window_bounds);
  }

  { // 4:3 monitor case, 1024x768, taskbar on right
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, taskbar_right_work_area, gfx::Rect(),
                    gfx::Rect(), gfx::Rect(), DEFAULT, &window_bounds);
    EXPECT_EQ(gfx::Rect(kWindowTilePixels, kWindowTilePixels,
                        taskbar_right_work_area.width() - kWindowTilePixels*2,
                        768 - kWindowTilePixels * 2),
              window_bounds);
  }

  { // 4:3 monitor case, 1024x768, taskbar on left
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, taskbar_left_work_area, gfx::Rect(),
                    gfx::Rect(), gfx::Rect(), DEFAULT, &window_bounds);
    EXPECT_EQ(gfx::Rect(taskbar_left_work_area.x() + kWindowTilePixels,
                        kWindowTilePixels,
                        taskbar_left_work_area.width() - kWindowTilePixels * 2,
                        (taskbar_left_work_area.height() -
                         kWindowTilePixels * 2)),
              window_bounds);
  }

  { // 4:3 monitor case, 1024x768, taskbar on top
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, taskbar_top_work_area, gfx::Rect(),
                    gfx::Rect(), gfx::Rect(), DEFAULT, &window_bounds);
    EXPECT_EQ(gfx::Rect(kWindowTilePixels,
                        taskbar_top_work_area.y() + kWindowTilePixels,
                        1024 - kWindowTilePixels * 2,
                        taskbar_top_work_area.height() - kWindowTilePixels * 2),
              window_bounds);
  }

  { // 4:3 monitor case, 1280x1024
    gfx::Rect window_bounds;
    GetWindowBounds(twelveeighty, twelveeighty, gfx::Rect(), gfx::Rect(),
                    gfx::Rect(), DEFAULT, &window_bounds);
    EXPECT_EQ(gfx::Rect(kWindowTilePixels, kWindowTilePixels,
                        1050,
                        1024 - kWindowTilePixels * 2),
              window_bounds);
  }

  { // 4:3 monitor case, 1600x1200
    gfx::Rect window_bounds;
    GetWindowBounds(sixteenhundred, sixteenhundred, gfx::Rect(), gfx::Rect(),
                    gfx::Rect(), DEFAULT, &window_bounds);
    EXPECT_EQ(gfx::Rect(kWindowTilePixels, kWindowTilePixels,
                        1050,
                        1200 - kWindowTilePixels * 2),
              window_bounds);
  }

  { // 16:10 monitor case, 1680x1050
    gfx::Rect window_bounds;
    GetWindowBounds(sixteeneighty, sixteeneighty, gfx::Rect(), gfx::Rect(),
                    gfx::Rect(), DEFAULT, &window_bounds);
    EXPECT_EQ(gfx::Rect(kWindowTilePixels, kWindowTilePixels,
                        840 - static_cast<int>(kWindowTilePixels * 1.5),
                        1050 - kWindowTilePixels * 2),
              window_bounds);
  }

  { // 16:10 monitor case, 1920x1200
    gfx::Rect window_bounds;
    GetWindowBounds(nineteentwenty, nineteentwenty, gfx::Rect(), gfx::Rect(),
                    gfx::Rect(), DEFAULT, &window_bounds);
    EXPECT_EQ(gfx::Rect(kWindowTilePixels, kWindowTilePixels,
                        960 - static_cast<int>(kWindowTilePixels * 1.5),
                        1200 - kWindowTilePixels * 2),
              window_bounds);
  }
}

// Test that the next opened window is positioned appropriately given the
// bounds of an existing window of the same type.
TEST(WindowSizerTest, LastWindowBoundsCase) {
  { // normal, in the middle of the screen somewhere.
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(kWindowTilePixels, kWindowTilePixels, 500, 400),
                    gfx::Rect(), LAST_ACTIVE, &window_bounds);
    EXPECT_EQ(gfx::Rect(kWindowTilePixels * 2,
                        kWindowTilePixels * 2, 500, 400), window_bounds);
  }

  { // taskbar on top.
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, taskbar_top_work_area, gfx::Rect(),
                    gfx::Rect(kWindowTilePixels, kWindowTilePixels, 500, 400),
                    gfx::Rect(), LAST_ACTIVE, &window_bounds);
    EXPECT_EQ(gfx::Rect(kWindowTilePixels * 2,
                        std::max(kWindowTilePixels * 2,
                                 34 /* toolbar height */),
                        500, 400), window_bounds);
  }

  { // Too small to satisify the minimum visibility condition.
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(kWindowTilePixels, kWindowTilePixels, 29, 29),
                    gfx::Rect(), LAST_ACTIVE, &window_bounds);
    EXPECT_EQ(gfx::Rect(kWindowTilePixels * 2,
                        kWindowTilePixels * 2,
                        30 /* not 29 */,
                        30 /* not 29 */),
              window_bounds);
  }


  { // Normal.
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(kWindowTilePixels, kWindowTilePixels, 500, 400),
                    gfx::Rect(), LAST_ACTIVE, &window_bounds);
    EXPECT_EQ(gfx::Rect(kWindowTilePixels * 2,
                        kWindowTilePixels * 2, 500, 400), window_bounds);
  }
}

// Test that the window opened is sized appropriately given persisted sizes.
TEST(WindowSizerTest, PersistedBoundsCase) {
  { // normal, in the middle of the screen somewhere.
    gfx::Rect initial_bounds(kWindowTilePixels, kWindowTilePixels, 500, 400);

    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(), initial_bounds,
                    gfx::Rect(), PERSISTED, &window_bounds);
    EXPECT_EQ(initial_bounds, window_bounds);
  }

  { // Normal.
    gfx::Rect initial_bounds(0, 0, 1024, 768);

    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(), initial_bounds,
                    gfx::Rect(), PERSISTED, &window_bounds);
    EXPECT_EQ(initial_bounds, window_bounds);
  }

  { // normal, on non-primary monitor in negative coords.
    gfx::Rect initial_bounds(-600, 10, 500, 400);

    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, left_nonprimary,
                    initial_bounds, gfx::Rect(), PERSISTED, &window_bounds);
    EXPECT_EQ(initial_bounds, window_bounds);
  }

  { // normal, on non-primary monitor in negative coords.
    gfx::Rect initial_bounds(-1024, 0, 1024, 768);

    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, left_nonprimary,
                    initial_bounds, gfx::Rect(), PERSISTED, &window_bounds);
    EXPECT_EQ(initial_bounds, window_bounds);
  }

  { // Non-primary monitor resoultion has changed, but the monitor still
    // completely contains the window.

    gfx::Rect initial_bounds(1074, 50, 600, 500);

    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(1024, 0, 800, 600),
                    initial_bounds, right_nonprimary, PERSISTED,
                    &window_bounds);
    EXPECT_EQ(initial_bounds, window_bounds);
  }

  { // Non-primary monitor resoultion has changed, and the window is partially
    // off-screen.

    gfx::Rect initial_bounds(1274, 50, 600, 500);

    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(1024, 0, 800, 600),
                    initial_bounds, right_nonprimary, PERSISTED,
                    &window_bounds);
    EXPECT_EQ(gfx::Rect(1224, 50, 600, 500), window_bounds);
  }

  { // Non-primary monitor resoultion has changed, and the window is now too
    // large for the monitor.

    gfx::Rect initial_bounds(1274, 50, 900, 700);

    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(1024, 0, 800, 600),
                    initial_bounds, right_nonprimary, PERSISTED,
                    &window_bounds);
    EXPECT_EQ(gfx::Rect(1024, 0, 800, 600), window_bounds);
  }

  { // width and height too small
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(kWindowTilePixels, kWindowTilePixels, 29, 29),
                    gfx::Rect(), PERSISTED, &window_bounds);
    EXPECT_EQ(gfx::Rect(kWindowTilePixels, kWindowTilePixels,
                        30 /* not 29 */, 30 /* not 29 */),
              window_bounds);
  }

#if defined(OS_MACOSX)
  { // Saved state is too tall to possibly be resized.  Mac resizers
    // are at the bottom of the window, and no piece of a window can
    // be moved higher than the menubar.  (Perhaps the user changed
    // resolution to something smaller before relaunching Chrome?)
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(kWindowTilePixels, kWindowTilePixels, 30, 5000),
                    gfx::Rect(), PERSISTED, &window_bounds);
    EXPECT_EQ(tentwentyfour.height(), window_bounds.height());
  }
#endif  // defined(OS_MACOSX)
}

//////////////////////////////////////////////////////////////////////////////
// The following unittests have different results on Mac/non-Mac because we
// reposition windows aggressively on Mac.  The *WithAggressiveReposition tests
// are run on Mac, and the *WithNonAggressiveRepositioning tests are run on
// other platforms.

#if defined(OS_MACOSX)
TEST(WindowSizerTest, LastWindowOffscreenWithAggressiveRepositioning) {
  { // taskbar on left.  The new window overlaps slightly with the taskbar, so
    // it is moved to be flush with the left edge of the work area.
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, taskbar_left_work_area, gfx::Rect(),
                    gfx::Rect(kWindowTilePixels, kWindowTilePixels, 500, 400),
                    gfx::Rect(), LAST_ACTIVE, &window_bounds);
    EXPECT_EQ(gfx::Rect(taskbar_left_work_area.x(),
                        kWindowTilePixels * 2, 500, 400), window_bounds);
  }

  { // offset would put the new window offscreen at the bottom
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(10, 729, 500, 400), gfx::Rect(), LAST_ACTIVE,
                    &window_bounds);
    EXPECT_EQ(gfx::Rect(10 + kWindowTilePixels,
                        0 /* not 729 + kWindowTilePixels */,
                        500, 400),
              window_bounds);
  }

  { // offset would put the new window offscreen at the right
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(985, 10, 500, 400), gfx::Rect(), LAST_ACTIVE,
                    &window_bounds);
    EXPECT_EQ(gfx::Rect(0 /* not 985 + kWindowTilePixels*/,
                        10 + kWindowTilePixels,
                        500, 400),
              window_bounds);
  }

  { // offset would put the new window offscreen at the bottom right
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(985, 729, 500, 400), gfx::Rect(), LAST_ACTIVE,
                    &window_bounds);
    EXPECT_EQ(gfx::Rect(0 /* not 985 + kWindowTilePixels*/,
                        0 /* not 729 + kWindowTilePixels*/,
                        500, 400),
              window_bounds);
  }
}

TEST(WindowSizerTest, PersistedWindowOffscreenWithAggressiveRepositioning) {
  { // off the left
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(-471, 50, 500, 400), gfx::Rect(), PERSISTED,
                    &window_bounds);
    EXPECT_EQ(gfx::Rect(0 /* not -471 */, 50, 500, 400), window_bounds);
  }

  { // off the top
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(50, -370, 500, 400), gfx::Rect(), PERSISTED,
                    &window_bounds);
    EXPECT_EQ(gfx::Rect(50, 0, 500, 400), window_bounds);
  }

  { // off the right
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(995, 50, 500, 400), gfx::Rect(), PERSISTED,
                    &window_bounds);
    EXPECT_EQ(gfx::Rect(0 /* not 995 */, 50, 500, 400), window_bounds);
  }

  { // off the bottom
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(50, 739, 500, 400), gfx::Rect(), PERSISTED,
                    &window_bounds);
    EXPECT_EQ(gfx::Rect(50, 0 /* not 739 */, 500, 400), window_bounds);
  }

  { // off the topleft
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(-471, -371, 500, 400), gfx::Rect(), PERSISTED,
                    &window_bounds);
    EXPECT_EQ(gfx::Rect(0 /* not -471 */, 0 /* not -371 */, 500, 400),
              window_bounds);
  }

  { // off the topright
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(995, -371, 500, 400), gfx::Rect(), PERSISTED,
                    &window_bounds);
    EXPECT_EQ(gfx::Rect(0 /* not 995 */, 0 /* not -371 */, 500, 400),
                        window_bounds);
  }

  { // off the bottomleft
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(-471, 739, 500, 400), gfx::Rect(), PERSISTED,
                    &window_bounds);
    EXPECT_EQ(gfx::Rect(0 /* not -471 */, 0 /* not 739 */, 500, 400),
                        window_bounds);
  }

  { // off the bottomright
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(995, 739, 500, 400), gfx::Rect(), PERSISTED,
                    &window_bounds);
    EXPECT_EQ(gfx::Rect(0 /* not 995 */, 0 /* not 739 */, 500, 400),
                        window_bounds);
  }

  { // entirely off left
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(-700, 50, 500, 400), gfx::Rect(), PERSISTED,
                    &window_bounds);
    EXPECT_EQ(gfx::Rect(0 /* not -700 */, 50, 500, 400), window_bounds);
  }

  { // entirely off left (monitor was detached since last run)
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(-700, 50, 500, 400), left_nonprimary,
                    PERSISTED, &window_bounds);
    EXPECT_EQ(gfx::Rect(0, 50, 500, 400), window_bounds);
  }

  { // entirely off top
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(50, -500, 500, 400), gfx::Rect(), PERSISTED,
                    &window_bounds);
    EXPECT_EQ(gfx::Rect(50, 0, 500, 400), window_bounds);
  }

  { // entirely off top (monitor was detached since last run)
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(50, -500, 500, 400), top_nonprimary, PERSISTED,
                    &window_bounds);
    EXPECT_EQ(gfx::Rect(50, 0, 500, 400), window_bounds);
  }

  { // entirely off right
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(1200, 50, 500, 400), gfx::Rect(), PERSISTED,
                    &window_bounds);
    EXPECT_EQ(gfx::Rect(0 /* not 1200 */, 50, 500, 400), window_bounds);
  }

  { // entirely off right (monitor was detached since last run)
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(1200, 50, 500, 400), right_nonprimary, PERSISTED,
                    &window_bounds);
    EXPECT_EQ(gfx::Rect(524 /* not 1200 */, 50, 500, 400), window_bounds);
  }

  { // entirely off bottom
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(50, 800, 500, 400), gfx::Rect(), PERSISTED,
                    &window_bounds);
    EXPECT_EQ(gfx::Rect(50, 0 /* not 800 */, 500, 400), window_bounds);
  }

  { // entirely off bottom (monitor was detached since last run)
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(50, 800, 500, 400), bottom_nonprimary,
                    PERSISTED, &window_bounds);
    EXPECT_EQ(gfx::Rect(50, 368 /* not 800 */, 500, 400), window_bounds);
  }

  { // wider than the screen. off both the left and right
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(-100, 50, 2000, 400), gfx::Rect(), PERSISTED,
                    &window_bounds);
    EXPECT_EQ(gfx::Rect(0 /* not -100 */, 50, 2000, 400), window_bounds);
  }
}
#else
TEST(WindowSizerTest, LastWindowOffscreenWithNonAggressiveRepositioning) {
  { // taskbar on left.
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, taskbar_left_work_area, gfx::Rect(),
                    gfx::Rect(kWindowTilePixels, kWindowTilePixels, 500, 400),
                    gfx::Rect(), LAST_ACTIVE, &window_bounds);
    EXPECT_EQ(gfx::Rect(kWindowTilePixels * 2,
                        kWindowTilePixels * 2, 500, 400), window_bounds);
  }

  // Linux does not tile windows, so tile adjustment tests don't make sense.
#if !defined(OS_POSIX) || defined(OS_MACOSX)
  { // offset would put the new window offscreen at the bottom but the minimum
    // visibility condition is barely satisfied without relocation.
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(10, 728, 500, 400), gfx::Rect(), LAST_ACTIVE,
                    &window_bounds);
    EXPECT_EQ(gfx::Rect(10 + kWindowTilePixels, 738,
                        500, 400), window_bounds);
  }

  { // offset would put the new window offscreen at the bottom and the minimum
    // visibility condition is satisified by relocation.
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(10, 729, 500, 400), gfx::Rect(), LAST_ACTIVE,
                    &window_bounds);
    EXPECT_EQ(gfx::Rect(10 + kWindowTilePixels, 738 /* not 739 */, 500, 400),
              window_bounds);
  }

  { // offset would put the new window offscreen at the right but the minimum
    // visibility condition is barely satisfied without relocation.
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(984, 10, 500, 400), gfx::Rect(), LAST_ACTIVE,
                    &window_bounds);
    EXPECT_EQ(gfx::Rect(994, 10 + kWindowTilePixels, 500, 400), window_bounds);
  }

  { // offset would put the new window offscreen at the right and the minimum
    // visibility condition is satisified by relocation.
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(985, 10, 500, 400), gfx::Rect(), LAST_ACTIVE,
                    &window_bounds);
    EXPECT_EQ(gfx::Rect(994 /* not 995 */, 10 + kWindowTilePixels,
                        500, 400), window_bounds);
  }

  { // offset would put the new window offscreen at the bottom right and the
    // minimum visibility condition is satisified by relocation.
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(985, 729, 500, 400), gfx::Rect(), LAST_ACTIVE,
                    &window_bounds);
    EXPECT_EQ(gfx::Rect(994 /* not 995 */, 738 /* not 739 */, 500, 400),
              window_bounds);
  }
#endif  // !defined(OS_POSIX) || defined(OS_MACOSX)
}

TEST(WindowSizerTest, PersistedWindowOffscreenWithNonAggressiveRepositioning) {
  { // off the left but the minimum visibility condition is barely satisfied
    // without relocaiton.
    gfx::Rect initial_bounds(-470, 50, 500, 400);

    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    initial_bounds, gfx::Rect(), PERSISTED,
                    &window_bounds);
    EXPECT_EQ(initial_bounds, window_bounds);
  }

  { // off the left and the minimum visibility condition is satisfied by
    // relocation.
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(-471, 50, 500, 400), gfx::Rect(), PERSISTED,
                    &window_bounds);
    EXPECT_EQ(gfx::Rect(-470 /* not -471 */, 50, 500, 400), window_bounds);
  }

  { // off the top
    gfx::Rect initial_bounds(50, -370, 500, 400);

    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(50, -370, 500, 400), gfx::Rect(), PERSISTED,
                    &window_bounds);
    EXPECT_EQ(gfx::Rect(50, 0, 500, 400), window_bounds);
  }

  { // off the right but the minimum visibility condition is barely satisified
    // without relocation.
    gfx::Rect initial_bounds(994, 50, 500, 400);

    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    initial_bounds, gfx::Rect(), PERSISTED,
                    &window_bounds);
    EXPECT_EQ(initial_bounds, window_bounds);
  }

  { // off the right and the minimum visibility condition is satisified by
    // relocation.
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(995, 50, 500, 400), gfx::Rect(), PERSISTED,
                    &window_bounds);
    EXPECT_EQ(gfx::Rect(994 /* not 995 */, 50, 500, 400), window_bounds);
  }

  { // off the bottom but the minimum visibility condition is barely satisified
    // without relocation.
    gfx::Rect initial_bounds(50, 738, 500, 400);

    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    initial_bounds, gfx::Rect(), PERSISTED,
                    &window_bounds);
    EXPECT_EQ(initial_bounds, window_bounds);
  }

  { // off the bottom and the minimum visibility condition is satisified by
    // relocation.
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(50, 739, 500, 400), gfx::Rect(), PERSISTED,
                    &window_bounds);
    EXPECT_EQ(gfx::Rect(50, 738 /* not 739 */, 500, 400), window_bounds);
  }

  { // off the topleft
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(-471, -371, 500, 400), gfx::Rect(), PERSISTED,
                    &window_bounds);
    EXPECT_EQ(gfx::Rect(-470 /* not -471 */, 0, 500, 400),
              window_bounds);
  }

  { // off the topright and the minimum visibility condition is satisified by
    // relocation.
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(995, -371, 500, 400), gfx::Rect(), PERSISTED,
                    &window_bounds);
    EXPECT_EQ(gfx::Rect(994 /* not 995 */, 0, 500, 400),
                        window_bounds);
  }

  { // off the bottomleft and the minimum visibility condition is satisified by
    // relocation.
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(-471, 739, 500, 400), gfx::Rect(), PERSISTED,
                    &window_bounds);
    EXPECT_EQ(gfx::Rect(-470 /* not -471 */, 738 /* not 739 */, 500, 400),
                        window_bounds);
  }

  { // off the bottomright and the minimum visibility condition is satisified by
    // relocation.
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(995, 739, 500, 400), gfx::Rect(), PERSISTED,
                    &window_bounds);
    EXPECT_EQ(gfx::Rect(994 /* not 995 */, 738 /* not 739 */, 500, 400),
                        window_bounds);
  }

  { // entirely off left
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(-700, 50, 500, 400), gfx::Rect(), PERSISTED,
                    &window_bounds);
    EXPECT_EQ(gfx::Rect(-470 /* not -700 */, 50, 500, 400), window_bounds);
  }

  { // entirely off left (monitor was detached since last run)
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(-700, 50, 500, 400), left_nonprimary, PERSISTED,
                    &window_bounds);
    EXPECT_EQ(gfx::Rect(0, 50, 500, 400), window_bounds);
  }

  { // entirely off top
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(50, -500, 500, 400), gfx::Rect(), PERSISTED,
                    &window_bounds);
    EXPECT_EQ(gfx::Rect(50, 0, 500, 400), window_bounds);
  }

  { // entirely off top (monitor was detached since last run)
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(50, -500, 500, 400), top_nonprimary,
                    PERSISTED, &window_bounds);
    EXPECT_EQ(gfx::Rect(50, 0, 500, 400), window_bounds);
  }

  { // entirely off right
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(1200, 50, 500, 400), gfx::Rect(), PERSISTED,
                    &window_bounds);
    EXPECT_EQ(gfx::Rect(994 /* not 1200 */, 50, 500, 400), window_bounds);
  }

  { // entirely off right (monitor was detached since last run)
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(1200, 50, 500, 400), right_nonprimary,
                    PERSISTED, &window_bounds);
    EXPECT_EQ(gfx::Rect(524, 50, 500, 400), window_bounds);
  }

  { // entirely off bottom
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(50, 800, 500, 400), gfx::Rect(), PERSISTED,
                    &window_bounds);
    EXPECT_EQ(gfx::Rect(50, 738 /* not 800 */, 500, 400), window_bounds);
  }

  { // entirely off bottom (monitor was detached since last run)
    gfx::Rect window_bounds;
    GetWindowBounds(tentwentyfour, tentwentyfour, gfx::Rect(),
                    gfx::Rect(50, 800, 500, 400), bottom_nonprimary,
                    PERSISTED, &window_bounds);
    EXPECT_EQ(gfx::Rect(50, 368, 500, 400), window_bounds);
  }
}
#endif  //defined(OS_MACOSX)
