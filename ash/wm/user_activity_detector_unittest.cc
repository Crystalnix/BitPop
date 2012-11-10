// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/user_activity_detector.h"

#include "ash/shell.h"
#include "ash/test/ash_test_base.h"
#include "ash/wm/user_activity_observer.h"
#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "base/time.h"
#include "ui/aura/event.h"
#include "ui/aura/test/test_windows.h"
#include "ui/aura/window.h"
#include "ui/base/events.h"
#include "ui/base/keycodes/keyboard_codes.h"
#include "ui/gfx/point.h"

namespace ash {
namespace test {

// Implementation that just counts the number of times we've been told that the
// user is active.
class TestUserActivityObserver : public UserActivityObserver {
 public:
  TestUserActivityObserver() : num_invocations_(0) {}

  int num_invocations() const { return num_invocations_; }
  void reset_stats() { num_invocations_ = 0; }

  // UserActivityObserver implementation.
  virtual void OnUserActivity() OVERRIDE { num_invocations_++; }

 private:
  // Number of times that OnUserActivity() has been called.
  int num_invocations_;

  DISALLOW_COPY_AND_ASSIGN(TestUserActivityObserver);
};

class UserActivityDetectorTest : public AshTestBase {
 public:
  UserActivityDetectorTest() {}
  virtual ~UserActivityDetectorTest() {}

  virtual void SetUp() OVERRIDE {
    AshTestBase::SetUp();
    observer_.reset(new TestUserActivityObserver);
    detector_ = Shell::GetInstance()->user_activity_detector();
    detector_->AddObserver(observer_.get());

    now_ = base::TimeTicks::Now();
    detector_->set_now_for_test(now_);
  }

  virtual void TearDown() OVERRIDE {
    detector_->RemoveObserver(observer_.get());
    AshTestBase::TearDown();
  }

 protected:
  // Move |detector_|'s idea of the current time forward by |delta|.
  void AdvanceTime(base::TimeDelta delta) {
    now_ += delta;
    detector_->set_now_for_test(now_);
  }

  UserActivityDetector* detector_;  // not owned

  scoped_ptr<TestUserActivityObserver> observer_;

  base::TimeTicks now_;

 private:
  DISALLOW_COPY_AND_ASSIGN(UserActivityDetectorTest);
};

// Checks that the observer is notified in response to different types of input
// events.
TEST_F(UserActivityDetectorTest, Basic) {
  scoped_ptr<aura::Window> window(
      aura::test::CreateTestWindowWithId(12345, NULL));

  aura::KeyEvent key_event(ui::ET_KEY_PRESSED, ui::VKEY_A, ui::EF_NONE);
  EXPECT_FALSE(detector_->PreHandleKeyEvent(window.get(), &key_event));
  EXPECT_EQ(1, observer_->num_invocations());
  observer_->reset_stats();

  base::TimeDelta advance_delta =
      base::TimeDelta::FromSeconds(UserActivityDetector::kNotifyIntervalMs);
  AdvanceTime(advance_delta);
  aura::MouseEvent mouse_event(
      ui::ET_MOUSE_MOVED, gfx::Point(), gfx::Point(), ui::EF_NONE);
  EXPECT_FALSE(detector_->PreHandleMouseEvent(window.get(), &mouse_event));
  EXPECT_EQ(1, observer_->num_invocations());
  observer_->reset_stats();

  AdvanceTime(advance_delta);
  aura::TouchEvent touch_event(
      ui::ET_TOUCH_PRESSED, gfx::Point(), 0, base::TimeDelta());
  EXPECT_FALSE(detector_->PreHandleTouchEvent(window.get(), &touch_event));
  EXPECT_EQ(1, observer_->num_invocations());
  observer_->reset_stats();

  AdvanceTime(advance_delta);
  aura::GestureEvent gesture_event(
      ui::ET_GESTURE_TAP, 0, 0, ui::EF_NONE, base::Time(),
      ui::GestureEventDetails(ui::ET_GESTURE_TAP, 0, 0), 0U);
  EXPECT_FALSE(detector_->PreHandleGestureEvent(window.get(), &gesture_event));
  EXPECT_EQ(1, observer_->num_invocations());
  observer_->reset_stats();
}

// Checks that observers aren't notified too frequently.
TEST_F(UserActivityDetectorTest, RateLimitNotifications) {
  scoped_ptr<aura::Window> window(
      aura::test::CreateTestWindowWithId(12345, NULL));

  // The observer should be notified about a key event.
  aura::KeyEvent event(ui::ET_KEY_PRESSED, ui::VKEY_A, ui::EF_NONE);
  EXPECT_FALSE(detector_->PreHandleKeyEvent(window.get(), &event));
  EXPECT_EQ(1, observer_->num_invocations());
  observer_->reset_stats();

  // It shouldn't be notified if a second event occurs
  // in the same instant in time.
  EXPECT_FALSE(detector_->PreHandleKeyEvent(window.get(), &event));
  EXPECT_EQ(0, observer_->num_invocations());
  observer_->reset_stats();

  // Advance the time, but not quite enough for another notification to be sent.
  AdvanceTime(
      base::TimeDelta::FromMilliseconds(
          UserActivityDetector::kNotifyIntervalMs - 100));
  EXPECT_FALSE(detector_->PreHandleKeyEvent(window.get(), &event));
  EXPECT_EQ(0, observer_->num_invocations());
  observer_->reset_stats();

  // Advance time by the notification interval, definitely moving out of the
  // rate limit. This should let us trigger another notification.
  AdvanceTime(base::TimeDelta::FromMilliseconds(
      UserActivityDetector::kNotifyIntervalMs));

  EXPECT_FALSE(detector_->PreHandleKeyEvent(window.get(), &event));
  EXPECT_EQ(1, observer_->num_invocations());
}

// Checks that the detector ignores synthetic mouse events.
TEST_F(UserActivityDetectorTest, IgnoreSyntheticMouseEvents) {
  scoped_ptr<aura::Window> window(
      aura::test::CreateTestWindowWithId(12345, NULL));
  aura::MouseEvent mouse_event(
      ui::ET_MOUSE_MOVED, gfx::Point(), gfx::Point(), ui::EF_IS_SYNTHESIZED);
  EXPECT_FALSE(detector_->PreHandleMouseEvent(window.get(), &mouse_event));
  EXPECT_EQ(0, observer_->num_invocations());
}

}  // namespace test
}  // namespace ash
