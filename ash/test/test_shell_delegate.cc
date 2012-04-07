// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/test/test_shell_delegate.h"

#include <algorithm>

#include "ash/shell.h"
#include "ash/shell_window_ids.h"
#include "grit/ui_resources.h"
#include "ui/aura/window.h"

namespace ash {
namespace test {

TestShellDelegate::TestShellDelegate() {
}

TestShellDelegate::~TestShellDelegate() {
}

views::Widget* TestShellDelegate::CreateStatusArea() {
  return NULL;
}

#if defined(OS_CHROMEOS)
void TestShellDelegate::LockScreen() {
}
#endif

void TestShellDelegate::Exit() {
}

void TestShellDelegate::BuildAppListModel(AppListModel* model) {
}

AppListViewDelegate* TestShellDelegate::CreateAppListViewDelegate() {
  return NULL;
}

std::vector<aura::Window*> TestShellDelegate::GetCycleWindowList(
    CycleSource source,
    CycleOrder order) const {
  // We just use the Shell's default container of windows, so tests can be
  // written with the usual CreateTestWindowWithId() calls. But window cycling
  // expects the topmost window at the front of the list, so reverse the order.
  aura::Window* default_container = Shell::GetInstance()->GetContainer(
      internal::kShellWindowId_DefaultContainer);
  std::vector<aura::Window*> windows = default_container->children();
  std::reverse(windows.begin(), windows.end());
  return windows;
}

void TestShellDelegate::CreateNewWindow() {
}

void TestShellDelegate::LauncherItemClicked(const LauncherItem& item) {
}

int TestShellDelegate::GetBrowserShortcutResourceId() {
  return IDR_AURA_LAUNCHER_BROWSER_SHORTCUT;
}

string16 TestShellDelegate::GetLauncherItemTitle(const LauncherItem& item) {
  return string16();
}

}  // namespace test
}  // namespace ash
