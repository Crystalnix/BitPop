// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_preference_api.h"

#include <map>
#include <utility>

#include "base/memory/singleton.h"
#include "base/stl_util.h"
#include "base/stringprintf.h"
#include "base/values.h"
#include "chrome/browser/extensions/api/proxy/proxy_api.h"
#include "chrome/browser/extensions/extension_preference_api_constants.h"
#include "chrome/browser/extensions/extension_preference_helpers.h"
#include "chrome/browser/extensions/extension_prefs.h"
#include "chrome/browser/extensions/extension_prefs_scope.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/extensions/extension_error_utils.h"
#include "chrome/common/extensions/permissions/api_permission.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_source.h"

namespace keys = extension_preference_api_constants;
namespace helpers = extension_preference_helpers;

using extensions::APIPermission;

namespace {

struct PrefMappingEntry {
  // Name of the preference referenced by the extension API JSON.
  const char* extension_pref;

  // Name of the preference in the PrefStores.
  const char* browser_pref;

  // Permission required to access this preference.
  // Use APIPermission::kInvalid for |permission| to express that no
  // permission is necessary.
  APIPermission::ID permission;
};

const char kOnPrefChangeFormat[] = "types.ChromeSetting.%s.onChange";

PrefMappingEntry kPrefMapping[] = {
#if defined(OS_CHROMEOS)
  { "protectedContentEnabled",
    prefs::kEnableCrosDRM,
    APIPermission::kPrivacy
  },
#endif  // defined(OS_CHROMEOS)
  { "alternateErrorPagesEnabled",
    prefs::kAlternateErrorPagesEnabled,
    APIPermission::kPrivacy
  },
  { "autofillEnabled",
    prefs::kAutofillEnabled,
    APIPermission::kPrivacy
  },
  { "hyperlinkAuditingEnabled",
    prefs::kEnableHyperlinkAuditing,
    APIPermission::kPrivacy
  },
  { "instantEnabled",
    prefs::kInstantEnabled,
    APIPermission::kPrivacy
  },
  { "managedModeEnabled",
    prefs::kInManagedMode,
    APIPermission::kManagedModePrivate
  },
  { "networkPredictionEnabled",
    prefs::kNetworkPredictionEnabled,
    APIPermission::kPrivacy
  },
  { "proxy",
    prefs::kProxy,
    APIPermission::kProxy
  },
  { "referrersEnabled",
    prefs::kEnableReferrers,
    APIPermission::kPrivacy
  },
  { "safeBrowsingEnabled",
    prefs::kSafeBrowsingEnabled,
    APIPermission::kPrivacy
  },
  { "searchSuggestEnabled",
    prefs::kSearchSuggestEnabled,
    APIPermission::kPrivacy
  },
  { "spellingServiceEnabled",
    prefs::kSpellCheckUseSpellingService,
    APIPermission::kPrivacy
  },
  { "thirdPartyCookiesAllowed",
    prefs::kBlockThirdPartyCookies,
    APIPermission::kPrivacy
  },
  { "translationServiceEnabled",
    prefs::kEnableTranslate,
    APIPermission::kPrivacy
  }
};

class IdentityPrefTransformer : public PrefTransformerInterface {
 public:
  virtual Value* ExtensionToBrowserPref(const Value* extension_pref,
                                        std::string* error,
                                        bool* bad_message) {
    return extension_pref->DeepCopy();
  }

  virtual Value* BrowserToExtensionPref(const Value* browser_pref) {
    return browser_pref->DeepCopy();
  }
};

class InvertBooleanTransformer : public PrefTransformerInterface {
 public:
  virtual Value* ExtensionToBrowserPref(const Value* extension_pref,
                                        std::string* error,
                                        bool* bad_message) {
    return InvertBooleanValue(extension_pref);
  }

  virtual Value* BrowserToExtensionPref(const Value* browser_pref) {
    return InvertBooleanValue(browser_pref);
  }

 private:
  static Value* InvertBooleanValue(const Value* value) {
    bool bool_value = false;
    bool result = value->GetAsBoolean(&bool_value);
    DCHECK(result);
    return Value::CreateBooleanValue(!bool_value);
  }
};

class PrefMapping {
 public:
  static PrefMapping* GetInstance() {
    return Singleton<PrefMapping>::get();
  }

