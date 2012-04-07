// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/browser_init.h"

#include <algorithm>   // For max().

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/compiler_specific.h"
#include "base/environment.h"
#include "base/event_recorder.h"
#include "base/file_path.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/metrics/histogram.h"
#include "base/path_service.h"
#include "base/string_number_conversions.h"
#include "base/string_split.h"
#include "base/threading/thread_restrictions.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/auto_launch_trial.h"
#include "chrome/browser/automation/automation_provider.h"
#include "chrome/browser/automation/automation_provider_list.h"
#include "chrome/browser/automation/chrome_frame_automation_provider.h"
#include "chrome/browser/automation/testing_automation_provider.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/component_updater/component_updater_service.h"
#include "chrome/browser/component_updater/flash_component_installer.h"
#include "chrome/browser/component_updater/pnacl/pnacl_component_installer.h"
#include "chrome/browser/component_updater/recovery_component_installer.h"
#include "chrome/browser/component_updater/swiftshader_component_installer.h"
#include "chrome/browser/custom_handlers/protocol_handler_registry.h"
#include "chrome/browser/defaults.h"
#include "chrome/browser/extensions/extension_creator.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/pack_extension_job.h"
#include "chrome/browser/first_run/first_run.h"
#include "chrome/browser/google/google_util.h"
#include "chrome/browser/infobars/infobar_tab_helper.h"
#include "chrome/browser/net/crl_set_fetcher.h"
#include "chrome/browser/net/predictor.h"
#include "chrome/browser/net/url_fixer_upper.h"
#include "chrome/browser/notifications/desktop_notification_service.h"
#include "chrome/browser/prefs/incognito_mode_prefs.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/printing/cloud_print/cloud_print_proxy_service.h"
#include "chrome/browser/printing/cloud_print/cloud_print_proxy_service_factory.h"
#include "chrome/browser/printing/print_dialog_cloud.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_io_data.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/search_engines/template_url.h"
#include "chrome/browser/search_engines/template_url_service.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/shell_integration.h"
#include "chrome/browser/tab_contents/link_infobar_delegate.h"
#include "chrome/browser/tab_contents/simple_alert_infobar_delegate.h"
#include "chrome/browser/tabs/pinned_tab_codec.h"
#include "chrome/browser/tabs/tab_strip_model.h"
#include "chrome/browser/ui/browser_dialogs.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/dialog_style.h"
#include "chrome/browser/ui/tab_contents/tab_contents_wrapper.h"
#include "chrome/browser/ui/webui/sync_promo/sync_promo_dialog.h"
#include "chrome/browser/ui/webui/sync_promo/sync_promo_trial.h"
#include "chrome/browser/ui/webui/sync_promo/sync_promo_ui.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_result_codes.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/chrome_version_info.h"
#include "chrome/common/extensions/extension_constants.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "chrome/installer/util/browser_distribution.h"
#include "content/browser/child_process_security_policy.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_details.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/locale_settings.h"
#include "grit/theme_resources.h"
#include "grit/theme_resources_standard.h"
#include "net/base/net_util.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"

#if defined(OS_MACOSX)
#include "base/mac/mac_util.h"
#include "chrome/browser/ui/cocoa/keystone_infobar_delegate.h"
#endif

#if defined(TOOLKIT_USES_GTK)
#include "chrome/browser/ui/gtk/gtk_util.h"
#endif

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/cros/cros_library.h"
#include "chrome/browser/chromeos/cros/network_library.h"
#include "chrome/browser/chromeos/customization_document.h"
#include "chrome/browser/chromeos/dbus/dbus_thread_manager.h"
#include "chrome/browser/chromeos/enterprise_extension_observer.h"
#include "chrome/browser/chromeos/gview_request_interceptor.h"
#include "chrome/browser/chromeos/network_message_observer.h"
#include "chrome/browser/chromeos/power/low_battery_observer.h"
#include "chrome/browser/chromeos/sms_observer.h"
#if defined(TOOLKIT_USES_GTK)
#include "chrome/browser/chromeos/legacy_window_manager/wm_message_listener.h"
#endif
#endif

#if defined(TOOLKIT_VIEWS) && defined(OS_LINUX)
#include "ui/base/touch/touch_factory.h"
#endif

#if defined(OS_WIN)
#include "chrome/installer/util/auto_launch_util.h"
#endif

using content::BrowserThread;
using content::OpenURLParams;
using content::Referrer;
using content::WebContents;

namespace {

static const int kMaxInfobarShown = 5;

#if defined(OS_WIN)
// The delegate for the infobar shown when Chrome was auto-launched.
class AutolaunchInfoBarDelegate : public ConfirmInfoBarDelegate {
 public:
  AutolaunchInfoBarDelegate(InfoBarTabHelper* infobar_helper,
                            PrefService* prefs);
  virtual ~AutolaunchInfoBarDelegate();

 private:
  void AllowExpiry() { should_expire_ = true; }

  // ConfirmInfoBarDelegate:
  virtual bool ShouldExpire(
      const content::LoadCommittedDetails& details) const OVERRIDE;
  virtual gfx::Image* GetIcon() const OVERRIDE;
  virtual string16 GetMessageText() const OVERRIDE;
  virtual string16 GetButtonLabel(InfoBarButton button) const OVERRIDE;
  virtual bool Accept() OVERRIDE;
  virtual bool Cancel() OVERRIDE;

  // The prefs to use.
  PrefService* prefs_;

  // Whether the user clicked one of the buttons.
  bool action_taken_;

  // Whether the info-bar should be dismissed on the next navigation.
  bool should_expire_;

  // Used to delay the expiration of the info-bar.
  base::WeakPtrFactory<AutolaunchInfoBarDelegate> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(AutolaunchInfoBarDelegate);
};

AutolaunchInfoBarDelegate::AutolaunchInfoBarDelegate(
    InfoBarTabHelper* infobar_helper,
    PrefService* prefs)
    : ConfirmInfoBarDelegate(infobar_helper),
      prefs_(prefs),
      action_taken_(false),
      should_expire_(false),
      ALLOW_THIS_IN_INITIALIZER_LIST(weak_factory_(this)) {
  auto_launch_trial::UpdateInfobarShownMetric();

  int count = prefs_->GetInteger(prefs::kShownAutoLaunchInfobar);
  prefs_->SetInteger(prefs::kShownAutoLaunchInfobar, count + 1);

  // We want the info-bar to stick-around for a few seconds and then be hidden
  // on the next navigation after that.
  MessageLoop::current()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&AutolaunchInfoBarDelegate::AllowExpiry,
                 weak_factory_.GetWeakPtr()),
      base::TimeDelta::FromSeconds(8));
}

AutolaunchInfoBarDelegate::~AutolaunchInfoBarDelegate() {
  if (!action_taken_) {
    auto_launch_trial::UpdateInfobarResponseMetric(
        auto_launch_trial::INFOBAR_IGNORE);
  }
}

bool AutolaunchInfoBarDelegate::ShouldExpire(
    const content::LoadCommittedDetails& details) const {
  return details.is_navigation_to_different_page() && should_expire_;
}

gfx::Image* AutolaunchInfoBarDelegate::GetIcon() const {
  return &ResourceBundle::GetSharedInstance().GetNativeImageNamed(
      IDR_PRODUCT_LOGO_32);
}

string16 AutolaunchInfoBarDelegate::GetMessageText() const {
  return l10n_util::GetStringUTF16(IDS_AUTO_LAUNCH_INFOBAR_TEXT);
}

string16 AutolaunchInfoBarDelegate::GetButtonLabel(
    InfoBarButton button) const {
  return l10n_util::GetStringUTF16((button == BUTTON_OK) ?
      IDS_AUTO_LAUNCH_OK : IDS_AUTO_LAUNCH_REVERT);
}

bool AutolaunchInfoBarDelegate::Accept() {
  action_taken_ = true;
  auto_launch_trial::UpdateInfobarResponseMetric(
      auto_launch_trial::INFOBAR_OK);
  return true;
}

bool AutolaunchInfoBarDelegate::Cancel() {
  action_taken_ = true;

  // Track infobar reponse.
  auto_launch_trial::UpdateInfobarResponseMetric(
      auto_launch_trial::INFOBAR_CUT_IT_OUT);
  // Also make sure we keep track of how many disable and how many enable.
  const bool auto_launch = false;
  auto_launch_trial::UpdateToggleAutoLaunchMetric(auto_launch);

  content::BrowserThread::PostTask(
      content::BrowserThread::FILE, FROM_HERE,
      base::Bind(&auto_launch_util::SetWillLaunchAtLogin,
                 auto_launch, FilePath()));
  return true;
}

#endif  // OS_WIN

// DefaultBrowserInfoBarDelegate ----------------------------------------------

// The delegate for the infobar shown when Chrome is not the default browser.
class DefaultBrowserInfoBarDelegate : public ConfirmInfoBarDelegate {
 public:
  explicit DefaultBrowserInfoBarDelegate(InfoBarTabHelper* infobar_helper,
                                         PrefService* prefs);

