// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/options/core_options_handler.h"

#include "base/json/json_reader.h"
#include "base/memory/scoped_ptr.h"
#include "base/string16.h"
#include "base/string_number_conversions.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/google/google_util.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "content/browser/user_metrics.h"
#include "content/common/notification_details.h"
#include "content/common/notification_type.h"
#include "googleurl/src/gurl.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/locale_settings.h"
#include "grit/theme_resources.h"
#include "ui/base/l10n/l10n_util.h"

CoreOptionsHandler::CoreOptionsHandler()
    : handlers_host_(NULL) {
}

CoreOptionsHandler::~CoreOptionsHandler() {}

void CoreOptionsHandler::Initialize() {
  clear_plugin_lso_data_enabled_.Init(prefs::kClearPluginLSODataEnabled,
                                      g_browser_process->local_state(),
                                      this);
  UpdateClearPluginLSOData();
}

void CoreOptionsHandler::GetLocalizedValues(
    DictionaryValue* localized_strings) {
  DCHECK(localized_strings);
  // Main
  localized_strings->SetString("title",
      l10n_util::GetStringUTF16(IDS_SETTINGS_TITLE));

  // Managed prefs
  localized_strings->SetString("managedPrefsBannerText",
      l10n_util::GetStringUTF16(IDS_OPTIONS_MANAGED_PREFS));

  // Search
  RegisterTitle(localized_strings, "searchPage", IDS_OPTIONS_SEARCH_PAGE_TITLE);
  localized_strings->SetString("searchPlaceholder",
      l10n_util::GetStringUTF16(IDS_OPTIONS_SEARCH_PLACEHOLDER));
  localized_strings->SetString("searchPageNoMatches",
      l10n_util::GetStringUTF16(IDS_OPTIONS_SEARCH_PAGE_NO_MATCHES));
  localized_strings->SetString("searchPageHelpLabel",
      l10n_util::GetStringUTF16(IDS_OPTIONS_SEARCH_PAGE_HELP_LABEL));
  localized_strings->SetString("searchPageHelpTitle",
      l10n_util::GetStringFUTF16(IDS_OPTIONS_SEARCH_PAGE_HELP_TITLE,
          l10n_util::GetStringUTF16(IDS_PRODUCT_NAME)));
  localized_strings->SetString("searchPageHelpURL",
      google_util::AppendGoogleLocaleParam(
          GURL(chrome::kChromeHelpURL)).spec());

  // Common
  localized_strings->SetString("ok",
      l10n_util::GetStringUTF16(IDS_OK));
  localized_strings->SetString("cancel",
      l10n_util::GetStringUTF16(IDS_CANCEL));
  localized_strings->SetString("learnMore",
      l10n_util::GetStringUTF16(IDS_LEARN_MORE));
  localized_strings->SetString("close",
      l10n_util::GetStringUTF16(IDS_CLOSE));
}

void CoreOptionsHandler::Uninitialize() {
  std::string last_pref;
  for (PreferenceCallbackMap::const_iterator iter = pref_callback_map_.begin();
       iter != pref_callback_map_.end();
       ++iter) {
    if (last_pref != iter->first) {
      StopObservingPref(iter->first);
      last_pref = iter->first;
    }
  }
}

WebUIMessageHandler* CoreOptionsHandler::Attach(WebUI* web_ui) {
  WebUIMessageHandler* result = WebUIMessageHandler::Attach(web_ui);
  DCHECK(web_ui_);
  registrar_.Init(web_ui_->GetProfile()->GetPrefs());
  return result;
}

void CoreOptionsHandler::Observe(NotificationType type,
                                 const NotificationSource& source,
                                 const NotificationDetails& details) {
  if (type == NotificationType::PREF_CHANGED)
    NotifyPrefChanged(Details<std::string>(details).ptr());
}

void CoreOptionsHandler::RegisterMessages() {
  web_ui_->RegisterMessageCallback("coreOptionsInitialize",
      NewCallback(this, &CoreOptionsHandler::HandleInitialize));
  web_ui_->RegisterMessageCallback("fetchPrefs",
      NewCallback(this, &CoreOptionsHandler::HandleFetchPrefs));
  web_ui_->RegisterMessageCallback("observePrefs",
      NewCallback(this, &CoreOptionsHandler::HandleObservePrefs));
  web_ui_->RegisterMessageCallback("setBooleanPref",
      NewCallback(this, &CoreOptionsHandler::HandleSetBooleanPref));
  web_ui_->RegisterMessageCallback("setIntegerPref",
      NewCallback(this, &CoreOptionsHandler::HandleSetIntegerPref));
  web_ui_->RegisterMessageCallback("setDoublePref",
      NewCallback(this, &CoreOptionsHandler::HandleSetDoublePref));
  web_ui_->RegisterMessageCallback("setStringPref",
      NewCallback(this, &CoreOptionsHandler::HandleSetStringPref));
  web_ui_->RegisterMessageCallback("setListPref",
      NewCallback(this, &CoreOptionsHandler::HandleSetListPref));
  web_ui_->RegisterMessageCallback("clearPref",
      NewCallback(this, &CoreOptionsHandler::HandleClearPref));
  web_ui_->RegisterMessageCallback("coreOptionsUserMetricsAction",
      NewCallback(this, &CoreOptionsHandler::HandleUserMetricsAction));
}

