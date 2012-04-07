// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/options2/browser_options_handler2.h"

#include "base/basictypes.h"
#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/command_line.h"
#include "base/memory/singleton.h"
#include "base/path_service.h"
#include "base/stl_util.h"
#include "base/string_number_conversions.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "base/value_conversions.h"
#include "chrome/browser/auto_launch_trial.h"
#include "chrome/browser/autocomplete/autocomplete.h"
#include "chrome/browser/autocomplete/autocomplete_match.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/custom_home_pages_table_model.h"
#include "chrome/browser/instant/instant_confirm_dialog.h"
#include "chrome/browser/instant/instant_controller.h"
#include "chrome/browser/instant/instant_field_trial.h"
#include "chrome/browser/net/url_fixer_upper.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_info_cache.h"
#include "chrome/browser/profiles/profile_info_util.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/search_engines/template_url.h"
#include "chrome/browser/search_engines/template_url_service.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/sync/profile_sync_service.h"
#include "chrome/browser/sync/profile_sync_service_factory.h"
#include "chrome/browser/sync/sync_setup_flow.h"
#include "chrome/browser/sync/sync_ui_util.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/webui/favicon_source.h"
#include "chrome/browser/ui/webui/web_ui_util.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/net/gaia/google_service_auth_error.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/user_metrics.h"
#include "content/public/browser/web_contents.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/locale_settings.h"
#include "grit/theme_resources.h"
#include "ui/base/l10n/l10n_util.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/cros_settings.h"
#include "chrome/browser/chromeos/dbus/dbus_thread_manager.h"
#include "chrome/browser/chromeos/dbus/power_manager_client.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/options/take_photo_dialog.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/views/window.h"
#include "third_party/skia/include/core/SkBitmap.h"
#endif  // defined(OS_CHROMEOS)

#if defined(OS_WIN)
#include "chrome/installer/util/auto_launch_util.h"
#endif  // defined(OS_WIN)

#if defined(TOOLKIT_GTK)
#include "chrome/browser/ui/gtk/gtk_theme_service.h"
#endif  // defined(TOOLKIT_GTK)

using content::BrowserThread;
using content::UserMetricsAction;

