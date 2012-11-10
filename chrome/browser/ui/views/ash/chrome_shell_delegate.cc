// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/ash/chrome_shell_delegate.h"

#include "ash/launcher/launcher_types.h"
#include "ash/system/tray/system_tray_delegate.h"
#include "ash/wm/window_util.h"
#include "base/command_line.h"
#include "chrome/browser/chromeos/login/screen_locker.h"
#include "chrome/browser/extensions/api/terminal/terminal_extension_helper.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/sessions/tab_restore_service.h"
#include "chrome/browser/sessions/tab_restore_service_factory.h"
#include "chrome/browser/ui/ash/app_list/app_list_view_delegate.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/views/ash/launcher/chrome_launcher_controller.h"
#include "chrome/browser/ui/views/ash/user_action_handler.h"
#include "chrome/browser/ui/views/ash/window_positioner.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/webui/chrome_web_contents_handler.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/user_metrics.h"
#include "content/public/browser/web_contents.h"
#include "grit/generated_resources.h"
#include "ui/aura/client/user_action_client.h"
#include "ui/aura/window.h"

#if defined(OS_CHROMEOS)
#include "ash/keyboard_overlay/keyboard_overlay_view.h"
#include "base/chromeos/chromeos_version.h"
#include "chrome/browser/chromeos/accessibility/accessibility_util.h"
#include "chrome/browser/chromeos/background/ash_user_wallpaper_delegate.h"
#include "chrome/browser/chromeos/extensions/file_manager_util.h"
#include "chrome/browser/chromeos/kiosk_mode/kiosk_mode_settings.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/login/webui_login_display_host.h"
#include "chrome/browser/chromeos/system/ash_system_tray_delegate.h"
#include "chrome/browser/ui/webui/chromeos/login/oobe_ui.h"
#include "chrome/browser/ui/webui/chromeos/mobile_setup_dialog.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/power_manager_client.h"
#include "chromeos/dbus/session_manager_client.h"
#endif

namespace {

// Returns the browser that should handle accelerators.
Browser* GetTargetBrowser() {
  Browser* browser = browser::FindBrowserWithWindow(ash::wm::GetActiveWindow());
  if (browser)
    return browser;
  return browser::FindOrCreateTabbedBrowser(
      ProfileManager::GetDefaultProfileOrOffTheRecord());
}

}  // namespace

// static
ChromeShellDelegate* ChromeShellDelegate::instance_ = NULL;

ChromeShellDelegate::ChromeShellDelegate()
    : window_positioner_(new WindowPositioner()),
      ALLOW_THIS_IN_INITIALIZER_LIST(weak_factory_(this)) {
  instance_ = this;
#if defined(OS_CHROMEOS)
  registrar_.Add(
      this,
      chrome::NOTIFICATION_LOGIN_USER_PROFILE_PREPARED,
      content::NotificationService::AllSources());
  registrar_.Add(
      this,
      chrome::NOTIFICATION_SESSION_STARTED,
      content::NotificationService::AllSources());
#endif
}

ChromeShellDelegate::~ChromeShellDelegate() {
  if (instance_ == this)
    instance_ = NULL;
}

bool ChromeShellDelegate::IsUserLoggedIn() {
#if defined(OS_CHROMEOS)
  // When running a Chrome OS build outside of a device (i.e. on a developer's
  // workstation) and not running as login-manager, pretend like we're always
  // logged in.
  if (!base::chromeos::IsRunningOnChromeOS() &&
      !CommandLine::ForCurrentProcess()->HasSwitch(switches::kLoginManager)) {
    return true;
  }

  return chromeos::UserManager::Get()->IsUserLoggedIn();
#else
  return true;
#endif
}

  // Returns true if we're logged in and browser has been started
bool ChromeShellDelegate::IsSessionStarted() {
#if defined(OS_CHROMEOS)
  return chromeos::UserManager::Get()->IsSessionStarted();
#else
  return true;
#endif
}

void ChromeShellDelegate::LockScreen() {
#if defined(OS_CHROMEOS)
  if (!CommandLine::ForCurrentProcess()->HasSwitch(switches::kGuestSession) &&
      !chromeos::KioskModeSettings::Get()->IsKioskModeEnabled()) {
    chromeos::DBusThreadManager::Get()->GetSessionManagerClient()->
        RequestLockScreen();
  }
#endif
}

void ChromeShellDelegate::UnlockScreen() {
  // This is used only for testing thus far.
  NOTIMPLEMENTED();
}

