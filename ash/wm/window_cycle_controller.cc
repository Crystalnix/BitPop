// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/window_cycle_controller.h"

#include <algorithm>

#include "ash/shell_delegate.h"
#include "ash/shell_window_ids.h"
#include "ash/wm/activation_controller.h"
#include "ash/wm/window_cycle_list.h"
#include "ash/wm/window_util.h"
#include "ui/aura/event.h"
#include "ui/aura/event_filter.h"
#include "ui/aura/root_window.h"

namespace ash {

namespace {

// List of containers whose children we will cycle through.
const int kContainerIds[] = {
  internal::kShellWindowId_DefaultContainer,
  internal::kShellWindowId_AlwaysOnTopContainer
};

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
WindowCycleEventFilter::WindowCycleEventFilter() {
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

}  // namespace

//////////////////////////////////////////////////////////////////////////////
// WindowCycleController, public:

WindowCycleController::WindowCycleController(
    internal::ActivationController* activation_controller)
  : activation_controller_(activation_controller) {
  activation_controller_->AddObserver(this);
}

WindowCycleController::~WindowCycleController() {
  Shell::RootWindowList root_windows = Shell::GetAllRootWindows();
  for (Shell::RootWindowList::const_iterator iter = root_windows.begin();
       iter != root_windows.end(); ++iter) {
    for (size_t i = 0; i < arraysize(kContainerIds); ++i) {
      aura::Window* container = Shell::GetContainer(*iter, kContainerIds[i]);
      if (container)
        container->RemoveObserver(this);
    }
  }

  activation_controller_->RemoveObserver(this);
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

// static
std::vector<aura::Window*> WindowCycleController::BuildWindowList(
    const std::list<aura::Window*>* mru_windows) {
  WindowCycleList::WindowList windows;
  Shell::RootWindowList root_windows = Shell::GetAllRootWindows();

  for (Shell::RootWindowList::const_iterator iter = root_windows.begin();
       iter != root_windows.end(); ++iter) {
    if (*iter == Shell::GetActiveRootWindow())
      continue;
    for (size_t i = 0; i < arraysize(kContainerIds); ++i) {
      aura::Window* container = Shell::GetContainer(*iter, kContainerIds[i]);
      WindowCycleList::WindowList children = container->children();
      windows.insert(windows.end(), children.begin(), children.end());
    }
  }

  // Add windows in the active root windows last so that the topmost window
  // in the active root window becomes the front of the list.
  for (size_t i = 0; i < arraysize(kContainerIds); ++i) {
    aura::Window* container =
        Shell::GetContainer(Shell::GetActiveRootWindow(), kContainerIds[i]);

    WindowCycleList::WindowList children = container->children();
    windows.insert(windows.end(), children.begin(), children.end());
  }

  // Removes unfocusable windows.
  WindowCycleList::WindowList::iterator last =
      std::remove_if(
          windows.begin(),
          windows.end(),
          std::not1(std::ptr_fun(ash::wm::CanActivateWindow)));
  windows.erase(last, windows.end());

  // Put the windows in the mru_windows list at the head, if it's available.
  if (mru_windows) {
    // Iterate through the list backwards, so that we can move each window to
    // the front of the windows list as we find them.
    for (std::list<aura::Window*>::const_reverse_iterator ix =
         mru_windows->rbegin();
         ix != mru_windows->rend(); ++ix) {
      WindowCycleList::WindowList::iterator window =
          std::find(windows.begin(), windows.end(), *ix);
      if (window != windows.end()) {
        windows.erase(window);
        windows.push_back(*ix);
      }
    }
  }

  // Window cycling expects the topmost window at the front of the list.
  std::reverse(windows.begin(), windows.end());

  return windows;
}

void WindowCycleController::OnRootWindowAdded(aura::RootWindow* root_window) {
  for (size_t i = 0; i < arraysize(kContainerIds); ++i) {
    aura::Window* container =
        Shell::GetContainer(root_window, kContainerIds[i]);
    container->AddObserver(this);
  }
}

//////////////////////////////////////////////////////////////////////////////
// WindowCycleController, private:

void WindowCycleController::StartCycling() {
  windows_.reset(new WindowCycleList(BuildWindowList(&mru_windows_)));
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
    Shell::GetInstance()->RemoveEnvEventFilter(event_filter_.get());
    event_filter_.reset();
  }

  // Add the currently focused window to the MRU list
  aura::Window* active_window = wm::GetActiveWindow();
  mru_windows_.remove(active_window);
  mru_windows_.push_front(active_window);
}

// static
bool WindowCycleController::IsTrackedContainer(aura::Window* window) {
  if (!window)
    return false;
  for (size_t i = 0; i < arraysize(kContainerIds); ++i) {
    if (window->id() == kContainerIds[i]) {
      return true;
    }
  }
  return false;
}

void WindowCycleController::InstallEventFilter() {
  event_filter_.reset(new WindowCycleEventFilter());
  Shell::GetInstance()->AddEnvEventFilter(event_filter_.get());
}

void WindowCycleController::OnWindowActivated(aura::Window* active,
                                              aura::Window* old_active) {
  if (active && !IsCycling() && IsTrackedContainer(active->parent())) {
    mru_windows_.remove(active);
    mru_windows_.push_front(active);
  }
}

void WindowCycleController::OnWillRemoveWindow(aura::Window* window) {
  mru_windows_.remove(window);
}

void WindowCycleController::OnWindowDestroying(aura::Window* window) {
  window->RemoveObserver(this);
}

}  // namespace ash
