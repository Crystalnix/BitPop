// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SHELL_DELEGATE_H_
#define ASH_SHELL_DELEGATE_H_

#include <vector>

#include "ash/ash_export.h"
#include "ash/shell.h"
#include "base/callback.h"
#include "base/string16.h"

namespace app_list {
class AppListViewDelegate;
}

namespace aura {
class Window;
namespace client {
class UserActionClient;
}
}

namespace views {
class Widget;
}

namespace ash {

class LauncherDelegate;
class LauncherModel;
struct LauncherItem;
class SystemTray;
class SystemTrayDelegate;
class UserWallpaperDelegate;

enum UserMetricsAction {
  UMA_ACCEL_PREVWINDOW_TAB,
  UMA_ACCEL_NEXTWINDOW_TAB,
  UMA_ACCEL_PREVWINDOW_F5,
  UMA_ACCEL_NEXTWINDOW_F5,
  UMA_ACCEL_NEWTAB_T,
  UMA_ACCEL_SEARCH_LWIN,
  UMA_MOUSE_DOWN,
  UMA_TOUCHSCREEN_TAP_DOWN,
};

// Delegate of the Shell.
class ASH_EXPORT ShellDelegate {
 public:
  // The Shell owns the delegate.
  virtual ~ShellDelegate() {}

  // Returns true if user has logged in.
  virtual bool IsUserLoggedIn() = 0;

  // Returns true if we're logged in and browser has been started
  virtual bool IsSessionStarted() = 0;

  // Invoked when a user locks the screen.
  virtual void LockScreen() = 0;

  // Unlock the screen. Currently used only for tests.
  virtual void UnlockScreen() = 0;

  // Returns true if the screen is currently locked.
  virtual bool IsScreenLocked() const = 0;

  // Shuts down the environment.
  virtual void Shutdown() = 0;

  // Invoked when the user uses Ctrl-Shift-Q to close chrome.
  virtual void Exit() = 0;

  // Invoked when the user uses Ctrl+T to open a new tab.
  virtual void NewTab() = 0;

  // Invoked when the user uses Ctrl-N or Ctrl-Shift-N to open a new window.
  virtual void NewWindow(bool incognito) = 0;

  // Invoked when the user uses Ctrl-M or Ctrl-O to open file manager.
  virtual void OpenFileManager(bool as_dialog) = 0;

  // Invoked when the user opens Crosh.
  virtual void OpenCrosh() = 0;

  // Invoked when the user needs to set up mobile networking.
  virtual void OpenMobileSetup(const std::string& service_path) = 0;

  // Invoked when the user uses Shift+Ctrl+T to restore the closed tab.
  virtual void RestoreTab() = 0;

  // Moves keyboard focus to the next pane. Returns false if no browser window
  // is created.
  virtual bool RotatePaneFocus(Shell::Direction direction) = 0;

  // Shows the keyboard shortcut overlay.
  virtual void ShowKeyboardOverlay() = 0;

  // Shows the task manager window.
  virtual void ShowTaskManager() = 0;

  // Get the current browser context. This will get us the current profile.
  virtual content::BrowserContext* GetCurrentBrowserContext() = 0;

  // Invoked when the user presses a shortcut to toggle spoken feedback
  // for accessibility.
  virtual void ToggleSpokenFeedback() = 0;

  // Returns true if spoken feedback is enabled.
  virtual bool IsSpokenFeedbackEnabled() const = 0;

  // Invoked to create an AppListViewDelegate. Shell takes the ownership of
  // the created delegate.
  virtual app_list::AppListViewDelegate* CreateAppListViewDelegate() = 0;

  // Creates a new LauncherDelegate. Shell takes ownership of the returned
  // value.
  virtual LauncherDelegate* CreateLauncherDelegate(
      ash::LauncherModel* model) = 0;

  // Creates a system-tray delegate. Shell takes ownership of the delegate.
  virtual SystemTrayDelegate* CreateSystemTrayDelegate(SystemTray* tray) = 0;

  // Creates a user wallpaper delegate. Shell takes ownership of the delegate.
  virtual UserWallpaperDelegate* CreateUserWallpaperDelegate() = 0;

  // Creates a user action client. Shell takes ownership of the object.
  virtual aura::client::UserActionClient* CreateUserActionClient() = 0;

  // Opens the feedback page for "Report Issue".
  virtual void OpenFeedbackPage() = 0;

  // Records that the user performed an action.
  virtual void RecordUserMetricsAction(UserMetricsAction action) = 0;
};

}  // namespace ash

#endif  // ASH_SHELL_DELEGATE_H_