bool ChromeShellDelegate::IsScreenLocked() const {
#if defined(OS_CHROMEOS)
  if (!chromeos::ScreenLocker::default_screen_locker())
    return false;
  return chromeos::ScreenLocker::default_screen_locker()->locked();
#else
  return false;
#endif
}

void ChromeShellDelegate::Shutdown() {
#if defined(OS_CHROMEOS)
  content::RecordAction(content::UserMetricsAction("Shutdown"));
  chromeos::DBusThreadManager::Get()->GetPowerManagerClient()->
      RequestShutdown();
#endif
}

void ChromeShellDelegate::Exit() {
  browser::AttemptUserExit();
}

void ChromeShellDelegate::NewTab() {
  Browser* browser = GetTargetBrowser();
  // If the browser was not active, we call BrowserWindow::Show to make it
  // visible. Otherwise, we let Browser::NewTab handle the active window change.
  const bool was_active = browser->window()->IsActive();
  chrome::NewTab(browser);
  if (!was_active)
    browser->window()->Show();
}

void ChromeShellDelegate::NewWindow(bool is_incognito) {
  Profile* profile = ProfileManager::GetDefaultProfileOrOffTheRecord();
  chrome::NewEmptyWindow(
      is_incognito ? profile->GetOffTheRecordProfile() : profile);
}

void ChromeShellDelegate::OpenFileManager(bool as_dialog) {
#if defined(OS_CHROMEOS)
  if (as_dialog) {
    Browser* browser =
        browser::FindBrowserWithWindow(ash::wm::GetActiveWindow());
    // Open the select file dialog only if there is an active browser where the
    // selected file is displayed. Otherwise open a file manager in a tab.
    if (browser) {
      browser->OpenFile();
      return;
    }
  }
  file_manager_util::OpenApplication();
#endif
}

void ChromeShellDelegate::OpenCrosh() {
#if defined(OS_CHROMEOS)
  Browser* browser = GetTargetBrowser();
  GURL crosh_url = TerminalExtensionHelper::GetCroshExtensionURL(
      browser->profile());
  if (!crosh_url.is_valid())
    return;
  browser->OpenURL(
      content::OpenURLParams(crosh_url,
                             content::Referrer(),
                             NEW_FOREGROUND_TAB,
                             content::PAGE_TRANSITION_GENERATED,
                             false));
#endif
}

void ChromeShellDelegate::OpenMobileSetup(const std::string& service_path) {
#if defined(OS_CHROMEOS)
  MobileSetupDialog::Show(service_path);
#endif
}

void ChromeShellDelegate::RestoreTab() {
  Browser* browser = GetTargetBrowser();
  // Do not restore tabs while in the incognito mode.
  if (browser->profile()->IsOffTheRecord())
    return;
  TabRestoreService* service =
      TabRestoreServiceFactory::GetForProfile(browser->profile());
  if (!service)
    return;
  if (service->IsLoaded()) {
    chrome::RestoreTab(browser);
  } else {
    service->LoadTabsFromLastSession();
    // LoadTabsFromLastSession is asynchronous, so TabRestoreService has not
    // finished loading the entries at this point. Wait for next event cycle
    // which loads the restored tab entries.
    MessageLoop::current()->PostTask(
        FROM_HERE,
        base::Bind(&ChromeShellDelegate::RestoreTab,
                   weak_factory_.GetWeakPtr()));
  }
}

bool ChromeShellDelegate::RotatePaneFocus(ash::Shell::Direction direction) {
  aura::Window* window = ash::wm::GetActiveWindow();
  if (!window)
    return false;

  Browser* browser = browser::FindBrowserWithWindow(window);
  if (!browser)
    return false;

  switch (direction) {
    case ash::Shell::FORWARD:
      chrome::FocusNextPane(browser);
      break;
    case ash::Shell::BACKWARD:
      chrome::FocusPreviousPane(browser);
      break;
  }
  return true;
}

void ChromeShellDelegate::ShowKeyboardOverlay() {
#if defined(OS_CHROMEOS)
  // TODO(mazda): Move the show logic to ash (http://crbug.com/124222).
  Profile* profile = ProfileManager::GetDefaultProfileOrOffTheRecord();
  std::string url(chrome::kChromeUIKeyboardOverlayURL);
  KeyboardOverlayView::ShowDialog(profile,
                                  new ChromeWebContentsHandler,
                                  GURL(url));
#endif
}

