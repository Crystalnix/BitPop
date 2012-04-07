// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/window_cycle_controller.h"

#include "ash/shell.h"
#include "ash/shell_delegate.h"
#include "ash/wm/window_cycle_list.h"
#include "ash/wm/window_util.h"
#include "ui/aura/event.h"
#include "ui/aura/event_filter.h"
#include "ui/aura/root_window.h"

namespace ash {

// Filter to watch for the termination of a keyboard gesture to cycle through
// multiple windows.
class WindowCycleEventFilter : public aura::EventFilter {
 public:
  WindowCycleEventFilter();
  virtual ~WindowCycleEventFilter();

  // Overridden from aura::EventFilter:
  virtual bool PreHandleKeyEvent(aura::Window* target,
                                 aura::KeyEvent* event) OVERRIDE;
  virtual bool PreHandleMouseEvent(aura::Window* target,
                                   aura::MouseEvent* event) OVERRIDE;
  virtual ui::TouchStatus PreHandleTouchEvent(aura::Window* target,
                                              aura::TouchEvent* event) OVERRIDE;
  virtual ui::GestureStatus PreHandleGestureEvent(
      aura::Window* target,
      aura::GestureEvent* event) OVERRIDE;
 private:
  DISALLOW_COPY_AND_ASSIGN(WindowCycleEventFilter);
};

// Watch for all keyboard events by filtering the root window.
WindowCycleEventFilter::WindowCycleEventFilter()
    : aura::EventFilter(aura::RootWindow::GetInstance()) {
}

WindowCycleEventFilter::~WindowCycleEventFilter() {
}

bool WindowCycleEventFilter::PreHandleKeyEvent(
    aura::Window* target,
    aura::KeyEvent* event) {
  // Views uses VKEY_MENU for both left and right Alt keys.
  if (event->key_code() == ui::VKEY_MENU &&
      event->type() == ui::ET_KEY_RELEASED) {
    Shell::GetInstance()->window_cycle_controller()->AltKeyReleased();
    // Warning: |this| will be deleted from here on.
  }
  return false;  // Always let the event propagate.
}

bool WindowCycleEventFilter::PreHandleMouseEvent(
    aura::Window* target,
    aura::MouseEvent* event) {
  return false;  // Not handled.
}

ui::TouchStatus WindowCycleEventFilter::PreHandleTouchEvent(
    aura::Window* target,
    aura::TouchEvent* event) {
  return ui::TOUCH_STATUS_UNKNOWN;  // Not handled.
}

ui::GestureStatus WindowCycleEventFilter::PreHandleGestureEvent(
    aura::Window* target,
    aura::GestureEvent* event) {
  return ui::GESTURE_STATUS_UNKNOWN;  // Not handled.
}

//////////////////////////////////////////////////////////////////////////////
// WindowCycleController, public:

WindowCycleController::WindowCycleController() {
}

WindowCycleController::~WindowCycleController() {
  StopCycling();
}

// static
bool WindowCycleController::CanCycle() {
  // Don't allow window cycling if the screen is locked or a modal dialog is
  // open.
  return !Shell::GetInstance()->IsScreenLocked() &&
         !Shell::GetInstance()->IsModalWindowOpen();
}

void WindowCycleController::HandleCycleWindow(Direction direction,
                                              bool is_alt_down) {
  if (!CanCycle())
    return;

  if (is_alt_down) {
    if (!IsCycling()) {
      // This is the start of an alt-tab cycle through multiple windows, so
      // listen for the alt key being released to stop cycling.
      StartCycling();
      Step(direction);
      InstallEventFilter();
    } else {
      // We're in the middle of an alt-tab cycle, just step forward.
      Step(direction);
    }
  } else {
    // This is a simple, single-step window cycle.
    StartCycling();
    Step(direction);
    StopCycling();
  }
}

void WindowCycleController::AltKeyReleased() {
  StopCycling();
}

//////////////////////////////////////////////////////////////////////////////
// WindowCycleController, private:

void WindowCycleController::StartCycling() {
  // Most-recently-used cycling is confusing in compact window mode because
  // you can't see all the windows.
  windows_.reset(new WindowCycleList(
      ash::Shell::GetInstance()->delegate()->GetCycleWindowList(
          ShellDelegate::SOURCE_KEYBOARD,
          Shell::GetInstance()->IsWindowModeCompact() ?
          ShellDelegate::ORDER_LINEAR : ShellDelegate::ORDER_MRU)));
}

void WindowCycleController::Step(Direction direction) {
    DCHECK(windows_.get());
    windows_->Step(direction == FORWARD ? WindowCycleList::FORWARD :
                   WindowCycleList::BACKWARD);
}

void WindowCycleController::StopCycling() {
  windows_.reset();
  // Remove our key event filter.
  if (event_filter_.get()) {
    Shell::GetInstance()->RemoveRootWindowEventFilter(event_filter_.get());
    event_filter_.reset();
  }
}

void WindowCycleController::InstallEventFilter() {
  event_filter_.reset(new WindowCycleEventFilter());
  Shell::GetInstance()->AddRootWindowEventFilter(event_filter_.get());
}

}  // namespace ash
