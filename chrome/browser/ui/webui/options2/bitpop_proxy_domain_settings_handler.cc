// Copyright (c) 2012 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2012 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/options2/bitpop_proxy_domain_settings_handler.h"

#include "base/bind.h"
#include "base/string_number_conversions.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/web_ui.h"
#include "grit/generated_resources.h"
#include "grit/locale_settings.h"
#include "ui/base/l10n/l10n_util.h"

namespace {
  const char kOnUpdateProxyDomains[] = "bitpop.onUpdateProxyDomains";
}

namespace options2 {

BitpopProxyDomainSettingsHandler::BitpopProxyDomainSettingsHandler() {
}

BitpopProxyDomainSettingsHandler::~BitpopProxyDomainSettingsHandler() {
}

void BitpopProxyDomainSettingsHandler::InitializeHandler() {
}

void BitpopProxyDomainSettingsHandler::InitializePage() {
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
}

void BitpopProxyDomainSettingsHandler::OnUpdateDomains() {
  Profile* profile = Profile::FromWebUI(web_ui())->GetOriginalProfile();
  EventRouter* router = profile->GetExtensionEventRouter();
  router->DispatchEventToExtension(chrome::kUncensorISPExtensionId,
    )
}

}  // namespace options2