namespace options2 {

BrowserOptionsHandler::BrowserOptionsHandler()
    : template_url_service_(NULL),
      ALLOW_THIS_IN_INITIALIZER_LIST(weak_ptr_factory_for_file_(this)),
      ALLOW_THIS_IN_INITIALIZER_LIST(weak_ptr_factory_for_ui_(this)) {
  multiprofile_ = ProfileManager::IsMultipleProfilesEnabled();
#if !defined(OS_MACOSX)
  default_browser_worker_ = new ShellIntegration::DefaultBrowserWorker(this);
#endif
}

BrowserOptionsHandler::~BrowserOptionsHandler() {
  ProfileSyncService* sync_service(ProfileSyncServiceFactory::
      GetInstance()->GetForProfile(Profile::FromWebUI(web_ui())));
  if (sync_service)
    sync_service->RemoveObserver(this);

  if (default_browser_worker_.get())
    default_browser_worker_->ObserverDestroyed();
  if (template_url_service_)
    template_url_service_->RemoveObserver(this);
}

void BrowserOptionsHandler::GetLocalizedValues(
    DictionaryValue* localized_strings) {
  DCHECK(localized_strings);

  static OptionsStringResource resources[] = {
    { "advancedOptionsButtonTitle", IDS_OPTIONS_ADVANCED_BUTTON_TITLE },
    { "autologinEnabled", IDS_OPTIONS_PASSWORDS_AUTOLOGIN },
    { "browsingData", IDS_OPTIONS_BROWSING_DATA_GROUP_NAME },  // needed?
    { "changeHomePage", IDS_OPTIONS_CHANGE_HOME_PAGE },
    { "customizeSync", IDS_OPTIONS2_CUSTOMIZE_SYNC_BUTTON_LABEL },
    { "defaultSearchManageEngines", IDS_OPTIONS_DEFAULTSEARCH_MANAGE_ENGINES },
    { "homePageTitle", IDS_OPTIONS2_HOMEPAGE_TITLE },
    { "homepageUseNewTab", IDS_OPTIONS_HOMEPAGE_USE_NEWTAB },
    { "homepageUseURL", IDS_OPTIONS_HOMEPAGE_USE_URL },
    { "instantConfirmMessage", IDS_INSTANT_OPT_IN_MESSAGE },
    { "instantConfirmTitle", IDS_INSTANT_OPT_IN_TITLE },
    { "importData", IDS_OPTIONS_IMPORT_DATA_BUTTON },
    { "manageDataDescription", IDS_OPTIONS_MANAGE_DATA_DESCRIPTION },
    { "profilesCreate", IDS_PROFILES_CREATE_BUTTON_LABEL },
    { "profilesDelete", IDS_PROFILES_DELETE_BUTTON_LABEL },
    { "profilesDeleteSingle", IDS_PROFILES_DELETE_SINGLE_BUTTON_LABEL },
    { "profilesListItemCurrent", IDS_PROFILES_LIST_ITEM_CURRENT },
    { "profilesManage", IDS_PROFILES_MANAGE_BUTTON_LABEL },
    { "sectionTitleAdvanced", IDS_OPTIONS_ADVANCED_TAB_LABEL },
    { "sectionTitleAppearance", IDS_APPEARANCE_GROUP_NAME },
    { "sectionTitleDefaultBrowser", IDS_OPTIONS_DEFAULTBROWSER_GROUP_NAME },
    { "sectionTitleUsers", IDS_PROFILES_OPTIONS_GROUP_NAME },
    { "sectionTitleSearch", IDS_OPTIONS_DEFAULTSEARCH_GROUP_NAME },
    { "sectionTitleStartup", IDS_OPTIONS_STARTUP_GROUP_NAME },
    { "sectionTitleSync", IDS_SYNC_OPTIONS_GROUP_NAME },
    { "startupSetPages", IDS_OPTIONS2_STARTUP_SET_PAGES },
    { "startupShowDefaultAndNewTab",
      IDS_OPTIONS_STARTUP_SHOW_DEFAULT_AND_NEWTAB},
    { "startupShowLastSession", IDS_OPTIONS_STARTUP_SHOW_LAST_SESSION },
    { "startupShowPages", IDS_OPTIONS2_STARTUP_SHOW_PAGES },
    { "themesGallery", IDS_THEMES_GALLERY_BUTTON },
    { "themesGalleryURL", IDS_THEMES_GALLERY_URL },
    { "toolbarGroupName", IDS_OPTIONS2_TOOLBAR_GROUP_NAME },
    { "toolbarShowBookmarksBar", IDS_OPTIONS_TOOLBAR_SHOW_BOOKMARKS_BAR },
    { "toolbarShowHomeButton", IDS_OPTIONS_TOOLBAR_SHOW_HOME_BUTTON },
#if defined(TOOLKIT_GTK)
    { "showWindowDecorations", IDS_SHOW_WINDOW_DECORATIONS },
    { "themesGTKButton", IDS_THEMES_GTK_BUTTON },
    { "themesSetClassic", IDS_THEMES_SET_CLASSIC },
#else
    { "themes", IDS_THEMES_GROUP_NAME },
    { "themesReset", IDS_THEMES_RESET_BUTTON },
#endif
#if defined(OS_CHROMEOS)
    { "brightnessDecrease", IDS_OPTIONS_SETTINGS_BRIGHTNESS_DECREASE },
    { "brightnessIncrease", IDS_OPTIONS_SETTINGS_BRIGHTNESS_INCREASE },
    { "changePicture", IDS_OPTIONS_CHANGE_PICTURE },
    { "deviceGroupBrightness", IDS_OPTIONS_SETTINGS_BRIGHTNESS_DESCRIPTION },
    { "deviceGroupKeyboard", IDS_OPTIONS_DEVICE_GROUP_KEYBOARD_SECTION },
    { "deviceGroupPointer", IDS_OPTIONS_DEVICE_GROUP_POINTER_SECTION },
    { "enableScreenlock", IDS_OPTIONS_ENABLE_SCREENLOCKER_CHECKBOX },
    { "internetOptionsButtonTitle", IDS_OPTIONS_INTERNET_OPTIONS_BUTTON_TITLE },
    { "keyboardSettingsButtonTitle",
      IDS_OPTIONS_DEVICE_GROUP_KEYBOARD_SETTINGS_BUTTON_TITLE },
    { "manageAccountsButtonTitle", IDS_OPTIONS_ACCOUNTS_BUTTON_TITLE },
    { "pointerSensitivityLess",
      IDS_OPTIONS_SETTINGS_SENSITIVITY_LESS_DESCRIPTION },
    { "pointerSensitivityMore",
      IDS_OPTIONS_SETTINGS_SENSITIVITY_MORE_DESCRIPTION },
    { "pointerSettingsButtonTitle",
      IDS_OPTIONS_DEVICE_GROUP_POINTER_SETTINGS_BUTTON_TITLE },
    { "sectionTitleDevice", IDS_OPTIONS_DEVICE_GROUP_NAME },
    { "sectionTitleInternet", IDS_OPTIONS_INTERNET_OPTIONS_GROUP_LABEL },
#endif
  };

  RegisterStrings(localized_strings, resources, arraysize(resources));
  RegisterTitle(localized_strings, "browserPage", IDS_SETTINGS_TITLE);

  localized_strings->SetString(
      "syncOverview",
      l10n_util::GetStringFUTF16(IDS_SYNC_OVERVIEW,
                                 l10n_util::GetStringUTF16(IDS_PRODUCT_NAME)));

  localized_strings->SetString("syncLearnMoreURL", chrome::kSyncLearnMoreURL);
  localized_strings->SetString(
      "profilesSingleUser",
      l10n_util::GetStringFUTF16(IDS_PROFILES_SINGLE_USER_MESSAGE,
                                 l10n_util::GetStringUTF16(IDS_PRODUCT_NAME)));

  string16 learn_more_url = ASCIIToUTF16(chrome::kInstantLearnMoreURL);
  localized_strings->SetString(
      "defaultSearchGroupLabel",
      l10n_util::GetStringFUTF16(IDS_SEARCH_PREF_EXPLANATION, learn_more_url));
  localized_strings->SetString(
      "instantPrefAndWarning",
      l10n_util::GetStringFUTF16(IDS_INSTANT_PREF_WITH_WARNING,
                                 learn_more_url));
  localized_strings->SetString("instantLearnMoreLink", learn_more_url);

  localized_strings->SetString(
      "defaultBrowserUnknown",
      l10n_util::GetStringFUTF16(IDS_OPTIONS_DEFAULTBROWSER_UNKNOWN,
          l10n_util::GetStringUTF16(IDS_PRODUCT_NAME)));
  localized_strings->SetString(
      "defaultBrowserUseAsDefault",
      l10n_util::GetStringFUTF16(IDS_OPTIONS_DEFAULTBROWSER_USEASDEFAULT,
          l10n_util::GetStringUTF16(IDS_PRODUCT_NAME)));
  localized_strings->SetString(
      "autoLaunchText",
      l10n_util::GetStringFUTF16(IDS_AUTOLAUNCH_TEXT,
          l10n_util::GetStringUTF16(IDS_PRODUCT_NAME)));

#if defined(OS_CHROMEOS)
  if (chromeos::UserManager::Get()->user_is_logged_in()) {
    localized_strings->SetString("username",
        chromeos::UserManager::Get()->logged_in_user().email());
  }
#endif
}

void BrowserOptionsHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "becomeDefaultBrowser",
      base::Bind(&BrowserOptionsHandler::BecomeDefaultBrowser,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "setDefaultSearchEngine",
      base::Bind(&BrowserOptionsHandler::SetDefaultSearchEngine,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "requestAutocompleteSuggestions",
      base::Bind(&BrowserOptionsHandler::RequestAutocompleteSuggestions,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "enableInstant",
      base::Bind(&BrowserOptionsHandler::EnableInstant,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "disableInstant",
      base::Bind(&BrowserOptionsHandler::DisableInstant,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getInstantFieldTrialStatus",
      base::Bind(&BrowserOptionsHandler::GetInstantFieldTrialStatus,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "createProfile",
      base::Bind(&BrowserOptionsHandler::CreateProfile,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "themesReset",
      base::Bind(&BrowserOptionsHandler::ThemesReset,
                 base::Unretained(this)));
#if defined(TOOLKIT_GTK)
  web_ui()->RegisterMessageCallback(
      "themesSetGTK",
      base::Bind(&BrowserOptionsHandler::ThemesSetGTK,
                 base::Unretained(this)));
#endif
#if defined(OS_CHROMEOS)
  web_ui()->RegisterMessageCallback(
      "decreaseScreenBrightness",
      base::Bind(&BrowserOptionsHandler::DecreaseScreenBrightnessCallback,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "increaseScreenBrightness",
      base::Bind(&BrowserOptionsHandler::IncreaseScreenBrightnessCallback,
                 base::Unretained(this)));
#endif
}

void BrowserOptionsHandler::OnStateChanged() {
  string16 status_label;
  string16 link_label;
  ProfileSyncService* service(ProfileSyncServiceFactory::
      GetInstance()->GetForProfile(Profile::FromWebUI(web_ui())));
  DCHECK(service);
  bool managed = service->IsManaged();
  bool sync_setup_completed = service->HasSyncSetupCompleted();
  bool status_has_error = sync_ui_util::GetStatusLabels(
      service, sync_ui_util::WITH_HTML, &status_label, &link_label) ==
          sync_ui_util::SYNC_ERROR;

  string16 start_stop_button_label;
  bool is_start_stop_button_visible = false;
  bool is_start_stop_button_enabled = false;
  if (sync_setup_completed) {
    start_stop_button_label =
        l10n_util::GetStringUTF16(IDS_SYNC_STOP_SYNCING_BUTTON_LABEL);
#if defined(OS_CHROMEOS)
    is_start_stop_button_visible = false;
#else
    is_start_stop_button_visible = true;
#endif  // defined(OS_CHROMEOS)
    is_start_stop_button_enabled = !managed;
  } else if (service->SetupInProgress()) {
    start_stop_button_label =
        l10n_util::GetStringUTF16(IDS_SYNC_NTP_SETUP_IN_PROGRESS);
    is_start_stop_button_visible = true;
    is_start_stop_button_enabled = false;
  } else {
    start_stop_button_label =
        l10n_util::GetStringFUTF16(IDS_SYNC_START_SYNC_BUTTON_LABEL,
            l10n_util::GetStringUTF16(IDS_SHORT_PRODUCT_NAME));
    is_start_stop_button_visible = true;
    is_start_stop_button_enabled = !managed;
  }

  scoped_ptr<Value> completed(Value::CreateBooleanValue(sync_setup_completed));
  web_ui()->CallJavascriptFunction("BrowserOptions.setSyncSetupCompleted",
                                   *completed);

  scoped_ptr<Value> label(Value::CreateStringValue(status_label));
  web_ui()->CallJavascriptFunction("BrowserOptions.setSyncStatus", *label);

  scoped_ptr<Value> enabled(
      Value::CreateBooleanValue(is_start_stop_button_enabled));
  web_ui()->CallJavascriptFunction("BrowserOptions.setStartStopButtonEnabled",
                                   *enabled);

  scoped_ptr<Value> visible(
      Value::CreateBooleanValue(is_start_stop_button_visible));
  web_ui()->CallJavascriptFunction("BrowserOptions.setStartStopButtonVisible",
                                   *visible);

  label.reset(Value::CreateStringValue(start_stop_button_label));
  web_ui()->CallJavascriptFunction("BrowserOptions.setStartStopButtonLabel",
                                   *label);

  label.reset(Value::CreateStringValue(link_label));
  web_ui()->CallJavascriptFunction("BrowserOptions.setSyncActionLinkLabel",
                                   *label);

  enabled.reset(Value::CreateBooleanValue(!managed));
  web_ui()->CallJavascriptFunction("BrowserOptions.setSyncActionLinkEnabled",
                                   *enabled);

  visible.reset(Value::CreateBooleanValue(status_has_error));
  web_ui()->CallJavascriptFunction("BrowserOptions.setSyncStatusErrorVisible",
                                   *visible);

  enabled.reset(Value::CreateBooleanValue(
      !service->unrecoverable_error_detected()));
  web_ui()->CallJavascriptFunction(
      "BrowserOptions.setCustomizeSyncButtonEnabled",
      *enabled);

  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kEnableAutologin)) {
    visible.reset(Value::CreateBooleanValue(
        service->AreCredentialsAvailable()));
    web_ui()->CallJavascriptFunction("BrowserOptions.setAutoLoginVisible",
                                     *visible);
  }

  // Set profile creation text and button if multi-profiles switch is on.
  visible.reset(Value::CreateBooleanValue(multiprofile_));
  web_ui()->CallJavascriptFunction("BrowserOptions.setProfilesSectionVisible",
                                   *visible);
  if (multiprofile_)
    SendProfilesInfo();
}

void BrowserOptionsHandler::Initialize() {
  Profile* profile = Profile::FromWebUI(web_ui());

  ProfileSyncService* sync_service(ProfileSyncServiceFactory::
      GetInstance()->GetForProfile(Profile::FromWebUI(web_ui())));
  if (sync_service) {
    sync_service->AddObserver(this);
    OnStateChanged();
  } else {
    web_ui()->CallJavascriptFunction("options.BrowserOptions.hideSyncSection");
  }

  // Create our favicon data source.
  profile->GetChromeURLDataManager()->AddDataSource(
      new FaviconSource(profile, FaviconSource::FAVICON));

  homepage_.Init(prefs::kHomePage, profile->GetPrefs(), NULL);
  default_browser_policy_.Init(prefs::kDefaultBrowserSettingEnabled,
                               g_browser_process->local_state(),
                               this);
  UpdateDefaultBrowserState();

  pref_change_registrar_.Init(profile->GetPrefs());
  pref_change_registrar_.Add(prefs::kHomePageIsNewTabPage, this);
  pref_change_registrar_.Add(prefs::kHomePage, this);

  registrar_.Add(this, chrome::NOTIFICATION_PROFILE_CACHED_INFO_CHANGED,
                 content::NotificationService::AllSources());
#if defined(OS_CHROMEOS)
  registrar_.Add(this, chrome::NOTIFICATION_LOGIN_USER_IMAGE_CHANGED,
                 content::NotificationService::AllSources());
#endif
  registrar_.Add(this, chrome::NOTIFICATION_BROWSER_THEME_CHANGED,
                 content::Source<ThemeService>(
                     ThemeServiceFactory::GetForProfile(profile)));

  UpdateSearchEngines();
  UpdateHomePageLabel();
  ObserveThemeChanged();

  autocomplete_controller_.reset(new AutocompleteController(profile, this));

#if defined(OS_WIN)
  BrowserThread::PostTask(BrowserThread::FILE, FROM_HERE,
      base::Bind(&BrowserOptionsHandler::CheckAutoLaunch,
                 weak_ptr_factory_for_ui_.GetWeakPtr(),
                 weak_ptr_factory_for_file_.GetWeakPtr()));
  weak_ptr_factory_for_ui_.DetachFromThread();
#endif
}

void BrowserOptionsHandler::CheckAutoLaunch(
    base::WeakPtr<BrowserOptionsHandler> weak_this) {
#if defined(OS_WIN)
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));

  // Pass in weak pointer to this to avoid race if BrowserOptionsHandler is
  // deleted.
  BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
      base::Bind(&BrowserOptionsHandler::CheckAutoLaunchCallback,
                 weak_this,
                 auto_launch_trial::IsInAutoLaunchGroup(),
                 auto_launch_util::WillLaunchAtLogin(FilePath())));
#endif
}

void BrowserOptionsHandler::CheckAutoLaunchCallback(
    bool is_in_auto_launch_group,
    bool will_launch_at_login) {
#if defined(OS_WIN)
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (is_in_auto_launch_group) {
    web_ui()->RegisterMessageCallback("toggleAutoLaunch",
        base::Bind(&BrowserOptionsHandler::ToggleAutoLaunch,
        base::Unretained(this)));

    base::FundamentalValue enabled(will_launch_at_login);
    web_ui()->CallJavascriptFunction("BrowserOptions.updateAutoLaunchState",
      enabled);
  }
#endif
}

void BrowserOptionsHandler::UpdateDefaultBrowserState() {
  // Check for side-by-side first.
  if (!ShellIntegration::CanSetAsDefaultBrowser()) {
    SetDefaultBrowserUIString(IDS_OPTIONS_DEFAULTBROWSER_SXS);
    return;
  }

#if defined(OS_MACOSX)
  ShellIntegration::DefaultWebClientState state =
      ShellIntegration::IsDefaultBrowser();
  int status_string_id;
  if (state == ShellIntegration::IS_DEFAULT_WEB_CLIENT)
    status_string_id = IDS_OPTIONS_DEFAULTBROWSER_DEFAULT;
  else if (state == ShellIntegration::NOT_DEFAULT_WEB_CLIENT)
    status_string_id = IDS_OPTIONS_DEFAULTBROWSER_NOTDEFAULT;
  else
    status_string_id = IDS_OPTIONS_DEFAULTBROWSER_UNKNOWN;

  SetDefaultBrowserUIString(status_string_id);
#else
  default_browser_worker_->StartCheckIsDefault();
#endif
}

void BrowserOptionsHandler::BecomeDefaultBrowser(const ListValue* args) {
  // If the default browser setting is managed then we should not be able to
  // call this function.
  if (default_browser_policy_.IsManaged())
    return;

  content::RecordAction(UserMetricsAction("Options_SetAsDefaultBrowser"));
#if defined(OS_MACOSX)
  if (ShellIntegration::SetAsDefaultBrowser())
    UpdateDefaultBrowserState();
#else
  default_browser_worker_->StartSetAsDefault();
  // Callback takes care of updating UI.
#endif

  // If the user attempted to make Chrome the default browser, then he/she
  // arguably wants to be notified when that changes.
  PrefService* prefs = Profile::FromWebUI(web_ui())->GetPrefs();
  prefs->SetBoolean(prefs::kCheckDefaultBrowser, true);
}

int BrowserOptionsHandler::StatusStringIdForState(
    ShellIntegration::DefaultWebClientState state) {
  if (state == ShellIntegration::IS_DEFAULT_WEB_CLIENT)
    return IDS_OPTIONS_DEFAULTBROWSER_DEFAULT;
  if (state == ShellIntegration::NOT_DEFAULT_WEB_CLIENT)
    return IDS_OPTIONS_DEFAULTBROWSER_NOTDEFAULT;
  return IDS_OPTIONS_DEFAULTBROWSER_UNKNOWN;
}

void BrowserOptionsHandler::SetDefaultWebClientUIState(
    ShellIntegration::DefaultWebClientUIState state) {
  int status_string_id;
  if (state == ShellIntegration::STATE_IS_DEFAULT)
    status_string_id = IDS_OPTIONS_DEFAULTBROWSER_DEFAULT;
  else if (state == ShellIntegration::STATE_NOT_DEFAULT)
    status_string_id = IDS_OPTIONS_DEFAULTBROWSER_NOTDEFAULT;
  else if (state == ShellIntegration::STATE_UNKNOWN)
    status_string_id = IDS_OPTIONS_DEFAULTBROWSER_UNKNOWN;
  else
    return;  // Still processing.

  SetDefaultBrowserUIString(status_string_id);
}

void BrowserOptionsHandler::SetDefaultBrowserUIString(int status_string_id) {
  scoped_ptr<Value> status_string(Value::CreateStringValue(
      l10n_util::GetStringFUTF16(status_string_id,
                                 l10n_util::GetStringUTF16(IDS_PRODUCT_NAME))));

  scoped_ptr<Value> is_default(Value::CreateBooleanValue(
      status_string_id == IDS_OPTIONS_DEFAULTBROWSER_DEFAULT));

  scoped_ptr<Value> can_be_default(Value::CreateBooleanValue(
      !default_browser_policy_.IsManaged() &&
      (status_string_id == IDS_OPTIONS_DEFAULTBROWSER_DEFAULT ||
       status_string_id == IDS_OPTIONS_DEFAULTBROWSER_NOTDEFAULT)));

  web_ui()->CallJavascriptFunction(
      "BrowserOptions.updateDefaultBrowserState",
      *status_string, *is_default, *can_be_default);
}

void BrowserOptionsHandler::OnTemplateURLServiceChanged() {
  if (!template_url_service_ || !template_url_service_->loaded())
    return;

  const TemplateURL* default_url =
      template_url_service_->GetDefaultSearchProvider();

  int default_index = 0;
  ListValue search_engines;
  std::vector<const TemplateURL*> model_urls =
      template_url_service_->GetTemplateURLs();
  for (size_t i = 0; i < model_urls.size(); ++i) {
    if (!model_urls[i]->ShowInDefaultList())
      continue;

    DictionaryValue* entry = new DictionaryValue();
    entry->SetString("name", model_urls[i]->short_name());
    entry->SetInteger("index", i);
    search_engines.Append(entry);
    if (model_urls[i] == default_url)
      default_index = i;
  }

  scoped_ptr<Value> default_value(Value::CreateIntegerValue(default_index));
  scoped_ptr<Value> default_managed(Value::CreateBooleanValue(
      template_url_service_->is_default_search_managed()));

  web_ui()->CallJavascriptFunction("BrowserOptions.updateSearchEngines",
                                   search_engines, *default_value,
                                   *default_managed);
}

void BrowserOptionsHandler::SetDefaultSearchEngine(const ListValue* args) {
  int selected_index = -1;
  if (!ExtractIntegerValue(args, &selected_index)) {
    NOTREACHED();
    return;
  }

  std::vector<const TemplateURL*> model_urls =
      template_url_service_->GetTemplateURLs();
  if (selected_index >= 0 &&
      selected_index < static_cast<int>(model_urls.size()))
    template_url_service_->SetDefaultSearchProvider(model_urls[selected_index]);

  content::RecordAction(UserMetricsAction("Options_SearchEngineChanged"));
}

void BrowserOptionsHandler::UpdateSearchEngines() {
  template_url_service_ =
      TemplateURLServiceFactory::GetForProfile(Profile::FromWebUI(web_ui()));
  if (template_url_service_) {
    template_url_service_->Load();
    template_url_service_->AddObserver(this);
    OnTemplateURLServiceChanged();
  }
}

void BrowserOptionsHandler::UpdateHomePageLabel() const {
  Profile* profile = Profile::FromWebUI(web_ui());
  PrefService* prefs = profile->GetPrefs();
  scoped_ptr<Value> label;
  string16 str;

  if (prefs->GetBoolean(prefs::kHomePageIsNewTabPage)) {
    str = l10n_util::GetStringUTF16(IDS_OPTIONS_SHOW_HOME_BUTTON_FOR_NTP);
  } else {
    str = l10n_util::GetStringFUTF16(
        IDS_OPTIONS_SHOW_HOME_BUTTON_FOR_URL,
        UTF8ToUTF16(prefs->GetString(prefs::kHomePage)));
  }

  label.reset(Value::CreateStringValue(str));
  web_ui()->CallJavascriptFunction("BrowserOptions.updateHomePageLabel",
                                   *label);
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
    std::string* pref = content::Details<std::string>(details).ptr();
    if (*pref == prefs::kDefaultBrowserSettingEnabled) {
      UpdateDefaultBrowserState();
    } else if (*pref == prefs::kHomePageIsNewTabPage ||
               *pref == prefs::kHomePage) {
      UpdateHomePageLabel();
    } else {
      NOTREACHED();
    }
  } else if (multiprofile_ &&
             type == chrome::NOTIFICATION_PROFILE_CACHED_INFO_CHANGED) {
    SendProfilesInfo();
  } else {
    NOTREACHED();
  }
}

void BrowserOptionsHandler::RequestAutocompleteSuggestions(
    const ListValue* args) {
  string16 input;
  CHECK_EQ(args->GetSize(), 1U);
  CHECK(args->GetString(0, &input));

  autocomplete_controller_->Start(input, string16(), true, false, false,
                                  AutocompleteInput::ALL_MATCHES);
}

void BrowserOptionsHandler::EnableInstant(const ListValue* args) {
  InstantController::Enable(Profile::FromWebUI(web_ui()));
}

void BrowserOptionsHandler::DisableInstant(const ListValue* args) {
  InstantController::Disable(Profile::FromWebUI(web_ui()));
}

void BrowserOptionsHandler::ToggleAutoLaunch(const ListValue* args) {
#if defined(OS_WIN)
  if (!auto_launch_trial::IsInAutoLaunchGroup())
    return;

  bool enable;
  CHECK_EQ(args->GetSize(), 1U);
  CHECK(args->GetBoolean(0, &enable));

  // Make sure we keep track of how many disable and how many enable.
  auto_launch_trial::UpdateToggleAutoLaunchMetric(enable);
  content::BrowserThread::PostTask(
      content::BrowserThread::FILE, FROM_HERE,
      base::Bind(&auto_launch_util::SetWillLaunchAtLogin, enable, FilePath()));
#endif  // OS_WIN
}

void BrowserOptionsHandler::GetInstantFieldTrialStatus(const ListValue* args) {
  Profile* profile = Profile::FromWebUI(web_ui());
  base::FundamentalValue enabled(
      InstantFieldTrial::IsInstantExperiment(profile) &&
      !InstantFieldTrial::IsHiddenExperiment(profile));
  web_ui()->CallJavascriptFunction("BrowserOptions.setInstantFieldTrialStatus",
                                   enabled);
}

void BrowserOptionsHandler::OnResultChanged(bool default_match_changed) {
  const AutocompleteResult& result = autocomplete_controller_->result();
  ListValue suggestions;
  for (size_t i = 0; i < result.size(); ++i) {
    const AutocompleteMatch& match = result.match_at(i);
    AutocompleteMatch::Type type = match.type;
    if (type != AutocompleteMatch::HISTORY_URL &&
        type != AutocompleteMatch::HISTORY_TITLE &&
        type != AutocompleteMatch::HISTORY_BODY &&
        type != AutocompleteMatch::HISTORY_KEYWORD &&
        type != AutocompleteMatch::NAVSUGGEST)
      continue;
    DictionaryValue* entry = new DictionaryValue();
    entry->SetString("title", match.description);
    entry->SetString("displayURL", match.contents);
    entry->SetString("url", match.destination_url.spec());
    suggestions.Append(entry);
  }

  web_ui()->CallJavascriptFunction(
      "BrowserOptions.updateAutocompleteSuggestions", suggestions);
}

void BrowserOptionsHandler::SendProfilesInfo() {
  ProfileInfoCache& cache =
      g_browser_process->profile_manager()->GetProfileInfoCache();
  ListValue profile_info_list;
  FilePath current_profile_path =
      web_ui()->GetWebContents()->GetBrowserContext()->GetPath();
  for (size_t i = 0, e = cache.GetNumberOfProfiles(); i < e; ++i) {
    DictionaryValue* profile_value = new DictionaryValue();
    FilePath profile_path = cache.GetPathOfProfileAtIndex(i);
    profile_value->SetString("name", cache.GetNameOfProfileAtIndex(i));
    profile_value->Set("filePath", base::CreateFilePathValue(profile_path));
    profile_value->SetBoolean("isCurrentProfile",
                              profile_path == current_profile_path);

    bool is_gaia_picture =
        cache.IsUsingGAIAPictureOfProfileAtIndex(i) &&
        cache.GetGAIAPictureOfProfileAtIndex(i);
    if (is_gaia_picture) {
      gfx::Image icon = profiles::GetAvatarIconForWebUI(
          cache.GetAvatarIconOfProfileAtIndex(i), true);
      profile_value->SetString("iconURL", web_ui_util::GetImageDataUrl(icon));
    } else {
      size_t icon_index = cache.GetAvatarIconIndexOfProfileAtIndex(i);
      profile_value->SetString("iconURL",
                               cache.GetDefaultAvatarIconUrl(icon_index));
    }

    profile_info_list.Append(profile_value);
  }

  web_ui()->CallJavascriptFunction("BrowserOptions.setProfilesInfo",
                                   profile_info_list);
}

void BrowserOptionsHandler::CreateProfile(const ListValue* args) {
  ProfileManager::CreateMultiProfileAsync();
}

void BrowserOptionsHandler::ObserveThemeChanged() {
  Profile* profile = Profile::FromWebUI(web_ui());
#if defined(TOOLKIT_GTK)
  GtkThemeService* theme_service = GtkThemeService::GetFrom(profile);
  bool is_gtk_theme = theme_service->UsingNativeTheme();
  base::FundamentalValue gtk_enabled(!is_gtk_theme);
  web_ui()->CallJavascriptFunction("BrowserOptions.setGtkThemeButtonEnabled",
                                   gtk_enabled);
#else
  ThemeService* theme_service = ThemeServiceFactory::GetForProfile(profile);
  bool is_gtk_theme = false;
#endif

  bool is_classic_theme = !is_gtk_theme && theme_service->UsingDefaultTheme();
  base::FundamentalValue enabled(!is_classic_theme);
  web_ui()->CallJavascriptFunction("BrowserOptions.setThemesResetButtonEnabled",
                                   enabled);
}

void BrowserOptionsHandler::ThemesReset(const ListValue* args) {
  content::RecordAction(UserMetricsAction("Options_ThemesReset"));
  Profile* profile = Profile::FromWebUI(web_ui());
  ThemeServiceFactory::GetForProfile(profile)->UseDefaultTheme();
}

#if defined(TOOLKIT_GTK)
void BrowserOptionsHandler::ThemesSetGTK(const ListValue* args) {
  content::RecordAction(UserMetricsAction("Options_GtkThemeSet"));
  Profile* profile = Profile::FromWebUI(web_ui());
  ThemeServiceFactory::GetForProfile(profile)->SetNativeTheme();
}
#endif

#if defined(OS_CHROMEOS)
void BrowserOptionsHandler::UpdateAccountPicture() {
  std::string email = chromeos::UserManager::Get()->logged_in_user().email();
  if (!email.empty()) {
    web_ui()->CallJavascriptFunction("BrowserOptions.updateAccountPicture");
    base::StringValue email_value(email);
    web_ui()->CallJavascriptFunction("BrowserOptions.updateAccountPicture",
                                     email_value);
  }
}

void BrowserOptionsHandler::DecreaseScreenBrightnessCallback(
    const ListValue* args) {
  // Do not allow the options button to turn off the backlight, as that
  // can make it very difficult to see the increase brightness button.
  chromeos::DBusThreadManager::Get()->GetPowerManagerClient()->
      DecreaseScreenBrightness(false);
}

void BrowserOptionsHandler::IncreaseScreenBrightnessCallback(
    const ListValue* args) {
  chromeos::DBusThreadManager::Get()->GetPowerManagerClient()->
      IncreaseScreenBrightness();
}
#endif

}  // namespace options2
