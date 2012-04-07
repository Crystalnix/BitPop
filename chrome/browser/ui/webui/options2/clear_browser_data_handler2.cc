// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/options2/clear_browser_data_handler2.h"

#include "base/basictypes.h"
#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/string16.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/web_ui.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/locale_settings.h"
#include "ui/base/l10n/l10n_util.h"

namespace options2 {

ClearBrowserDataHandler::ClearBrowserDataHandler() : remover_(NULL) {
}

ClearBrowserDataHandler::~ClearBrowserDataHandler() {
  if (remover_)
    remover_->RemoveObserver(this);
}

void ClearBrowserDataHandler::Initialize() {
  clear_plugin_lso_data_enabled_.Init(prefs::kClearPluginLSODataEnabled,
                                      Profile::FromWebUI(web_ui())->GetPrefs(),
                                      NULL);
}

void ClearBrowserDataHandler::GetLocalizedValues(
    DictionaryValue* localized_strings) {
  DCHECK(localized_strings);

  static OptionsStringResource resources[] = {
    { "clearBrowserDataLabel", IDS_CLEAR_BROWSING_DATA_LABEL },
    { "deleteBrowsingHistoryCheckbox", IDS_DEL_BROWSING_HISTORY_CHKBOX },
    { "deleteDownloadHistoryCheckbox", IDS_DEL_DOWNLOAD_HISTORY_CHKBOX },
    { "deleteCacheCheckbox", IDS_DEL_CACHE_CHKBOX },
    { "deleteCookiesCheckbox", IDS_DEL_COOKIES_CHKBOX },
    { "deleteCookiesFlashCheckbox", IDS_DEL_COOKIES_FLASH_CHKBOX },
    { "deletePasswordsCheckbox", IDS_DEL_PASSWORDS_CHKBOX },
    { "deleteFormDataCheckbox", IDS_DEL_FORM_DATA_CHKBOX },
    { "clearBrowserDataCommit", IDS_CLEAR_BROWSING_DATA_COMMIT },
    { "flashStorageSettings", IDS_FLASH_STORAGE_SETTINGS },
    { "flash_storage_url", IDS_FLASH_STORAGE_URL },
    { "clearDataDeleting", IDS_CLEAR_DATA_DELETING },
  };

  RegisterStrings(localized_strings, resources, arraysize(resources));
  RegisterTitle(localized_strings, "clearBrowserDataOverlay",
                IDS_CLEAR_BROWSING_DATA_TITLE);

  ListValue* time_list = new ListValue;
  for (int i = 0; i < 5; i++) {
    string16 label_string;
    switch (i) {
      case 0:
        label_string = l10n_util::GetStringUTF16(IDS_CLEAR_DATA_HOUR);
        break;
      case 1:
        label_string = l10n_util::GetStringUTF16(IDS_CLEAR_DATA_DAY);
        break;
      case 2:
        label_string = l10n_util::GetStringUTF16(IDS_CLEAR_DATA_WEEK);
        break;
      case 3:
        label_string = l10n_util::GetStringUTF16(IDS_CLEAR_DATA_4WEEKS);
        break;
      case 4:
        label_string = l10n_util::GetStringUTF16(IDS_CLEAR_DATA_EVERYTHING);
        break;
    }
    ListValue* option = new ListValue();
    option->Append(Value::CreateIntegerValue(i));
    option->Append(Value::CreateStringValue(label_string));
    time_list->Append(option);
  }
  localized_strings->Set("clearBrowserDataTimeList", time_list);
}

void ClearBrowserDataHandler::RegisterMessages() {
  // Setup handlers specific to this panel.
  web_ui()->RegisterMessageCallback("performClearBrowserData",
      base::Bind(&ClearBrowserDataHandler::HandleClearBrowserData,
                 base::Unretained(this)));
}

void ClearBrowserDataHandler::HandleClearBrowserData(const ListValue* value) {
  Profile* profile = Profile::FromWebUI(web_ui());
  PrefService* prefs = profile->GetPrefs();

  int remove_mask = 0;
  if (prefs->GetBoolean(prefs::kDeleteBrowsingHistory))
    remove_mask |= BrowsingDataRemover::REMOVE_HISTORY;
  if (prefs->GetBoolean(prefs::kDeleteDownloadHistory))
    remove_mask |= BrowsingDataRemover::REMOVE_DOWNLOADS;
  if (prefs->GetBoolean(prefs::kDeleteCache))
    remove_mask |= BrowsingDataRemover::REMOVE_CACHE;
  if (prefs->GetBoolean(prefs::kDeleteCookies)) {
    int site_data_mask = BrowsingDataRemover::REMOVE_SITE_DATA;
    // Don't try to clear LSO data if it's not supported.
    if (!*clear_plugin_lso_data_enabled_)
      site_data_mask &= ~BrowsingDataRemover::REMOVE_PLUGIN_DATA;
    remove_mask |= site_data_mask;
  }
  if (prefs->GetBoolean(prefs::kDeletePasswords))
    remove_mask |= BrowsingDataRemover::REMOVE_PASSWORDS;
  if (prefs->GetBoolean(prefs::kDeleteFormData))
    remove_mask |= BrowsingDataRemover::REMOVE_FORM_DATA;

  int period_selected = prefs->GetInteger(prefs::kDeleteTimePeriod);

  base::FundamentalValue state(true);
  web_ui()->CallJavascriptFunction("ClearBrowserDataOverlay.setClearingState",
                                   state);

  // If we are still observing a previous data remover, we need to stop
  // observing.
  if (remover_)
    remover_->RemoveObserver(this);

  // BrowsingDataRemover deletes itself when done.
  remover_ = new BrowsingDataRemover(profile,
      static_cast<BrowsingDataRemover::TimePeriod>(period_selected),
      base::Time());
  remover_->AddObserver(this);
  remover_->Remove(remove_mask);
}

void ClearBrowserDataHandler::OnBrowsingDataRemoverDone() {
  // No need to remove ourselves as an observer as BrowsingDataRemover deletes
  // itself after we return.
  remover_ = NULL;
  web_ui()->CallJavascriptFunction("ClearBrowserDataOverlay.doneClearing");
}

}  // namespace options2
