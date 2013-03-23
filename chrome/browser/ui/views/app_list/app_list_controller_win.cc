// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sstream>

#include "base/command_line.h"
#include "base/file_util.h"
#include "base/lazy_instance.h"
#include "base/path_service.h"
#include "base/time.h"
#include "base/timer.h"
#include "base/utf_string_conversions.h"
#include "base/win/shortcut.h"
#include "chrome/app/chrome_dll_resource.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/extensions/extension_prefs.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/shell_integration.h"
#include "chrome/browser/ui/app_list/app_list_controller.h"
#include "chrome/browser/ui/app_list/app_list_view_delegate.h"
#include "chrome/browser/ui/extensions/application_launch.h"
#include "chrome/browser/ui/views/browser_dialogs.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/installer/launcher_support/chrome_launcher_support.h"
#include "chrome/installer/util/util_constants.h"
#include "content/public/browser/browser_thread.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/google_chrome_strings.h"
#include "ui/app_list/app_list_view.h"
#include "ui/app_list/pagination_model.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/win/shell.h"
#include "ui/gfx/display.h"
#include "ui/gfx/screen.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/widget/widget.h"

namespace {

// Offset from the cursor to the point of the bubble arrow. It looks weird
// if the arrow comes up right on top of the cursor, so it is offset by this
// amount.
static const int kAnchorOffset = 25;

// Icons are added to the resources of the DLL using icon names. The icon index
// for the app list icon is named IDR_X_APP_LIST. Creating shortcuts needs to
// specify a resource index, which are different to icon names.  They are 0
// based and contiguous. As Google Chrome builds have extra icons the icon for
// Google Chrome builds need to be higher. Unfortunately these indexes are not
// in any generated header file.
#if defined(GOOGLE_CHROME_BUILD)
const int kAppListIconIndex = 5;
#else
const int kAppListIconIndex = 1;
#endif

CommandLine GetAppListCommandLine() {
  const char* const kSwitchesToCopy[] = { switches::kUserDataDir };
  CommandLine* current = CommandLine::ForCurrentProcess();
  CommandLine command_line(current->GetProgram());
  command_line.CopySwitchesFrom(*current, kSwitchesToCopy,
                                arraysize(kSwitchesToCopy));
  command_line.AppendSwitch(switches::kShowAppList);
  return command_line;
}

string16 GetAppModelId() {
  // The AppModelId should be the same for all profiles in a user data directory
  // but different for different user data directories, so base it on the
  // initial profile in the current user data directory.
  FilePath initial_profile_path;
  CommandLine* command_line = CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kUserDataDir)) {
    initial_profile_path =
        command_line->GetSwitchValuePath(switches::kUserDataDir).AppendASCII(
            chrome::kInitialProfile);
  }
  return ShellIntegration::GetAppListAppModelIdForProfile(initial_profile_path);
}

class AppListControllerDelegateWin : public AppListControllerDelegate {
 public:
  AppListControllerDelegateWin();
  virtual ~AppListControllerDelegateWin();

 private:
  // AppListController overrides:
  virtual void DismissView() OVERRIDE;
  virtual void ViewClosing() OVERRIDE;
  virtual void ViewActivationChanged(bool active) OVERRIDE;
  virtual bool CanPin() OVERRIDE;
  virtual void AboutToUninstallApp() OVERRIDE;
  virtual void UninstallAppCompleted() OVERRIDE;
  virtual bool CanShowCreateShortcutsDialog() OVERRIDE;
  virtual void ShowCreateShortcutsDialog(
      Profile* profile,
      const std::string& extension_id) OVERRIDE;
  virtual void ActivateApp(Profile* profile,
                           const std::string& extension_id,
                           int event_flags) OVERRIDE;
  virtual void LaunchApp(Profile* profile,
                         const std::string& extension_id,
                         int event_flags) OVERRIDE;

  DISALLOW_COPY_AND_ASSIGN(AppListControllerDelegateWin);
};

// The AppListController class manages global resources needed for the app
// list to operate, and controls when the app list is opened and closed.
class AppListController {
 public:
  AppListController()
      : current_view_(NULL),
        can_close_app_list_(true),
        app_list_is_showing_(false) {}
  ~AppListController() {}

  void set_can_close(bool can_close) { can_close_app_list_ = can_close; }
  bool can_close() { return can_close_app_list_; }
  void CreateAppList();
  void ShowAppList();
  void DismissAppList();
  void AppListClosing();
  void AppListActivationChanged(bool active);
  app_list::AppListView* GetView() { return current_view_; }

