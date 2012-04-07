// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/root_window_event_filter.h"

#include "ash/shell.h"
#include "ash/wm/activation_controller.h"
#include "ash/wm/window_util.h"
#include "ui/aura/event.h"
#include "ui/aura/focus_manager.h"
#include "ui/aura/root_window.h"
#include "ui/aura/window_delegate.h"
#include "ui/base/hit_test.h"

namespace ash {
namespace internal {

// Returns the default cursor for a window component.
gfx::NativeCursor CursorForWindowComponent(int window_component) {
  switch (window_component) {
    case HTBOTTOM:
      return aura::kCursorSouthResize;
    case HTBOTTOMLEFT:
      return aura::kCursorSouthWestResize;
    case HTBOTTOMRIGHT:
      return aura::kCursorSouthEastResize;
    case HTLEFT:
      return aura::kCursorWestResize;
    case HTRIGHT:
      return aura::kCursorEastResize;
    case HTTOP:
      return aura::kCursorNorthResize;
    case HTTOPLEFT:
      return aura::kCursorNorthWestResize;
    case HTTOPRIGHT:
      return aura::kCursorNorthEastResize;
    default:
      return aura::kCursorNull;
  }
}

////////////////////////////////////////////////////////////////////////////////
// RootWindowEventFilter, public:

RootWindowEventFilter::RootWindowEventFilter()
    : EventFilter(aura::RootWindow::GetInstance()) {
}

RootWindowEventFilter::~RootWindowEventFilter() {
  // Additional filters are not owned by RootWindowEventFilter and they
  // should all be removed when running here. |filters_| has
  // check_empty == true and will DCHECK failure if it is not empty.
}

void RootWindowEventFilter::AddFilter(aura::EventFilter* filter) {
  filters_.AddObserver(filter);
}

void RootWindowEventFilter::RemoveFilter(aura::EventFilter* filter) {
  filters_.RemoveObserver(filter);
}

size_t RootWindowEventFilter::GetFilterCount() const {
  return filters_.size();
}

////////////////////////////////////////////////////////////////////////////////
// RootWindowEventFilter, EventFilter implementation:

bool RootWindowEventFilter::PreHandleKeyEvent(aura::Window* target,
                                              aura::KeyEvent* event) {
  return FilterKeyEvent(target, event);
}

bool RootWindowEventFilter::PreHandleMouseEvent(aura::Window* target,
                                                aura::MouseEvent* event) {
  // We must always update the cursor, otherwise the cursor can get stuck if an
  // event filter registered with us consumes the event.
  if (event->type() == ui::ET_MOUSE_MOVED) {
    // Shows the cursor when mouse moved.
    SetCursorVisible(target, event, true);

    UpdateCursor(target, event);
  }

  if (FilterMouseEvent(target, event))
    return true;

  if (event->type() == ui::ET_MOUSE_PRESSED && GetActiveWindow() != target)
    target->GetFocusManager()->SetFocusedWindow(target);

  return false;
}

ui::TouchStatus RootWindowEventFilter::PreHandleTouchEvent(
    aura::Window* target,
    aura::TouchEvent* event) {
  ui::TouchStatus status = FilterTouchEvent(target, event);
  if (status != ui::TOUCH_STATUS_UNKNOWN)
    return status;

  if (event->type() == ui::ET_TOUCH_PRESSED) {
    // Hides the cursor when touch pressed.
    SetCursorVisible(target, event, false);

    target->GetFocusManager()->SetFocusedWindow(target);
  }
  return ui::TOUCH_STATUS_UNKNOWN;
}

ui::GestureStatus RootWindowEventFilter::PreHandleGestureEvent(
    aura::Window* target,
    aura::GestureEvent* event) {
  // TODO(sad):
  return ui::GESTURE_STATUS_UNKNOWN;
}

////////////////////////////////////////////////////////////////////////////////
// RootWindowEventFilter, private:

void RootWindowEventFilter::UpdateCursor(aura::Window* target,
                                         aura::MouseEvent* event) {
  gfx::NativeCursor cursor = target->GetCursor(event->location());
  if (event->flags() & ui::EF_IS_NON_CLIENT) {
    int window_component =
        target->delegate()->GetNonClientComponent(event->location());
    cursor = CursorForWindowComponent(window_component);
  }
  aura::RootWindow::GetInstance()->SetCursor(cursor);
}

void RootWindowEventFilter::SetCursorVisible(aura::Window* target,
                                             aura::LocatedEvent* event,
                                             bool show) {
  aura::RootWindow::GetInstance()->ShowCursor(show);
}

bool RootWindowEventFilter::FilterKeyEvent(aura::Window* target,
                                           aura::KeyEvent* event) {
  bool handled = false;
  if (filters_.might_have_observers()) {
    ObserverListBase<aura::EventFilter>::Iterator it(filters_);
    aura::EventFilter* filter;
    while (!handled && (filter = it.GetNext()) != NULL)
      handled = filter->PreHandleKeyEvent(target, event);
  }
  return handled;
}

bool RootWindowEventFilter::FilterMouseEvent(aura::Window* target,
                                             aura::MouseEvent* event) {
  bool handled = false;
  if (filters_.might_have_observers()) {
    ObserverListBase<aura::EventFilter>::Iterator it(filters_);
    aura::EventFilter* filter;
    while (!handled && (filter = it.GetNext()) != NULL)
      handled = filter->PreHandleMouseEvent(target, event);
  }
  return handled;
}

ui::TouchStatus RootWindowEventFilter::FilterTouchEvent(
    aura::Window* target,
    aura::TouchEvent* event) {
  ui::TouchStatus status = ui::TOUCH_STATUS_UNKNOWN;
  if (filters_.might_have_observers()) {
    ObserverListBase<aura::EventFilter>::Iterator it(filters_);
    aura::EventFilter* filter;
    while (status == ui::TOUCH_STATUS_UNKNOWN &&
        (filter = it.GetNext()) != NULL) {
      status = filter->PreHandleTouchEvent(target, event);
    }
  }
  return status;
}

}  // namespace internal
}  // namespace ash