 private:
  virtual ~DefaultBrowserInfoBarDelegate();

  void AllowExpiry() { should_expire_ = true; }

  // ConfirmInfoBarDelegate:
  virtual bool ShouldExpire(
      const content::LoadCommittedDetails& details) const OVERRIDE;
  virtual gfx::Image* GetIcon() const OVERRIDE;
  virtual string16 GetMessageText() const OVERRIDE;
  virtual string16 GetButtonLabel(InfoBarButton button) const OVERRIDE;
  virtual bool NeedElevation(InfoBarButton button) const OVERRIDE;
  virtual bool Accept() OVERRIDE;
  virtual bool Cancel() OVERRIDE;

  // The prefs to use.
  PrefService* prefs_;

  // Whether the user clicked one of the buttons.
  bool action_taken_;

  // Whether the info-bar should be dismissed on the next navigation.
  bool should_expire_;

  // Used to delay the expiration of the info-bar.
  base::WeakPtrFactory<DefaultBrowserInfoBarDelegate> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(DefaultBrowserInfoBarDelegate);
};

DefaultBrowserInfoBarDelegate::DefaultBrowserInfoBarDelegate(
    InfoBarTabHelper* infobar_helper,
    PrefService* prefs)
    : ConfirmInfoBarDelegate(infobar_helper),
      prefs_(prefs),
      action_taken_(false),
      should_expire_(false),
      ALLOW_THIS_IN_INITIALIZER_LIST(weak_factory_(this)) {
  // We want the info-bar to stick-around for few seconds and then be hidden
  // on the next navigation after that.
  MessageLoop::current()->PostDelayedTask(
      FROM_HERE, base::Bind(&DefaultBrowserInfoBarDelegate::AllowExpiry,
                            weak_factory_.GetWeakPtr()),
      base::TimeDelta::FromSeconds(8));
}

DefaultBrowserInfoBarDelegate::~DefaultBrowserInfoBarDelegate() {
  if (!action_taken_)
    UMA_HISTOGRAM_COUNTS("DefaultBrowserWarning.Ignored", 1);
}

bool DefaultBrowserInfoBarDelegate::ShouldExpire(
    const content::LoadCommittedDetails& details) const {
  return details.is_navigation_to_different_page() && should_expire_;
}

gfx::Image* DefaultBrowserInfoBarDelegate::GetIcon() const {
  return &ResourceBundle::GetSharedInstance().GetNativeImageNamed(
     IDR_PRODUCT_LOGO_32);
}

string16 DefaultBrowserInfoBarDelegate::GetMessageText() const {
  return l10n_util::GetStringUTF16(IDS_DEFAULT_BROWSER_INFOBAR_SHORT_TEXT);
}

string16 DefaultBrowserInfoBarDelegate::GetButtonLabel(
    InfoBarButton button) const {
  return l10n_util::GetStringUTF16((button == BUTTON_OK) ?
      IDS_SET_AS_DEFAULT_INFOBAR_BUTTON_LABEL :
      IDS_DONT_ASK_AGAIN_INFOBAR_BUTTON_LABEL);
}

bool DefaultBrowserInfoBarDelegate::NeedElevation(InfoBarButton button) const {
  return button == BUTTON_OK;
}

bool DefaultBrowserInfoBarDelegate::Accept() {
  action_taken_ = true;
  UMA_HISTOGRAM_COUNTS("DefaultBrowserWarning.SetAsDefault", 1);
  BrowserThread::PostTask(
      BrowserThread::FILE,
      FROM_HERE,
      base::Bind(base::IgnoreResult(&ShellIntegration::SetAsDefaultBrowser)));
  return true;
}

bool DefaultBrowserInfoBarDelegate::Cancel() {
  action_taken_ = true;
  UMA_HISTOGRAM_COUNTS("DefaultBrowserWarning.DontSetAsDefault", 1);
  // User clicked "Don't ask me again", remember that.
  prefs_->SetBoolean(prefs::kCheckDefaultBrowser, false);
  return true;
}

#if defined(OS_WIN)
void CheckAutoLaunchCallback() {
  if (!auto_launch_trial::IsInAutoLaunchGroup())
    return;

  Browser* browser = BrowserList::GetLastActive();
  TabContentsWrapper* tab = browser->GetSelectedTabContentsWrapper();

  // Don't show the info-bar if there are already info-bars showing.
  InfoBarTabHelper* infobar_helper = tab->infobar_tab_helper();
  if (infobar_helper->infobar_count() > 0)
    return;

  infobar_helper->AddInfoBar(
      new AutolaunchInfoBarDelegate(infobar_helper,
      tab->profile()->GetPrefs()));
}
#endif

void NotifyNotDefaultBrowserCallback() {
  Browser* browser = BrowserList::GetLastActive();
  if (!browser)
    return;  // Reached during ui tests.

  // In ChromeBot tests, there might be a race. This line appears to get
  // called during shutdown and |tab| can be NULL.
  TabContentsWrapper* tab = browser->GetSelectedTabContentsWrapper();
  if (!tab)
    return;

  // Don't show the info-bar if there are already info-bars showing.
  InfoBarTabHelper* infobar_helper = tab->infobar_tab_helper();
  if (infobar_helper->infobar_count() > 0)
    return;

  infobar_helper->AddInfoBar(
      new DefaultBrowserInfoBarDelegate(infobar_helper,
                                        tab->profile()->GetPrefs()));
}

void CheckDefaultBrowserCallback() {
  if (ShellIntegration::IsDefaultBrowser() ||
      !ShellIntegration::CanSetAsDefaultBrowser()) {
    return;
  }
  BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
                          base::Bind(&NotifyNotDefaultBrowserCallback));
}


// SessionCrashedInfoBarDelegate ----------------------------------------------

// A delegate for the InfoBar shown when the previous session has crashed.
class SessionCrashedInfoBarDelegate : public ConfirmInfoBarDelegate {
 public:
  SessionCrashedInfoBarDelegate(Profile* profile,
                                InfoBarTabHelper* infobar_helper);

 private:
  virtual ~SessionCrashedInfoBarDelegate();

  // ConfirmInfoBarDelegate:
  virtual gfx::Image* GetIcon() const OVERRIDE;
  virtual string16 GetMessageText() const OVERRIDE;
  virtual int GetButtons() const OVERRIDE;
  virtual string16 GetButtonLabel(InfoBarButton button) const OVERRIDE;
  virtual bool Accept() OVERRIDE;

  // The Profile that we restore sessions from.
  Profile* profile_;

  DISALLOW_COPY_AND_ASSIGN(SessionCrashedInfoBarDelegate);
};

SessionCrashedInfoBarDelegate::SessionCrashedInfoBarDelegate(
    Profile* profile,
    InfoBarTabHelper* infobar_helper)
    : ConfirmInfoBarDelegate(infobar_helper),
      profile_(profile) {
}

SessionCrashedInfoBarDelegate::~SessionCrashedInfoBarDelegate() {
}

gfx::Image* SessionCrashedInfoBarDelegate::GetIcon() const {
  return &ResourceBundle::GetSharedInstance().GetNativeImageNamed(
      IDR_INFOBAR_RESTORE_SESSION);
}

string16 SessionCrashedInfoBarDelegate::GetMessageText() const {
  return l10n_util::GetStringUTF16(IDS_SESSION_CRASHED_VIEW_MESSAGE);
}

int SessionCrashedInfoBarDelegate::GetButtons() const {
  return BUTTON_OK;
}

string16 SessionCrashedInfoBarDelegate::GetButtonLabel(
    InfoBarButton button) const {
  DCHECK_EQ(BUTTON_OK, button);
  return l10n_util::GetStringUTF16(IDS_SESSION_CRASHED_VIEW_RESTORE_BUTTON);
}

bool SessionCrashedInfoBarDelegate::Accept() {
  uint32 behavior = 0;
  Browser* browser = BrowserList::GetLastActiveWithProfile(profile_);
  if (browser && browser->tab_count() == 1
      && browser->GetWebContentsAt(0)->GetURL() ==
      GURL(chrome::kChromeUINewTabURL)) {
    // There is only one tab and its the new tab page, make session restore
    // clobber it.
    behavior = SessionRestore::CLOBBER_CURRENT_TAB;
  }
  SessionRestore::RestoreSession(
      profile_, browser, behavior, std::vector<GURL>());
  return true;
}


// Utility functions ----------------------------------------------------------

enum LaunchMode {
  LM_TO_BE_DECIDED = 0,       // Possibly direct launch or via a shortcut.
  LM_AS_WEBAPP,               // Launched as a installed web application.
  LM_WITH_URLS,               // Launched with urls in the cmd line.
  LM_SHORTCUT_NONE,           // Not launched from a shortcut.
  LM_SHORTCUT_NONAME,         // Launched from shortcut but no name available.
  LM_SHORTCUT_UNKNOWN,        // Launched from user-defined shortcut.
  LM_SHORTCUT_QUICKLAUNCH,    // Launched from the quick launch bar.
  LM_SHORTCUT_DESKTOP,        // Launched from a desktop shortcut.
  LM_SHORTCUT_STARTMENU,      // Launched from start menu.
  LM_LINUX_MAC_BEOS           // Other OS buckets start here.
};

