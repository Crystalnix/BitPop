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
    { "uncensorNewLocation", IDS_OPTIONS_UNCENSOR_NEW_LOCATION }
  };
  
  RegisterStrings(localized_strings, resources, arraysize(resources));
  RegisterTitle(localized_strings, "uncensorPage",
                IDS_OPTIONS_UNCENSOR_TAB_LABEL);
}

void UncensorOptionsHandler::Initialize() {
  /*
  Profile* profile = web_ui_->GetProfile();
  
  ExtensionService *service = profile->GetExtensionService();
  const ExtensionList *extensions = service->extensions();
  
  //std::string s_id = "";
  const Extension *uncensor = NULL;
  for (ExtensionList::const_iterator it = extensions->begin(); it != extensions->end(); it++) {
    //s_id += (*it)->id() + "\n";
    if ((*it)->id() == "cggihifpjlnhpfogmkdfmfgmchbncjpn") {
      uncensor = (*it);
      break;
    }
  }
  
  if (uncensor) {
    GURL backgrUrl = uncensor->background_url();
    
    BackgroundContentsService* background_contents_service =
         BackgroundContentsServiceFactory::GetForProfile(profile);
    const std::vector<BackgroundContents*> bgrContents = background_contents_service->GetBackgroundContents();
    std::string s = backgrUrl.spec() + "\n";
    for (std::vector<BackgroundContents*>::const_iterator it = bgrContents.begin(); it != bgrContents.end(); it++) {
      s += (*it)->GetURL().spec() + "\n";
      //if ((*it)->GetURL() == backgrUrl) {
      //   
      //}
    }
  
    scoped_ptr<Value> test_string(Value::CreateStringValue(s));

    web_ui_->CallJavascriptFunction("UncensorOptions.test",
                                    *(test_string.get()));
  }
  */
}

void UncensorOptionsHandler::RegisterMessages() {
  
}
