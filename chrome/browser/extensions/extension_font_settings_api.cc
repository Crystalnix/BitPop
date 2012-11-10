// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Font Settings Extension API implementation.

#include "chrome/browser/extensions/extension_font_settings_api.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "base/json/json_writer.h"
#include "base/stringprintf.h"
#include "base/string_util.h"
#include "base/values.h"
#include "chrome/browser/extensions/extension_preference_helpers.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/extensions/api/font_settings.h"
#include "chrome/common/extensions/extension_error_utils.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/font_list_async.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_source.h"

#if defined(OS_WIN)
#include "ui/gfx/font.h"
#include "ui/gfx/platform_font_win.h"
#endif

using extensions::APIPermission;

namespace fonts = extensions::api::font_settings;

namespace {

const char kFontIdKey[] = "fontId";
const char kGenericFamilyKey[] = "genericFamily";
const char kLevelOfControlKey[] = "levelOfControl";
const char kDisplayNameKey[] = "displayName";
const char kPixelSizeKey[] = "pixelSize";
const char kScriptKey[] = "script";

const char kSetFromIncognitoError[] =
    "Can't modify regular settings from an incognito context.";

const char kOnDefaultFixedFontSizeChanged[] =
    "fontSettings.onDefaultFixedFontSizeChanged";
const char kOnDefaultFontSizeChanged[] =
    "fontSettings.onDefaultFontSizeChanged";
const char kOnFontChanged[] = "fontSettings.onFontChanged";
const char kOnMinimumFontSizeChanged[] =
    "fontSettings.onMinimumFontSizeChanged";

// Format for font name preference paths.
const char kWebKitFontPrefFormat[] = "webkit.webprefs.fonts.%s.%s";
const char kWebKitFontPrefPrefix[] = "webkit.webprefs.fonts.";

// Gets the font name preference path for |generic_family| and |script|. If
// |script| is NULL, uses prefs::kWebKitCommonScript.
std::string GetFontNamePrefPath(const std::string& generic_family,
                                const std::string* script) {
  return StringPrintf(kWebKitFontPrefFormat,
                      generic_family.c_str(),
                      script ? script->c_str() : prefs::kWebKitCommonScript);
}

// Extracts the generic family and script from font name pref path |pref_path|.
bool ParseFontNamePrefPath(std::string pref_path,
                           std::string* generic_family,
                           std::string* script) {
  if (!StartsWithASCII(pref_path, kWebKitFontPrefPrefix, true))
    return false;

  size_t start = strlen(kWebKitFontPrefPrefix);
  size_t pos = pref_path.find('.', start);
  if (pos == std::string::npos || pos + 1 == pref_path.length())
    return false;
  *generic_family = pref_path.substr(start, pos - start);
  *script = pref_path.substr(pos + 1);
  return true;
}

// Returns the localized name of a font so that it can be matched within the
// list of system fonts. On Windows, the list of system fonts has names only
// for the system locale, but the pref value may be in the English name.
std::string MaybeGetLocalizedFontName(const std::string& font_name) {
#if defined(OS_WIN)
  if (!font_name.empty()) {
    gfx::Font font(font_name, 12);  // dummy font size
    return static_cast<gfx::PlatformFontWin*>(font.platform_font())->
        GetLocalizedFontName();
  }
#endif
  return font_name;
}

// Registers |obs| to observe per-script font prefs under the path |map_name|.
void RegisterFontFamilyMapObserver(PrefChangeRegistrar* registrar,
                                   const char* map_name,
                                   content::NotificationObserver* obs) {
  for (size_t i = 0; i < prefs::kWebKitScriptsForFontFamilyMapsLength; ++i) {
    const char* script = prefs::kWebKitScriptsForFontFamilyMaps[i];
    std::string pref_name = base::StringPrintf("%s.%s", map_name, script);
    registrar->Add(pref_name.c_str(), obs);
  }
}

}  // namespace

ExtensionFontSettingsEventRouter::ExtensionFontSettingsEventRouter(
    Profile* profile) : profile_(profile) {}

ExtensionFontSettingsEventRouter::~ExtensionFontSettingsEventRouter() {}

