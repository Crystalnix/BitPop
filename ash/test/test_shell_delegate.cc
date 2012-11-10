// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/test/test_shell_delegate.h"

#include <algorithm>

#include "ash/shell.h"
#include "ash/shell_window_ids.h"
#include "ash/test/test_launcher_delegate.h"
#include "content/public/test/test_browser_context.h"
#include "grit/ui_resources.h"
#include "ui/aura/window.h"

namespace ash {
namespace test {

TestShellDelegate::TestShellDelegate()
    : locked_(false),
      spoken_feedback_enabled_(false) {
}

TestShellDelegate::~TestShellDelegate() {
}

bool TestShellDelegate::IsUserLoggedIn() {
  return true;
}

bool TestShellDelegate::IsSessionStarted() {
  return true;
}

void TestShellDelegate::LockScreen() {
  locked_ = true;
}

void TestShellDelegate::UnlockScreen() {
  locked_ = false;
}

bool TestShellDelegate::IsScreenLocked() const {
  return locked_;
}

void TestShellDelegate::Shutdown() {
}

void TestShellDelegate::Exit() {
}

void TestShellDelegate::NewTab() {
}

void TestShellDelegate::NewWindow(bool incognito) {
}

void TestShellDelegate::OpenFileManager(bool as_dialog) {
}

void TestShellDelegate::OpenCrosh() {
}

void TestShellDelegate::OpenMobileSetup(const std::string& service_path) {
}

void TestShellDelegate::RestoreTab() {
}

bool TestShellDelegate::RotatePaneFocus(Shell::Direction direction) {
  return true;
}

void TestShellDelegate::ShowKeyboardOverlay() {
}

void TestShellDelegate::ShowTaskManager() {
}

content::BrowserContext* TestShellDelegate::GetCurrentBrowserContext() {
  return new content::TestBrowserContext();
}

void TestShellDelegate::ToggleSpokenFeedback() {
  spoken_feedback_enabled_ = !spoken_feedback_enabled_;
}

bool TestShellDelegate::IsSpokenFeedbackEnabled() const {
  return spoken_feedback_enabled_;
}

app_list::AppListViewDelegate* TestShellDelegate::CreateAppListViewDelegate() {
  return NULL;
}

LauncherDelegate* TestShellDelegate::CreateLauncherDelegate(
    ash::LauncherModel* model) {
  return new TestLauncherDelegate(model);
}

SystemTrayDelegate* TestShellDelegate::CreateSystemTrayDelegate(
    SystemTray* tray) {
  return NULL;
}

UserWallpaperDelegate* TestShellDelegate::CreateUserWallpaperDelegate() {
  return NULL;
}

aura::client::UserActionClient* TestShellDelegate::CreateUserActionClient() {
  return NULL;
}

void TestShellDelegate::OpenFeedbackPage() {
}

void TestShellDelegate::RecordUserMetricsAction(UserMetricsAction action) {
}

}  // namespace test
}  // namespace ash
