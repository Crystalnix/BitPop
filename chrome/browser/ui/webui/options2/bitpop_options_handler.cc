// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/options2/bitpop_options_handler.h"

#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/command_line.h"
#include "base/memory/singleton.h"
#include "base/path_service.h"
#include "base/stl_util.h"
#include "base/string_number_conversions.h"
#include "base/utf_string_conversions.h"
#include "base/value_conversions.h"
#include "base/values.h"
#include "chrome/browser/auto_launch_trial.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_page_zoom.h"
#include "chrome/browser/custom_home_pages_table_model.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/instant/instant_controller.h"
#include "chrome/browser/net/url_fixer_upper.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/printing/cloud_print/cloud_print_proxy_service.h"
#include "chrome/browser/printing/cloud_print/cloud_print_proxy_service_factory.h"
#include "chrome/browser/printing/cloud_print/cloud_print_setup_flow.h"
#include "chrome/browser/printing/cloud_print/cloud_print_url.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_info_cache.h"
#include "chrome/browser/profiles/profile_info_util.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/search_engines/template_url.h"
#include "chrome/browser/search_engines/template_url_service.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/service/service_process_control.h"
#include "chrome/browser/signin/signin_manager.h"
#include "chrome/browser/signin/signin_manager_factory.h"
#include "chrome/browser/sync/profile_sync_service.h"
#include "chrome/browser/sync/profile_sync_service_factory.h"
#include "chrome/browser/sync/sync_ui_util.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/chrome_select_file_policy.h"
#include "chrome/browser/ui/options/options_util.h"
#include "chrome/browser/ui/webui/chrome_url_data_manager.h"
#include "chrome/browser/ui/webui/favicon_source.h"
#include "chrome/browser/ui/webui/web_ui_util.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/net/gaia/google_service_auth_error.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/user_metrics.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#include "content/public/common/page_zoom.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/locale_settings.h"
#include "grit/theme_resources.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebView.h"
#include "ui/base/l10n/l10n_util.h"

#if !defined(OS_CHROMEOS)
#include "chrome/browser/printing/cloud_print/cloud_print_setup_handler.h"
#include "chrome/browser/ui/webui/options2/advanced_options_utils.h"
#endif

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/accessibility/accessibility_util.h"
#include "chrome/browser/chromeos/extensions/wallpaper_manager_util.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/options/take_photo_dialog.h"
#include "chrome/browser/chromeos/settings/cros_settings.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/webui/options2/chromeos/timezone_options_util.h"
#include "ui/gfx/image/image_skia.h"
#endif  // defined(OS_CHROMEOS)

#if defined(OS_WIN)
#include "chrome/installer/util/auto_launch_util.h"
#endif  // defined(OS_WIN)

#if defined(TOOLKIT_GTK)
#include "chrome/browser/ui/gtk/gtk_theme_service.h"
#endif  // defined(TOOLKIT_GTK)

using content::BrowserContext;
using content::BrowserThread;
using content::DownloadManager;
using content::OpenURLParams;
using content::Referrer;
using content::UserMetricsAction;

