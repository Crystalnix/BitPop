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

BrowserOptionsHandler::BrowserOptionsHandler()
    : template_url_service_(NULL),
      ALLOW_THIS_IN_INITIALIZER_LIST(weak_ptr_factory_for_file_(this)),
      ALLOW_THIS_IN_INITIALIZER_LIST(weak_ptr_factory_for_ui_(this)) {
  multiprofile_ = ProfileManager::IsMultipleProfilesEnabled();
}

BrowserOptionsHandler::~BrowserOptionsHandler() {
}

void BrowserOptionsHandler::GetLocalizedValues(DictionaryValue* values) {
  DCHECK(values);

  static OptionsStringResource resources[] = {
    { "advancedSectionTitleCloudPrint", IDS_GOOGLE_CLOUD_PRINT },
    { "currentUserOnly", IDS_OPTIONS_CURRENT_USER_ONLY },
    { "advancedSectionTitleContent",
      IDS_OPTIONS_ADVANCED_SECTION_TITLE_CONTENT },
    { "advancedSectionTitleLanguages",
      IDS_OPTIONS_ADVANCED_SECTION_TITLE_LANGUAGES },
    { "advancedSectionTitleNetwork",
      IDS_OPTIONS_ADVANCED_SECTION_TITLE_NETWORK },
    { "advancedSectionTitlePrivacy",
      IDS_OPTIONS_ADVANCED_SECTION_TITLE_PRIVACY },
    { "advancedSectionTitleSecurity",
      IDS_OPTIONS_ADVANCED_SECTION_TITLE_SECURITY },
    { "autofillEnabled", IDS_OPTIONS_AUTOFILL_ENABLE },
    { "autologinEnabled", IDS_OPTIONS_PASSWORDS_AUTOLOGIN },
    { "autoOpenFileTypesInfo", IDS_OPTIONS_OPEN_FILE_TYPES_AUTOMATICALLY },
    { "autoOpenFileTypesResetToDefault",
      IDS_OPTIONS_AUTOOPENFILETYPES_RESETTODEFAULT },
    { "changeHomePage", IDS_OPTIONS_CHANGE_HOME_PAGE },
    { "certificatesManageButton", IDS_OPTIONS_CERTIFICATES_MANAGE_BUTTON },
    { "customizeSync", IDS_OPTIONS2_CUSTOMIZE_SYNC_BUTTON_LABEL },
    { "defaultFontSizeLabel", IDS_OPTIONS_DEFAULT_FONT_SIZE_LABEL },
    { "defaultSearchManageEngines", IDS_OPTIONS_DEFAULTSEARCH_MANAGE_ENGINES },
    { "defaultZoomFactorLabel", IDS_OPTIONS_DEFAULT_ZOOM_LEVEL_LABEL },
#if defined(OS_CHROMEOS)
    { "disableGData", IDS_OPTIONS_DISABLE_GDATA },
#endif
    { "disableWebServices", IDS_OPTIONS_DISABLE_WEB_SERVICES },
#if defined(OS_CHROMEOS)
    { "displayOptionsButton", IDS_OPTIONS_SETTINGS_DISPLAY_OPTIONS_TAB_TITLE },
    { "displayOptionsTitle", IDS_OPTIONS_SETTINGS_SECTION_TITLE_DISPLAY },
#endif
    { "downloadLocationAskForSaveLocation",
      IDS_OPTIONS_DOWNLOADLOCATION_ASKFORSAVELOCATION },
    { "downloadLocationBrowseTitle",
      IDS_OPTIONS_DOWNLOADLOCATION_BROWSE_TITLE },
    { "downloadLocationChangeButton",
      IDS_OPTIONS_DOWNLOADLOCATION_CHANGE_BUTTON },
    { "downloadLocationGroupName", IDS_OPTIONS_DOWNLOADLOCATION_GROUP_NAME },
    { "enableLogging", IDS_OPTIONS_ENABLE_LOGGING },
    { "fontSettingsCustomizeFontsButton",
      IDS_OPTIONS_FONTSETTINGS_CUSTOMIZE_FONTS_BUTTON },
    { "fontSizeLabelCustom", IDS_OPTIONS_FONT_SIZE_LABEL_CUSTOM },
    { "fontSizeLabelLarge", IDS_OPTIONS_FONT_SIZE_LABEL_LARGE },
    { "fontSizeLabelMedium", IDS_OPTIONS_FONT_SIZE_LABEL_MEDIUM },
    { "fontSizeLabelSmall", IDS_OPTIONS_FONT_SIZE_LABEL_SMALL },
    { "fontSizeLabelVeryLarge", IDS_OPTIONS_FONT_SIZE_LABEL_VERY_LARGE },
    { "fontSizeLabelVerySmall", IDS_OPTIONS_FONT_SIZE_LABEL_VERY_SMALL },
    { "hideAdvancedSettings", IDS_SETTINGS_HIDE_ADVANCED_SETTINGS },
    { "homePageNtp", IDS_OPTIONS_HOMEPAGE_NTP },
    { "homePageShowHomeButton", IDS_OPTIONS_TOOLBAR_SHOW_HOME_BUTTON },
    { "homePageUseNewTab", IDS_OPTIONS_HOMEPAGE_USE_NEWTAB },
    { "homePageUseURL", IDS_OPTIONS_HOMEPAGE_USE_URL },
    { "instantConfirmMessage", IDS_INSTANT_OPT_IN_MESSAGE },
    { "instantConfirmTitle", IDS_INSTANT_OPT_IN_TITLE },
    { "importData", IDS_OPTIONS_IMPORT_DATA_BUTTON },
    { "improveBrowsingExperience", IDS_OPTIONS_IMPROVE_BROWSING_EXPERIENCE },
    { "languageAndSpellCheckSettingsButton",
#if defined(OS_CHROMEOS)
      IDS_OPTIONS_SETTINGS_LANGUAGES_CUSTOMIZE },
#else
      IDS_OPTIONS_LANGUAGE_AND_SPELLCHECK_BUTTON },
#endif
    { "linkDoctorPref", IDS_OPTIONS_LINKDOCTOR_PREF },
    { "manageAutofillSettings", IDS_OPTIONS_MANAGE_AUTOFILL_SETTINGS_LINK },
    { "managePasswords", IDS_OPTIONS_PASSWORDS_MANAGE_PASSWORDS_LINK },
    { "networkPredictionEnabledDescription",
      IDS_NETWORK_PREDICTION_ENABLED_DESCRIPTION },
    { "passwordsAndAutofillGroupName",
      IDS_OPTIONS_PASSWORDS_AND_FORMS_GROUP_NAME },
    { "passwordManagerEnabled", IDS_OPTIONS_PASSWORD_MANAGER_ENABLE },
    { "passwordGenerationEnabledDescription",
      IDS_OPTIONS_PASSWORD_GENERATION_ENABLED_LABEL },
    { "privacyClearDataButton", IDS_OPTIONS_PRIVACY_CLEAR_DATA_BUTTON },
    { "privacyContentSettingsButton",
      IDS_OPTIONS_PRIVACY_CONTENT_SETTINGS_BUTTON },
#if defined(OS_WIN)
    { "privacyWin8Data", IDS_WINDOWS8_PRIVACY_HANDLING_INFO },
#endif
    { "profilesCreate", IDS_PROFILES_CREATE_BUTTON_LABEL },
    { "profilesDelete", IDS_PROFILES_DELETE_BUTTON_LABEL },
    { "profilesDeleteSingle", IDS_PROFILES_DELETE_SINGLE_BUTTON_LABEL },
    { "profilesListItemCurrent", IDS_PROFILES_LIST_ITEM_CURRENT },
    { "profilesManage", IDS_PROFILES_MANAGE_BUTTON_LABEL },
    { "proxiesLabel", IDS_OPTIONS_PROXIES_LABEL },
    { "safeBrowsingEnableProtection",
      IDS_OPTIONS_SAFEBROWSING_ENABLEPROTECTION },
    { "sectionTitleAppearance", IDS_APPEARANCE_GROUP_NAME },
    { "sectionTitleDefaultBrowser", IDS_OPTIONS_DEFAULTBROWSER_GROUP_NAME },
    { "sectionTitleUsers", IDS_PROFILES_OPTIONS_GROUP_NAME },
    { "sectionTitleSearch", IDS_OPTIONS_DEFAULTSEARCH_GROUP_NAME },
    { "sectionTitleStartup", IDS_OPTIONS_STARTUP_GROUP_NAME },
    { "sectionTitleSync", IDS_SYNC_OPTIONS_GROUP_NAME },
    { "spellingConfirmMessage", IDS_CONTENT_CONTEXT_SPELLING_BUBBLE_TEXT },
    { "spellingConfirmTitle", IDS_CONTENT_CONTEXT_SPELLING_ASK_GOOGLE },
    { "spellingConfirmEnable", IDS_CONTENT_CONTEXT_SPELLING_BUBBLE_ENABLE },
    { "spellingConfirmDisable", IDS_CONTENT_CONTEXT_SPELLING_BUBBLE_DISABLE },
    { "spellingPref", IDS_OPTIONS_SPELLING_PREF },
    { "startupRestoreLastSession", IDS_OPTIONS_STARTUP_RESTORE_LAST_SESSION },
    { "settingsTitle", IDS_SETTINGS_TITLE },
    { "showAdvancedSettings", IDS_SETTINGS_SHOW_ADVANCED_SETTINGS },
    { "sslCheckRevocation", IDS_OPTIONS_SSL_CHECKREVOCATION },
    { "startupSetPages", IDS_OPTIONS2_STARTUP_SET_PAGES },
    { "startupShowNewTab", IDS_OPTIONS2_STARTUP_SHOW_NEWTAB },
    { "startupShowPages", IDS_OPTIONS2_STARTUP_SHOW_PAGES },
    { "suggestPref", IDS_OPTIONS_SUGGEST_PREF },
    { "syncButtonTextInProgress", IDS_SYNC_NTP_SETUP_IN_PROGRESS },
    { "syncButtonTextStop", IDS_SYNC_STOP_SYNCING_BUTTON_LABEL },
    { "themesGallery", IDS_THEMES_GALLERY_BUTTON },
    { "themesGalleryURL", IDS_THEMES_GALLERY_URL },
    { "tabsToLinksPref", IDS_OPTIONS_TABS_TO_LINKS_PREF },
    { "toolbarShowBookmarksBar", IDS_OPTIONS_TOOLBAR_SHOW_BOOKMARKS_BAR },
    { "toolbarShowHomeButton", IDS_OPTIONS_TOOLBAR_SHOW_HOME_BUTTON },
    { "translateEnableTranslate",
      IDS_OPTIONS_TRANSLATE_ENABLE_TRANSLATE },
#if defined(TOOLKIT_GTK)
    { "showWindowDecorations", IDS_SHOW_WINDOW_DECORATIONS },
    { "themesGTKButton", IDS_THEMES_GTK_BUTTON },
    { "themesSetClassic", IDS_THEMES_SET_CLASSIC },
#else
    { "themes", IDS_THEMES_GROUP_NAME },
    { "themesReset", IDS_THEMES_RESET_BUTTON },
#endif
#if defined(OS_CHROMEOS)
    { "accessibilityHighContrast",
      IDS_OPTIONS_SETTINGS_ACCESSIBILITY_HIGH_CONTRAST_DESCRIPTION },
    { "accessibilityScreenMagnifier",
      IDS_OPTIONS_SETTINGS_ACCESSIBILITY_SCREEN_MAGNIFIER_DESCRIPTION },
    { "accessibilitySpokenFeedback",
      IDS_OPTIONS_SETTINGS_ACCESSIBILITY_DESCRIPTION },
    { "accessibilityTitle",
      IDS_OPTIONS_SETTINGS_SECTION_TITLE_ACCESSIBILITY },
    { "accessibilityVirtualKeyboard",
      IDS_OPTIONS_SETTINGS_ACCESSIBILITY_VIRTUAL_KEYBOARD_DESCRIPTION },
    { "changePicture", IDS_OPTIONS_CHANGE_PICTURE_CAPTION },
    { "datetimeTitle", IDS_OPTIONS_SETTINGS_SECTION_TITLE_DATETIME },
    { "deviceGroupDescription", IDS_OPTIONS_DEVICE_GROUP_DESCRIPTION },
    { "deviceGroupPointer", IDS_OPTIONS2_DEVICE_GROUP_POINTER_SECTION },
    { "mouseSpeed", IDS_OPTIONS2_SETTINGS_MOUSE_SPEED_DESCRIPTION },
    { "touchpadSpeed", IDS_OPTIONS2_SETTINGS_TOUCHPAD_SPEED_DESCRIPTION },
    { "enableScreenlock", IDS_OPTIONS_ENABLE_SCREENLOCKER_CHECKBOX },
    { "internetOptionsButtonTitle", IDS_OPTIONS_INTERNET_OPTIONS_BUTTON_TITLE },
    { "keyboardSettingsButtonTitle",
      IDS_OPTIONS2_DEVICE_GROUP_KEYBOARD_SETTINGS_BUTTON_TITLE },
    { "manageAccountsButtonTitle", IDS_OPTIONS_ACCOUNTS_BUTTON_TITLE },
    { "noPointingDevices", IDS_OPTIONS_NO_POINTING_DEVICES },
    { "sectionTitleDevice", IDS_OPTIONS_DEVICE_GROUP_NAME },
    { "sectionTitleInternet", IDS_OPTIONS_INTERNET_OPTIONS_GROUP_LABEL },
    { "syncOverview", IDS_SYNC_OVERVIEW },
    { "syncButtonTextStart", IDS_SYNC_SETUP_BUTTON_LABEL },
    { "timezone", IDS_OPTIONS_SETTINGS_TIMEZONE_DESCRIPTION },
    { "use24HourClock", IDS_OPTIONS_SETTINGS_USE_24HOUR_CLOCK_DESCRIPTION },
#else
    { "cloudPrintConnectorEnabledManageButton",
      IDS_OPTIONS_CLOUD_PRINT_CONNECTOR_ENABLED_MANAGE_BUTTON},
    { "cloudPrintConnectorEnablingButton",
      IDS_OPTIONS_CLOUD_PRINT_CONNECTOR_ENABLING_BUTTON },
    { "proxiesConfigureButton", IDS_OPTIONS_PROXIES_CONFIGURE_BUTTON },
#endif
#if defined(OS_CHROMEOS) && defined(USE_ASH)
    { "setWallpaper", IDS_SET_WALLPAPER_BUTTON },
#endif
#if !defined(OS_MACOSX) && !defined(OS_CHROMEOS)
    { "advancedSectionTitleBackground",
      IDS_OPTIONS_ADVANCED_SECTION_TITLE_BACKGROUND },
    { "backgroundModeCheckbox", IDS_OPTIONS_BACKGROUND_ENABLE_BACKGROUND_MODE },
#endif
#if defined(OS_MACOSX)
    { "checkForUpdateGroupName", IDS_OPTIONS_CHECKFORUPDATE_GROUP_NAME },
    { "updatesAutoCheckDaily", IDS_OPTIONS_UPDATES_AUTOCHECK_LABEL },
#endif
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

void BrowserOptionsHandler::RegisterMessages() {
//   web_ui()->RegisterMessageCallback(
//       "becomeDefaultBrowser",
//       base::Bind(&BrowserOptionsHandler::BecomeDefaultBrowser,
//                  base::Unretained(this)));
//   web_ui()->RegisterMessageCallback(
//       "setDefaultSearchEngine",
//       base::Bind(&BrowserOptionsHandler::SetDefaultSearchEngine,
//                  base::Unretained(this)));
//   web_ui()->RegisterMessageCallback(
//       "enableInstant",
//       base::Bind(&BrowserOptionsHandler::EnableInstant,
//                  base::Unretained(this)));
//   web_ui()->RegisterMessageCallback(
//       "disableInstant",
//       base::Bind(&BrowserOptionsHandler::DisableInstant,
//                  base::Unretained(this)));
//   web_ui()->RegisterMessageCallback(
//       "createProfile",
//       base::Bind(&BrowserOptionsHandler::CreateProfile,
//                  base::Unretained(this)));
//   web_ui()->RegisterMessageCallback(
//       "createProfileInfo",
//       base::Bind(&BrowserOptionsHandler::CreateProfileInfo,
//                  base::Unretained(this)));
//   web_ui()->RegisterMessageCallback(
//       "themesReset",
//       base::Bind(&BrowserOptionsHandler::ThemesReset,
//                  base::Unretained(this)));
// #if defined(TOOLKIT_GTK)
//   web_ui()->RegisterMessageCallback(
//       "themesSetGTK",
//       base::Bind(&BrowserOptionsHandler::ThemesSetGTK,
//                  base::Unretained(this)));
// #endif
//   web_ui()->RegisterMessageCallback(
//       "selectDownloadLocation",
//       base::Bind(&BrowserOptionsHandler::HandleSelectDownloadLocation,
//                  base::Unretained(this)));
//   web_ui()->RegisterMessageCallback(
//       "autoOpenFileTypesAction",
//       base::Bind(&BrowserOptionsHandler::HandleAutoOpenButton,
//                  base::Unretained(this)));
//   web_ui()->RegisterMessageCallback(
//       "defaultFontSizeAction",
//       base::Bind(&BrowserOptionsHandler::HandleDefaultFontSize,
//                  base::Unretained(this)));
//   web_ui()->RegisterMessageCallback(
//       "defaultZoomFactorAction",
//       base::Bind(&BrowserOptionsHandler::HandleDefaultZoomFactor,
//                  base::Unretained(this)));
// #if !defined(OS_CHROMEOS)
//   web_ui()->RegisterMessageCallback(
//       "metricsReportingCheckboxAction",
//       base::Bind(&BrowserOptionsHandler::HandleMetricsReportingCheckbox,
//                  base::Unretained(this)));
// #endif
// #if !defined(USE_NSS) && !defined(USE_OPENSSL)
//   web_ui()->RegisterMessageCallback(
//       "showManageSSLCertificates",
//       base::Bind(&BrowserOptionsHandler::ShowManageSSLCertificates,
//                  base::Unretained(this)));
// #endif
//   web_ui()->RegisterMessageCallback(
//       "showCloudPrintManagePage",
//       base::Bind(&BrowserOptionsHandler::ShowCloudPrintManagePage,
//                  base::Unretained(this)));
// #if !defined(OS_CHROMEOS)
//   if (cloud_print_connector_ui_enabled_) {
//     web_ui()->RegisterMessageCallback(
//         "showCloudPrintSetupDialog",
//         base::Bind(&BrowserOptionsHandler::ShowCloudPrintSetupDialog,
//                    base::Unretained(this)));
//     web_ui()->RegisterMessageCallback(
//         "disableCloudPrintConnector",
//         base::Bind(&BrowserOptionsHandler::HandleDisableCloudPrintConnector,
//                    base::Unretained(this)));
//   }
//   web_ui()->RegisterMessageCallback(
//       "showNetworkProxySettings",
//       base::Bind(&BrowserOptionsHandler::ShowNetworkProxySettings,
//                  base::Unretained(this)));
// #endif
//   web_ui()->RegisterMessageCallback(
//       "checkRevocationCheckboxAction",
//       base::Bind(&BrowserOptionsHandler::HandleCheckRevocationCheckbox,
//                  base::Unretained(this)));
// #if !defined(OS_MACOSX) && !defined(OS_CHROMEOS)
//   web_ui()->RegisterMessageCallback(
//       "backgroundModeAction",
//       base::Bind(&BrowserOptionsHandler::HandleBackgroundModeCheckbox,
//                  base::Unretained(this)));
// #endif
// #if defined(OS_CHROMEOS)
//   web_ui()->RegisterMessageCallback(
//       "openWallpaperManager",
//       base::Bind(&BrowserOptionsHandler::HandleOpenWallpaperManager,
//                  base::Unretained(this)));
//   web_ui()->RegisterMessageCallback(
//       "spokenFeedbackChange",
//       base::Bind(&BrowserOptionsHandler::SpokenFeedbackChangeCallback,
//                  base::Unretained(this)));
//   web_ui()->RegisterMessageCallback(
//       "highContrastChange",
//       base::Bind(&BrowserOptionsHandler::HighContrastChangeCallback,
//                  base::Unretained(this)));
//   web_ui()->RegisterMessageCallback(
//       "screenMagnifierChange",
//       base::Bind(&BrowserOptionsHandler::ScreenMagnifierChangeCallback,
//                  base::Unretained(this)));
//   web_ui()->RegisterMessageCallback(
//       "virtualKeyboardChange",
//       base::Bind(&BrowserOptionsHandler::VirtualKeyboardChangeCallback,
//                  base::Unretained(this)));
// #endif
// #if defined(OS_MACOSX)
//   web_ui()->RegisterMessageCallback(
//       "toggleAutomaticUpdates",
//       base::Bind(&BrowserOptionsHandler::ToggleAutomaticUpdates,
//                  base::Unretained(this)));

// #endif
}

void BrowserOptionsHandler::InitializeHandler() {
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
//         base::Bind(&BrowserOptionsHandler::CheckAutoLaunch,
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

void BrowserOptionsHandler::InitializePage() {
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

bool BrowserOptionsHandler::IsInteractiveSetDefaultPermitted() {
  return true;  // This is UI so we can allow it.
}

void BrowserOptionsHandler::Observe(
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


}  // namespace options2
