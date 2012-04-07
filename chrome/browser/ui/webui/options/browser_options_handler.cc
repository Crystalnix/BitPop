// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/options/browser_options_handler.h"

#include "base/basictypes.h"
#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/memory/singleton.h"
#include "base/path_service.h"
#include "base/string_number_conversions.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
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
#include "chrome/browser/search_engines/template_url.h"
#include "chrome/browser/search_engines/template_url_service.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/webui/favicon_source.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/user_metrics.h"
#include "content/public/browser/web_ui.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"

#if defined(OS_WIN)
#include "chrome/installer/util/auto_launch_util.h"
#endif

using content::BrowserThread;
using content::UserMetricsAction;

BrowserOptionsHandler::BrowserOptionsHandler()
    : template_url_service_(NULL),
      startup_custom_pages_table_model_(NULL),
      ALLOW_THIS_IN_INITIALIZER_LIST(weak_ptr_factory_for_file_(this)),
      ALLOW_THIS_IN_INITIALIZER_LIST(weak_ptr_factory_for_ui_(this)) {
#if !defined(OS_MACOSX)
  default_browser_worker_ = new ShellIntegration::DefaultBrowserWorker(this);
#endif
}

BrowserOptionsHandler::~BrowserOptionsHandler() {
  if (default_browser_worker_.get())
    default_browser_worker_->ObserverDestroyed();
  if (template_url_service_)
    template_url_service_->RemoveObserver(this);
}

void BrowserOptionsHandler::GetLocalizedValues(
    DictionaryValue* localized_strings) {
  DCHECK(localized_strings);

  static OptionsStringResource resources[] = {
    { "startupGroupName", IDS_OPTIONS_STARTUP_GROUP_NAME },
    { "startupShowDefaultAndNewTab",
      IDS_OPTIONS_STARTUP_SHOW_DEFAULT_AND_NEWTAB},
    { "startupShowLastSession", IDS_OPTIONS_STARTUP_SHOW_LAST_SESSION },
    { "startupShowPages", IDS_OPTIONS_STARTUP_SHOW_PAGES },
    { "startupAddLabel", IDS_OPTIONS_STARTUP_ADD_LABEL },
    { "startupUseCurrent", IDS_OPTIONS_STARTUP_USE_CURRENT },
    { "homepageGroupName", IDS_OPTIONS_HOMEPAGE_GROUP_NAME },
    { "homepageUseNewTab", IDS_OPTIONS_HOMEPAGE_USE_NEWTAB },
    { "homepageUseURL", IDS_OPTIONS_HOMEPAGE_USE_URL },
    { "toolbarGroupName", IDS_OPTIONS_TOOLBAR_GROUP_NAME },
    { "toolbarShowHomeButton", IDS_OPTIONS_TOOLBAR_SHOW_HOME_BUTTON },
    { "toolbarShowBookmarksBar", IDS_OPTIONS_TOOLBAR_SHOW_BOOKMARKS_BAR },
    { "defaultSearchGroupName", IDS_OPTIONS_DEFAULTSEARCH_GROUP_NAME },
    { "defaultSearchManageEngines", IDS_OPTIONS_DEFAULTSEARCH_MANAGE_ENGINES },
    { "instantName", IDS_INSTANT_PREF },
    { "instantWarningText", IDS_INSTANT_PREF_WARNING },
    { "instantConfirmTitle", IDS_INSTANT_OPT_IN_TITLE },
    { "instantConfirmMessage", IDS_INSTANT_OPT_IN_MESSAGE },
    { "defaultBrowserGroupName", IDS_OPTIONS_DEFAULTBROWSER_GROUP_NAME },
  };

  RegisterStrings(localized_strings, resources, arraysize(resources));
  RegisterTitle(localized_strings, "browserPage",
                IDS_OPTIONS_GENERAL_TAB_LABEL);

  localized_strings->SetString("instantLearnMoreLink",
      ASCIIToUTF16(chrome::kInstantLearnMoreURL));
  localized_strings->SetString("defaultBrowserUnknown",
      l10n_util::GetStringFUTF16(IDS_OPTIONS_DEFAULTBROWSER_UNKNOWN,
          l10n_util::GetStringUTF16(IDS_PRODUCT_NAME)));
  localized_strings->SetString("defaultBrowserUseAsDefault",
      l10n_util::GetStringFUTF16(IDS_OPTIONS_DEFAULTBROWSER_USEASDEFAULT,
          l10n_util::GetStringUTF16(IDS_PRODUCT_NAME)));
  localized_strings->SetString("autoLaunchText",
      l10n_util::GetStringFUTF16(IDS_AUTOLAUNCH_TEXT,
          l10n_util::GetStringUTF16(IDS_PRODUCT_NAME)));
}

void BrowserOptionsHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback("becomeDefaultBrowser",
      base::Bind(&BrowserOptionsHandler::BecomeDefaultBrowser,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("setDefaultSearchEngine",
      base::Bind(&BrowserOptionsHandler::SetDefaultSearchEngine,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("removeStartupPages",
      base::Bind(&BrowserOptionsHandler::RemoveStartupPages,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("addStartupPage",
      base::Bind(&BrowserOptionsHandler::AddStartupPage,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("editStartupPage",
      base::Bind(&BrowserOptionsHandler::EditStartupPage,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("setStartupPagesToCurrentPages",
      base::Bind(&BrowserOptionsHandler::SetStartupPagesToCurrentPages,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("dragDropStartupPage",
      base::Bind(&BrowserOptionsHandler::DragDropStartupPage,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("requestAutocompleteSuggestions",
      base::Bind(&BrowserOptionsHandler::RequestAutocompleteSuggestions,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("enableInstant",
      base::Bind(&BrowserOptionsHandler::EnableInstant,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("disableInstant",
      base::Bind(&BrowserOptionsHandler::DisableInstant,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("getInstantFieldTrialStatus",
      base::Bind(&BrowserOptionsHandler::GetInstantFieldTrialStatus,
                 base::Unretained(this)));
}

void BrowserOptionsHandler::Initialize() {
  Profile* profile = Profile::FromWebUI(web_ui());

  // Create our favicon data source.
  profile->GetChromeURLDataManager()->AddDataSource(
      new FaviconSource(profile, FaviconSource::FAVICON));

  homepage_.Init(prefs::kHomePage, profile->GetPrefs(), NULL);
  default_browser_policy_.Init(prefs::kDefaultBrowserSettingEnabled,
                               g_browser_process->local_state(),
                               this);
  UpdateDefaultBrowserState();

  startup_custom_pages_table_model_.reset(
      new CustomHomePagesTableModel(profile));
  startup_custom_pages_table_model_->SetObserver(this);
  UpdateStartupPages();

  pref_change_registrar_.Init(profile->GetPrefs());
  pref_change_registrar_.Add(prefs::kURLsToRestoreOnStartup, this);

  UpdateSearchEngines();

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

void BrowserOptionsHandler::UpdateStartupPages() {
  Profile* profile = Profile::FromWebUI(web_ui());
  const SessionStartupPref startup_pref =
      SessionStartupPref::GetStartupPref(profile->GetPrefs());
  startup_custom_pages_table_model_->SetURLs(startup_pref.urls);
}

void BrowserOptionsHandler::OnModelChanged() {
  ListValue startup_pages;
  int page_count = startup_custom_pages_table_model_->RowCount();
  std::vector<GURL> urls = startup_custom_pages_table_model_->GetURLs();
  for (int i = 0; i < page_count; ++i) {
    DictionaryValue* entry = new DictionaryValue();
    entry->SetString("title", startup_custom_pages_table_model_->GetText(i, 0));
    entry->SetString("url", urls[i].spec());
    entry->SetString("tooltip",
                     startup_custom_pages_table_model_->GetTooltip(i));
    entry->SetString("modelIndex", base::IntToString(i));
    startup_pages.Append(entry);
  }

  web_ui()->CallJavascriptFunction("BrowserOptions.updateStartupPages",
                                   startup_pages);
}

void BrowserOptionsHandler::OnItemsChanged(int start, int length) {
  OnModelChanged();
}

void BrowserOptionsHandler::OnItemsAdded(int start, int length) {
  OnModelChanged();
}

void BrowserOptionsHandler::OnItemsRemoved(int start, int length) {
  OnModelChanged();
}

void BrowserOptionsHandler::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  if (type == chrome::NOTIFICATION_PREF_CHANGED) {
    std::string* pref = content::Details<std::string>(details).ptr();
    if (*pref == prefs::kDefaultBrowserSettingEnabled) {
      UpdateDefaultBrowserState();
    } else if (*pref == prefs::kURLsToRestoreOnStartup) {
      UpdateStartupPages();
    } else {
      NOTREACHED();
    }
  } else {
    NOTREACHED();
  }
}

void BrowserOptionsHandler::SetStartupPagesToCurrentPages(
    const ListValue* args) {
  startup_custom_pages_table_model_->SetToCurrentlyOpenPages();
  SaveStartupPagesPref();
}

void BrowserOptionsHandler::RemoveStartupPages(const ListValue* args) {
  for (int i = args->GetSize() - 1; i >= 0; --i) {
    std::string string_value;
    CHECK(args->GetString(i, &string_value));

    int selected_index;
    base::StringToInt(string_value, &selected_index);
    if (selected_index < 0 ||
        selected_index >= startup_custom_pages_table_model_->RowCount()) {
      NOTREACHED();
      return;
    }
    startup_custom_pages_table_model_->Remove(selected_index);
  }

  SaveStartupPagesPref();
}

void BrowserOptionsHandler::AddStartupPage(const ListValue* args) {
  std::string url_string;
  CHECK_EQ(args->GetSize(), 1U);
  CHECK(args->GetString(0, &url_string));

  GURL url = URLFixerUpper::FixupURL(url_string, std::string());
  if (!url.is_valid())
    return;
  int index = startup_custom_pages_table_model_->RowCount();
  startup_custom_pages_table_model_->Add(index, url);
  SaveStartupPagesPref();
}

void BrowserOptionsHandler::EditStartupPage(const ListValue* args) {
  std::string url_string;
  std::string index_string;
  int index;
  CHECK_EQ(args->GetSize(), 2U);
  CHECK(args->GetString(0, &index_string));
  CHECK(base::StringToInt(index_string, &index));
  CHECK(args->GetString(1, &url_string));

  if (index < 0 || index > startup_custom_pages_table_model_->RowCount()) {
    NOTREACHED();
    return;
  }

  std::vector<GURL> urls = startup_custom_pages_table_model_->GetURLs();
  urls[index] = URLFixerUpper::FixupURL(url_string, std::string());
  startup_custom_pages_table_model_->SetURLs(urls);
  SaveStartupPagesPref();
}

void BrowserOptionsHandler::DragDropStartupPage(const ListValue* args) {
  CHECK_EQ(args->GetSize(), 2U);

  std::string value;
  int to_index;

  CHECK(args->GetString(0, &value));
  base::StringToInt(value, &to_index);

  ListValue* selected;
  CHECK(args->GetList(1, &selected));

  std::vector<int> index_list;
  for (size_t i = 0; i < selected->GetSize(); ++i) {
    int index;
    CHECK(selected->GetString(i, &value));
    base::StringToInt(value, &index);
    index_list.push_back(index);
  }

  startup_custom_pages_table_model_->MoveURLs(to_index, index_list);
  SaveStartupPagesPref();
}

void BrowserOptionsHandler::SaveStartupPagesPref() {
  PrefService* prefs = Profile::FromWebUI(web_ui())->GetPrefs();

  SessionStartupPref pref = SessionStartupPref::GetStartupPref(prefs);
  pref.urls = startup_custom_pages_table_model_->GetURLs();

  SessionStartupPref::SetStartupPref(prefs, pref);
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