namespace options2 {

BitpopOptionsHandler::BitpopOptionsHandler()
    : template_url_service_(NULL),
      ALLOW_THIS_IN_INITIALIZER_LIST(weak_ptr_factory_for_file_(this)),
      ALLOW_THIS_IN_INITIALIZER_LIST(weak_ptr_factory_for_ui_(this)) {
  multiprofile_ = ProfileManager::IsMultipleProfilesEnabled();
}

BitpopOptionsHandler::~BitpopOptionsHandler() {
}

void BitpopOptionsHandler::GetLocalizedValues(DictionaryValue* values) {
  DCHECK(values);

  static OptionsStringResource resources[] = {
    { "askBeforeUsing", IDS_BITPOP_ASK_BEFORE_USING_PROXY },
    { "bitpopSettingsTitle", IDS_BITPOP_SETTINGS_TITLE },
    { "facebookShowChat", IDS_BITPOP_FACEBOOK_SHOW_CHAT_LABEL },
    { "facebookShowJewels", IDS_BITPOP_FACEBOOK_SHOW_JEWELS_LABEL },
    { "neverUseProxy", IDS_BITPOP_NEVER_USE_PROXY },
    { "openFacebookNotificationsOptions", 
        IDS_BITPOP_FACEBOOK_OPEN_NOTIFICATION_SETTINGS },
    { "openProxyDomainSettings", 
        IDS_BITPOP_OPEN_PROXY_DOMAIN_SETTINGS_BUTTON_TITLE },
    { "openUncensorFilterLists",
        IDS_BITPOP_UNCENSOR_OPEN_LIST_BUTTON_TITLE },
    { "sectionTitleBitpopFacebookSidebar",
        IDS_BITPOP_FACEBOOK_SIDEBAR_SECTION_TITLE },
    { "sectionTitleFacebookNotifications",
        IDS_BITPOP_FACEBOOK_NOTIFICATIONS_SECTION_TITLE },
    { "sectionTitleGlobalProxyControl",
        IDS_BITPOP_GLOBAL_PROXY_CONTROL_TITLE },
    { "sectionTitleUncensorFilterControl",
        IDS_BITPOP_UNCENSOR_FILTER_CONTROL },
    { "showMessageForActiveProxy",
        IDS_BITPOP_SHOW_MESSAGE_FOR_ACTIVE_PROXY },
    { "uncensorAlwaysRedirectOn",
        IDS_BITPOP_UNCENSOR_ALWAYS_REDIRECT },
    { "uncensorNeverRedirectOff",
        IDS_BITPOP_UNCENSOR_NEVER_REDIRECT },
    { "uncensorNotifyUpdates",
        IDS_BITPOP_UNCENSOR_NOTIFY_UPDATES },
    { "uncensorShowMessage",
        IDS_BITPOP_UNCENSOR_SHOW_MESSAGE },
    { "useAutoProxy", IDS_BITPOP_USE_AUTO_PROXY },
    { "whenToUseProxy", IDS_BITPOP_WHEN_TO_USE_PROXY },
  };

  RegisterStrings(values, resources, arraysize(resources));
  RegisterCloudPrintValues(values);

#if !defined(OS_CHROMEOS)
  values->SetString(
      "syncOverview",
      l10n_util::GetStringFUTF16(IDS_SYNC_OVERVIEW,
                                 l10n_util::GetStringUTF16(IDS_PRODUCT_NAME)));
  values->SetString(
      "syncButtonTextStart",
      l10n_util::GetStringFUTF16(IDS_SYNC_START_SYNC_BUTTON_LABEL,
          l10n_util::GetStringUTF16(IDS_SHORT_PRODUCT_NAME)));
#endif

  values->SetString("syncLearnMoreURL", chrome::kSyncLearnMoreURL);
  values->SetString(
      "profilesSingleUser",
      l10n_util::GetStringFUTF16(IDS_PROFILES_SINGLE_USER_MESSAGE,
                                 l10n_util::GetStringUTF16(IDS_PRODUCT_NAME)));

  string16 omnibox_url = ASCIIToUTF16(chrome::kOmniboxLearnMoreURL);
  values->SetString(
      "defaultSearchGroupLabel",
      l10n_util::GetStringFUTF16(IDS_SEARCH_PREF_EXPLANATION, omnibox_url));

  string16 instant_learn_more_url = ASCIIToUTF16(chrome::kInstantLearnMoreURL);
  values->SetString(
      "instantPrefAndWarning",
      l10n_util::GetStringFUTF16(IDS_INSTANT_PREF_WITH_WARNING,
                                 instant_learn_more_url));
  values->SetString("instantLearnMoreLink", instant_learn_more_url);

  values->SetString(
      "defaultBrowserUnknown",
      l10n_util::GetStringFUTF16(IDS_OPTIONS_DEFAULTBROWSER_UNKNOWN,
          l10n_util::GetStringUTF16(IDS_PRODUCT_NAME)));
  values->SetString(
      "defaultBrowserUseAsDefault",
      l10n_util::GetStringFUTF16(IDS_OPTIONS_DEFAULTBROWSER_USEASDEFAULT,
          l10n_util::GetStringUTF16(IDS_PRODUCT_NAME)));
  values->SetString(
      "autoLaunchText",
      l10n_util::GetStringFUTF16(IDS_AUTOLAUNCH_TEXT,
          l10n_util::GetStringUTF16(IDS_PRODUCT_NAME)));

#if defined(OS_CHROMEOS)
  if (chromeos::UserManager::Get()->IsUserLoggedIn()) {
    values->SetString("username",
        chromeos::UserManager::Get()->GetLoggedInUser().email());
  }
#endif

  // Pass along sync status early so it will be available during page init.
  values->Set("syncData", GetSyncStateDictionary().release());

#if defined(OS_WIN)
  values->SetString("privacyWin8DataLearnMoreURL",
                    chrome::kPrivacyWin8DataLearnMoreURL);
#endif
  values->SetString("privacyLearnMoreURL", chrome::kPrivacyLearnMoreURL);
  values->SetString("sessionRestoreLearnMoreURL",
                    chrome::kSessionRestoreLearnMoreURL);

  values->SetString(
      "languageSectionLabel",
      l10n_util::GetStringFUTF16(
          IDS_OPTIONS_ADVANCED_LANGUAGE_LABEL,
          l10n_util::GetStringUTF16(IDS_SHORT_PRODUCT_NAME)));

#if defined(OS_CHROMEOS)
  values->SetString("cloudPrintLearnMoreURL", chrome::kCloudPrintLearnMoreURL);

  // TODO(pastarmovj): replace this with a call to the CrosSettings list
  // handling functionality to come.
  values->Set("timezoneList", GetTimezoneList().release());
#endif
#if defined(OS_MACOSX)
  values->SetString("macPasswordsWarning",
      l10n_util::GetStringUTF16(IDS_OPTIONS_PASSWORDS_MAC_WARNING));
  values->SetBoolean("multiple_profiles",
      g_browser_process->profile_manager()->GetNumberOfProfiles() > 1);
#endif

  if (multiprofile_)
    values->Set("profilesInfo", GetProfilesInfoList().release());
}

void BitpopOptionsHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
    "openFacebookNotificationsOptions",
    base::Bind(&BitpopOptionsHandler::OpenFacebookNotificationsOptions,
               base::Unretained(this)));

