// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/options/font_settings_handler.h"

#include <string>

#include "base/basictypes.h"
#include "base/bind.h"
#include "base/i18n/rtl.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/character_encoding.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/options/font_settings_utils.h"
#include "chrome/common/pref_names.h"
#include "content/common/notification_details.h"
#include "content/common/notification_type.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"

FontSettingsHandler::FontSettingsHandler() {
}

FontSettingsHandler::~FontSettingsHandler() {
}

void FontSettingsHandler::GetLocalizedValues(
    DictionaryValue* localized_strings) {
  DCHECK(localized_strings);

  static OptionsStringResource resources[] = {
    { "fontSettingsStandard",
      IDS_FONT_LANGUAGE_SETTING_FONT_SELECTOR_STANDARD_LABEL },
    { "fontSettingsSerif",
      IDS_FONT_LANGUAGE_SETTING_FONT_SELECTOR_SERIF_LABEL },
    { "fontSettingsSansSerif",
      IDS_FONT_LANGUAGE_SETTING_FONT_SELECTOR_SANS_SERIF_LABEL },
    { "fontSettingsFixedWidth",
      IDS_FONT_LANGUAGE_SETTING_FONT_SELECTOR_FIXED_WIDTH_LABEL },
    { "fontSettingsMinimumSize",
      IDS_FONT_LANGUAGE_SETTING_MINIMUM_FONT_SIZE_TITLE },
    { "fontSettingsEncoding",
      IDS_FONT_LANGUAGE_SETTING_FONT_SUB_DIALOG_ENCODING_TITLE },
    { "fontSettingsSizeTiny",
      IDS_FONT_LANGUAGE_SETTING_FONT_SIZE_TINY },
    { "fontSettingsSizeHuge",
      IDS_FONT_LANGUAGE_SETTING_FONT_SIZE_HUGE },
    { "fontSettingsLoremIpsum",
      IDS_FONT_LANGUAGE_SETTING_LOREM_IPSUM },
  };

  RegisterStrings(localized_strings, resources, arraysize(resources));
  RegisterTitle(localized_strings, "fontSettingsPage",
                IDS_FONT_LANGUAGE_SETTING_FONT_TAB_TITLE);
  localized_strings->SetString("fontSettingsPlaceholder",
      l10n_util::GetStringUTF16(
          IDS_FONT_LANGUAGE_SETTING_PLACEHOLDER));
}

void FontSettingsHandler::Initialize() {
  DCHECK(web_ui_);
  SetUpStandardFontSample();
  SetUpSerifFontSample();
  SetUpSansSerifFontSample();
  SetUpFixedFontSample();
  SetUpMinimumFontSample();
}

WebUIMessageHandler* FontSettingsHandler::Attach(WebUI* web_ui) {
  // Call through to superclass.
  WebUIMessageHandler* handler = OptionsPageUIHandler::Attach(web_ui);

  // Perform validation for saved fonts.
  DCHECK(web_ui_);
  PrefService* pref_service = web_ui_->GetProfile()->GetPrefs();
  FontSettingsUtilities::ValidateSavedFonts(pref_service);

  // Register for preferences that we need to observe manually.
  standard_font_.Init(prefs::kWebKitStandardFontFamily, pref_service, this);
  serif_font_.Init(prefs::kWebKitSerifFontFamily, pref_service, this);
  sans_serif_font_.Init(prefs::kWebKitSansSerifFontFamily, pref_service, this);
  fixed_font_.Init(prefs::kWebKitFixedFontFamily, pref_service, this);
  font_encoding_.Init(prefs::kDefaultCharset, pref_service, this);
  default_font_size_.Init(prefs::kWebKitDefaultFontSize, pref_service, this);
  default_fixed_font_size_.Init(prefs::kWebKitDefaultFixedFontSize,
                                pref_service, this);
  minimum_font_size_.Init(prefs::kWebKitMinimumFontSize, pref_service, this);

  // Return result from the superclass.
  return handler;
}

void FontSettingsHandler::RegisterMessages() {
  web_ui_->RegisterMessageCallback("fetchFontsData",
      NewCallback(this, &FontSettingsHandler::HandleFetchFontsData));
}

void FontSettingsHandler::HandleFetchFontsData(const ListValue* args) {
  content::GetFontListAsync(
      base::Bind(&FontSettingsHandler::FontsListHasLoaded, AsWeakPtr()));
}

