// Copyright (c) 2012 House of Life Property Ltd. All rights reserved.
// Copyright (c) 2012 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/options2/bitpop_uncensor_filter_handler.h"

#include "base/bind.h"
#include "base/string_number_conversions.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url.h"
#include "chrome/browser/search_engines/template_url_service.h"
#include "chrome/browser/ui/search_engines/keyword_editor_controller.h"
#include "chrome/browser/ui/search_engines/template_url_table_model.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/web_ui.h"
#include "grit/generated_resources.h"
#include "grit/locale_settings.h"
#include "ui/base/l10n/l10n_util.h"

namespace options2 {

BitpopUncensorFilterHandler::BitpopUncensorFilterHandler() {
}

BitpopUncensorFilterHandler::~BitpopUncensorFilterHandler() {
}

void BitpopUncensorFilterHandler::InitializeHandler() {
}

void BitpopUncensorFilterHandler::InitializePage() {
  Profile* profile = Profile::FromWebUI(web_ui());
  PrefService* prefs = profile->GetPrefs();

  prefs->
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
  // web_ui()->RegisterMessageCallback(
  //     "managerSetDefaultSearchEngine",
  //     base::Bind(&BitpopUncensorFilterHandler::SetDefaultSearchEngine,
  //                base::Unretained(this)));
}

void BitpopUncensorFilterHandler::OnModelChanged() {
  // DCHECK(list_controller_.get());
  // if (!list_controller_->loaded())
  //   return;

  // // Find the default engine.
  // const TemplateURL* default_engine =
  //     list_controller_->url_model()->GetDefaultSearchProvider();
  // int default_index = list_controller_->table_model()->IndexOfTemplateURL(
  //     default_engine);

  // // Build the first list (default search engine options).
  // ListValue defaults_list;
  // int last_default_engine_index =
  //     list_controller_->table_model()->last_search_engine_index();
  // for (int i = 0; i < last_default_engine_index; ++i) {
  //   defaults_list.Append(CreateDictionaryForEngine(i, i == default_index));
  // }

  // // Build the second list (other search templates).
  // ListValue others_list;
  // if (last_default_engine_index < 0)
  //   last_default_engine_index = 0;
  // int engine_count = list_controller_->table_model()->RowCount();
  // for (int i = last_default_engine_index; i < engine_count; ++i) {
  //   others_list.Append(CreateDictionaryForEngine(i, i == default_index));
  // }

  // // Build the extension keywords list.
  // ListValue keyword_list;
  // ExtensionService* extension_service =
  //     Profile::FromWebUI(web_ui())->GetExtensionService();
  // if (extension_service) {
  //   const ExtensionSet* extensions = extension_service->extensions();
  //   for (ExtensionSet::const_iterator it = extensions->begin();
  //        it != extensions->end(); ++it) {
  //     if ((*it)->omnibox_keyword().size() > 0)
  //       keyword_list.Append(CreateDictionaryForExtension(*(*it)));
  //   }
  // }

  // web_ui()->CallJavascriptFunction("SearchEngineManager.updateSearchEngineList",
  //                                  defaults_list, others_list, keyword_list);
}

}  // namespace options2
