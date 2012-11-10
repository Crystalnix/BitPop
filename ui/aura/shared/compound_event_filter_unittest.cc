// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/aura/shared/compound_event_filter.h"

#include "ui/aura/client/activation_client.h"
#include "ui/aura/client/cursor_client.h"
#include "ui/aura/env.h"
#include "ui/aura/root_window.h"
#include "ui/aura/shared/root_window_capture_client.h"
#include "ui/aura/test/aura_test_base.h"
#include "ui/aura/test/event_generator.h"
#include "ui/aura/test/test_activation_client.h"
#include "ui/aura/test/test_windows.h"

namespace {

base::TimeDelta GetTime() {
  return base::Time::NowFromSystemTime() - base::Time();
}

class TestVisibleClient : public aura::client::CursorClient {
 public:
  TestVisibleClient() : visible_(false) {}
  virtual ~TestVisibleClient() {}

  virtual void SetCursor(gfx::NativeCursor cursor) OVERRIDE {
  }

  virtual void ShowCursor(bool show) OVERRIDE {
    visible_ = show;
  }

  virtual bool IsCursorVisible() const OVERRIDE {
    return visible_;
  }

 private:
  bool visible_;
};

}

namespace aura {

namespace {

// An event filter that consumes all gesture events.
class ConsumeGestureEventFilter : public EventFilter {
 public:
  ConsumeGestureEventFilter() {}
  virtual ~ConsumeGestureEventFilter() {}

 private:
  // Overridden from EventFilter.
  virtual bool PreHandleKeyEvent(Window* target, KeyEvent* event) OVERRIDE {
    return false;
  }

  virtual bool PreHandleMouseEvent(Window* target,
                                   MouseEvent* event) OVERRIDE {
    return false;
  }

  virtual ui::TouchStatus PreHandleTouchEvent(
      Window* target,
      TouchEvent* event) OVERRIDE {
    return ui::TOUCH_STATUS_UNKNOWN;
  }

  virtual ui::GestureStatus PreHandleGestureEvent(
      Window* target,
      GestureEvent* event) OVERRIDE {
    return ui::GESTURE_STATUS_CONSUMED;
  }

  DISALLOW_COPY_AND_ASSIGN(ConsumeGestureEventFilter);
};

}  // namespace

namespace test {

typedef AuraTestBase CompoundEventFilterTest;

TEST_F(CompoundEventFilterTest, TouchHidesCursor) {
  aura::Env::GetInstance()->SetEventFilter(new shared::CompoundEventFilter());
  aura::client::SetActivationClient(root_window(),
                                    new TestActivationClient(root_window()));
  aura::client::SetCaptureClient(
      root_window(), new shared::RootWindowCaptureClient(root_window()));
  TestWindowDelegate delegate;
  scoped_ptr<Window> window(CreateTestWindowWithDelegate(&delegate, 1234,
      gfx::Rect(5, 5, 100, 100), NULL));
  window->Show();
  window->SetCapture();

  TestVisibleClient cursor_client;
  aura::client::SetCursorClient(root_window(), &cursor_client);

  MouseEvent mouse(ui::ET_MOUSE_MOVED, gfx::Point(10, 10),
      gfx::Point(10, 10), 0);
  root_window()->AsRootWindowHostDelegate()->OnHostMouseEvent(&mouse);
  EXPECT_TRUE(cursor_client.IsCursorVisible());

  // This press is required for the GestureRecognizer to associate a target
  // with kTouchId
  TouchEvent press(ui::ET_TOUCH_PRESSED, gfx::Point(90, 90), 1, GetTime());
  root_window()->AsRootWindowHostDelegate()->OnHostTouchEvent(&press);
  EXPECT_FALSE(cursor_client.IsCursorVisible());

  TouchEvent move(ui::ET_TOUCH_MOVED, gfx::Point(10, 10), 1,
                  GetTime());
  root_window()->AsRootWindowHostDelegate()->OnHostTouchEvent(&move);
  EXPECT_FALSE(cursor_client.IsCursorVisible());

  TouchEvent release(ui::ET_TOUCH_RELEASED, gfx::Point(10, 10), 1, GetTime());
  root_window()->AsRootWindowHostDelegate()->OnHostTouchEvent(&release);
  EXPECT_FALSE(cursor_client.IsCursorVisible());

  // Move the cursor again. The cursor should be visible.
  root_window()->AsRootWindowHostDelegate()->OnHostMouseEvent(&mouse);
  EXPECT_TRUE(cursor_client.IsCursorVisible());

  // Now activate the window and press on it again.
  aura::client::GetActivationClient(
      root_window())->ActivateWindow(window.get());
  root_window()->AsRootWindowHostDelegate()->OnHostTouchEvent(&press);
  EXPECT_FALSE(cursor_client.IsCursorVisible());
}

// Tests that tapping a window gives the window focus.
TEST_F(CompoundEventFilterTest, GestureFocusesWindow) {
  aura::Env::GetInstance()->SetEventFilter(new shared::CompoundEventFilter());
  aura::client::SetActivationClient(root_window(),
                                    new TestActivationClient(root_window()));
  aura::client::SetCaptureClient(
      root_window(), new shared::RootWindowCaptureClient(root_window()));
  TestWindowDelegate delegate;
  scoped_ptr<Window> window(CreateTestWindowWithDelegate(&delegate, 1234,
      gfx::Rect(5, 5, 100, 100), NULL));
  window->Show();

  EXPECT_TRUE(window->CanFocus());
  EXPECT_FALSE(window->HasFocus());

  // Tap on the window should give it focus.
  EventGenerator generator(root_window(), gfx::Point(50, 50));
  generator.PressTouch();
  EXPECT_TRUE(window->HasFocus());
}

// Tests that if an event filter consumes a gesture, then it doesn't focus the
// window.
TEST_F(CompoundEventFilterTest, FilterConsumedGesture) {
  shared::CompoundEventFilter* compound_filter =
      new shared::CompoundEventFilter();
  scoped_ptr<EventFilter> gesture_filter(new ConsumeGestureEventFilter());
  compound_filter->AddFilter(gesture_filter.get());
  aura::Env::GetInstance()->SetEventFilter(compound_filter);
  aura::client::SetActivationClient(root_window(),
                                    new TestActivationClient(root_window()));
  aura::client::SetCaptureClient(
      root_window(), new shared::RootWindowCaptureClient(root_window()));
  TestWindowDelegate delegate;
  scoped_ptr<Window> window(CreateTestWindowWithDelegate(&delegate, 1234,
      gfx::Rect(5, 5, 100, 100), NULL));
  window->Show();

  EXPECT_TRUE(window->CanFocus());
  EXPECT_FALSE(window->HasFocus());

  // Tap on the window should not focus it since the filter will be consuming
  // the gestures.
  EventGenerator generator(root_window(), gfx::Point(50, 50));
  generator.PressTouch();
  EXPECT_FALSE(window->HasFocus());

  compound_filter->RemoveFilter(gesture_filter.get());
}

}  // namespace test
}  // namespace aura
