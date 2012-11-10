// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/aura/test/event_generator.h"

#include "base/memory/scoped_ptr.h"
#include "ui/aura/event.h"
#include "ui/aura/root_window.h"

#if defined(USE_X11)
#include <X11/Xlib.h>
#include "ui/base/x/x11_util.h"
#endif

namespace {

class TestKeyEvent : public aura::KeyEvent {
 public:
  TestKeyEvent(const base::NativeEvent& native_event, int flags)
      : KeyEvent(native_event, false /* is_char */) {
    set_flags(flags);
  }
};

class TestTouchEvent : public aura::TouchEvent {
 public:
  TestTouchEvent(ui::EventType type,
                 const gfx::Point& root_location,
                 int flags)
      : TouchEvent(type, root_location, 0,
                   base::Time::NowFromSystemTime() - base::Time()) {
    set_flags(flags);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(TestTouchEvent);
};

gfx::Point CenterOfWindowInRootWindowCoordinate(aura::RootWindow* root_window,
                                                aura::Window* window) {
  gfx::Point center = window->bounds().CenterPoint();
  aura::Window::ConvertPointToWindow(window->parent(), root_window, &center);
  return center;
}

}  // namespace

namespace aura {
namespace test {

EventGenerator::EventGenerator(RootWindow* root_window)
    : root_window_(root_window),
      flags_(0) {
}

EventGenerator::EventGenerator(RootWindow* root_window, const gfx::Point& point)
    : root_window_(root_window),
      flags_(0),
      current_location_(point) {
}

EventGenerator::EventGenerator(RootWindow* root_window, Window* window)
    : root_window_(root_window),
      flags_(0),
      current_location_(CenterOfWindowInRootWindowCoordinate(root_window,
                                                             window)) {
}

EventGenerator::~EventGenerator() {
}

void EventGenerator::PressLeftButton() {
  if ((flags_ & ui::EF_LEFT_MOUSE_BUTTON) == 0) {
    flags_ |= ui::EF_LEFT_MOUSE_BUTTON;
    MouseEvent mouseev(
        ui::ET_MOUSE_PRESSED, current_location_, current_location_, flags_);
    Dispatch(mouseev);
  }
}

void EventGenerator::ReleaseLeftButton() {
  if (flags_ & ui::EF_LEFT_MOUSE_BUTTON) {
    MouseEvent mouseev(
        ui::ET_MOUSE_RELEASED, current_location_, current_location_, flags_);
    Dispatch(mouseev);
    flags_ ^= ui::EF_LEFT_MOUSE_BUTTON;
  }
}

void EventGenerator::ClickLeftButton() {
  PressLeftButton();
  ReleaseLeftButton();
}

void EventGenerator::DoubleClickLeftButton() {
  flags_ |= ui::EF_IS_DOUBLE_CLICK;
  PressLeftButton();
  flags_ ^= ui::EF_IS_DOUBLE_CLICK;
  ReleaseLeftButton();
}

void EventGenerator::MoveMouseTo(const gfx::Point& point, int count) {
  DCHECK_GT(count, 0);
  const ui::EventType event_type = (flags_ & ui::EF_LEFT_MOUSE_BUTTON) ?
      ui::ET_MOUSE_DRAGGED : ui::ET_MOUSE_MOVED;
  const gfx::Point diff = point.Subtract(current_location_);
  for (int i = 1; i <= count; i++) {
    const gfx::Point move_point = current_location_.Add(
        gfx::Point(diff.x() / count * i, diff.y() / count * i));
    MouseEvent mouseev(event_type, move_point, move_point, flags_);
    Dispatch(mouseev);
  }
  current_location_ = point;
}

void EventGenerator::MoveMouseRelativeTo(const Window* window,
                                         const gfx::Point& point) {
  gfx::Point root_point(point);
  aura::Window::ConvertPointToWindow(window, root_window_, &root_point);

  MoveMouseTo(root_point);
}

void EventGenerator::DragMouseTo(const gfx::Point& point) {
  PressLeftButton();
  MoveMouseTo(point);
  ReleaseLeftButton();
}

void EventGenerator::MoveMouseToCenterOf(Window* window) {
  MoveMouseTo(CenterOfWindowInRootWindowCoordinate(root_window_, window));
}

void EventGenerator::PressTouch() {
  TestTouchEvent touchev(ui::ET_TOUCH_PRESSED, current_location_, flags_);
  Dispatch(touchev);
}

void EventGenerator::ReleaseTouch() {
  TestTouchEvent touchev(ui::ET_TOUCH_RELEASED, current_location_, flags_);
  Dispatch(touchev);
}

void EventGenerator::PressMoveAndReleaseTouchTo(const gfx::Point& point) {
  PressTouch();

  TestTouchEvent touchev(ui::ET_TOUCH_MOVED, point, flags_);
  Dispatch(touchev);

  current_location_ = point;

  ReleaseTouch();
}

void EventGenerator::PressMoveAndReleaseTouchToCenterOf(Window* window) {
  PressMoveAndReleaseTouchTo(CenterOfWindowInRootWindowCoordinate(root_window_,
                                                                  window));
}

void EventGenerator::GestureTapAt(const gfx::Point& location) {
  const int kTouchId = 2;
  TouchEvent press(ui::ET_TOUCH_PRESSED, location, kTouchId,
      base::Time::NowFromSystemTime() - base::Time());
  Dispatch(press);

  TouchEvent release(ui::ET_TOUCH_RELEASED, location, kTouchId,
      press.time_stamp() + base::TimeDelta::FromMilliseconds(50));
  Dispatch(release);
}

void EventGenerator::GestureTapDownAndUp(const gfx::Point& location) {
  const int kTouchId = 3;
  TouchEvent press(ui::ET_TOUCH_PRESSED, location, kTouchId,
      base::Time::NowFromSystemTime() - base::Time());
  Dispatch(press);

  TouchEvent release(ui::ET_TOUCH_RELEASED, location, kTouchId,
      press.time_stamp() + base::TimeDelta::FromMilliseconds(1000));
  Dispatch(release);
}

void EventGenerator::GestureScrollSequence(const gfx::Point& start,
                                           const gfx::Point& end,
                                           const base::TimeDelta& step_delay,
                                           int steps) {
  const int kTouchId = 5;
  base::TimeDelta timestamp = base::Time::NowFromSystemTime() - base::Time();
  TouchEvent press(ui::ET_TOUCH_PRESSED, start, kTouchId, timestamp);
  Dispatch(press);

  int dx = (end.x() - start.x()) / steps;
  int dy = (end.y() - start.y()) / steps;
  gfx::Point location = start;
  for (int i = 0; i < steps; ++i) {
    location.Offset(dx, dy);
    timestamp += step_delay;
    TouchEvent move(ui::ET_TOUCH_MOVED, location, kTouchId, timestamp);
    Dispatch(move);
  }

  TouchEvent release(ui::ET_TOUCH_RELEASED, end, kTouchId, timestamp);
  Dispatch(release);
}

void EventGenerator::PressKey(ui::KeyboardCode key_code, int flags) {
  DispatchKeyEvent(true, key_code, flags);
}

void EventGenerator::ReleaseKey(ui::KeyboardCode key_code, int flags) {
  DispatchKeyEvent(false, key_code, flags);
}

void EventGenerator::Dispatch(Event& event) {
  switch (event.type()) {
    case ui::ET_KEY_PRESSED:
    case ui::ET_KEY_RELEASED:
      root_window_->AsRootWindowHostDelegate()->OnHostKeyEvent(
          static_cast<KeyEvent*>(&event));
      break;
    case ui::ET_MOUSE_PRESSED:
    case ui::ET_MOUSE_DRAGGED:
    case ui::ET_MOUSE_RELEASED:
    case ui::ET_MOUSE_MOVED:
    case ui::ET_MOUSE_ENTERED:
    case ui::ET_MOUSE_EXITED:
    case ui::ET_MOUSEWHEEL:
      root_window_->AsRootWindowHostDelegate()->OnHostMouseEvent(
          static_cast<MouseEvent*>(&event));
      break;
    case ui::ET_TOUCH_RELEASED:
    case ui::ET_TOUCH_PRESSED:
    case ui::ET_TOUCH_MOVED:
    case ui::ET_TOUCH_STATIONARY:
    case ui::ET_TOUCH_CANCELLED:
      root_window_->AsRootWindowHostDelegate()->OnHostTouchEvent(
          static_cast<TouchEvent*>(&event));
      break;
    default:
      NOTIMPLEMENTED();
      break;
  }
}

void EventGenerator::DispatchKeyEvent(bool is_press,
                                      ui::KeyboardCode key_code,
                                      int flags) {
#if defined(OS_WIN)
  MSG native_event =
      { NULL, (is_press ? WM_KEYDOWN : WM_KEYUP), key_code, 0 };
  TestKeyEvent keyev(native_event, flags);
#else
  ui::EventType type = is_press ? ui::ET_KEY_PRESSED : ui::ET_KEY_RELEASED;
#if defined(USE_X11)
  scoped_ptr<XEvent> native_event(new XEvent);
  ui::InitXKeyEventForTesting(type, key_code, flags, native_event.get());
  TestKeyEvent keyev(native_event.get(), flags);
#else
  KeyEvent keyev(type, key_code, flags);
#endif  // USE_X11
#endif  // OS_WIN
  Dispatch(keyev);
}

}  // namespace test
}  // namespace aura