void ExtensionFontSettingsEventRouter::Init() {
  registrar_.Init(profile_->GetPrefs());

  AddPrefToObserve(prefs::kWebKitDefaultFixedFontSize,
                   kOnDefaultFixedFontSizeChanged,
                   kPixelSizeKey);
  AddPrefToObserve(prefs::kWebKitDefaultFontSize,
                   kOnDefaultFontSizeChanged,
                   kPixelSizeKey);
  AddPrefToObserve(prefs::kWebKitMinimumFontSize,
                   kOnMinimumFontSizeChanged,
                   kPixelSizeKey);

  RegisterFontFamilyMapObserver(&registrar_,
                                prefs::kWebKitStandardFontFamilyMap, this);
  RegisterFontFamilyMapObserver(&registrar_,
                                prefs::kWebKitSerifFontFamilyMap, this);
  RegisterFontFamilyMapObserver(&registrar_,
                                prefs::kWebKitSansSerifFontFamilyMap, this);
  RegisterFontFamilyMapObserver(&registrar_,
                                prefs::kWebKitFixedFontFamilyMap, this);
  RegisterFontFamilyMapObserver(&registrar_,
                                prefs::kWebKitCursiveFontFamilyMap, this);
  RegisterFontFamilyMapObserver(&registrar_,
                                prefs::kWebKitFantasyFontFamilyMap, this);
}

void ExtensionFontSettingsEventRouter::AddPrefToObserve(const char* pref_name,
                                                        const char* event_name,
                                                        const char* key) {
  registrar_.Add(pref_name, this);
  pref_event_map_[pref_name] = std::make_pair(event_name, key);
}

void ExtensionFontSettingsEventRouter::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  if (type != chrome::NOTIFICATION_PREF_CHANGED) {
    NOTREACHED();
    return;
  }

  PrefService* pref_service = content::Source<PrefService>(source).ptr();
  bool incognito = (pref_service != profile_->GetPrefs());
  // We're only observing pref changes on the regular profile.
  DCHECK(!incognito);
  const std::string* pref_name =
      content::Details<const std::string>(details).ptr();

  PrefEventMap::iterator iter = pref_event_map_.find(*pref_name);
  if (iter != pref_event_map_.end()) {
    const std::string& event_name = iter->second.first;
    const std::string& key = iter->second.second;
    OnFontPrefChanged(pref_service, *pref_name, event_name, key, incognito);
    return;
  }

  std::string generic_family;
  std::string script;
  if (ParseFontNamePrefPath(*pref_name, &generic_family, &script)) {
    OnFontNamePrefChanged(pref_service, *pref_name, generic_family, script,
                          incognito);
    return;
  }

  NOTREACHED();
}

void ExtensionFontSettingsEventRouter::OnFontNamePrefChanged(
    PrefService* pref_service,
    const std::string& pref_name,
    const std::string& generic_family,
    const std::string& script,
    bool incognito) {
  const PrefService::Preference* pref = pref_service->FindPreference(
      pref_name.c_str());
  CHECK(pref);

  std::string font_name;
  if (!pref->GetValue()->GetAsString(&font_name)) {
    NOTREACHED();
    return;
  }
  font_name = MaybeGetLocalizedFontName(font_name);

  ListValue args;
  DictionaryValue* dict = new DictionaryValue();
  args.Append(dict);
  dict->SetString(kFontIdKey, font_name);
  dict->SetString(kGenericFamilyKey, generic_family);
  dict->SetString(kScriptKey, script);

  extension_preference_helpers::DispatchEventToExtensions(
      profile_,
      kOnFontChanged,
      &args,
      APIPermission::kFontSettings,
      incognito,
      pref_name);
}

void ExtensionFontSettingsEventRouter::OnFontPrefChanged(
    PrefService* pref_service,
    const std::string& pref_name,
    const std::string& event_name,
    const std::string& key,
    bool incognito) {
  const PrefService::Preference* pref = pref_service->FindPreference(
      pref_name.c_str());
  CHECK(pref);

  ListValue args;
  DictionaryValue* dict = new DictionaryValue();
  args.Append(dict);
  dict->Set(key, pref->GetValue()->DeepCopy());

  extension_preference_helpers::DispatchEventToExtensions(
      profile_,
      event_name,
      &args,
      APIPermission::kFontSettings,
      incognito,
      pref_name);
}