 private:
  // Utility methods for showing the app list.
  void GetArrowLocationAndUpdateAnchor(
      const gfx::Display& display,
      int min_space_x,
      int min_space_y,
      views::BubbleBorder::ArrowLocation* arrow,
      gfx::Point* anchor);
  void UpdateArrowPositionAndAnchorPoint(const gfx::Point& cursor);
  string16 GetAppListIconPath();

  // Check if the app list or the taskbar has focus. The app list is kept
  // visible whenever either of these have focus, which allows it to be
  // pinned but will hide it if it otherwise loses focus. This is checked
  // periodically whenever the app list does not have focus.
  void CheckTaskbarOrViewHasFocus();

  // Weak pointer. The view manages its own lifetime.
  app_list::AppListView* current_view_;

  // Timer used to check if the taskbar or app list is active. Using a timer
  // means we don't need to hook Windows, which is apparently not possible
  // since Vista (and is not nice at any time).
  base::RepeatingTimer<AppListController> timer_;

  app_list::PaginationModel pagination_model_;

  // True if the controller can close the app list.
  bool can_close_app_list_;

  // True if the app list is showing. Used to ensure we only ever have 0 or 1
  // browser process keep-alives active.
  bool app_list_is_showing_;

  DISALLOW_COPY_AND_ASSIGN(AppListController);
};

base::LazyInstance<AppListController>::Leaky g_app_list_controller =
    LAZY_INSTANCE_INITIALIZER;

AppListControllerDelegateWin::AppListControllerDelegateWin() {}

AppListControllerDelegateWin::~AppListControllerDelegateWin() {}

void AppListControllerDelegateWin::DismissView() {
  g_app_list_controller.Get().DismissAppList();
}

void AppListControllerDelegateWin::ViewActivationChanged(bool active) {
  g_app_list_controller.Get().AppListActivationChanged(active);
}

void AppListControllerDelegateWin::ViewClosing() {
  g_app_list_controller.Get().AppListClosing();
}

bool AppListControllerDelegateWin::CanPin() {
  return false;
}

void AppListControllerDelegateWin::AboutToUninstallApp() {
  g_app_list_controller.Get().set_can_close(false);
}

void AppListControllerDelegateWin::UninstallAppCompleted() {
  g_app_list_controller.Get().set_can_close(true);
}

bool AppListControllerDelegateWin::CanShowCreateShortcutsDialog() {
  return true;
}

void AppListControllerDelegateWin::ShowCreateShortcutsDialog(
    Profile* profile,
    const std::string& extension_id) {
  ExtensionService* service = profile->GetExtensionService();
  DCHECK(service);
  const extensions::Extension* extension = service->GetInstalledExtension(
      extension_id);
  DCHECK(extension);

  app_list::AppListView* view = g_app_list_controller.Get().GetView();
  if (!view)
    return;

  gfx::NativeWindow parent_hwnd =
      view->GetWidget()->GetTopLevelWidget()->GetNativeWindow();
  chrome::ShowCreateChromeAppShortcutsDialog(parent_hwnd, profile, extension);
}

void AppListControllerDelegateWin::ActivateApp(Profile* profile,
                                               const std::string& extension_id,
                                               int event_flags) {
  LaunchApp(profile, extension_id, event_flags);
}

void AppListControllerDelegateWin::LaunchApp(Profile* profile,
                                             const std::string& extension_id,
                                             int event_flags) {
  ExtensionService* service = profile->GetExtensionService();
  DCHECK(service);
  const extensions::Extension* extension = service->GetInstalledExtension(
      extension_id);
  DCHECK(extension);

  // Look up the app preference to find out the right launch container. Default
  // is to launch as a regular tab.
  extension_misc::LaunchContainer launch_container =
      service->extension_prefs()->GetLaunchContainer(extension,
          extensions::ExtensionPrefs::LAUNCH_REGULAR);

  application_launch::LaunchParams params(profile, extension, launch_container,
      NEW_FOREGROUND_TAB);
  application_launch::OpenApplication(params);
}

void AppListController::CreateAppList() {
#if !defined(USE_AURA)
  if (current_view_)
    return;

  // The controller will be owned by the view delegate, and the delegate is
  // owned by the app list view. The app list view manages it's own lifetime.
  current_view_ = new app_list::AppListView(
      new AppListViewDelegate(new AppListControllerDelegateWin()));
  gfx::Point cursor = gfx::Screen::GetNativeScreen()->GetCursorScreenPoint();
  current_view_->InitAsBubble(GetDesktopWindow(),
                              &pagination_model_,
                              NULL,
                              cursor,
                              views::BubbleBorder::BOTTOM_LEFT);

  HWND hwnd =
      current_view_->GetWidget()->GetTopLevelWidget()->GetNativeWindow();
  ui::win::SetAppIdForWindow(GetAppModelId(), hwnd);
  CommandLine relaunch = GetAppListCommandLine();
  string16 app_name(l10n_util::GetStringUTF16(IDS_APP_LIST_SHORTCUT_NAME));
  ui::win::SetRelaunchDetailsForWindow(
      relaunch.GetCommandLineString(), app_name, hwnd);
  ::SetWindowText(hwnd, app_name.c_str());
  string16 icon_path = GetAppListIconPath();
  ui::win::SetAppIconForWindow(icon_path, hwnd);
#endif
}