#if defined(OS_WIN)
// Undocumented flag in the startup info structure tells us what shortcut was
// used to launch the browser. See http://www.catch22.net/tuts/undoc01 for
// more information. Confirmed to work on XP, Vista and Win7.
LaunchMode GetLaunchShortcutKind() {
  STARTUPINFOW si = { sizeof(si) };
  GetStartupInfoW(&si);
  if (si.dwFlags & 0x800) {
    if (!si.lpTitle)
      return LM_SHORTCUT_NONAME;
    std::wstring shortcut(si.lpTitle);
    // The windows quick launch path is not localized.
    if (shortcut.find(L"\\Quick Launch\\") != std::wstring::npos)
      return LM_SHORTCUT_QUICKLAUNCH;
    scoped_ptr<base::Environment> env(base::Environment::Create());
    std::string appdata_path;
    env->GetVar("USERPROFILE", &appdata_path);
    if (!appdata_path.empty() &&
        shortcut.find(ASCIIToWide(appdata_path)) != std::wstring::npos)
      return LM_SHORTCUT_DESKTOP;
    return LM_SHORTCUT_UNKNOWN;
  }
  return LM_SHORTCUT_NONE;
}
#else
// TODO(cpu): Port to other platforms.
LaunchMode GetLaunchShortcutKind() {
  return LM_LINUX_MAC_BEOS;
}
#endif

// Log in a histogram the frequency of launching by the different methods. See
// LaunchMode enum for the actual values of the buckets.
void RecordLaunchModeHistogram(LaunchMode mode) {
  int bucket = (mode == LM_TO_BE_DECIDED) ? GetLaunchShortcutKind() : mode;
  UMA_HISTOGRAM_COUNTS_100("Launch.Modes", bucket);
}

static bool in_startup = false;

GURL GetWelcomePageURL() {
  std::string welcome_url = l10n_util::GetStringUTF8(IDS_WELCOME_PAGE_URL);
  return GURL(welcome_url);
}

void UrlsToTabs(const std::vector<GURL>& urls,
                std::vector<BrowserInit::LaunchWithProfile::Tab>* tabs) {
  for (size_t i = 0; i < urls.size(); ++i) {
    BrowserInit::LaunchWithProfile::Tab tab;
    tab.is_pinned = false;
    tab.url = urls[i];
    tabs->push_back(tab);
  }
}

// Return true if the command line option --app-id is used.  Set
// |out_extension| to the app to open, and |out_launch_container|
// to the type of window into which the app should be open.
bool GetAppLaunchContainer(
    Profile* profile,
    const std::string& app_id,
    const Extension** out_extension,
    extension_misc::LaunchContainer* out_launch_container) {

  ExtensionService* extensions_service = profile->GetExtensionService();
  const Extension* extension =
      extensions_service->GetExtensionById(app_id, false);

  // The extension with id |app_id| may have been uninstalled.
  if (!extension)
    return false;

  // Look at preferences to find the right launch container.  If no
  // preference is set, launch as a window.
  extension_misc::LaunchContainer launch_container =
      extensions_service->extension_prefs()->GetLaunchContainer(
          extension, ExtensionPrefs::LAUNCH_WINDOW);

  *out_extension = extension;
  *out_launch_container = launch_container;
  return true;
}

void RecordCmdLineAppHistogram() {
  UMA_HISTOGRAM_ENUMERATION(extension_misc::kAppLaunchHistogram,
                            extension_misc::APP_LAUNCH_CMD_LINE_APP,
                            extension_misc::APP_LAUNCH_BUCKET_BOUNDARY);
}

void RecordAppLaunches(
    Profile* profile,
    const std::vector<GURL>& cmd_line_urls,
    const std::vector<BrowserInit::LaunchWithProfile::Tab>& autolaunch_tabs) {
  ExtensionService* extension_service = profile->GetExtensionService();
  DCHECK(extension_service);
  for (size_t i = 0; i < cmd_line_urls.size(); ++i) {
    if (extension_service->IsInstalledApp(cmd_line_urls.at(i))) {
      UMA_HISTOGRAM_ENUMERATION(extension_misc::kAppLaunchHistogram,
                                extension_misc::APP_LAUNCH_CMD_LINE_URL,
                                extension_misc::APP_LAUNCH_BUCKET_BOUNDARY);
    }
  }
  for (size_t i = 0; i < autolaunch_tabs.size(); ++i) {
    if (extension_service->IsInstalledApp(autolaunch_tabs.at(i).url)) {
      UMA_HISTOGRAM_ENUMERATION(extension_misc::kAppLaunchHistogram,
                                extension_misc::APP_LAUNCH_AUTOLAUNCH,
                                extension_misc::APP_LAUNCH_BUCKET_BOUNDARY);
    }
  }
}

void RegisterComponentsForUpdate(const CommandLine& command_line) {
  ComponentUpdateService* cus = g_browser_process->component_updater();
  if (!cus)
    return;
  // Registration can be before of after cus->Start() so it is ok to post
  // a task to the UI thread to do registration once you done the necessary
  // file IO to know you existing component version.
  RegisterRecoveryComponent(cus, g_browser_process->local_state());
  RegisterPepperFlashComponent(cus);
  RegisterNPAPIFlashComponent(cus);
  RegisterSwiftShaderComponent(cus);

  // CRLSetFetcher attempts to load a CRL set from either the local disk or
  // network.
  if (!command_line.HasSwitch(switches::kDisableCRLSets))
    g_browser_process->crl_set_fetcher()->StartInitialLoad(cus);

  // This developer version of Pnacl should only be installed for developers.
  if (command_line.HasSwitch(switches::kEnablePnacl)) {
    RegisterPnaclComponent(cus);
  }

  cus->Start();
}

}  // namespace


// BrowserInit ----------------------------------------------------------------

BrowserInit::BrowserInit() {}

BrowserInit::~BrowserInit() {}

// static
bool BrowserInit::was_restarted_read_ = false;

void BrowserInit::AddFirstRunTab(const GURL& url) {
  first_run_tabs_.push_back(url);
}

// static
bool BrowserInit::InProcessStartup() {
  return in_startup;
}

// static
void BrowserInit::RegisterUserPrefs(PrefService* prefs) {
  prefs->RegisterIntegerPref(
      prefs::kShownAutoLaunchInfobar, 0, PrefService::UNSYNCABLE_PREF);
}

bool BrowserInit::LaunchBrowser(const CommandLine& command_line,
                                Profile* profile,
                                const FilePath& cur_dir,
                                IsProcessStartup process_startup,
                                IsFirstRun is_first_run,
                                int* return_code) {
  in_startup = process_startup == IS_PROCESS_STARTUP;
  DCHECK(profile);

  // Continue with the incognito profile from here on if Incognito mode
  // is forced.
  if (IncognitoModePrefs::ShouldLaunchIncognito(command_line,
                                                profile->GetPrefs())) {
    profile = profile->GetOffTheRecordProfile();
  } else if (command_line.HasSwitch(switches::kIncognito)) {
    LOG(WARNING) << "Incognito mode disabled by policy, launching a normal "
                 << "browser session.";
  }

  BrowserInit::LaunchWithProfile lwp(cur_dir, command_line, this, is_first_run);
  std::vector<GURL> urls_to_launch = BrowserInit::GetURLsFromCommandLine(
      command_line, cur_dir, profile);
  bool launched = lwp.Launch(profile, urls_to_launch, in_startup);
  in_startup = false;

  if (!launched) {
    LOG(ERROR) << "launch error";
    if (return_code)
      *return_code = chrome::RESULT_CODE_INVALID_CMDLINE_URL;
    return false;
  }

#if defined(OS_CHROMEOS)
  // Initialize Chrome OS preferences like touch pad sensitivity. For the
  // preferences to work in the guest mode, the initialization has to be
  // done after |profile| is switched to the incognito profile (which
  // is actually GuestSessionProfile in the guest mode). See the
  // GetOffTheRecordProfile() call above.
  profile->InitChromeOSPreferences();

#if defined(TOOLKIT_USES_GTK)
  // Create the WmMessageListener so that it can listen for messages regardless
  // of what window has focus.
  chromeos::WmMessageListener::GetInstance();
#endif

  if (process_startup) {
    // This observer is a singleton. It is never deleted but the pointer is kept
    // in a static so that it isn't reported as a leak.
    static chromeos::LowBatteryObserver* low_battery_observer =
        new chromeos::LowBatteryObserver(profile);
    chromeos::DBusThreadManager::Get()->GetPowerManagerClient()->AddObserver(
        low_battery_observer);

    static chromeos::NetworkMessageObserver* network_message_observer =
        new chromeos::NetworkMessageObserver(profile);
    chromeos::CrosLibrary::Get()->GetNetworkLibrary()
        ->AddNetworkManagerObserver(network_message_observer);
    chromeos::CrosLibrary::Get()->GetNetworkLibrary()
        ->AddCellularDataPlanObserver(network_message_observer);
    chromeos::CrosLibrary::Get()->GetNetworkLibrary()
        ->AddUserActionObserver(network_message_observer);

    static chromeos::SmsObserver* sms_observer =
        new chromeos::SmsObserver(profile);
    chromeos::CrosLibrary::Get()->GetNetworkLibrary()
        ->AddNetworkManagerObserver(sms_observer);

    profile->SetupChromeOSEnterpriseExtensionObserver();
  }
#endif
  return true;
}