bool ClearFontFunction::RunImpl() {
  if (profile_->IsOffTheRecord()) {
    error_ = kSetFromIncognitoError;
    return false;
  }

  scoped_ptr<fonts::ClearFont::Params> params(
      fonts::ClearFont::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  std::string pref_path = GetFontNamePrefPath(params->details.generic_family,
                                              params->details.script.get());

  // Ensure |pref_path| really is for a registered per-script font pref.
  EXTENSION_FUNCTION_VALIDATE(
      profile_->GetPrefs()->FindPreference(pref_path.c_str()));

  extensions::ExtensionPrefs* prefs =
      profile_->GetExtensionService()->extension_prefs();
  prefs->RemoveExtensionControlledPref(extension_id(),
                                       pref_path.c_str(),
                                       extensions::kExtensionPrefsScopeRegular);
  return true;
}

bool GetFontFunction::RunImpl() {
  scoped_ptr<fonts::GetFont::Params> params(
      fonts::GetFont::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  std::string pref_path = GetFontNamePrefPath(params->details.generic_family,
                                              params->details.script.get());
  PrefService* prefs = profile_->GetPrefs();
  const PrefService::Preference* pref =
      prefs->FindPreference(pref_path.c_str());

  std::string font_name;
  EXTENSION_FUNCTION_VALIDATE(
      pref && pref->GetValue()->GetAsString(&font_name));
  font_name = MaybeGetLocalizedFontName(font_name);

  // We don't support incognito-specific font prefs, so don't consider them when
  // getting level of control.
  const bool kIncognito = false;
  std::string level_of_control =
      extension_preference_helpers::GetLevelOfControl(profile_,
                                                      extension_id(),
                                                      pref_path,
                                                      kIncognito);

  DictionaryValue* result = new DictionaryValue();
  result->SetString(kFontIdKey, font_name);
  result->SetString(kLevelOfControlKey, level_of_control);
  SetResult(result);
  return true;
}

bool SetFontFunction::RunImpl() {
  if (profile_->IsOffTheRecord()) {
    error_ = kSetFromIncognitoError;
    return false;
  }

  scoped_ptr<fonts::SetFont::Params> params(
      fonts::SetFont::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  std::string pref_path = GetFontNamePrefPath(params->details.generic_family,
                                              params->details.script.get());
  // Ensure |pref_path| really is for a registered font pref.
  EXTENSION_FUNCTION_VALIDATE(
      profile_->GetPrefs()->FindPreference(pref_path.c_str()));

  extensions::ExtensionPrefs* prefs =
      profile_->GetExtensionService()->extension_prefs();
  prefs->SetExtensionControlledPref(
      extension_id(),
      pref_path.c_str(),
      extensions::kExtensionPrefsScopeRegular,
      Value::CreateStringValue(params->details.font_id));
  return true;
}

bool GetFontListFunction::RunImpl() {
  content::GetFontListAsync(
      Bind(&GetFontListFunction::FontListHasLoaded, this));
  return true;
}

void GetFontListFunction::FontListHasLoaded(scoped_ptr<ListValue> list) {
  bool success = CopyFontsToResult(list.get());
  SendResponse(success);
}

bool GetFontListFunction::CopyFontsToResult(ListValue* fonts) {
  scoped_ptr<ListValue> result(new ListValue());
  for (ListValue::iterator it = fonts->begin(); it != fonts->end(); ++it) {
    ListValue* font_list_value;
    if (!(*it)->GetAsList(&font_list_value)) {
      NOTREACHED();
      return false;
    }

    std::string name;
    if (!font_list_value->GetString(0, &name)) {
      NOTREACHED();
      return false;
    }

    std::string localized_name;
    if (!font_list_value->GetString(1, &localized_name)) {
      NOTREACHED();
      return false;
    }

    DictionaryValue* font_name = new DictionaryValue();
    font_name->Set(kFontIdKey, Value::CreateStringValue(name));
    font_name->Set(kDisplayNameKey, Value::CreateStringValue(localized_name));
    result->Append(font_name);
  }

  SetResult(result.release());
  return true;
}

bool ClearFontPrefExtensionFunction::RunImpl() {
  if (profile_->IsOffTheRecord()) {
    error_ = kSetFromIncognitoError;
    return false;
  }

  extensions::ExtensionPrefs* prefs =
      profile_->GetExtensionService()->extension_prefs();
  prefs->RemoveExtensionControlledPref(extension_id(),
                                       GetPrefName(),
                                       extensions::kExtensionPrefsScopeRegular);
  return true;
}

bool GetFontPrefExtensionFunction::RunImpl() {
  PrefService* prefs = profile_->GetPrefs();
  const PrefService::Preference* pref = prefs->FindPreference(GetPrefName());
  EXTENSION_FUNCTION_VALIDATE(pref);

  // We don't support incognito-specific font prefs, so don't consider them when
  // getting level of control.
  const bool kIncognito = false;

  std::string level_of_control =
      extension_preference_helpers::GetLevelOfControl(profile_,
                                                      extension_id(),
                                                      GetPrefName(),
                                                      kIncognito);

  DictionaryValue* result = new DictionaryValue();
  result->Set(GetKey(), pref->GetValue()->DeepCopy());
  result->SetString(kLevelOfControlKey, level_of_control);
  SetResult(result);
  return true;
}

bool SetFontPrefExtensionFunction::RunImpl() {
  if (profile_->IsOffTheRecord()) {
    error_ = kSetFromIncognitoError;
    return false;
  }

  DictionaryValue* details = NULL;
  EXTENSION_FUNCTION_VALIDATE(args_->GetDictionary(0, &details));

  Value* value;
  EXTENSION_FUNCTION_VALIDATE(details->Get(GetKey(), &value));

  extensions::ExtensionPrefs* prefs =
      profile_->GetExtensionService()->extension_prefs();
  prefs->SetExtensionControlledPref(extension_id(),
                                    GetPrefName(),
                                    extensions::kExtensionPrefsScopeRegular,
                                    value->DeepCopy());
  return true;
}

const char* ClearDefaultFontSizeFunction::GetPrefName() {
  return prefs::kWebKitDefaultFontSize;
}

const char* GetDefaultFontSizeFunction::GetPrefName() {
  return prefs::kWebKitDefaultFontSize;
}

const char* GetDefaultFontSizeFunction::GetKey() {
  return kPixelSizeKey;
}

const char* SetDefaultFontSizeFunction::GetPrefName() {
  return prefs::kWebKitDefaultFontSize;
}

const char* SetDefaultFontSizeFunction::GetKey() {
  return kPixelSizeKey;
}

const char* ClearDefaultFixedFontSizeFunction::GetPrefName() {
  return prefs::kWebKitDefaultFixedFontSize;
}

const char* GetDefaultFixedFontSizeFunction::GetPrefName() {
  return prefs::kWebKitDefaultFixedFontSize;
}

const char* GetDefaultFixedFontSizeFunction::GetKey() {
  return kPixelSizeKey;
}

const char* SetDefaultFixedFontSizeFunction::GetPrefName() {
  return prefs::kWebKitDefaultFixedFontSize;
}

const char* SetDefaultFixedFontSizeFunction::GetKey() {
  return kPixelSizeKey;
}

const char* ClearMinimumFontSizeFunction::GetPrefName() {
  return prefs::kWebKitMinimumFontSize;
}

const char* GetMinimumFontSizeFunction::GetPrefName() {
  return prefs::kWebKitMinimumFontSize;
}

const char* GetMinimumFontSizeFunction::GetKey() {
  return kPixelSizeKey;
}

const char* SetMinimumFontSizeFunction::GetPrefName() {
  return prefs::kWebKitMinimumFontSize;
}

const char* SetMinimumFontSizeFunction::GetKey() {
  return kPixelSizeKey;
}