void ChromeShellDelegate::ShowTaskManager() {
  Browser* browser = browser::FindOrCreateTabbedBrowser(
      ProfileManager::GetDefaultProfileOrOffTheRecord());
  chrome::OpenTaskManager(browser, false);
}

content::BrowserContext* ChromeShellDelegate::GetCurrentBrowserContext() {
  return ProfileManager::GetDefaultProfile();
}

void ChromeShellDelegate::ToggleSpokenFeedback() {
#if defined(OS_CHROMEOS)
  content::WebUI* login_screen_web_ui = NULL;
  chromeos::WebUILoginDisplayHost* host =
      static_cast<chromeos::WebUILoginDisplayHost*>(
          chromeos::BaseLoginDisplayHost::default_host());
  if (host && host->GetOobeUI())
    login_screen_web_ui = host->GetOobeUI()->web_ui();
  chromeos::accessibility::ToggleSpokenFeedback(login_screen_web_ui);
#endif
}

bool ChromeShellDelegate::IsSpokenFeedbackEnabled() const {
#if defined(OS_CHROMEOS)
  return chromeos::accessibility::IsSpokenFeedbackEnabled();
#else
  return false;
#endif
}

app_list::AppListViewDelegate*
    ChromeShellDelegate::CreateAppListViewDelegate() {
  // Shell will own the created delegate.
  return new AppListViewDelegate;
}

ash::LauncherDelegate* ChromeShellDelegate::CreateLauncherDelegate(
    ash::LauncherModel* model) {
  ChromeLauncherController* controller =
      new ChromeLauncherController(NULL, model);
  controller->Init();
  return controller;
}

ash::SystemTrayDelegate* ChromeShellDelegate::CreateSystemTrayDelegate(
    ash::SystemTray* tray) {
#if defined(OS_CHROMEOS)
  return chromeos::CreateSystemTrayDelegate(tray);
#else
  return NULL;
#endif
}

ash::UserWallpaperDelegate* ChromeShellDelegate::CreateUserWallpaperDelegate() {
#if defined(OS_CHROMEOS)
  return chromeos::CreateUserWallpaperDelegate();
#else
  return NULL;
#endif
}

aura::client::UserActionClient* ChromeShellDelegate::CreateUserActionClient() {
  return new UserActionHandler;
}

void ChromeShellDelegate::OpenFeedbackPage() {
  chrome::OpenFeedbackDialog(GetTargetBrowser());
}

void ChromeShellDelegate::RecordUserMetricsAction(
    ash::UserMetricsAction action) {
  switch (action) {
    case ash::UMA_ACCEL_PREVWINDOW_TAB:
      content::RecordAction(content::UserMetricsAction("Accel_PrevWindow_Tab"));
      break;
    case ash::UMA_ACCEL_NEXTWINDOW_TAB:
      content::RecordAction(content::UserMetricsAction("Accel_NextWindow_Tab"));
      break;
    case ash::UMA_ACCEL_PREVWINDOW_F5:
      content::RecordAction(content::UserMetricsAction("Accel_PrevWindow_F5"));
      break;
    case ash::UMA_ACCEL_NEXTWINDOW_F5:
      content::RecordAction(content::UserMetricsAction("Accel_NextWindow_F5"));
      break;
    case ash::UMA_ACCEL_NEWTAB_T:
      content::RecordAction(content::UserMetricsAction("Accel_NewTab_T"));
      break;
    case ash::UMA_ACCEL_SEARCH_LWIN:
      content::RecordAction(content::UserMetricsAction("Accel_Search_LWin"));
      break;
    case ash::UMA_MOUSE_DOWN:
      content::RecordAction(content::UserMetricsAction("Mouse_Down"));
      break;
    case ash::UMA_TOUCHSCREEN_TAP_DOWN:
      content::RecordAction(content::UserMetricsAction("Touchscreen_Down"));
      break;
  }
}

void ChromeShellDelegate::Observe(int type,
                                  const content::NotificationSource& source,
                                  const content::NotificationDetails& details) {
#if defined(OS_CHROMEOS)
  switch (type) {
    case chrome::NOTIFICATION_LOGIN_USER_PROFILE_PREPARED:
      ash::Shell::GetInstance()->CreateLauncher();
      break;
    case chrome::NOTIFICATION_SESSION_STARTED:
      ash::Shell::GetInstance()->ShowLauncher();
      break;
    default:
      NOTREACHED() << "Unexpected notification " << type;
  }
#else
  // MSVC++ warns about switch statements without any cases.
  NOTREACHED() << "Unexpected notification " << type;
#endif
}
