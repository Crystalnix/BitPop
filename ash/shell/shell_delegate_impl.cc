// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/shell/shell_delegate_impl.h"

#include "ash/shell/example_factory.h"
#include "ash/shell/launcher_delegate_impl.h"
#include "ash/shell/toplevel_window.h"
#include "ash/shell_window_ids.h"
#include "base/message_loop.h"
#include "ui/aura/window.h"

namespace ash {
namespace shell {

ShellDelegateImpl::ShellDelegateImpl()
    : watcher_(NULL),
      launcher_delegate_(NULL),
      locked_(false),
      spoken_feedback_enabled_(false) {
}

ShellDelegateImpl::~ShellDelegateImpl() {
}

void ShellDelegateImpl::SetWatcher(WindowWatcher* watcher) {
  watcher_ = watcher;
  if (launcher_delegate_)
    launcher_delegate_->set_watcher(watcher);
}

bool ShellDelegateImpl::IsUserLoggedIn() {
  return true;
}

bool ShellDelegateImpl::IsSessionStarted() {
  return true;
}

void ShellDelegateImpl::LockScreen() {
  ash::shell::CreateLockScreen();
  locked_ = true;
  ash::Shell::GetInstance()->UpdateShelfVisibility();
}

void ShellDelegateImpl::UnlockScreen() {
  locked_ = false;
  ash::Shell::GetInstance()->UpdateShelfVisibility();
}

bool ShellDelegateImpl::IsScreenLocked() const {
  return locked_;
}

void ShellDelegateImpl::Shutdown() {
}

void ShellDelegateImpl::Exit() {
  MessageLoopForUI::current()->Quit();
}

void ShellDelegateImpl::NewTab() {
}

void ShellDelegateImpl::NewWindow(bool incognito) {
  ash::shell::ToplevelWindow::CreateParams create_params;
  create_params.can_resize = true;
  create_params.can_maximize = true;
  ash::shell::ToplevelWindow::CreateToplevelWindow(create_params);
}

void ShellDelegateImpl::OpenFileManager(bool as_dialog) {
}

void ShellDelegateImpl::OpenCrosh() {
}

void ShellDelegateImpl::OpenMobileSetup(const std::string& service_path) {
}

void ShellDelegateImpl::RestoreTab() {
}

bool ShellDelegateImpl::RotatePaneFocus(Shell::Direction direction) {
  return true;
}

void ShellDelegateImpl::ShowKeyboardOverlay() {
}

void ShellDelegateImpl::ShowTaskManager() {
}

content::BrowserContext* ShellDelegateImpl::GetCurrentBrowserContext() {
  return Shell::GetInstance()->browser_context();
}

void ShellDelegateImpl::ToggleSpokenFeedback() {
  spoken_feedback_enabled_ = !spoken_feedback_enabled_;
}

bool ShellDelegateImpl::IsSpokenFeedbackEnabled() const {
  return spoken_feedback_enabled_;
}

app_list::AppListViewDelegate* ShellDelegateImpl::CreateAppListViewDelegate() {
  return ash::shell::CreateAppListViewDelegate();
}

ash::LauncherDelegate* ShellDelegateImpl::CreateLauncherDelegate(
    ash::LauncherModel* model) {
  launcher_delegate_ = new LauncherDelegateImpl(watcher_);
  return launcher_delegate_;
}

ash::SystemTrayDelegate* ShellDelegateImpl::CreateSystemTrayDelegate(
    ash::SystemTray* tray) {
  return NULL;
}

ash::UserWallpaperDelegate* ShellDelegateImpl::CreateUserWallpaperDelegate() {
  return NULL;
}

aura::client::UserActionClient* ShellDelegateImpl::CreateUserActionClient() {
  return NULL;
}

void ShellDelegateImpl::OpenFeedbackPage() {
}

void ShellDelegateImpl::RecordUserMetricsAction(UserMetricsAction action) {
}

}  // namespace shell
}  // namespace ash