// static
bool BrowserInit::WasRestarted() {
  // Stores the value of the preference kWasRestarted had when it was read.
  static bool was_restarted = false;

  if (!was_restarted_read_) {
    PrefService* pref_service = g_browser_process->local_state();
    was_restarted = pref_service->GetBoolean(prefs::kWasRestarted);
    pref_service->SetBoolean(prefs::kWasRestarted, false);
    was_restarted_read_ = true;
  }
  return was_restarted;
}

// static
SessionStartupPref BrowserInit::GetSessionStartupPref(
    const CommandLine& command_line,
    Profile* profile) {
  SessionStartupPref pref = SessionStartupPref::GetStartupPref(profile);
  if (command_line.HasSwitch(switches::kRestoreLastSession) ||
      BrowserInit::WasRestarted()) {
    pref.type = SessionStartupPref::LAST;
  }
  if (pref.type == SessionStartupPref::LAST &&
      IncognitoModePrefs::ShouldLaunchIncognito(command_line,
                                                profile->GetPrefs())) {
    // We don't store session information when incognito. If the user has
    // chosen to restore last session and launched incognito, fallback to
    // default launch behavior.
    pref.type = SessionStartupPref::DEFAULT;
  }
  return pref;
}


// BrowserInit::LaunchWithProfile::Tab ----------------------------------------

BrowserInit::LaunchWithProfile::Tab::Tab() : is_app(false), is_pinned(true) {}

BrowserInit::LaunchWithProfile::Tab::~Tab() {}


// BrowserInit::LaunchWithProfile ---------------------------------------------

BrowserInit::LaunchWithProfile::LaunchWithProfile(
    const FilePath& cur_dir,
    const CommandLine& command_line,
    IsFirstRun is_first_run)
        : cur_dir_(cur_dir),
          command_line_(command_line),
          profile_(NULL),
          browser_init_(NULL),
          is_first_run_(is_first_run == IS_FIRST_RUN) {
}

BrowserInit::LaunchWithProfile::LaunchWithProfile(
    const FilePath& cur_dir,
    const CommandLine& command_line,
    BrowserInit* browser_init,
    IsFirstRun is_first_run)
        : cur_dir_(cur_dir),
          command_line_(command_line),
          profile_(NULL),
          browser_init_(browser_init),
          is_first_run_(is_first_run == IS_FIRST_RUN) {
}

BrowserInit::LaunchWithProfile::~LaunchWithProfile() {
}

bool BrowserInit::LaunchWithProfile::Launch(
    Profile* profile,
    const std::vector<GURL>& urls_to_open,
    bool process_startup) {
  DCHECK(profile);
  profile_ = profile;

  if (command_line_.HasSwitch(switches::kDnsLogDetails))
    chrome_browser_net::EnablePredictorDetailedLog(true);
  if (command_line_.HasSwitch(switches::kDnsPrefetchDisable) &&
      profile->GetNetworkPredictor()) {
    profile->GetNetworkPredictor()->EnablePredictor(false);
  }

  if (command_line_.HasSwitch(switches::kDumpHistogramsOnExit))
    base::StatisticsRecorder::set_dump_on_exit(true);

  if (command_line_.HasSwitch(switches::kRemoteDebuggingPort)) {
    std::string port_str =
        command_line_.GetSwitchValueASCII(switches::kRemoteDebuggingPort);
    int64 port;
    if (base::StringToInt64(port_str, &port) && port > 0 && port < 65535) {
      std::string frontend_str;
      if (command_line_.HasSwitch(switches::kRemoteDebuggingFrontend)) {
        frontend_str = command_line_.GetSwitchValueASCII(
            switches::kRemoteDebuggingFrontend);
      }
      g_browser_process->InitDevToolsHttpProtocolHandler(
          profile,
          "127.0.0.1",
          static_cast<int>(port),
          frontend_str);
    } else {
      DLOG(WARNING) << "Invalid http debugger port number " << port;
    }
  }

  // Open the required browser windows and tabs. First, see if
  // we're being run as an application window. If so, the user
  // opened an app shortcut.  Don't restore tabs or open initial
  // URLs in that case. The user should see the window as an app,
  // not as chrome.
  if (OpenApplicationWindow(profile)) {
    RecordLaunchModeHistogram(LM_AS_WEBAPP);
  } else {
    RecordLaunchModeHistogram(urls_to_open.empty()?
                              LM_TO_BE_DECIDED : LM_WITH_URLS);
    ProcessLaunchURLs(process_startup, urls_to_open);

    // If this is an app launch, but we didn't open an app window, it may
    // be an app tab.
    OpenApplicationTab(profile);

    if (process_startup) {
      if (browser_defaults::kOSSupportsOtherBrowsers &&
          !command_line_.HasSwitch(switches::kNoDefaultBrowserCheck)) {
        if (!CheckIfAutoLaunched(profile)) {
          // Check whether we are the default browser.
          CheckDefaultBrowser(profile);
        }
      }
#if defined(OS_MACOSX)
      // Check whether the auto-update system needs to be promoted from user
      // to system.
      KeystoneInfoBar::PromotionInfoBar(profile);
#endif
    }
  }

#if defined(OS_WIN)
  // Print the selected page if the command line switch exists. Note that the
  // current selected tab would be the page which will be printed.
  if (command_line_.HasSwitch(switches::kPrint)) {
    Browser* browser = BrowserList::GetLastActive();
    browser->Print();
  }
#endif

  // If we're recording or playing back, startup the EventRecorder now
  // unless otherwise specified.
  if (!command_line_.HasSwitch(switches::kNoEvents)) {
    FilePath script_path;
    PathService::Get(chrome::FILE_RECORDED_SCRIPT, &script_path);

    bool record_mode = command_line_.HasSwitch(switches::kRecordMode);
    bool playback_mode = command_line_.HasSwitch(switches::kPlaybackMode);

    if (record_mode && chrome::kRecordModeEnabled)
      base::EventRecorder::current()->StartRecording(script_path);
    if (playback_mode)
      base::EventRecorder::current()->StartPlayback(script_path);
  }

#if defined(OS_WIN)
  if (process_startup)
    ShellIntegration::MigrateChromiumShortcuts();
#endif  // defined(OS_WIN)

  return true;
}

bool BrowserInit::LaunchWithProfile::IsAppLaunch(std::string* app_url,
                                                 std::string* app_id) {
  if (command_line_.HasSwitch(switches::kApp)) {
    if (app_url)
      *app_url = command_line_.GetSwitchValueASCII(switches::kApp);
    return true;
  }
  if (command_line_.HasSwitch(switches::kAppId)) {
    if (app_id)
      *app_id = command_line_.GetSwitchValueASCII(switches::kAppId);
    return true;
  }
  return false;
}

bool BrowserInit::LaunchWithProfile::OpenApplicationTab(Profile* profile) {
  std::string app_id;
  // App shortcuts to URLs always open in an app window.  Because this
  // function will open an app that should be in a tab, there is no need
  // to look at the app URL.  OpenApplicationWindow() will open app url
  // shortcuts.
  if (!IsAppLaunch(NULL, &app_id) || app_id.empty())
    return false;

  extension_misc::LaunchContainer launch_container;
  const Extension* extension;
  if (!GetAppLaunchContainer(profile, app_id, &extension, &launch_container))
    return false;

  // If the user doesn't want to open a tab, fail.
  if (launch_container != extension_misc::LAUNCH_TAB)
    return false;

  RecordCmdLineAppHistogram();

  WebContents* app_tab = Browser::OpenApplicationTab(profile, extension, GURL(),
                                                     NEW_FOREGROUND_TAB);
  return (app_tab != NULL);
}