  bool FindBrowserPrefForExtensionPref(const std::string& extension_pref,
                                       std::string* browser_pref,
                                       APIPermission::ID* permission) {
    PrefMap::iterator it = mapping_.find(extension_pref);
    if (it != mapping_.end()) {
      *browser_pref = it->second.first;
      *permission = it->second.second;
      return true;
    }
    return false;
  }

  bool FindEventForBrowserPref(const std::string& browser_pref,
                               std::string* event_name,
                               APIPermission::ID* permission) {
    PrefMap::iterator it = event_mapping_.find(browser_pref);
    if (it != event_mapping_.end()) {
      *event_name = it->second.first;
      *permission = it->second.second;
      return true;
    }
    return false;
  }

  PrefTransformerInterface* FindTransformerForBrowserPref(
      const std::string& browser_pref) {
    std::map<std::string, PrefTransformerInterface*>::iterator it =
        transformers_.find(browser_pref);
    if (it != transformers_.end())
      return it->second;
    else
      return identity_transformer_.get();
  }

 private:
  friend struct DefaultSingletonTraits<PrefMapping>;

  PrefMapping() {
    identity_transformer_.reset(new IdentityPrefTransformer());
    for (size_t i = 0; i < arraysize(kPrefMapping); ++i) {
      mapping_[kPrefMapping[i].extension_pref] =
          std::make_pair(kPrefMapping[i].browser_pref,
                         kPrefMapping[i].permission);
      std::string event_name =
          base::StringPrintf(kOnPrefChangeFormat,
                             kPrefMapping[i].extension_pref);
      event_mapping_[kPrefMapping[i].browser_pref] =
          std::make_pair(event_name, kPrefMapping[i].permission);
    }
    DCHECK_EQ(arraysize(kPrefMapping), mapping_.size());
    DCHECK_EQ(arraysize(kPrefMapping), event_mapping_.size());
    RegisterPrefTransformer(prefs::kProxy,
                            new extensions::ProxyPrefTransformer());
    RegisterPrefTransformer(prefs::kBlockThirdPartyCookies,
                            new InvertBooleanTransformer());
  }

  ~PrefMapping() {
    STLDeleteContainerPairSecondPointers(transformers_.begin(),
                                         transformers_.end());
  }

  void RegisterPrefTransformer(const std::string& browser_pref,
                               PrefTransformerInterface* transformer) {
    DCHECK_EQ(0u, transformers_.count(browser_pref)) <<
        "Trying to register pref transformer for " << browser_pref << " twice";
    transformers_[browser_pref] = transformer;
  }

  typedef std::map<std::string,
                   std::pair<std::string, APIPermission::ID> >
          PrefMap;

  // Mapping from extension pref keys to browser pref keys and permissions.
  PrefMap mapping_;

  // Mapping from browser pref keys to extension event names and permissions.
  PrefMap event_mapping_;

  // Mapping from browser pref keys to transformers.
  std::map<std::string, PrefTransformerInterface*> transformers_;

  scoped_ptr<PrefTransformerInterface> identity_transformer_;

  DISALLOW_COPY_AND_ASSIGN(PrefMapping);
};

}  // namespace

ExtensionPreferenceEventRouter::ExtensionPreferenceEventRouter(
    Profile* profile) : profile_(profile) {
  registrar_.Init(profile_->GetPrefs());
  incognito_registrar_.Init(profile_->GetOffTheRecordPrefs());
  for (size_t i = 0; i < arraysize(kPrefMapping); ++i) {
    registrar_.Add(kPrefMapping[i].browser_pref, this);
    incognito_registrar_.Add(kPrefMapping[i].browser_pref, this);
  }
}

ExtensionPreferenceEventRouter::~ExtensionPreferenceEventRouter() { }

void ExtensionPreferenceEventRouter::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  if (type == chrome::NOTIFICATION_PREF_CHANGED) {
    const std::string* pref_key =
        content::Details<const std::string>(details).ptr();
    OnPrefChanged(content::Source<PrefService>(source).ptr(), *pref_key);
  } else {
    NOTREACHED();
  }
}