void AppListController::ShowAppList() {
#if !defined(USE_AURA)
  if (!current_view_)
    CreateAppList();

  if (app_list_is_showing_)
    return;
  app_list_is_showing_ = true;
  browser::StartKeepAlive();
  gfx::Point cursor = gfx::Screen::GetNativeScreen()->GetCursorScreenPoint();
  UpdateArrowPositionAndAnchorPoint(cursor);
  current_view_->Show();
  current_view_->GetWidget()->Activate();
#endif
}

void AppListController::DismissAppList() {
  if (current_view_ && app_list_is_showing_ && can_close_app_list_) {
    current_view_->GetWidget()->Hide();
    timer_.Stop();
    browser::EndKeepAlive();
    app_list_is_showing_ = false;
  }
}

void AppListController::AppListClosing() {
  current_view_ = NULL;
  timer_.Stop();
}

void AppListController::AppListActivationChanged(bool active) {
  const int kFocusCheckIntervalMS = 250;
  if (active) {
    timer_.Stop();
    return;
  }

  timer_.Start(FROM_HERE,
               base::TimeDelta::FromMilliseconds(kFocusCheckIntervalMS), this,
               &AppListController::CheckTaskbarOrViewHasFocus);
}

void AppListController::GetArrowLocationAndUpdateAnchor(
    const gfx::Display& display,
    int min_space_x,
    int min_space_y,
    views::BubbleBorder::ArrowLocation* arrow,
    gfx::Point* anchor) {
  const gfx::Rect& work_area = display.work_area();

  // First ensure anchor is within the work area.
  if (!work_area.Contains(*anchor)) {
    anchor->set_x(std::max(anchor->x(), work_area.x()));
    anchor->set_x(std::min(anchor->x(), work_area.right()));
    anchor->set_y(std::max(anchor->y(), work_area.y()));
    anchor->set_y(std::min(anchor->y(), work_area.bottom()));
  }

  // Only consider bottom and top arrow locations if the taskbar is not on the
  // sides. Otherwise it is easy to end up with the app list coming up under the
  // taskbar.
  if (work_area.width() == display.size().width()) {
    // Prefer the bottom as it is the most natural position.
    if (anchor->y() - work_area.y() >= min_space_y) {
      *arrow = views::BubbleBorder::BOTTOM_CENTER;
      anchor->Offset(0, -kAnchorOffset);
      return;
    }

    // The view won't fit above the cursor. Will it fit below?
    if (work_area.bottom() - anchor->y() >= min_space_y) {
      *arrow = views::BubbleBorder::TOP_CENTER;
      anchor->Offset(0, kAnchorOffset);
      return;
    }
  }

  // Now try on the right.
  if (work_area.right() - anchor->x() >= min_space_x) {
    *arrow = views::BubbleBorder::LEFT_CENTER;
    anchor->Offset(kAnchorOffset, 0);
    return;
  }

  *arrow = views::BubbleBorder::RIGHT_CENTER;
  anchor->Offset(-kAnchorOffset, 0);
}

void AppListController::UpdateArrowPositionAndAnchorPoint(
    const gfx::Point& cursor) {
  const int kArrowSize = 10;
  const int kPadding = 20;

  gfx::Point anchor(cursor);
  gfx::Size preferred = current_view_->GetPreferredSize();
  // Add the size of the arrow to the space needed, as the preferred size is
  // of the view excluding the arrow.
  int min_space_x = preferred.width() + kAnchorOffset + kPadding + kArrowSize;
  int min_space_y = preferred.height() + kAnchorOffset + kPadding + kArrowSize;

  gfx::Screen* screen =
      gfx::Screen::GetScreenFor(current_view_->GetWidget()->GetNativeView());
  gfx::Display display = screen->GetDisplayNearestPoint(anchor);
  views::BubbleBorder::ArrowLocation arrow;
  GetArrowLocationAndUpdateAnchor(display,
                                  min_space_x,
                                  min_space_y,
                                  &arrow,
                                  &anchor);
  current_view_->SetBubbleArrowLocation(arrow);
  current_view_->SetAnchorPoint(anchor);
}