bool BrowserInit::LaunchWithProfile::OpenApplicationWindow(Profile* profile) {
  std::string url_string, app_id;
  if (!IsAppLaunch(&url_string, &app_id))
    return false;

  // This can fail if the app_id is invalid.  It can also fail if the
  // extension is external, and has not yet been installed.
  // TODO(skerner): Do something reasonable here. Pop up a warning panel?
  // Open an URL to the gallery page of the extension id?
  if (!app_id.empty()) {
    extension_misc::LaunchContainer launch_container;
    const Extension* extension;
    if (!GetAppLaunchContainer(profile, app_id, &extension, &launch_container))
      return false;

    // TODO(skerner): Could pass in |extension| and |launch_container|,
    // and avoid calling GetAppLaunchContainer() both here and in
    // OpenApplicationTab().

    if (launch_container == extension_misc::LAUNCH_TAB)
      return false;

    RecordCmdLineAppHistogram();
    WebContents* tab_in_app_window = Browser::OpenApplication(
        profile, extension, launch_container, GURL(), NEW_WINDOW);
    return (tab_in_app_window != NULL);
  }

  if (url_string.empty())
    return false;

#if defined(OS_WIN)  // Fix up Windows shortcuts.
  ReplaceSubstringsAfterOffset(&url_string, 0, "\\x", "%");
#endif
  GURL url(url_string);

  // Restrict allowed URLs for --app switch.
  if (!url.is_empty() && url.is_valid()) {
    ChildProcessSecurityPolicy *policy =
        ChildProcessSecurityPolicy::GetInstance();
    if (policy->IsWebSafeScheme(url.scheme()) ||
        url.SchemeIs(chrome::kFileScheme)) {

      if (profile->GetExtensionService()->IsInstalledApp(url)) {
        RecordCmdLineAppHistogram();
      } else {
        UMA_HISTOGRAM_ENUMERATION(
            extension_misc::kAppLaunchHistogram,
            extension_misc::APP_LAUNCH_CMD_LINE_APP_LEGACY,
            extension_misc::APP_LAUNCH_BUCKET_BOUNDARY);
      }
      WebContents* app_tab = Browser::OpenAppShortcutWindow(
          profile,
          url,
          true);  // Update app info.
      return (app_tab != NULL);
    }
  }
  return false;
}

void BrowserInit::LaunchWithProfile::ProcessLaunchURLs(
    bool process_startup,
    const std::vector<GURL>& urls_to_open) {
  // If we're starting up in "background mode" (no open browser window) then
  // don't open any browser windows.
  if (process_startup && command_line_.HasSwitch(switches::kNoStartupWindow))
    return;

  if (process_startup && ProcessStartupURLs(urls_to_open)) {
    // ProcessStartupURLs processed the urls, nothing else to do.
    return;
  }

  if (!process_startup) {
    // Even if we're not starting a new process, this may conceptually be
    // "startup" for the user and so should be handled in a similar way.  Eg.,
    // Chrome may have been running in the background due to an app with a
    // background page being installed, or running with only an app window
    // displayed.
    SessionService* service = SessionServiceFactory::GetForProfile(profile_);
    if (service && service->ShouldNewWindowStartSession()) {
      // Restore the last session if any.
      if (service->RestoreIfNecessary(urls_to_open))
        return;
      // Open user-specified URLs like pinned tabs and startup tabs.
      if (ProcessSpecifiedURLs(urls_to_open))
        return;
    }
  }

  // Session startup didn't occur, open the urls.

  Browser* browser = NULL;
  std::vector<GURL> adjust_urls = urls_to_open;
  if (adjust_urls.empty())
    AddStartupURLs(&adjust_urls);
  else if (!command_line_.HasSwitch(switches::kOpenInNewWindow))
    browser = BrowserList::GetLastActiveWithProfile(profile_);

  browser = OpenURLsInBrowser(browser, process_startup, adjust_urls);
  if (process_startup)
    AddInfoBarsIfNecessary(browser);
}

bool BrowserInit::LaunchWithProfile::ProcessStartupURLs(
    const std::vector<GURL>& urls_to_open) {
  SessionStartupPref pref = GetSessionStartupPref(command_line_, profile_);
  if (command_line_.HasSwitch(switches::kTestingChannelID) &&
      !command_line_.HasSwitch(switches::kRestoreLastSession) &&
      browser_defaults::kDefaultSessionStartupType !=
      SessionStartupPref::DEFAULT) {
    // When we have non DEFAULT session start type, then we won't open up a
    // fresh session. But none of the tests are written with this in mind, so
    // we explicitly ignore it during testing.
    return false;
  }

  if (pref.type == SessionStartupPref::LAST) {
    if (!profile_->DidLastSessionExitCleanly() &&
        !command_line_.HasSwitch(switches::kRestoreLastSession)) {
      // The last session crashed. It's possible automatically loading the
      // page will trigger another crash, locking the user out of chrome.
      // To avoid this, don't restore on startup but instead show the crashed
      // infobar.
      return false;
    }

  uint32 restore_behavior = SessionRestore::SYNCHRONOUS |
                            SessionRestore::ALWAYS_CREATE_TABBED_BROWSER;
#if defined(OS_MACOSX)
    // On Mac, when restoring a session with no windows, suppress the creation
    // of a new window in the case where the system is launching Chrome via a
    // login item or Lion's resume feature.
    if (base::mac::WasLaunchedAsLoginOrResumeItem()) {
      restore_behavior = restore_behavior &
                         ~SessionRestore::ALWAYS_CREATE_TABBED_BROWSER;
    }
#endif

    Browser* browser = SessionRestore::RestoreSession(profile_,
                                                      NULL,
                                                      restore_behavior,
                                                      urls_to_open);
    AddInfoBarsIfNecessary(browser);
    return true;
  }

  Browser* browser = ProcessSpecifiedURLs(urls_to_open);
  if (!browser)
    return false;

  AddInfoBarsIfNecessary(browser);
  return true;
}

Browser* BrowserInit::LaunchWithProfile::ProcessSpecifiedURLs(
    const std::vector<GURL>& urls_to_open) {
  SessionStartupPref pref = GetSessionStartupPref(command_line_, profile_);
  std::vector<Tab> tabs;
  // Pinned tabs should not be displayed when chrome is launched
  // in icognito mode.
  if (!IncognitoModePrefs::ShouldLaunchIncognito(command_line_,
                                                 profile_->GetPrefs())) {
    tabs = PinnedTabCodec::ReadPinnedTabs(profile_);
  }

  RecordAppLaunches(profile_, urls_to_open, tabs);

  if (!urls_to_open.empty()) {
    // If urls were specified on the command line, use them.
    UrlsToTabs(urls_to_open, &tabs);
  } else if (pref.type == SessionStartupPref::URLS && !pref.urls.empty()) {
    // Only use the set of urls specified in preferences if nothing was
    // specified on the command line. Filter out any urls that are to be
    // restored by virtue of having been previously pinned.
    AddUniqueURLs(pref.urls, &tabs);
  } else if (pref.type == SessionStartupPref::DEFAULT && !tabs.empty()) {
    // Make sure the home page is opened even if there are pinned tabs.
    std::vector<GURL> urls;
    AddStartupURLs(&urls);
    UrlsToTabs(urls, &tabs);
  }

  if (tabs.empty())
    return NULL;

  Browser* browser = OpenTabsInBrowser(NULL, true, tabs);
  return browser;
}

void BrowserInit::LaunchWithProfile::AddUniqueURLs(
    const std::vector<GURL>& urls,
    std::vector<Tab>* tabs) {
  size_t num_existing_tabs = tabs->size();
  for (size_t i = 0; i < urls.size(); ++i) {
    bool in_tabs = false;
    for (size_t j = 0; j < num_existing_tabs; ++j) {
      if (urls[i] == (*tabs)[j].url) {
        in_tabs = true;
        break;
      }
    }
    if (!in_tabs) {
      BrowserInit::LaunchWithProfile::Tab tab;
      tab.is_pinned = false;
      tab.url = urls[i];
      tabs->push_back(tab);
    }
  }
}

Browser* BrowserInit::LaunchWithProfile::OpenURLsInBrowser(
    Browser* browser,
    bool process_startup,
    const std::vector<GURL>& urls) {
  std::vector<Tab> tabs;
  UrlsToTabs(urls, &tabs);
  return OpenTabsInBrowser(browser, process_startup, tabs);
}

Browser* BrowserInit::LaunchWithProfile::OpenTabsInBrowser(
        Browser* browser,
        bool process_startup,
        const std::vector<Tab>& in_tabs) {
  DCHECK(!in_tabs.empty());

  // If we don't yet have a profile, try to use the one we're given from
  // |browser|. While we may not end up actually using |browser| (since it
  // could be a popup window), we can at least use the profile.
  if (!profile_ && browser)
    profile_ = browser->profile();

  std::vector<Tab> tabs(in_tabs);
  size_t active_tab_index =
      ShowSyncPromoDialog(process_startup, &browser, &tabs);
  bool first_tab = active_tab_index == std::string::npos;

  if (!browser || !browser->is_type_tabbed()) {
    browser = Browser::Create(profile_);
  } else {
#if defined(TOOLKIT_GTK)
    // Setting the time of the last action on the window here allows us to steal
    // focus, which is what the user wants when opening a new tab in an existing
    // browser window.
    gtk_util::SetWMLastUserActionTime(browser->window()->GetNativeHandle());
#endif
  }

#if !defined(OS_MACOSX)
  // In kiosk mode, we want to always be fullscreen, so switch to that now.
  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kKioskMode))
    browser->ToggleFullscreenMode(false);