void FontSettingsHandler::FontsListHasLoaded(
    scoped_refptr<content::FontListResult> list) {
  ListValue encoding_list;
  const std::vector<CharacterEncoding::EncodingInfo>* encodings;
  PrefService* pref_service = web_ui_->GetProfile()->GetPrefs();
  encodings = CharacterEncoding::GetCurrentDisplayEncodings(
      g_browser_process->GetApplicationLocale(),
      pref_service->GetString(prefs::kStaticEncodings),
      pref_service->GetString(prefs::kRecentlySelectedEncoding));
  DCHECK(encodings);
  DCHECK(!encodings->empty());

  std::vector<CharacterEncoding::EncodingInfo>::const_iterator it;
  for (it = encodings->begin(); it != encodings->end(); ++it) {
    ListValue* option = new ListValue();
    if (it->encoding_id) {
      int cmd_id = it->encoding_id;
      std::string encoding =
      CharacterEncoding::GetCanonicalEncodingNameByCommandId(cmd_id);
      string16 name = it->encoding_display_name;
      base::i18n::AdjustStringForLocaleDirection(&name);
      option->Append(Value::CreateStringValue(encoding));
      option->Append(Value::CreateStringValue(name));
    } else {
      // Add empty name/value to indicate a separator item.
      option->Append(Value::CreateStringValue(""));
      option->Append(Value::CreateStringValue(""));
    }
    encoding_list.Append(option);
  }

  ListValue selected_values;
  selected_values.Append(Value::CreateStringValue(standard_font_.GetValue()));
  selected_values.Append(Value::CreateStringValue(serif_font_.GetValue()));
  selected_values.Append(Value::CreateStringValue(sans_serif_font_.GetValue()));
  selected_values.Append(Value::CreateStringValue(fixed_font_.GetValue()));
  selected_values.Append(Value::CreateStringValue(font_encoding_.GetValue()));

  web_ui_->CallJavascriptFunction("FontSettings.setFontsData",
                                  *list->list.get(), encoding_list,
                                  selected_values);
}

void FontSettingsHandler::Observe(NotificationType type,
                                  const NotificationSource& source,
                                  const NotificationDetails& details) {
  if (type == NotificationType::PREF_CHANGED) {
    std::string* pref_name = Details<std::string>(details).ptr();
    if (*pref_name == prefs::kWebKitStandardFontFamily) {
      SetUpStandardFontSample();
    } else if (*pref_name == prefs::kWebKitSerifFontFamily) {
      SetUpSerifFontSample();
    } else if (*pref_name == prefs::kWebKitSansSerifFontFamily) {
      SetUpSansSerifFontSample();
    } else if (*pref_name == prefs::kWebKitFixedFontFamily ||
               *pref_name == prefs::kWebKitDefaultFixedFontSize) {
      SetUpFixedFontSample();
    } else if (*pref_name == prefs::kWebKitDefaultFontSize) {
      SetUpStandardFontSample();
      SetUpSerifFontSample();
      SetUpSansSerifFontSample();
    } else if (*pref_name == prefs::kWebKitMinimumFontSize) {
      SetUpMinimumFontSample();
    }
  }
}

void FontSettingsHandler::SetUpStandardFontSample() {
  StringValue font_value(standard_font_.GetValue());
  FundamentalValue size_value(default_font_size_.GetValue());
  web_ui_->CallJavascriptFunction(
      "FontSettings.setUpStandardFontSample", font_value, size_value);
}

void FontSettingsHandler::SetUpSerifFontSample() {
  StringValue font_value(serif_font_.GetValue());
  FundamentalValue size_value(default_font_size_.GetValue());
  web_ui_->CallJavascriptFunction(
      "FontSettings.setUpSerifFontSample", font_value, size_value);
}

void FontSettingsHandler::SetUpSansSerifFontSample() {
  StringValue font_value(sans_serif_font_.GetValue());
  FundamentalValue size_value(default_font_size_.GetValue());
  web_ui_->CallJavascriptFunction(
      "FontSettings.setUpSansSerifFontSample", font_value, size_value);
}

void FontSettingsHandler::SetUpFixedFontSample() {
  StringValue font_value(fixed_font_.GetValue());
  FundamentalValue size_value(default_fixed_font_size_.GetValue());
  web_ui_->CallJavascriptFunction(
      "FontSettings.setUpFixedFontSample", font_value, size_value);
}

void FontSettingsHandler::SetUpMinimumFontSample() {
  FundamentalValue size_value(minimum_font_size_.GetValue());
  web_ui_->CallJavascriptFunction("FontSettings.setUpMinimumFontSample",
                                  size_value);
}