void CoreOptionsHandler::HandleInitialize(const ListValue* args) {
  DCHECK(handlers_host_);
  handlers_host_->InitializeHandlers();
}

Value* CoreOptionsHandler::FetchPref(const std::string& pref_name) {
  PrefService* pref_service = web_ui_->GetProfile()->GetPrefs();

  const PrefService::Preference* pref =
      pref_service->FindPreference(pref_name.c_str());

  Value* return_value;
  if (pref) {
    DictionaryValue* dict = new DictionaryValue;
    dict->Set("value", pref->GetValue()->DeepCopy());
    dict->SetBoolean("managed", pref->IsManaged());
    return_value = dict;
  } else {
    return_value = Value::CreateNullValue();
  }
  return return_value;
}

void CoreOptionsHandler::ObservePref(const std::string& pref_name) {
  registrar_.Add(pref_name.c_str(), this);
}

void CoreOptionsHandler::SetPref(const std::string& pref_name,
                                 const Value* value,
                                 const std::string& metric) {
  PrefService* pref_service = web_ui_->GetProfile()->GetPrefs();

  switch (value->GetType()) {
    case Value::TYPE_BOOLEAN:
    case Value::TYPE_INTEGER:
    case Value::TYPE_DOUBLE:
    case Value::TYPE_STRING:
      pref_service->Set(pref_name.c_str(), *value);
      break;

    default:
      NOTREACHED();
      return;
  }

  pref_service->ScheduleSavePersistentPrefs();
  ProcessUserMetric(value, metric);
}

void CoreOptionsHandler::ClearPref(const std::string& pref_name,
                                   const std::string& metric) {
  PrefService* pref_service = web_ui_->GetProfile()->GetPrefs();
  pref_service->ClearPref(pref_name.c_str());
  pref_service->ScheduleSavePersistentPrefs();

  if (!metric.empty())
    UserMetricsRecordAction(UserMetricsAction(metric.c_str()));
}

void CoreOptionsHandler::ProcessUserMetric(const Value* value,
                                           const std::string& metric) {
  if (metric.empty())
    return;

  std::string metric_string = metric;
  if (value->IsType(Value::TYPE_BOOLEAN)) {
    bool bool_value;
    CHECK(value->GetAsBoolean(&bool_value));
    metric_string += bool_value ? "_Enable" : "_Disable";
  }

  UserMetricsRecordAction(UserMetricsAction(metric_string.c_str()));
}

void CoreOptionsHandler::StopObservingPref(const std::string& path) {
  registrar_.Remove(path.c_str(), this);
}

void CoreOptionsHandler::HandleFetchPrefs(const ListValue* args) {
  // First param is name of callback function, so, there needs to be at least
  // one more element for the actual preference identifier.
  DCHECK_GE(static_cast<int>(args->GetSize()), 2);

  // Get callback JS function name.
  Value* callback;
  if (!args->Get(0, &callback) || !callback->IsType(Value::TYPE_STRING))
    return;

  string16 callback_function;
  if (!callback->GetAsString(&callback_function))
    return;

  // Get the list of name for prefs to build the response dictionary.
  DictionaryValue result_value;
  Value* list_member;

  for (size_t i = 1; i < args->GetSize(); i++) {
    if (!args->Get(i, &list_member))
      break;

    if (!list_member->IsType(Value::TYPE_STRING))
      continue;

    std::string pref_name;
    if (!list_member->GetAsString(&pref_name))
      continue;

    result_value.Set(pref_name.c_str(), FetchPref(pref_name));
  }
  web_ui_->CallJavascriptFunction(UTF16ToASCII(callback_function),
                                  result_value);
}