void ExtensionPreferenceEventRouter::OnPrefChanged(
    PrefService* pref_service,
    const std::string& browser_pref) {
  bool incognito = (pref_service != profile_->GetPrefs());

  std::string event_name;
  APIPermission::ID permission = APIPermission::kInvalid;
  bool rv = PrefMapping::GetInstance()->FindEventForBrowserPref(
      browser_pref, &event_name, &permission);
  DCHECK(rv);

  ListValue args;
  DictionaryValue* dict = new DictionaryValue();
  args.Append(dict);
  const PrefService::Preference* pref =
      pref_service->FindPreference(browser_pref.c_str());
  CHECK(pref);
  ExtensionService* extension_service = profile_->GetExtensionService();
  PrefTransformerInterface* transformer =
      PrefMapping::GetInstance()->FindTransformerForBrowserPref(browser_pref);
  dict->Set(keys::kValue,
            transformer->BrowserToExtensionPref(pref->GetValue()));
  if (incognito) {
    extensions::ExtensionPrefs* ep = extension_service->extension_prefs();
    dict->SetBoolean(keys::kIncognitoSpecific,
                     ep->HasIncognitoPrefValue(browser_pref));
  }

  helpers::DispatchEventToExtensions(profile_,
                                     event_name,
                                     &args,
                                     permission,
                                     incognito,
                                     browser_pref);
}

PreferenceFunction::~PreferenceFunction() { }

bool PreferenceFunction::ValidateBrowserPref(
    const std::string& extension_pref_key,
    std::string* browser_pref_key) {
  APIPermission::ID permission = APIPermission::kInvalid;
  EXTENSION_FUNCTION_VALIDATE(
      PrefMapping::GetInstance()->FindBrowserPrefForExtensionPref(
          extension_pref_key, browser_pref_key, &permission));
  if (!GetExtension()->HasAPIPermission(permission)) {
    error_ = ExtensionErrorUtils::FormatErrorMessage(
        keys::kPermissionErrorMessage, extension_pref_key);
    return false;
  }
  return true;
}

GetPreferenceFunction::~GetPreferenceFunction() { }

bool GetPreferenceFunction::RunImpl() {
  std::string pref_key;
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(0, &pref_key));
  DictionaryValue* details = NULL;
  EXTENSION_FUNCTION_VALIDATE(args_->GetDictionary(1, &details));

  bool incognito = false;
  if (details->HasKey(keys::kIncognitoKey))
    EXTENSION_FUNCTION_VALIDATE(details->GetBoolean(keys::kIncognitoKey,
                                                    &incognito));

  // Check incognito access.
  if (incognito && !include_incognito()) {
    error_ = keys::kIncognitoErrorMessage;
    return false;
  }

  // Obtain pref.
  std::string browser_pref;
  if (!ValidateBrowserPref(pref_key, &browser_pref))
    return false;
  PrefService* prefs = incognito ? profile_->GetOffTheRecordPrefs()
                                 : profile_->GetPrefs();
  const PrefService::Preference* pref =
      prefs->FindPreference(browser_pref.c_str());
  CHECK(pref);

  scoped_ptr<DictionaryValue> result(new DictionaryValue);

  // Retrieve level of control.
  std::string level_of_control =
      helpers::GetLevelOfControl(profile_, extension_id(), browser_pref,
                                 incognito);
  result->SetString(keys::kLevelOfControl, level_of_control);

  // Retrieve pref value.
  PrefTransformerInterface* transformer =
      PrefMapping::GetInstance()->FindTransformerForBrowserPref(browser_pref);
  result->Set(keys::kValue,
              transformer->BrowserToExtensionPref(pref->GetValue()));

  // Retrieve incognito status.
  if (incognito) {
    extensions::ExtensionPrefs* ep =
        profile_->GetExtensionService()->extension_prefs();
    result->SetBoolean(keys::kIncognitoSpecific,
                       ep->HasIncognitoPrefValue(browser_pref));
  }

  SetResult(result.release());
  return true;
}

SetPreferenceFunction::~SetPreferenceFunction() { }