//   web_ui()->RegisterMessageCallback(
//       "becomeDefaultBrowser",
//       base::Bind(&BitpopOptionsHandler::BecomeDefaultBrowser,
//                  base::Unretained(this)));
//   web_ui()->RegisterMessageCallback(
//       "setDefaultSearchEngine",
//       base::Bind(&BitpopOptionsHandler::SetDefaultSearchEngine,
//                  base::Unretained(this)));
//   web_ui()->RegisterMessageCallback(
//       "enableInstant",
//       base::Bind(&BitpopOptionsHandler::EnableInstant,
//                  base::Unretained(this)));
//   web_ui()->RegisterMessageCallback(
//       "disableInstant",
//       base::Bind(&BitpopOptionsHandler::DisableInstant,
//                  base::Unretained(this)));
//   web_ui()->RegisterMessageCallback(
//       "createProfile",
//       base::Bind(&BitpopOptionsHandler::CreateProfile,
//                  base::Unretained(this)));
//   web_ui()->RegisterMessageCallback(
//       "createProfileInfo",
//       base::Bind(&BitpopOptionsHandler::CreateProfileInfo,
//                  base::Unretained(this)));
//   web_ui()->RegisterMessageCallback(
//       "themesReset",
//       base::Bind(&BitpopOptionsHandler::ThemesReset,
//                  base::Unretained(this)));
// #if defined(TOOLKIT_GTK)
//   web_ui()->RegisterMessageCallback(
//       "themesSetGTK",
//       base::Bind(&BitpopOptionsHandler::ThemesSetGTK,
//                  base::Unretained(this)));
// #endif
//   web_ui()->RegisterMessageCallback(
//       "selectDownloadLocation",
//       base::Bind(&BitpopOptionsHandler::HandleSelectDownloadLocation,
//                  base::Unretained(this)));
//   web_ui()->RegisterMessageCallback(
//       "autoOpenFileTypesAction",
//       base::Bind(&BitpopOptionsHandler::HandleAutoOpenButton,
//                  base::Unretained(this)));
//   web_ui()->RegisterMessageCallback(
//       "defaultFontSizeAction",
//       base::Bind(&BitpopOptionsHandler::HandleDefaultFontSize,
//                  base::Unretained(this)));
//   web_ui()->RegisterMessageCallback(
//       "defaultZoomFactorAction",
//       base::Bind(&BitpopOptionsHandler::HandleDefaultZoomFactor,
//                  base::Unretained(this)));
// #if !defined(OS_CHROMEOS)
//   web_ui()->RegisterMessageCallback(
//       "metricsReportingCheckboxAction",
//       base::Bind(&BitpopOptionsHandler::HandleMetricsReportingCheckbox,
//                  base::Unretained(this)));
// #endif
// #if !defined(USE_NSS) && !defined(USE_OPENSSL)
//   web_ui()->RegisterMessageCallback(
//       "showManageSSLCertificates",
//       base::Bind(&BitpopOptionsHandler::ShowManageSSLCertificates,
//                  base::Unretained(this)));
// #endif
//   web_ui()->RegisterMessageCallback(
//       "showCloudPrintManagePage",
//       base::Bind(&BitpopOptionsHandler::ShowCloudPrintManagePage,
//                  base::Unretained(this)));
// #if !defined(OS_CHROMEOS)
//   if (cloud_print_connector_ui_enabled_) {
//     web_ui()->RegisterMessageCallback(
//         "showCloudPrintSetupDialog",
//         base::Bind(&BitpopOptionsHandler::ShowCloudPrintSetupDialog,
//                    base::Unretained(this)));
//     web_ui()->RegisterMessageCallback(
//         "disableCloudPrintConnector",
//         base::Bind(&BitpopOptionsHandler::HandleDisableCloudPrintConnector,
//                    base::Unretained(this)));
//   }
//   web_ui()->RegisterMessageCallback(
//       "showNetworkProxySettings",
//       base::Bind(&BitpopOptionsHandler::ShowNetworkProxySettings,
//                  base::Unretained(this)));
// #endif
//   web_ui()->RegisterMessageCallback(
//       "checkRevocationCheckboxAction",
//       base::Bind(&BitpopOptionsHandler::HandleCheckRevocationCheckbox,
//                  base::Unretained(this)));
// #if !defined(OS_MACOSX) && !defined(OS_CHROMEOS)
//   web_ui()->RegisterMessageCallback(
//       "backgroundModeAction",
//       base::Bind(&BitpopOptionsHandler::HandleBackgroundModeCheckbox,
//                  base::Unretained(this)));
// #endif
// #if defined(OS_CHROMEOS)
//   web_ui()->RegisterMessageCallback(
//       "openWallpaperManager",
//       base::Bind(&BitpopOptionsHandler::HandleOpenWallpaperManager,
//                  base::Unretained(this)));
//   web_ui()->RegisterMessageCallback(
//       "spokenFeedbackChange",
//       base::Bind(&BitpopOptionsHandler::SpokenFeedbackChangeCallback,
//                  base::Unretained(this)));
//   web_ui()->RegisterMessageCallback(
//       "highContrastChange",
//       base::Bind(&BitpopOptionsHandler::HighContrastChangeCallback,
//                  base::Unretained(this)));
//   web_ui()->RegisterMessageCallback(
//       "screenMagnifierChange",
//       base::Bind(&BitpopOptionsHandler::ScreenMagnifierChangeCallback,
//                  base::Unretained(this)));
//   web_ui()->RegisterMessageCallback(
//       "virtualKeyboardChange",
//       base::Bind(&BitpopOptionsHandler::VirtualKeyboardChangeCallback,
//                  base::Unretained(this)));
// #endif
// #if defined(OS_MACOSX)
//   web_ui()->RegisterMessageCallback(
//       "toggleAutomaticUpdates",
//       base::Bind(&BitpopOptionsHandler::ToggleAutomaticUpdates,
//                  base::Unretained(this)));

