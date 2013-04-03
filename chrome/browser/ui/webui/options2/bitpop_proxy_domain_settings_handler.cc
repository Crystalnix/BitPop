// Copyright (c) 2012 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2012 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/options2/bitpop_proxy_domain_settings_handler.h"

#include "base/bind.h"
#include "base/memory/scoped_ptr.h"
#include "base/string_number_conversions.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/extensions/event_router.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/web_ui.h"
#include "grit/generated_resources.h"
#include "grit/locale_settings.h"
#include "ui/base/l10n/l10n_util.h"

namespace {
  const char kOnUpdateProxyDomains[] = "bitpop.onProxyDomainsUpdate";
}

namespace options2 {

BitpopProxyDomainSettingsHandler::BitpopProxyDomainSettingsHandler() {
}

BitpopProxyDomainSettingsHandler::~BitpopProxyDomainSettingsHandler() {
}

void BitpopProxyDomainSettingsHandler::InitializeHandler() {
}

void BitpopProxyDomainSettingsHandler::InitializePage() {
  Profile* profile = Profile::FromWebUI(web_ui());
  PrefService* prefs = profile->GetPrefs();

  scoped_ptr<base::Value> siteList(base::Value::CreateStringValue(
      prefs->GetString(prefs::kBlockedSitesList)));
  scoped_ptr<base::Value> countryName(base::Value::CreateStringValue(
    prefs->GetString(prefs::kIPRecognitionCountryName)));

  web_ui()->CallJavascriptFunction(
      "BitpopProxyDomainSettingsOverlay.updateListFromPrefValue",
      *siteList);
  web_ui()->CallJavascriptFunction(
      "BitpopProxyDomainSettingsOverlay.updateCountryName",
      *countryName);
}

void BitpopProxyDomainSettingsHandler::GetLocalizedValues(
    base::DictionaryValue* localized_strings) {
  DCHECK(localized_strings);

  RegisterTitle(localized_strings, "uncensorBlockedSitesTitle",
                IDS_BITPOP_UNCENSOR_BLOCKED_SITES);
  localized_strings->SetString("aListOfSitesBlocked_start",
      l10n_util::GetStringUTF16(IDS_BITPOP_UNCENSOR_LIST_BLOCKED_SITES_START));
  localized_strings->SetString("aListOfSitesBlocked_end",
      l10n_util::GetStringUTF16(IDS_BITPOP_UNCENSOR_LIST_BLOCKED_SITES_END));
  localized_strings->SetString("updateDomainsButtonLabel",
      l10n_util::GetStringUTF16(IDS_BITPOP_UPDATE_DOMAINS_BUTTON_LABEL));
  localized_strings->SetString("useGlobalSettingDefaultOption",
      l10n_util::GetStringUTF16(IDS_BITPOP_USE_GLOBAL_SETTING));
}

void BitpopProxyDomainSettingsHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "updateDomains",
      base::Bind(&BitpopProxyDomainSettingsHandler::OnUpdateDomains,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "proxySiteListChange",
      base::Bind(&BitpopProxyDomainSettingsHandler::ChangeSiteList,
                 base::Unretained(this)));
}

void BitpopProxyDomainSettingsHandler::OnUpdateDomains(
      const base::ListValue* params) {
  Profile* profile = Profile::FromWebUI(web_ui())->GetOriginalProfile();
  extensions::EventRouter* router = profile->GetExtensionEventRouter();
  router->DispatchEventToExtension(chrome::kUncensorISPExtensionId,
      kOnUpdateProxyDomains,
      std::string("[]"),
      NULL,
      GURL()
    );
}

void BitpopProxyDomainSettingsHandler::ChangeSiteList(
      const base::ListValue* params) {

  std::string strValue;
  CHECK_EQ(params->GetSize(), 1U);
  CHECK(params->GetString(0, &strValue));

  Profile* profile = Profile::FromWebUI(web_ui());
  PrefService* pref_service = profile->GetPrefs();
  if (pref_service->IsUserModifiablePreference(prefs::kBlockedSitesList))
    pref_service->SetString(prefs::kBlockedSitesList, strValue);
  else {
    extensions::ExtensionPrefs* prefs =
        profile->GetExtensionService()->extension_prefs();
    prefs->SetExtensionControlledPref(
        chrome::kUncensorISPExtensionId,
        prefs::kBlockedSitesList,
        extensions::kExtensionPrefsScopeRegular,
        Value::CreateStringValue(strValue));
  }
}

}  // namespace options2