bool SetPreferenceFunction::RunImpl() {
  std::string pref_key;
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(0, &pref_key));
  DictionaryValue* details = NULL;
  EXTENSION_FUNCTION_VALIDATE(args_->GetDictionary(1, &details));

  Value* value = NULL;
  EXTENSION_FUNCTION_VALIDATE(details->Get(keys::kValue, &value));

  extensions::ExtensionPrefsScope scope =
      extensions::kExtensionPrefsScopeRegular;
  if (details->HasKey(keys::kScopeKey)) {
    std::string scope_str;
    EXTENSION_FUNCTION_VALIDATE(
        details->GetString(keys::kScopeKey, &scope_str));

    EXTENSION_FUNCTION_VALIDATE(helpers::StringToScope(scope_str, &scope));
  }

  // Check incognito scope.
  bool incognito =
      (scope == extensions::kExtensionPrefsScopeIncognitoPersistent ||
       scope == extensions::kExtensionPrefsScopeIncognitoSessionOnly);
  if (incognito) {
    // Regular profiles can't access incognito unless include_incognito is true.
    if (!profile()->IsOffTheRecord() && !include_incognito()) {
      error_ = keys::kIncognitoErrorMessage;
      return false;
    }
  } else {
    // Incognito profiles can't access regular mode ever, they only exist in
    // split mode.
    if (profile()->IsOffTheRecord()) {
      error_ = "Can't modify regular settings from an incognito context.";
      return false;
    }
  }

  if (scope == extensions::kExtensionPrefsScopeIncognitoSessionOnly &&
      !profile_->HasOffTheRecordProfile()) {
    error_ = keys::kIncognitoSessionOnlyErrorMessage;
    return false;
  }

  // Obtain pref.
  std::string browser_pref;
  if (!ValidateBrowserPref(pref_key, &browser_pref))
    return false;
  extensions::ExtensionPrefs* prefs =
      profile_->GetExtensionService()->extension_prefs();
  const PrefService::Preference* pref =
      prefs->pref_service()->FindPreference(browser_pref.c_str());
  CHECK(pref);

  // Validate new value.
  EXTENSION_FUNCTION_VALIDATE(value->GetType() == pref->GetType());
  PrefTransformerInterface* transformer =
      PrefMapping::GetInstance()->FindTransformerForBrowserPref(browser_pref);
  std::string error;
  bool bad_message = false;
  Value* browserPrefValue =
      transformer->ExtensionToBrowserPref(value, &error, &bad_message);
  if (!browserPrefValue) {
    error_ = error;
    bad_message_ = bad_message;
    return false;
  }

  prefs->SetExtensionControlledPref(extension_id(),
                                    browser_pref,
                                    scope,
                                    browserPrefValue);
  return true;
}

ClearPreferenceFunction::~ClearPreferenceFunction() { }

bool ClearPreferenceFunction::RunImpl() {
  std::string pref_key;
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(0, &pref_key));
  DictionaryValue* details = NULL;
  EXTENSION_FUNCTION_VALIDATE(args_->GetDictionary(1, &details));

  extensions::ExtensionPrefsScope scope =
      extensions::kExtensionPrefsScopeRegular;
  if (details->HasKey(keys::kScopeKey)) {
    std::string scope_str;
    EXTENSION_FUNCTION_VALIDATE(
        details->GetString(keys::kScopeKey, &scope_str));

    EXTENSION_FUNCTION_VALIDATE(helpers::StringToScope(scope_str, &scope));
  }

  // Check incognito scope.
  bool incognito =
      (scope == extensions::kExtensionPrefsScopeIncognitoPersistent ||
       scope == extensions::kExtensionPrefsScopeIncognitoSessionOnly);
  if (incognito) {
    // We don't check incognito permissions here, as an extension should be
    // always allowed to clear its own settings.
  } else {
    // Incognito profiles can't access regular mode ever, they only exist in
    // split mode.
    if (profile()->IsOffTheRecord()) {
      error_ = "Can't modify regular settings from an incognito context.";
      return false;
    }
  }

  std::string browser_pref;
  if (!ValidateBrowserPref(pref_key, &browser_pref))
    return false;

  extensions::ExtensionPrefs* prefs =
      profile_->GetExtensionService()->extension_prefs();
  prefs->RemoveExtensionControlledPref(extension_id(), browser_pref, scope);
  return true;
}