// #endif
}

void BitpopOptionsHandler::InitializeHandler() {
  Profile* profile = Profile::FromWebUI(web_ui());
  PrefService* prefs = profile->GetPrefs();

//   ProfileSyncService* sync_service(
//       ProfileSyncServiceFactory::GetInstance()->GetForProfile(profile));
//   if (sync_service)
//     sync_service->AddObserver(this);

//   // Create our favicon data source.
//   ChromeURLDataManager::AddDataSource(profile,
//       new FaviconSource(profile, FaviconSource::FAVICON));

//   default_browser_policy_.Init(prefs::kDefaultBrowserSettingEnabled,
//                                g_browser_process->local_state(),
//                                this);

//   registrar_.Add(this, chrome::NOTIFICATION_PROFILE_CACHED_INFO_CHANGED,
//                  content::NotificationService::AllSources());
// #if defined(OS_CHROMEOS)
//   registrar_.Add(this, chrome::NOTIFICATION_LOGIN_USER_IMAGE_CHANGED,
//                  content::NotificationService::AllSources());
// #endif
//   registrar_.Add(this, chrome::NOTIFICATION_BROWSER_THEME_CHANGED,
//                  content::Source<ThemeService>(
//                      ThemeServiceFactory::GetForProfile(profile)));

//   AddTemplateUrlServiceObserver();

// #if defined(OS_WIN)
//   const CommandLine& command_line = *CommandLine::ForCurrentProcess();
//   if (!command_line.HasSwitch(switches::kChromeFrame) &&
//       !command_line.HasSwitch(switches::kUserDataDir)) {
//     BrowserThread::PostTask(BrowserThread::FILE, FROM_HERE,
//         base::Bind(&BitpopOptionsHandler::CheckAutoLaunch,
//                    weak_ptr_factory_for_ui_.GetWeakPtr(),
//                    weak_ptr_factory_for_file_.GetWeakPtr(),
//                    profile->GetPath()));
//     weak_ptr_factory_for_ui_.DetachFromThread();
//   }
// #endif

// #if !defined(OS_CHROMEOS)
//   enable_metrics_recording_.Init(prefs::kMetricsReportingEnabled,
//                                  g_browser_process->local_state(), this);
//   cloud_print_connector_email_.Init(prefs::kCloudPrintEmail, prefs, this);
//   cloud_print_connector_enabled_.Init(prefs::kCloudPrintProxyEnabled, prefs,
//                                       this);
// #endif

//   rev_checking_enabled_.Init(prefs::kCertRevocationCheckingEnabled,
//                              g_browser_process->local_state(), this);

// #if !defined(OS_MACOSX) && !defined(OS_CHROMEOS)
//   background_mode_enabled_.Init(prefs::kBackgroundModeEnabled,
//                                 g_browser_process->local_state(), this);
// #endif

//   auto_open_files_.Init(prefs::kDownloadExtensionsToOpen, prefs, this);
//   default_font_size_.Init(prefs::kWebKitDefaultFontSize, prefs, this);
//   default_zoom_level_.Init(prefs::kDefaultZoomLevel, prefs, this);
// #if !defined(OS_CHROMEOS)
//   proxy_prefs_.reset(
//       PrefSetObserver::CreateProxyPrefSetObserver(prefs, this));
// #endif  // !defined(OS_CHROMEOS)
}

