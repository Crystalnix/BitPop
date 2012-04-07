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

gfx::Point CenterOfWindowInRootWindowCoordinate(aura::Window* window) {
  gfx::Point center = window->bounds().CenterPoint();
  aura::RootWindow* root_window = aura::RootWindow::GetInstance();
  aura::Window::ConvertPointToWindow(window->parent(), root_window, &center);
  return center;
}

}  // namespace

namespace aura {
namespace test {

EventGenerator::EventGenerator() : flags_(0) {
}

EventGenerator::EventGenerator(const gfx::Point& point)
    : flags_(0),
      current_location_(point) {
}

EventGenerator::EventGenerator(Window* window)
    : flags_(0),
      current_location_(CenterOfWindowInRootWindowCoordinate(window)) {
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
    flags_ ^= ui::EF_LEFT_MOUSE_BUTTON;
    MouseEvent mouseev(
        ui::ET_MOUSE_RELEASED, current_location_, current_location_, 0);
    Dispatch(mouseev);
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
  aura::RootWindow* root_window = aura::RootWindow::GetInstance();
  aura::Window::ConvertPointToWindow(window, root_window, &root_point);

  MoveMouseTo(root_point);
}

void EventGenerator::DragMouseTo(const gfx::Point& point) {
  PressLeftButton();
  MoveMouseTo(point);
  ReleaseLeftButton();
}

void EventGenerator::MoveMouseToCenterOf(Window* window) {
  MoveMouseTo(CenterOfWindowInRootWindowCoordinate(window));
}

void EventGenerator::PressTouch() {
  TouchEvent touchev(ui::ET_TOUCH_PRESSED, current_location_, flags_);
  Dispatch(touchev);
}

void EventGenerator::ReleaseTouch() {
  TouchEvent touchev(ui::ET_TOUCH_RELEASED, current_location_, flags_);
  Dispatch(touchev);
}

void EventGenerator::PressMoveAndReleaseTouchTo(const gfx::Point& point) {
  PressTouch();

  TouchEvent touchev(ui::ET_TOUCH_MOVED, point, flags_);
  Dispatch(touchev);

  current_location_ = point;

  ReleaseTouch();
}

void EventGenerator::PressMoveAndReleaseTouchToCenterOf(Window* window) {
  PressMoveAndReleaseTouchTo(CenterOfWindowInRootWindowCoordinate(window));
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
      aura::RootWindow::GetInstance()->DispatchKeyEvent(
          static_cast<KeyEvent*>(&event));
      break;
    case ui::ET_MOUSE_PRESSED:
    case ui::ET_MOUSE_DRAGGED:
    case ui::ET_MOUSE_RELEASED:
    case ui::ET_MOUSE_MOVED:
    case ui::ET_MOUSE_ENTERED:
    case ui::ET_MOUSE_EXITED:
    case ui::ET_MOUSEWHEEL:
      aura::RootWindow::GetInstance()->DispatchMouseEvent(
          static_cast<MouseEvent*>(&event));
      break;
    case ui::ET_TOUCH_RELEASED:
    case ui::ET_TOUCH_PRESSED:
    case ui::ET_TOUCH_MOVED:
    case ui::ET_TOUCH_STATIONARY:
    case ui::ET_TOUCH_CANCELLED:
      aura::RootWindow::GetInstance()->DispatchTouchEvent(
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
