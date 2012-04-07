// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SHELL_DELEGATE_H_
#define ASH_SHELL_DELEGATE_H_
#pragma once

#include <vector>

#include "ash/ash_export.h"
#include "base/callback.h"
#include "base/string16.h"

namespace aura {
class Window;
}

namespace gfx {
class Rect;
}

namespace views {
class Widget;
}

namespace ash {

class AppListModel;
class AppListViewDelegate;
struct LauncherItem;

// Delegate of the Shell.
class ASH_EXPORT ShellDelegate {
 public:
  enum CycleOrder {
    ORDER_MRU,  // Most recently used
    ORDER_LINEAR
  };

  // Source requesting the window list.
  enum CycleSource {
    // Windows are going to be used for alt-tab (or F5).
    SOURCE_KEYBOARD,

    // Windows are going to be cycled from the launcher.
    SOURCE_LAUNCHER,
  };

  // The Shell owns the delegate.
  virtual ~ShellDelegate() {}

  // Invoked to create a new status area. Can return NULL.
  virtual views::Widget* CreateStatusArea() = 0;

#if defined(OS_CHROMEOS)
  // Invoked when a user uses Ctrl-Shift-L to lock the screen.
  virtual void LockScreen() = 0;
#endif

  // Invoked when a user uses Ctrl-Shift-Q to close chrome.
  virtual void Exit() = 0;

  // Invoked to ask the delegate to populate the |model|.
  virtual void BuildAppListModel(AppListModel* model) = 0;

  // Invoked to create an AppListViewDelegate. Shell takes the ownership of
  // the created delegate.
  virtual AppListViewDelegate* CreateAppListViewDelegate() = 0;

  // Returns a list of windows to cycle with keyboard shortcuts (e.g. alt-tab
  // or the window switching key).  If |order_by_activity| is true then windows
  // are returned in most-recently-used order with the currently active window
  // at the front of the list.  Otherwise any order may be returned.  The list
  // does not contain NULL pointers.
  virtual std::vector<aura::Window*> GetCycleWindowList(
      CycleSource source,
      CycleOrder order) const = 0;

  // Launcher related methods --------------------------------------------------

  // Invoked when the user clicks on button in the launcher to create a new
  // window.
  virtual void CreateNewWindow() = 0;

  // Invoked when the user clicks on a window entry in the launcher.
  virtual void LauncherItemClicked(const LauncherItem& item) = 0;

  // Returns the resource id of the image to show on the browser shortcut
  // button.
  virtual int GetBrowserShortcutResourceId() = 0;

  // Returns the title to display for the specified launcher item.
  virtual string16 GetLauncherItemTitle(const LauncherItem& item) = 0;
};

}  // namespace ash

#endif  // ASH_SHELL_DELEGATE_H_