#endif

  for (size_t i = 0; i < tabs.size(); ++i) {
    // We skip URLs that we'd have to launch an external protocol handler for.
    // This avoids us getting into an infinite loop asking ourselves to open
    // a URL, should the handler be (incorrectly) configured to be us. Anyone
    // asking us to open such a URL should really ask the handler directly.
    bool handled_by_chrome = ProfileIOData::IsHandledURL(tabs[i].url) ||
        (profile_ && profile_->GetProtocolHandlerRegistry()->IsHandledProtocol(
            tabs[i].url.scheme()));
    if (!process_startup && !handled_by_chrome)
      continue;

    size_t index;
    if (tabs[i].url.SchemeIs(chrome::kChromeUIScheme) &&
        tabs[i].url.host() == chrome::kChromeUISyncPromoHost) {
      // The sync promo must always be the first tab. If the browser window
      // was spawned from the sync promo dialog then it might have other tabs
      // in it already. Explicilty set it to 0 to ensure that it's first.
      index = 0;
    } else {
      index = browser->GetIndexForInsertionDuringRestore(i);
    }

    int add_types = (first_tab || index == active_tab_index) ?
        TabStripModel::ADD_ACTIVE : TabStripModel::ADD_NONE;
    add_types |= TabStripModel::ADD_FORCE_INDEX;
    if (tabs[i].is_pinned)
      add_types |= TabStripModel::ADD_PINNED;

    browser::NavigateParams params(browser, tabs[i].url,
                                   content::PAGE_TRANSITION_START_PAGE);
    params.disposition = (first_tab || index == active_tab_index) ?
        NEW_FOREGROUND_TAB : NEW_BACKGROUND_TAB;
    params.tabstrip_index = index;
    params.tabstrip_add_types = add_types;
    params.extension_app_id = tabs[i].app_id;
    browser::Navigate(&params);

    first_tab = false;
  }
  if (!browser->GetSelectedWebContents()) {
    // TODO: this is a work around for 110909. Figure out why it's needed.
    if (!browser->tab_count())
      browser->AddBlankTab(true);
    else
      browser->ActivateTabAt(0, false);
  }

  browser->window()->Show();
  // TODO(jcampan): http://crbug.com/8123 we should not need to set the initial
  //                focus explicitly.
  browser->GetSelectedWebContents()->GetView()->SetInitialFocus();

  return browser;
}

void BrowserInit::LaunchWithProfile::AddInfoBarsIfNecessary(Browser* browser) {
  if (!browser || !profile_ || browser->tab_count() == 0)
    return;

  TabContentsWrapper* tab_contents = browser->GetSelectedTabContentsWrapper();
  AddCrashedInfoBarIfNecessary(browser, tab_contents);
  AddBadFlagsInfoBarIfNecessary(tab_contents);
  AddDNSCertProvenanceCheckingWarningInfoBarIfNecessary(tab_contents);
  AddObsoleteSystemInfoBarIfNecessary(tab_contents);
}

void BrowserInit::LaunchWithProfile::AddCrashedInfoBarIfNecessary(
    Browser* browser,
    TabContentsWrapper* tab) {
  // Assume that if the user is launching incognito they were previously
  // running incognito so that we have nothing to restore from.
  if (!profile_->DidLastSessionExitCleanly() && !profile_->IsOffTheRecord()) {
    // The last session didn't exit cleanly. Show an infobar to the user
    // so that they can restore if they want. The delegate deletes itself when
    // it is closed.
    tab->infobar_tab_helper()->AddInfoBar(
        new SessionCrashedInfoBarDelegate(profile_, tab->infobar_tab_helper()));
  }
}

void BrowserInit::LaunchWithProfile::AddBadFlagsInfoBarIfNecessary(
    TabContentsWrapper* tab) {
  // Unsupported flags for which to display a warning that "stability and
  // security will suffer".
  static const char* kBadFlags[] = {
    // These imply disabling the sandbox.
    switches::kSingleProcess,
    switches::kNoSandbox,
    switches::kInProcessWebGL,
    // This should only be used for tests and to disable Protector on ChromeOS.
#if !defined(OS_CHROMEOS)
    switches::kNoProtector,
#endif
    NULL
  };

  const char* bad_flag = NULL;
  for (const char** flag = kBadFlags; *flag; ++flag) {
    if (command_line_.HasSwitch(*flag)) {
      bad_flag = *flag;
      break;
    }
  }

  if (bad_flag) {
    tab->infobar_tab_helper()->AddInfoBar(
        new SimpleAlertInfoBarDelegate(
            tab->infobar_tab_helper(), NULL,
            l10n_util::GetStringFUTF16(
                IDS_BAD_FLAGS_WARNING_MESSAGE,
                UTF8ToUTF16(std::string("--") + bad_flag)),
            false));
  }
}

class LearnMoreInfoBar : public LinkInfoBarDelegate {
 public:
  LearnMoreInfoBar(InfoBarTabHelper* infobar_helper,
                   const string16& message,
                   const GURL& url);
  virtual ~LearnMoreInfoBar();

  virtual string16 GetMessageTextWithOffset(size_t* link_offset) const OVERRIDE;
  virtual string16 GetLinkText() const OVERRIDE;
  virtual bool LinkClicked(WindowOpenDisposition disposition) OVERRIDE;

 private:
  string16 message_;
  GURL learn_more_url_;

  DISALLOW_COPY_AND_ASSIGN(LearnMoreInfoBar);
};

LearnMoreInfoBar::LearnMoreInfoBar(InfoBarTabHelper* infobar_helper,
                                   const string16& message,
                                   const GURL& url)
    : LinkInfoBarDelegate(infobar_helper),
      message_(message),
      learn_more_url_(url) {
}

LearnMoreInfoBar::~LearnMoreInfoBar() {
}

string16 LearnMoreInfoBar::GetMessageTextWithOffset(size_t* link_offset) const {
  string16 text = message_;
  text.push_back(' ');  // Add a space before the following link.
  *link_offset = text.size();
  return text;
}

string16 LearnMoreInfoBar::GetLinkText() const {
  return l10n_util::GetStringUTF16(IDS_LEARN_MORE);
}

bool LearnMoreInfoBar::LinkClicked(WindowOpenDisposition disposition) {
  OpenURLParams params(
      learn_more_url_, Referrer(), disposition, content::PAGE_TRANSITION_LINK,
      false);
  owner()->web_contents()->OpenURL(params);
  return false;
}

// This is the page which provides information on DNS certificate provenance
// checking.
void BrowserInit::LaunchWithProfile::
    AddDNSCertProvenanceCheckingWarningInfoBarIfNecessary(
        TabContentsWrapper* tab) {
  if (!command_line_.HasSwitch(switches::kEnableDNSCertProvenanceChecking))
    return;

  const char* kLearnMoreURL =
      "http://dev.chromium.org/dnscertprovenancechecking";
  string16 message = l10n_util::GetStringUTF16(
      IDS_DNS_CERT_PROVENANCE_CHECKING_WARNING_MESSAGE);
  tab->infobar_tab_helper()->AddInfoBar(
      new LearnMoreInfoBar(tab->infobar_tab_helper(),
                           message,
                           GURL(kLearnMoreURL)));
}

void BrowserInit::LaunchWithProfile::AddObsoleteSystemInfoBarIfNecessary(
    TabContentsWrapper* tab) {
#if defined(TOOLKIT_USES_GTK)
  // We've deprecated support for Ubuntu Hardy.  Rather than attempting to
  // determine whether you're using that, we instead key off the GTK version;
  // this will also deprecate other distributions (including variants of Ubuntu)
  // that are of a similar age.
  // Version key:
  //   Ubuntu Hardy: GTK 2.12
  //   RHEL 6:       GTK 2.18
  //   Ubuntu Lucid: GTK 2.20
  if (gtk_check_version(2, 18, 0)) {
    string16 message = l10n_util::GetStringUTF16(IDS_SYSTEM_OBSOLETE_MESSAGE);
    // Link to an article in the help center on minimum system requirements.
    const char* kLearnMoreURL =
        "http://www.google.com/support/chrome/bin/answer.py?answer=95411";
    InfoBarTabHelper* infobar_helper = tab->infobar_tab_helper();
    infobar_helper->AddInfoBar(
        new LearnMoreInfoBar(infobar_helper,
                             message,
                             GURL(kLearnMoreURL)));
  }
#endif
}

