// Copyright (c) 2011 House of Life Property ltd.
// Copyright (c) 2011 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/options/uncensor_options_handler.h"

#include <string>
#include <vector>

#include "base/logging.h"
#include "base/string16.h"
#include "base/string_number_conversions.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/web_ui_util.h"
#include "grit/generated_resources.h"
#include "grit/webkit_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_prefs.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/browser/tab_contents/background_contents.h"
#include "chrome/browser/background_contents_service.h"
#include "chrome/browser/background_contents_service_factory.h"

UncensorOptionsHandler::UncensorOptionsHandler() {

}

UncensorOptionsHandler::~UncensorOptionsHandler() {

}

void UncensorOptionsHandler::GetLocalizedValues(DictionaryValue* localized_strings) {
  DCHECK(localized_strings);

  static OptionsStringResource resources[] = {
    { "uncensorPageDescription", IDS_OPTIONS_UNCENSOR_PAGE_DESCRIPTION },
    { "uncensorFilterControl", IDS_OPTIONS_UNCENSOR_FILTER_CONTROL },
    { "uncensorAlwaysRedirectOn", IDS_OPTIONS_UNCENSOR_REDIRECT_ON },
    { "uncensorNeverRedirectOff", IDS_OPTIONS_UNCENSOR_REDIRECT_OFF },
    { "uncensorNotices", IDS_OPTIONS_UNCENSOR_NOTICES },
    { "uncensorShowMessage", IDS_OPTIONS_UNCENSOR_SHOW_MESSAGE },
    { "uncensorNotifyUpdates", IDS_OPTIONS_UNCENSOR_NOTIFY_UPDATES },
    { "uncensorTheFilter", IDS_OPTIONS_UNCENSOR_THE_FILTER },
    { "uncensorOriginalDomain", IDS_OPTIONS_UNCENSOR_ORIGINAL_DOMAIN },
    { "uncensorNewLocation", IDS_OPTIONS_UNCENSOR_NEW_LOCATION },
    { "uncensorExceptions", IDS_OPTIONS_UNCENSOR_EXCEPTIONS }
  };

  RegisterStrings(localized_strings, resources, arraysize(resources));
  RegisterTitle(localized_strings, "uncensorPage",
                IDS_OPTIONS_UNCENSOR_TAB_LABEL);
}

void UncensorOptionsHandler::Initialize() {
}

void UncensorOptionsHandler::RegisterMessages() {
  web_ui_->RegisterMessageCallback(
      "setUncensorPrefs",
      NewCallback(this, &UncensorOptionsHandler::setUncensorPrefsValue));
}

void UncensorOptionsHandler::setUncensorPrefsValue(const ListValue* args) {
  Value* value;
  if (!args->Get(0, &value))
    return;

  Profile* profile = web_ui_->GetProfile();

  ExtensionPrefs* prefs = profile->GetExtensionService()->extension_prefs();
  prefs->SetExtensionControlledPref("ilhfbbmjdjgakaddblkoaadajjijpipm",
                                    "profile.uncensor",
                                    extension_prefs_scope::kRegular,
                                    value->DeepCopy());
}
