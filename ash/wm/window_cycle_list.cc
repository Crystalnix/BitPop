// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/window_cycle_list.h"

#include "ash/wm/window_util.h"
#include "ui/aura/window.h"

namespace ash {

WindowCycleList::WindowCycleList(const WindowList& windows)
    : windows_(windows),
      current_index_(-1) {
  // Locate the currently active window in the list to use as our start point.
  aura::Window* active_window = GetActiveWindow();

  // The active window may not be in the cycle list, which is expected if there
  // are additional modal windows on the screen.
  current_index_ = GetWindowIndex(active_window);

  for (WindowList::const_iterator i = windows_.begin(); i != windows_.end();
       ++i) {
    (*i)->AddObserver(this);
  }
}

WindowCycleList::~WindowCycleList() {
  for (WindowList::const_iterator i = windows_.begin(); i != windows_.end();
       ++i) {
    (*i)->RemoveObserver(this);
  }
}

void WindowCycleList::Step(Direction direction) {
  // Ensure we have at least one window to step to.
  if (windows_.empty())
    return;

  // Don't cycle through a list of one.
  if (windows_.size() == 1)
    return;

  if (current_index_ == -1) {
    // We weren't able to find our active window in the shell delegate's
    // provided window list.  Just switch to the first (or last) one.
    current_index_ = (direction == FORWARD ? 0 : windows_.size() - 1);
  } else {
    // We're in a valid cycle, so step forward or backward.
    current_index_ += (direction == FORWARD ? 1 : -1);
  }
  // Wrap to window list size.
  current_index_ = (current_index_ + windows_.size()) % windows_.size();
  DCHECK(windows_[current_index_]);
  // Make sure the next window is visible.
  windows_[current_index_]->Show();
  ActivateWindow(windows_[current_index_]);
}

int WindowCycleList::GetWindowIndex(aura::Window* window) {
  WindowList::const_iterator it =
      std::find(windows_.begin(), windows_.end(), window);
  if (it == windows_.end())
    return -1;  // Not found.
  return it - windows_.begin();
}

void WindowCycleList::OnWindowDestroyed(aura::Window* window) {
  window->RemoveObserver(this);

  WindowList::iterator i = std::find(windows_.begin(), windows_.end(), window);
  DCHECK(i != windows_.end());
  int removed_index = static_cast<int>(i - windows_.begin());
  windows_.erase(i);
  if (current_index_ > removed_index)
    current_index_--;
  else if (current_index_ == static_cast<int>(windows_.size()))
    current_index_--;
}

}  // namespace ash