void BrowserInit::LaunchWithProfile::AddStartupURLs(
    std::vector<GURL>* startup_urls) const {
  // If we have urls specified beforehand (i.e. from command line) use them
  // and nothing else.
  if (!startup_urls->empty())
    return;

  // If we have urls specified by the first run master preferences use them
  // and nothing else.
  if (browser_init_) {
    if (!browser_init_->first_run_tabs_.empty()) {
      std::vector<GURL>::iterator it = browser_init_->first_run_tabs_.begin();
      while (it != browser_init_->first_run_tabs_.end()) {
        // Replace magic names for the actual urls.
        if (it->host() == "new_tab_page") {
          startup_urls->push_back(GURL(chrome::kChromeUINewTabURL));
        } else if (it->host() == "welcome_page") {
          startup_urls->push_back(GetWelcomePageURL());
        } else {
          startup_urls->push_back(*it);
        }
        ++it;
      }
      browser_init_->first_run_tabs_.clear();
    }
  }

  // Otherwise open at least the new tab page (and the welcome page, if this
  // is the first time the browser is being started), or the set of URLs
  // specified on the command line.
  if (startup_urls->empty()) {
    startup_urls->push_back(GURL());  // New tab page.
    PrefService* prefs = g_browser_process->local_state();
    if (prefs->FindPreference(prefs::kShouldShowWelcomePage) &&
        prefs->GetBoolean(prefs::kShouldShowWelcomePage)) {
      // Reset the preference so we don't show the welcome page next time.
      prefs->ClearPref(prefs::kShouldShowWelcomePage);
      startup_urls->push_back(GetWelcomePageURL());
    }
  }

  // If the sync promo page is going to be displayed then insert it at the front
  // of the list.
  bool promo_suppressed = false;
  if (SyncPromoUI::ShouldShowSyncPromoAtStartup(profile_, is_first_run_,
                                                &promo_suppressed)) {
    SyncPromoUI::DidShowSyncPromoAtStartup(profile_);
    GURL old_url = (*startup_urls)[0];
    (*startup_urls)[0] =
        SyncPromoUI::GetSyncPromoURL(GURL(chrome::kChromeUINewTabURL), true,
                                     std::string());

    // An empty URL means to go to the home page.
    if (old_url.is_empty() &&
        profile_->GetHomePage() == GURL(chrome::kChromeUINewTabURL)) {
      old_url = GURL(chrome::kChromeUINewTabURL);
    }

    // If the old URL is not the NTP then insert it right after the sync promo.
    if (old_url != GURL(chrome::kChromeUINewTabURL))
      startup_urls->insert(startup_urls->begin() + 1, old_url);

    // If we have more than two startup tabs then skip the welcome page.
    if (startup_urls->size() > 2) {
      std::vector<GURL>::iterator it = std::find(
          startup_urls->begin(), startup_urls->end(), GetWelcomePageURL());
      if (it != startup_urls->end())
        startup_urls->erase(it);
    }
  } else if (promo_suppressed) {
    sync_promo_trial::RecordSyncPromoSuppressedForCurrentTrial();
  }
}

void BrowserInit::LaunchWithProfile::CheckDefaultBrowser(Profile* profile) {
  // We do not check if we are the default browser if:
  // - the user said "don't ask me again" on the infobar earlier.
  // - this is the first launch after the first run flow.
  // - There is a policy in control of this setting.
  if (!profile->GetPrefs()->GetBoolean(prefs::kCheckDefaultBrowser) ||
      is_first_run_) {
    return;
  }
  if (g_browser_process->local_state()->IsManagedPreference(
      prefs::kDefaultBrowserSettingEnabled)) {
    if (g_browser_process->local_state()->GetBoolean(
        prefs::kDefaultBrowserSettingEnabled)) {
      BrowserThread::PostTask(
          BrowserThread::FILE, FROM_HERE,
          base::Bind(
              base::IgnoreResult(&ShellIntegration::SetAsDefaultBrowser)));
    } else {
      // TODO(pastarmovj): We can't really do anything meaningful here yet but
      // just prevent showing the infobar.
    }
    return;
  }
  BrowserThread::PostTask(BrowserThread::FILE, FROM_HERE,
                          base::Bind(&CheckDefaultBrowserCallback));
}

bool BrowserInit::LaunchWithProfile::CheckIfAutoLaunched(Profile* profile) {
#if defined(OS_WIN)
  if (!auto_launch_trial::IsInAutoLaunchGroup())
    return false;

  int infobar_shown =
      profile->GetPrefs()->GetInteger(prefs::kShownAutoLaunchInfobar);
  if (infobar_shown >= kMaxInfobarShown)
    return false;

  const CommandLine& command_line = *CommandLine::ForCurrentProcess();
  if (command_line.HasSwitch(switches::kAutoLaunchAtStartup) ||
      first_run::IsChromeFirstRun()) {
    BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
                            base::Bind(&CheckAutoLaunchCallback));
    return true;
  }
#endif
  return false;
}

size_t BrowserInit::LaunchWithProfile::ShowSyncPromoDialog(
    bool process_startup,
    Browser** browser,
    std::vector<Tab>* tabs) {
  DCHECK(browser);
  DCHECK(tabs);

  // The dialog is only shown on process startup if no browser window is already
  // being displayed.
  if (!profile_ || *browser || !process_startup ||
      SyncPromoUI::GetSyncPromoVersion() != SyncPromoUI::VERSION_DIALOG) {
    return std::string::npos;
  }

  for (size_t i = 0; i < tabs->size(); ++i) {
    GURL url((*tabs)[i].url);
    if (url.SchemeIs(chrome::kChromeUIScheme) &&
        url.host() == chrome::kChromeUISyncPromoHost) {
      SyncPromoDialog dialog(profile_, url);
      dialog.ShowDialog();
      *browser = dialog.spawned_browser();
      if (!*browser) {
        // If no browser window was spawned then just replace the sync promo
        // with the next URL.
        (*tabs)[i].url = SyncPromoUI::GetNextPageURLForSyncPromoURL(url);
        return i;
      } else if (dialog.sync_promo_was_closed()) {
        tabs->erase(tabs->begin() + i);
        // The tab spawned by the dialog is at tab index 0 so return 0 to make
        // it the active tab.
        return 0;
      } else {
        // Since the sync promo is not closed it will be inserted at tab
        // index 0. The tab spawned by the dialog will be at index 1 so return
        // 0 to make it the active tab.
        return 1;
      }
    }
  }

  return std::string::npos;
}

std::vector<GURL> BrowserInit::GetURLsFromCommandLine(
    const CommandLine& command_line,
    const FilePath& cur_dir,
    Profile* profile) {
  std::vector<GURL> urls;
  const CommandLine::StringVector& params = command_line.GetArgs();

  for (size_t i = 0; i < params.size(); ++i) {
    FilePath param = FilePath(params[i]);
    // Handle Vista way of searching - "? <search-term>"
    if (param.value().size() > 2 &&
        param.value()[0] == '?' && param.value()[1] == ' ') {
      const TemplateURL* default_provider =
          TemplateURLServiceFactory::GetForProfile(profile)->
          GetDefaultSearchProvider();
      if (default_provider && default_provider->url()) {
        const TemplateURLRef* search_url = default_provider->url();
        DCHECK(search_url->SupportsReplacement());
        string16 search_term = param.LossyDisplayName().substr(2);
        urls.push_back(GURL(search_url->ReplaceSearchTermsUsingProfile(
            profile, *default_provider, search_term,
            TemplateURLRef::NO_SUGGESTIONS_AVAILABLE, string16())));
        continue;
      }
    }

    // Otherwise, fall through to treating it as a URL.

    // This will create a file URL or a regular URL.
    // This call can (in rare circumstances) block the UI thread.
    // Allow it until this bug is fixed.
    //  http://code.google.com/p/chromium/issues/detail?id=60641
    GURL url;
    {
      base::ThreadRestrictions::ScopedAllowIO allow_io;
      url = URLFixerUpper::FixupRelativeFile(cur_dir, param);
    }
    // Exclude dangerous schemes.
    if (url.is_valid()) {
      ChildProcessSecurityPolicy *policy =
          ChildProcessSecurityPolicy::GetInstance();
      if (policy->IsWebSafeScheme(url.scheme()) ||
          url.SchemeIs(chrome::kFileScheme) ||
#if defined(OS_CHROMEOS)
          // In ChromeOS, allow a settings page to be specified on the
          // command line. See ExistingUserController::OnLoginSuccess.
          (url.spec().find(chrome::kChromeUISettingsURL) == 0) ||
#endif
          (url.spec().compare(chrome::kAboutBlankURL) == 0)) {
        urls.push_back(url);
      }
    }
  }
  return urls;
}

