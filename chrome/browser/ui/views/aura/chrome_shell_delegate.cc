// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/aura/chrome_shell_delegate.h"

#include "ash/launcher/launcher_types.h"
#include "ash/wm/window_util.h"
#include "base/command_line.h"
#include "chrome/browser/defaults.h"
#include "chrome/browser/prefs/incognito_mode_prefs.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/views/aura/app_list/app_list_model_builder.h"
#include "chrome/browser/ui/views/aura/app_list/app_list_view_delegate.h"
#include "chrome/browser/ui/views/aura/launcher_icon_updater.h"
#include "chrome/browser/ui/views/aura/status_area_host_aura.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "grit/theme_resources.h"
#include "ui/aura/window.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/dbus/dbus_thread_manager.h"
#include "chrome/browser/chromeos/dbus/power_manager_client.h"
#endif
namespace {

// Returns a list of Aura windows from a BrowserList, using either a
// const_iterator or const_reverse_iterator.
template<typename IT>
std::vector<aura::Window*> GetTabbedBrowserWindows(IT begin, IT end) {
  std::vector<aura::Window*> windows;
  for (IT it = begin; it != end; ++it) {
    Browser* browser = *it;
    if (browser &&
        browser->is_type_tabbed() &&
        browser->window()->GetNativeHandle())
      windows.push_back(browser->window()->GetNativeHandle());
  }
  return windows;
}

}  // namespace

// static
ChromeShellDelegate* ChromeShellDelegate::instance_ = NULL;

ChromeShellDelegate::ChromeShellDelegate() {
  instance_ = this;
}

ChromeShellDelegate::~ChromeShellDelegate() {
  if (instance_ == this)
    instance_ = NULL;
}

StatusAreaView* ChromeShellDelegate::GetStatusArea() {
  return status_area_host_->GetStatusArea();
}

views::Widget* ChromeShellDelegate::CreateStatusArea() {
  status_area_host_.reset(new StatusAreaHostAura());
  views::Widget* status_area_widget =
      status_area_host_.get()->CreateStatusArea();
  return status_area_widget;
}

#if defined(OS_CHROMEOS)
void ChromeShellDelegate::LockScreen() {
  chromeos::DBusThreadManager::Get()->GetPowerManagerClient()->
      NotifyScreenLockRequested();
}
#endif

void ChromeShellDelegate::Exit() {
  BrowserList::AttemptUserExit();
}

void ChromeShellDelegate::BuildAppListModel(ash::AppListModel* model) {
  AppListModelBuilder builder(ProfileManager::GetDefaultProfile(),
                              model);
  builder.Build();
}

ash::AppListViewDelegate*
ChromeShellDelegate::CreateAppListViewDelegate() {
  // Shell will own the created delegate.
  return new AppListViewDelegate;
}

std::vector<aura::Window*> ChromeShellDelegate::GetCycleWindowList(
    CycleSource source,
    CycleOrder order) const {
  std::vector<aura::Window*> windows;
  switch (order) {
    case ORDER_MRU:
      // BrowserList maintains a list of browsers sorted by activity.
      windows = GetTabbedBrowserWindows(BrowserList::begin_last_active(),
                                        BrowserList::end_last_active());
      break;
    case ORDER_LINEAR:
      // Just return windows in creation order.
      windows = GetTabbedBrowserWindows(BrowserList::begin(),
                                        BrowserList::end());
      break;
    default:
      NOTREACHED();
      break;
  }
  return windows;
}

void ChromeShellDelegate::CreateNewWindow() {
  Profile* profile = ProfileManager::GetDefaultProfile();
  if (browser_defaults::kAlwaysOpenIncognitoWindow &&
      IncognitoModePrefs::ShouldLaunchIncognito(
          *CommandLine::ForCurrentProcess(),
          profile->GetPrefs())) {
    profile = profile->GetOffTheRecordProfile();
  }
  Browser::OpenEmptyWindow(profile);
}

void ChromeShellDelegate::LauncherItemClicked(
    const ash::LauncherItem& item) {
  LauncherIconUpdater::ActivateByID(item.id);
}

int ChromeShellDelegate::GetBrowserShortcutResourceId() {
  return IDR_PRODUCT_LOGO_32;
}

string16 ChromeShellDelegate::GetLauncherItemTitle(
    const ash::LauncherItem& item) {
  return LauncherIconUpdater::GetTitleByID(item.id);
}