void CoreOptionsHandler::HandleObservePrefs(const ListValue* args) {
  // First param is name is JS callback function name, the rest are pref
  // identifiers that we are observing.
  DCHECK_GE(static_cast<int>(args->GetSize()), 2);

  // Get preference change callback function name.
  string16 callback_func_name;
  if (!args->GetString(0, &callback_func_name))
    return;

  // Get all other parameters - pref identifiers.
  for (size_t i = 1; i < args->GetSize(); i++) {
    Value* list_member;
    if (!args->Get(i, &list_member))
      break;

    // Just ignore bad pref identifiers for now.
    std::string pref_name;
    if (!list_member->IsType(Value::TYPE_STRING) ||
        !list_member->GetAsString(&pref_name))
      continue;

    if (pref_callback_map_.find(pref_name) == pref_callback_map_.end())
      ObservePref(pref_name);

    pref_callback_map_.insert(
        PreferenceCallbackMap::value_type(pref_name,
                                          UTF16ToWideHack(callback_func_name)));
  }
}

void CoreOptionsHandler::HandleSetBooleanPref(const ListValue* args) {
  HandleSetPref(args, Value::TYPE_BOOLEAN);
}

void CoreOptionsHandler::HandleSetIntegerPref(const ListValue* args) {
  HandleSetPref(args, Value::TYPE_INTEGER);
}

void CoreOptionsHandler::HandleSetDoublePref(const ListValue* args) {
  HandleSetPref(args, Value::TYPE_DOUBLE);
}

void CoreOptionsHandler::HandleSetStringPref(const ListValue* args) {
  HandleSetPref(args, Value::TYPE_STRING);
}

void CoreOptionsHandler::HandleSetListPref(const ListValue* args) {
  HandleSetPref(args, Value::TYPE_LIST);
}

void CoreOptionsHandler::HandleSetPref(const ListValue* args,
                                       Value::ValueType type) {
  DCHECK_GT(static_cast<int>(args->GetSize()), 1);

  std::string pref_name;
  if (!args->GetString(0, &pref_name))
    return;

  Value* value;
  if (!args->Get(1, &value))
    return;

  scoped_ptr<Value> temp_value;

  // In JS all numbers are doubles.
  if (type == Value::TYPE_INTEGER) {
    double double_value;
    CHECK(value->GetAsDouble(&double_value));
    temp_value.reset(Value::CreateIntegerValue(static_cast<int>(double_value)));
    value = temp_value.get();

  // In case we have a List pref we got a JSON string.
  } else if (type == Value::TYPE_LIST) {
    std::string json_string;
    CHECK(value->GetAsString(&json_string));
    temp_value.reset(
        base::JSONReader().JsonToValue(json_string,
                                       false,  // no check_root
                                       false));  // no trailing comma
    value = temp_value.get();
  }

  CHECK_EQ(type, value->GetType());

  std::string metric;
  if (args->GetSize() > 2)
    args->GetString(2, &metric);

  SetPref(pref_name, value, metric);
}

void CoreOptionsHandler::HandleClearPref(const ListValue* args) {
  DCHECK_GT(static_cast<int>(args->GetSize()), 0);

  std::string pref_name;
  if (!args->GetString(0, &pref_name))
    return;

  std::string metric;
  if (args->GetSize() > 1)
    args->GetString(1, &metric);

  ClearPref(pref_name, metric);
}

void CoreOptionsHandler::HandleUserMetricsAction(const ListValue* args) {
  std::string metric = UTF16ToUTF8(ExtractStringValue(args));
  if (!metric.empty())
    UserMetricsRecordAction(UserMetricsAction(metric.c_str()));
}

void CoreOptionsHandler::UpdateClearPluginLSOData() {
  scoped_ptr<Value> enabled(
      Value::CreateBooleanValue(clear_plugin_lso_data_enabled_.GetValue()));
  web_ui_->CallJavascriptFunction(
      "OptionsPage.setClearPluginLSODataEnabled", *enabled);
}

void CoreOptionsHandler::NotifyPrefChanged(const std::string* pref_name) {
  if (*pref_name == prefs::kClearPluginLSODataEnabled) {
    // This preference is stored in Local State, not in the user preferences.
    UpdateClearPluginLSOData();
    return;
  }

  PrefService* pref_service = web_ui_->GetProfile()->GetPrefs();
  const PrefService::Preference* pref =
      pref_service->FindPreference(pref_name->c_str());
  if (pref) {
    for (PreferenceCallbackMap::const_iterator iter =
        pref_callback_map_.find(*pref_name);
        iter != pref_callback_map_.end(); ++iter) {
      const std::wstring& callback_function = iter->second;
      ListValue result_value;
      result_value.Append(Value::CreateStringValue(pref_name->c_str()));

      DictionaryValue* dict = new DictionaryValue;
      dict->Set("value", pref->GetValue()->DeepCopy());
      dict->SetBoolean("managed", pref->IsManaged());
      result_value.Append(dict);

      web_ui_->CallJavascriptFunction(WideToASCII(callback_function),
                                      result_value);
    }
  }
}