bool BrowserInit::ProcessCmdLineImpl(
    const CommandLine& command_line,
    const FilePath& cur_dir,
    bool process_startup,
    Profile* last_used_profile,
    const Profiles& last_opened_profiles,
    int* return_code,
    BrowserInit* browser_init) {
  DCHECK(last_used_profile);
  if (process_startup) {
    if (command_line.HasSwitch(switches::kDisablePromptOnRepost))
      content::NavigationController::DisablePromptOnRepost();

    RegisterComponentsForUpdate(command_line);

    // Look for the testing channel ID ONLY during process startup
    if (command_line.HasSwitch(switches::kTestingChannelID)) {
      std::string testing_channel_id = command_line.GetSwitchValueASCII(
          switches::kTestingChannelID);
      // TODO(sanjeevr) Check if we need to make this a singleton for
      // compatibility with the old testing code
      // If there are any extra parameters, we expect each one to generate a
      // new tab; if there are none then we get one homepage tab.
      int expected_tab_count = 1;
      if (command_line.HasSwitch(switches::kNoStartupWindow)) {
        expected_tab_count = 0;
#if defined(OS_CHROMEOS)
      // kLoginManager will cause Chrome to start up with the ChromeOS login
      // screen instead of a browser window, so it won't load any tabs.
      } else if (command_line.HasSwitch(switches::kLoginManager)) {
        expected_tab_count = 0;
#endif
      } else if (command_line.HasSwitch(switches::kRestoreLastSession)) {
        std::string restore_session_value(
            command_line.GetSwitchValueASCII(switches::kRestoreLastSession));
        base::StringToInt(restore_session_value, &expected_tab_count);
      } else {
        std::vector<GURL> urls_to_open = GetURLsFromCommandLine(
            command_line, cur_dir, last_used_profile);
        expected_tab_count =
            std::max(1, static_cast<int>(urls_to_open.size()));
      }
      if (!CreateAutomationProvider<TestingAutomationProvider>(
          testing_channel_id,
          last_used_profile,
          static_cast<size_t>(expected_tab_count)))
        return false;
    }
  }

  bool silent_launch = false;

  if (command_line.HasSwitch(switches::kAutomationClientChannelID)) {
    std::string automation_channel_id = command_line.GetSwitchValueASCII(
        switches::kAutomationClientChannelID);
    // If there are any extra parameters, we expect each one to generate a
    // new tab; if there are none then we have no tabs
    std::vector<GURL> urls_to_open = GetURLsFromCommandLine(
        command_line, cur_dir, last_used_profile);
    size_t expected_tabs =
        std::max(static_cast<int>(urls_to_open.size()), 0);
    if (expected_tabs == 0)
      silent_launch = true;

    if (command_line.HasSwitch(switches::kChromeFrame)) {
#if !defined(USE_AURA)
      if (!CreateAutomationProvider<ChromeFrameAutomationProvider>(
          automation_channel_id, last_used_profile, expected_tabs))
        return false;
#endif
    } else {
      if (!CreateAutomationProvider<AutomationProvider>(
          automation_channel_id, last_used_profile, expected_tabs))
        return false;
    }
  }

  // If we have been invoked to display a desktop notification on behalf of
  // the service process, we do not want to open any browser windows.
  if (command_line.HasSwitch(switches::kNotifyCloudPrintTokenExpired)) {
    silent_launch = true;
    CloudPrintProxyServiceFactory::GetForProfile(last_used_profile)->
        ShowTokenExpiredNotification();
  }

  // If we are just displaying a print dialog we shouldn't open browser
  // windows.
  if (command_line.HasSwitch(switches::kCloudPrintFile) &&
      print_dialog_cloud::CreatePrintDialogFromCommandLine(command_line)) {
    silent_launch = true;
  }

  // If we are checking the proxy enabled policy, don't open any windows.
  if (command_line.HasSwitch(switches::kCheckCloudPrintConnectorPolicy)) {
    silent_launch = true;
    if (CloudPrintProxyServiceFactory::GetForProfile(last_used_profile)->
        EnforceCloudPrintConnectorPolicyAndQuit())
      // Success, nothing more needs to be done, so return false to stop
      // launching and quit.
      return false;
  }

  if (command_line.HasSwitch(switches::kExplicitlyAllowedPorts)) {
    std::string allowed_ports =
        command_line.GetSwitchValueASCII(switches::kExplicitlyAllowedPorts);
    net::SetExplicitlyAllowedPorts(allowed_ports);
  }

#if defined(OS_CHROMEOS)
  // The browser will be launched after the user logs in.
  if (command_line.HasSwitch(switches::kLoginManager) ||
      command_line.HasSwitch(switches::kLoginPassword)) {
    silent_launch = true;
  }
#endif

#if defined(TOOLKIT_VIEWS) && defined(OS_LINUX)
  // Get a list of pointer-devices that should be treated as touch-devices.
  // This is primarily used for testing/debugging touch-event processing when a
  // touch-device isn't available.
  std::string touch_devices =
    command_line.GetSwitchValueASCII(switches::kTouchDevices);

  if (!touch_devices.empty()) {
    std::vector<std::string> devs;
    std::vector<unsigned int> device_ids;
    unsigned int devid;
    base::SplitString(touch_devices, ',', &devs);
    for (std::vector<std::string>::iterator iter = devs.begin();
        iter != devs.end(); ++iter) {
      if (base::StringToInt(*iter, reinterpret_cast<int*>(&devid)))
        device_ids.push_back(devid);
      else
        DLOG(WARNING) << "Invalid touch-device id: " << *iter;
    }
    ui::TouchFactory::GetInstance()->SetTouchDeviceList(device_ids);
  }
#endif

  // If we don't want to launch a new browser window or tab (in the case
  // of an automation request), we are done here.
  if (!silent_launch) {
    IsProcessStartup is_process_startup = process_startup ?
        IS_PROCESS_STARTUP : IS_NOT_PROCESS_STARTUP;
    IsFirstRun is_first_run = first_run::IsChromeFirstRun() ?
        IS_FIRST_RUN : IS_NOT_FIRST_RUN;
    // |last_opened_profiles| will be empty in the following circumstances:
    // - This is the first launch. |last_used_profile| is the initial profile.
    // - The user exited the browser by closing all windows for all
    // profiles. |last_used_profile| is the profile which owned the last open
    // window.
    // - Only incognito windows were open when the browser exited.
    // |last_used_profile| is the last used incognito profile. Restoring it will
    // create a browser window for the corresponding original profile.
    if (last_opened_profiles.empty()) {
      if (!browser_init->LaunchBrowser(command_line, last_used_profile, cur_dir,
              is_process_startup, is_first_run, return_code))
        return false;
    } else {
      // Launch the last used profile with the full command line, and the other
      // opened profiles without the URLs to launch.
      CommandLine command_line_without_urls(command_line.GetProgram());
      const CommandLine::SwitchMap& switches = command_line.GetSwitches();
      for (CommandLine::SwitchMap::const_iterator switch_it = switches.begin();
           switch_it != switches.end(); ++switch_it) {
        command_line_without_urls.AppendSwitchNative(switch_it->first,
                                                     switch_it->second);
      }
      // Launch the profiles in the order they became active.
      for (Profiles::const_iterator it = last_opened_profiles.begin();
           it != last_opened_profiles.end(); ++it) {
        // Don't launch additional profiles which would only open a new tab
        // page. When restarting after an update, all profiles will reopen last
        // open pages.
        SessionStartupPref startup_pref =
            GetSessionStartupPref(command_line, *it);
        if (*it != last_used_profile &&
            startup_pref.type != SessionStartupPref::LAST &&
            startup_pref.type != SessionStartupPref::URLS)
          continue;
        if (!browser_init->LaunchBrowser((*it == last_used_profile) ?
            command_line : command_line_without_urls, *it, cur_dir,
            is_process_startup, is_first_run, return_code))
          return false;
        // We've launched at least one browser.
        is_process_startup = BrowserInit::IS_NOT_PROCESS_STARTUP;
      }
    }
  }
  return true;
}

template <class AutomationProviderClass>
bool BrowserInit::CreateAutomationProvider(const std::string& channel_id,
                                           Profile* profile,
                                           size_t expected_tabs) {
  scoped_refptr<AutomationProviderClass> automation =
      new AutomationProviderClass(profile);

  if (!automation->InitializeChannel(channel_id))
    return false;
  automation->SetExpectedTabCount(expected_tabs);

  AutomationProviderList* list = g_browser_process->GetAutomationProviderList();
  DCHECK(list);
  list->AddProvider(automation);

  return true;
}

// static
void BrowserInit::ProcessCommandLineOnProfileCreated(
    const CommandLine& cmd_line,
    const FilePath& cur_dir,
    Profile* profile,
    Profile::CreateStatus status) {
  if (status == Profile::CREATE_STATUS_INITIALIZED)
    ProcessCmdLineImpl(cmd_line, cur_dir, false, profile, Profiles(), NULL,
                       NULL);
}

// static
void BrowserInit::ProcessCommandLineAlreadyRunning(const CommandLine& cmd_line,
                                                   const FilePath& cur_dir) {
  if (cmd_line.HasSwitch(switches::kProfileDirectory)) {
    ProfileManager* profile_manager = g_browser_process->profile_manager();
    FilePath path = cmd_line.GetSwitchValuePath(switches::kProfileDirectory);
    path = profile_manager->user_data_dir().Append(path);
    profile_manager->CreateProfileAsync(path,
        base::Bind(&BrowserInit::ProcessCommandLineOnProfileCreated,
                   cmd_line, cur_dir));
    return;
  }

  Profile* profile = ProfileManager::GetLastUsedProfile();
  if (!profile) {
    // We should only be able to get here if the profile already exists and
    // has been created.
    NOTREACHED();
    return;
  }
  ProcessCmdLineImpl(cmd_line, cur_dir, false, profile, Profiles(), NULL, NULL);
}
