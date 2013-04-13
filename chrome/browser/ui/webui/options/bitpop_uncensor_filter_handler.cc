// Copyright (c) 2012 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2012 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/options2/bitpop_uncensor_filter_handler.h"

#include "base/bind.h"
#include "base/memory/scoped_ptr.h"
#include "base/string_number_conversions.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/web_ui.h"
#include "grit/generated_resources.h"
#include "grit/locale_settings.h"
#include "ui/base/l10n/l10n_util.h"

namespace options {

BitpopUncensorFilterHandler::BitpopUncensorFilterHandler() {
}

BitpopUncensorFilterHandler::~BitpopUncensorFilterHandler() {
}

void BitpopUncensorFilterHandler::InitializeHandler() {
}

void BitpopUncensorFilterHandler::InitializePage() {
  Profile* profile = Profile::FromWebUI(web_ui());
  PrefService* prefs = profile->GetPrefs();

  scoped_ptr<base::Value> filter(base::Value::CreateStringValue(
      prefs->GetString(prefs::kUncensorDomainFilter)));
  scoped_ptr<base::Value> exceptions(base::Value::CreateStringValue(
      prefs->GetString(prefs::kUncensorDomainExceptions)));

  web_ui()->CallJavascriptFunction("BitpopUncensorFilterOverlay.initLists",
    *filter, *exceptions);
}

void BitpopUncensorFilterHandler::GetLocalizedValues(
    base::DictionaryValue* localized_strings) {
  DCHECK(localized_strings);

  RegisterTitle(localized_strings, "uncensorFilterOverlayTitle",
                IDS_BITPOP_UNCENSOR_FILTER_OVERLAY_TITLE);
  localized_strings->SetString("uncensorTheFilter",
      l10n_util::GetStringUTF16(IDS_BITPOP_UNCENSOR_THE_FILTER));
  localized_strings->SetString("uncensorExceptions",
      l10n_util::GetStringUTF16(IDS_BITPOP_UNCENSOR_EXCEPTION));
  localized_strings->SetString("uncensorOriginalDomainHeader",
      l10n_util::GetStringUTF16(IDS_BITPOP_UNCENSOR_ORIGINAL_DOMAIN));
  localized_strings->SetString("uncensorNewLocationHeader",
      l10n_util::GetStringUTF16(IDS_BITPOP_UNCENSOR_NEW_LOCATION));
}

void BitpopUncensorFilterHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "changeUncensorExceptions",
      base::Bind(&BitpopUncensorFilterHandler::ChangeUncensorExceptions,
                 base::Unretained(this)));
}

void BitpopUncensorFilterHandler::ChangeUncensorExceptions(
      const base::ListValue* params) {

  std::string strValue;
  CHECK_EQ(params->GetSize(), 1U);
  CHECK(params->GetString(0, &strValue));

  Profile* profile = Profile::FromWebUI(web_ui());
  PrefService* pref_service = profile->GetPrefs();
  if (pref_service->IsUserModifiablePreference(prefs::kUncensorDomainExceptions))
    pref_service->SetString(prefs::kUncensorDomainExceptions, strValue);
  else {
    extensions::ExtensionPrefs* prefs =
        profile->GetExtensionService()->extension_prefs();
    prefs->SetExtensionControlledPref(
        chrome::kUncensorFilterExtensionId,
        prefs::kUncensorDomainExceptions,
        extensions::kExtensionPrefsScopeRegular,
        Value::CreateStringValue(strValue));
  }
}

}  // namespace options