string16 AppListController::GetAppListIconPath() {
  FilePath icon_path;
  if (!PathService::Get(base::FILE_EXE, &icon_path)) {
    NOTREACHED();
    return string16();
  }

  std::stringstream ss;
  ss << "," << kAppListIconIndex;
  string16 result = icon_path.value();
  result.append(UTF8ToUTF16(ss.str()));
  return result;
}

void AppListController::CheckTaskbarOrViewHasFocus() {
#if !defined(USE_AURA)
  // Don't bother checking if the view has been closed.
  if (!current_view_)
    return;

  // First get the taskbar and jump lists windows (the jump list is the
  // context menu which the taskbar uses).
  HWND jump_list_hwnd = FindWindow(L"DV2ControlHost", NULL);
  HWND taskbar_hwnd = FindWindow(L"Shell_TrayWnd", NULL);
  HWND app_list_hwnd =
      current_view_->GetWidget()->GetTopLevelWidget()->GetNativeWindow();

  // Get the focused window, and check if it is one of these windows. Keep
  // checking it's parent until either we find one of these windows, or there
  // is no parent left.
  HWND focused_hwnd = GetForegroundWindow();
  while (focused_hwnd) {
    if (focused_hwnd == jump_list_hwnd ||
        focused_hwnd == taskbar_hwnd ||
        focused_hwnd == app_list_hwnd) {
      return;
    }
    focused_hwnd = GetParent(focused_hwnd);
  }

  // If we get here, the focused window is not the taskbar, it's context menu,
  // or the app list, so close the app list.
  DismissAppList();
#endif
}

// Check that a taskbar shortcut exists if it should, or does not exist if
// it should not. A taskbar shortcut should exist if the switch
// kShowAppListShortcut is set. The shortcut will be created or deleted in
// |user_data_dir| and will use a Windows Application Model Id of
// |app_model_id|.
// This runs on the FILE thread and not in the blocking IO thread pool as there
// are other tasks running (also on the FILE thread) which fiddle with shortcut
// icons (ShellIntegration::MigrateWin7ShortcutsOnPath). Having different
// threads fiddle with the same shortcuts could cause race issues.
void CheckAppListTaskbarShortcutOnFileThread(const FilePath& user_data_dir,
                                             const string16& app_model_id) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::FILE));

  const string16 shortcut_name = l10n_util::GetStringUTF16(
      IDS_APP_LIST_SHORTCUT_NAME);
  const FilePath shortcut_path(user_data_dir.Append(shortcut_name)
      .AddExtension(installer::kLnkExt));
  const bool should_show =
      CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kShowAppListShortcut) ||
      chrome_launcher_support::IsAppLauncherPresent();

  // This will not reshow a shortcut if it has been unpinned manually by the
  // user, as that will not delete the shortcut file.
  if (should_show && !file_util::PathExists(shortcut_path)) {
    FilePath chrome_exe;
    if (!PathService::Get(base::FILE_EXE, &chrome_exe)) {
      NOTREACHED();
      return;
    }

    base::win::ShortcutProperties shortcut_properties;
    shortcut_properties.set_target(chrome_exe);
    shortcut_properties.set_working_dir(chrome_exe.DirName());

    string16 wide_switches(GetAppListCommandLine().GetArgumentsString());
    shortcut_properties.set_arguments(wide_switches);
    shortcut_properties.set_description(shortcut_name);

    shortcut_properties.set_icon(chrome_exe, kAppListIconIndex);
    shortcut_properties.set_app_id(app_model_id);

    base::win::CreateOrUpdateShortcutLink(shortcut_path, shortcut_properties,
                                          base::win::SHORTCUT_CREATE_ALWAYS);
    base::win::TaskbarPinShortcutLink(shortcut_path.value().c_str());
    return;
  }

  if (!should_show && file_util::PathExists(shortcut_path)) {
    base::win::TaskbarUnpinShortcutLink(shortcut_path.value().c_str());
    file_util::Delete(shortcut_path, false);
  }
}

}  // namespace

namespace app_list_controller {

void InitAppList() {
  // Check that the presence of the app list shortcut matches the flag
  // kShowAppListShortcut. This will either create or delete a shortcut
  // file in the user data directory.
  // TODO(benwells): Remove this and the flag once the app list installation
  // is implemented.
  FilePath user_data_dir(g_browser_process->profile_manager()->user_data_dir());
  content::BrowserThread::PostTask(
      content::BrowserThread::FILE, FROM_HERE,
      base::Bind(&CheckAppListTaskbarShortcutOnFileThread, user_data_dir,
                 GetAppModelId()));
}

void ShowAppList() {
  // Create the App list.
  g_app_list_controller.Get().ShowAppList();
}

}  // namespace app_list_controller