void BitpopOptionsHandler::InitializePage() {
  // OnTemplateURLServiceChanged();
  // ObserveThemeChanged();
  // OnStateChanged();
  // UpdateDefaultBrowserState();

  // SetupMetricsReportingCheckbox();
  // SetupMetricsReportingSettingVisibility();
  // SetupPasswordGenerationSettingVisibility();
  // SetupFontSizeSelector();
  // SetupPageZoomSelector();
  // SetupAutoOpenFileTypes();
  // SetupProxySettingsSection();
  // SetupSSLConfigSettings();
}

bool BitpopOptionsHandler::IsInteractiveSetDefaultPermitted() {
  return true;  // This is UI so we can allow it.
}

void BitpopOptionsHandler::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  if (type == chrome::NOTIFICATION_BROWSER_THEME_CHANGED) {
    ObserveThemeChanged();
#if defined(OS_CHROMEOS)
  } else if (type == chrome::NOTIFICATION_LOGIN_USER_IMAGE_CHANGED) {
    UpdateAccountPicture();
#endif
  } else if (type == chrome::NOTIFICATION_PREF_CHANGED) {
    std::string* pref_name = content::Details<std::string>(details).ptr();
    if (*pref_name == prefs::kDefaultBrowserSettingEnabled) {
      UpdateDefaultBrowserState();
    } else if (*pref_name == prefs::kDownloadExtensionsToOpen) {
      SetupAutoOpenFileTypes();
#if !defined(OS_CHROMEOS)
    } else if (proxy_prefs_->IsObserved(*pref_name)) {
      SetupProxySettingsSection();
#endif  // !defined(OS_CHROMEOS)
    } else if ((*pref_name == prefs::kCloudPrintEmail) ||
               (*pref_name == prefs::kCloudPrintProxyEnabled)) {
#if !defined(OS_CHROMEOS)
      if (cloud_print_connector_ui_enabled_)
        SetupCloudPrintConnectorSection();
#endif
    } else if (*pref_name == prefs::kWebKitDefaultFontSize) {
      SetupFontSizeSelector();
    } else if (*pref_name == prefs::kDefaultZoomLevel) {
      SetupPageZoomSelector();
#if !defined(OS_MACOSX) && !defined(OS_CHROMEOS)
    } else if (*pref_name == prefs::kBackgroundModeEnabled) {
      SetupBackgroundModeSettings();
#endif
    } else {
      NOTREACHED();
    }
  } else if (type == chrome::NOTIFICATION_PROFILE_CACHED_INFO_CHANGED) {
    if (multiprofile_)
      SendProfilesInfo();
  } else {
    NOTREACHED();
  }
}

void BitpopOptionsHandler::OpenFacebookNotificationsOptions() {
  web_ui()->
}

}  // namespace options2
