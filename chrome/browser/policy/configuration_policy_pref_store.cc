// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/configuration_policy_pref_store.h"

#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/command_line.h"
#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/stl_util-inl.h"
#include "base/string16.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/policy/browser_policy_connector.h"
#include "chrome/browser/policy/configuration_policy_provider.h"
#include "chrome/browser/policy/policy_path_parser.h"
#include "chrome/browser/policy/profile_policy_connector.h"
#include "chrome/browser/policy/profile_policy_connector_factory.h"
#include "chrome/browser/prefs/pref_value_map.h"
#include "chrome/browser/prefs/proxy_config_dictionary.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/search_terms_data.h"
#include "chrome/browser/search_engines/template_url.h"
#include "chrome/common/pref_names.h"
#include "content/common/notification_service.h"
#include "policy/policy_constants.h"

namespace policy {

// Accepts policy settings from a ConfigurationPolicyProvider, converts them
// to preferences and caches the result.
class ConfigurationPolicyPrefKeeper
    : private ConfigurationPolicyStoreInterface {
 public:
  explicit ConfigurationPolicyPrefKeeper(ConfigurationPolicyProvider* provider);
  virtual ~ConfigurationPolicyPrefKeeper();

  // Get a preference value.
  PrefStore::ReadResult GetValue(const std::string& key,
                                 const Value** result) const;

  // Compute the set of preference names that are different in |keeper|. This
  // includes preferences that are missing in either one.
  void GetDifferingPrefPaths(const ConfigurationPolicyPrefKeeper* other,
                             std::vector<std::string>* differing_prefs) const;

 private:
  // ConfigurationPolicyStore methods:
  virtual void Apply(ConfigurationPolicyType setting, Value* value);

  // Policies that map to a single preference are handled
  // by an automated converter. Each one of these policies
  // has an entry in |simple_policy_map_| with the following type.
  struct PolicyToPreferenceMapEntry {
    Value::ValueType value_type;
    ConfigurationPolicyType policy_type;
    const char* preference_path;  // A DictionaryValue path, not a file path.
  };

  // Remove the preferences found in the map from |prefs_|. Returns true if any
  // such preferences were found and removed.
  bool RemovePreferencesOfMap(const PolicyToPreferenceMapEntry* map,
                              int table_size);

  bool ApplyPolicyFromMap(ConfigurationPolicyType policy,
                          Value* value,
                          const PolicyToPreferenceMapEntry* map,
                          int size);

  // Processes proxy-specific policies. Returns true if the specified policy
  // is a proxy-related policy. ApplyProxyPolicy assumes the ownership
  // of |value| in the case that the policy is proxy-specific.
  bool ApplyProxyPolicy(ConfigurationPolicyType policy, Value* value);

  // Handles sync-related policies. Returns true if the policy was handled.
  // Assumes ownership of |value| in that case.
  bool ApplySyncPolicy(ConfigurationPolicyType policy, Value* value);

  // Handles policies that affect Autofill. Returns true if the policy was
  // handled and assumes ownership of |value| in that case.
  bool ApplyAutofillPolicy(ConfigurationPolicyType policy, Value* value);

  // Processes download directory policy. Returns true if the specified policy
  // is the download directory policy. ApplyDownloadDirPolicy assumes the
  // ownership of |value| in the case that the policy is recognized.
  bool ApplyDownloadDirPolicy(ConfigurationPolicyType policy, Value* value);

  // Processes disk cache directory policy. Returns true if the specified policy
  // is the right one. ApplyDiskCacheDirPolicy assumes the
  // ownership of |value| in the case that the policy is recognized.
  bool ApplyDiskCacheDirPolicy(ConfigurationPolicyType policy, Value* value);

  // Processes file-selection dialogs policy. Returns true if the specified
  // policy is the file-selection dialogs policy.
  // ApplyFileSelectionDialogsPolicy assumes the ownership of |value| in the
  // case that the policy is recognized.
  bool ApplyFileSelectionDialogsPolicy(ConfigurationPolicyType policy,
                                       Value* value);

  // Processes default search provider policies. Returns true if the specified
  // policy is a default search provider related policy. In that case,
  // ApplyDefaultSearchPolicy takes ownership of |value|.
  bool ApplyDefaultSearchPolicy(ConfigurationPolicyType policy, Value* value);

  // Make sure that the |path| if present in |prefs_|.  If not, set it to
  // a blank string.
  void EnsureStringPrefExists(const std::string& path);

  // If the required entries for default search are specified and valid,
  // finalizes the policy-specified configuration by initializing the
  // unspecified map entries.  Otherwise wipes all default search related
  // map entries from |prefs_|.
  void FinalizeDefaultSearchPolicySettings();

  // If the required entries for the proxy settings are specified and valid,
  // finalizes the policy-specified configuration by initializing the
  // respective values in |prefs_|.
  void FinalizeProxyPolicySettings();

  // Returns true if the policy values stored in proxy_* represent a valid proxy
  // configuration, including the case in which there is no configuration at
  // all.
  bool CheckProxySettings();

  // Assumes CheckProxySettings returns true and applies the values stored
  // in proxy_*.
  void ApplyProxySettings();

  bool HasProxyPolicy(ConfigurationPolicyType policy) const;

  // Temporary cache that stores values until FinalizeProxyPolicySettings()
  // is called.
  std::map<ConfigurationPolicyType, Value*> proxy_policies_;

  PrefValueMap prefs_;

  static const PolicyToPreferenceMapEntry kSimplePolicyMap[];
  static const PolicyToPreferenceMapEntry kDefaultSearchPolicyMap[];

  DISALLOW_COPY_AND_ASSIGN(ConfigurationPolicyPrefKeeper);
};

const ConfigurationPolicyPrefKeeper::PolicyToPreferenceMapEntry
    ConfigurationPolicyPrefKeeper::kSimplePolicyMap[] = {
  { Value::TYPE_STRING, kPolicyHomepageLocation,  prefs::kHomePage },
  { Value::TYPE_BOOLEAN, kPolicyHomepageIsNewTabPage,
    prefs::kHomePageIsNewTabPage },
  { Value::TYPE_INTEGER, kPolicyRestoreOnStartup,
    prefs::kRestoreOnStartup},
  { Value::TYPE_LIST, kPolicyRestoreOnStartupURLs,
    prefs::kURLsToRestoreOnStartup },
  { Value::TYPE_BOOLEAN, kPolicyAlternateErrorPagesEnabled,
    prefs::kAlternateErrorPagesEnabled },
  { Value::TYPE_BOOLEAN, kPolicySearchSuggestEnabled,
    prefs::kSearchSuggestEnabled },
  { Value::TYPE_BOOLEAN, kPolicyDnsPrefetchingEnabled,
    prefs::kNetworkPredictionEnabled },
  { Value::TYPE_BOOLEAN, kPolicyDisableSpdy,
    prefs::kDisableSpdy },
  { Value::TYPE_LIST, kPolicyDisabledSchemes,
    prefs::kDisabledSchemes },
  { Value::TYPE_BOOLEAN, kPolicySafeBrowsingEnabled,
    prefs::kSafeBrowsingEnabled },
  { Value::TYPE_BOOLEAN, kPolicyPasswordManagerEnabled,
    prefs::kPasswordManagerEnabled },
  { Value::TYPE_BOOLEAN, kPolicyPasswordManagerAllowShowPasswords,
    prefs::kPasswordManagerAllowShowPasswords },
  { Value::TYPE_BOOLEAN, kPolicyPrintingEnabled,
    prefs::kPrintingEnabled },
  { Value::TYPE_BOOLEAN, kPolicyMetricsReportingEnabled,
    prefs::kMetricsReportingEnabled },
  { Value::TYPE_STRING, kPolicyApplicationLocaleValue,
    prefs::kApplicationLocale},
  { Value::TYPE_LIST, kPolicyExtensionInstallWhitelist,
    prefs::kExtensionInstallAllowList},
  { Value::TYPE_LIST, kPolicyExtensionInstallBlacklist,
    prefs::kExtensionInstallDenyList},
  { Value::TYPE_LIST, kPolicyExtensionInstallForcelist,
    prefs::kExtensionInstallForceList},
  { Value::TYPE_LIST, kPolicyDisabledPlugins,
    prefs::kPluginsDisabledPlugins},
  { Value::TYPE_LIST, kPolicyDisabledPluginsExceptions,
    prefs::kPluginsDisabledPluginsExceptions},
  { Value::TYPE_LIST, kPolicyEnabledPlugins,
    prefs::kPluginsEnabledPlugins},
  { Value::TYPE_BOOLEAN, kPolicyShowHomeButton,
    prefs::kShowHomeButton },
  { Value::TYPE_BOOLEAN, kPolicyJavascriptEnabled,
    prefs::kWebKitJavascriptEnabled },
  { Value::TYPE_BOOLEAN, kPolicyIncognitoEnabled,
    prefs::kIncognitoEnabled },
  { Value::TYPE_BOOLEAN, kPolicySavingBrowserHistoryDisabled,
    prefs::kSavingBrowserHistoryDisabled },
  { Value::TYPE_BOOLEAN, kPolicyClearSiteDataOnExit,
    prefs::kClearSiteDataOnExit },
  { Value::TYPE_BOOLEAN, kPolicyDeveloperToolsDisabled,
    prefs::kDevToolsDisabled },
  { Value::TYPE_BOOLEAN, kPolicyBlockThirdPartyCookies,
    prefs::kBlockThirdPartyCookies },
  { Value::TYPE_INTEGER, kPolicyDefaultCookiesSetting,
    prefs::kManagedDefaultCookiesSetting },
  { Value::TYPE_INTEGER, kPolicyDefaultImagesSetting,
    prefs::kManagedDefaultImagesSetting },
  { Value::TYPE_INTEGER, kPolicyDefaultJavaScriptSetting,
    prefs::kManagedDefaultJavaScriptSetting },
  { Value::TYPE_INTEGER, kPolicyDefaultPluginsSetting,
    prefs::kManagedDefaultPluginsSetting },
  { Value::TYPE_INTEGER, kPolicyDefaultPopupsSetting,
    prefs::kManagedDefaultPopupsSetting },
  { Value::TYPE_LIST, kPolicyCookiesAllowedForUrls,
    prefs::kManagedCookiesAllowedForUrls },
  { Value::TYPE_LIST, kPolicyCookiesBlockedForUrls,
    prefs::kManagedCookiesBlockedForUrls },
  { Value::TYPE_LIST, kPolicyCookiesSessionOnlyForUrls,
    prefs::kManagedCookiesSessionOnlyForUrls },
  { Value::TYPE_LIST, kPolicyImagesAllowedForUrls,
    prefs::kManagedImagesAllowedForUrls },
  { Value::TYPE_LIST, kPolicyImagesBlockedForUrls,
    prefs::kManagedImagesBlockedForUrls },
  { Value::TYPE_LIST, kPolicyJavaScriptAllowedForUrls,
    prefs::kManagedJavaScriptAllowedForUrls },
  { Value::TYPE_LIST, kPolicyJavaScriptBlockedForUrls,
    prefs::kManagedJavaScriptBlockedForUrls },
  { Value::TYPE_LIST, kPolicyPluginsAllowedForUrls,
    prefs::kManagedPluginsAllowedForUrls },
  { Value::TYPE_LIST, kPolicyPluginsBlockedForUrls,
    prefs::kManagedPluginsBlockedForUrls },
  { Value::TYPE_LIST, kPolicyPopupsAllowedForUrls,
    prefs::kManagedPopupsAllowedForUrls },
  { Value::TYPE_LIST, kPolicyPopupsBlockedForUrls,
    prefs::kManagedPopupsBlockedForUrls },
  { Value::TYPE_INTEGER, kPolicyDefaultNotificationSetting,
    prefs::kDesktopNotificationDefaultContentSetting },
  { Value::TYPE_INTEGER, kPolicyDefaultGeolocationSetting,
    prefs::kGeolocationDefaultContentSetting },
  { Value::TYPE_STRING, kPolicyAuthSchemes,
    prefs::kAuthSchemes },
  { Value::TYPE_BOOLEAN, kPolicyDisableAuthNegotiateCnameLookup,
    prefs::kDisableAuthNegotiateCnameLookup },
  { Value::TYPE_BOOLEAN, kPolicyEnableAuthNegotiatePort,
    prefs::kEnableAuthNegotiatePort },
  { Value::TYPE_STRING, kPolicyAuthServerWhitelist,
    prefs::kAuthServerWhitelist },
  { Value::TYPE_STRING, kPolicyAuthNegotiateDelegateWhitelist,
    prefs::kAuthNegotiateDelegateWhitelist },
  { Value::TYPE_STRING, kPolicyGSSAPILibraryName,
    prefs::kGSSAPILibraryName },
  { Value::TYPE_BOOLEAN, kPolicyAllowCrossOriginAuthPrompt,
    prefs::kAllowCrossOriginAuthPrompt },
  { Value::TYPE_BOOLEAN, kPolicyDisable3DAPIs,
    prefs::kDisable3DAPIs },
  { Value::TYPE_BOOLEAN, kPolicyDisablePluginFinder,
    prefs::kDisablePluginFinder },
  { Value::TYPE_INTEGER, kPolicyPolicyRefreshRate,
    prefs::kPolicyRefreshRate },
  { Value::TYPE_BOOLEAN, kPolicyInstantEnabled, prefs::kInstantEnabled },
  { Value::TYPE_BOOLEAN, kPolicyDefaultBrowserSettingEnabled,
    prefs::kDefaultBrowserSettingEnabled },
  { Value::TYPE_BOOLEAN, kPolicyCloudPrintProxyEnabled,
    prefs::kCloudPrintProxyEnabled },
  { Value::TYPE_BOOLEAN, kPolicyTranslateEnabled, prefs::kEnableTranslate },
  { Value::TYPE_BOOLEAN, kPolicyBookmarkBarEnabled, prefs::kEnableBookmarkBar },
  { Value::TYPE_BOOLEAN, kPolicyAllowOutdatedPlugins,
    prefs::kPluginsAllowOutdated },
  { Value::TYPE_BOOLEAN, kPolicyAlwaysAuthorizePlugins,
    prefs::kPluginsAlwaysAuthorize },
  { Value::TYPE_BOOLEAN, kPolicyEditBookmarksEnabled,
    prefs::kEditBookmarksEnabled },
  { Value::TYPE_BOOLEAN, kPolicyAllowFileSelectionDialogs,
    prefs::kAllowFileSelectionDialogs },

#if defined(OS_CHROMEOS)
  { Value::TYPE_BOOLEAN, kPolicyChromeOsLockOnIdleSuspend,
    prefs::kEnableScreenLock },
#endif
};

const ConfigurationPolicyPrefKeeper::PolicyToPreferenceMapEntry
    ConfigurationPolicyPrefKeeper::kDefaultSearchPolicyMap[] = {
  { Value::TYPE_BOOLEAN, kPolicyDefaultSearchProviderEnabled,
    prefs::kDefaultSearchProviderEnabled },
  { Value::TYPE_STRING, kPolicyDefaultSearchProviderName,
    prefs::kDefaultSearchProviderName },
  { Value::TYPE_STRING, kPolicyDefaultSearchProviderKeyword,
    prefs::kDefaultSearchProviderKeyword },
  { Value::TYPE_STRING, kPolicyDefaultSearchProviderSearchURL,
    prefs::kDefaultSearchProviderSearchURL },
  { Value::TYPE_STRING, kPolicyDefaultSearchProviderSuggestURL,
    prefs::kDefaultSearchProviderSuggestURL },
  { Value::TYPE_STRING, kPolicyDefaultSearchProviderInstantURL,
    prefs::kDefaultSearchProviderInstantURL },
  { Value::TYPE_STRING, kPolicyDefaultSearchProviderIconURL,
    prefs::kDefaultSearchProviderIconURL },
  { Value::TYPE_LIST, kPolicyDefaultSearchProviderEncodings,
    prefs::kDefaultSearchProviderEncodings },
};

ConfigurationPolicyPrefKeeper::ConfigurationPolicyPrefKeeper(
    ConfigurationPolicyProvider* provider) {
  if (!provider->Provide(this))
    LOG(WARNING) << "Failed to get policy from provider.";
  FinalizeProxyPolicySettings();
  FinalizeDefaultSearchPolicySettings();
}

ConfigurationPolicyPrefKeeper::~ConfigurationPolicyPrefKeeper() {
  DCHECK(proxy_policies_.empty());
}

PrefStore::ReadResult
ConfigurationPolicyPrefKeeper::GetValue(const std::string& key,
                                        const Value** result) const {
  const Value* stored_value = NULL;
  if (!prefs_.GetValue(key, &stored_value))
    return PrefStore::READ_NO_VALUE;

  // Check whether there's a default value, which indicates READ_USE_DEFAULT
  // should be returned.
  if (stored_value->IsType(Value::TYPE_NULL))
    return PrefStore::READ_USE_DEFAULT;

  *result = stored_value;
  return PrefStore::READ_OK;
}

void ConfigurationPolicyPrefKeeper::GetDifferingPrefPaths(
    const ConfigurationPolicyPrefKeeper* other,
    std::vector<std::string>* differing_prefs) const {
  prefs_.GetDifferingKeys(&other->prefs_, differing_prefs);
}

void ConfigurationPolicyPrefKeeper::Apply(ConfigurationPolicyType policy,
                                          Value* value) {
  if (ApplyProxyPolicy(policy, value))
    return;

  if (ApplySyncPolicy(policy, value))
    return;

  if (ApplyAutofillPolicy(policy, value))
    return;

  if (ApplyDownloadDirPolicy(policy, value))
    return;

  if (ApplyDiskCacheDirPolicy(policy, value))
    return;

  if (ApplyFileSelectionDialogsPolicy(policy, value))
    return;

  if (ApplyDefaultSearchPolicy(policy, value))
    return;

  if (ApplyPolicyFromMap(policy, value, kSimplePolicyMap,
                         arraysize(kSimplePolicyMap)))
    return;

  // Other policy implementations go here.
  NOTIMPLEMENTED();
  delete value;
}

bool ConfigurationPolicyPrefKeeper::RemovePreferencesOfMap(
    const PolicyToPreferenceMapEntry* map, int table_size) {
  bool found_any = false;
  for (int i = 0; i < table_size; ++i) {
    if (prefs_.RemoveValue(map[i].preference_path))
      found_any = true;
  }
  return found_any;
}

bool ConfigurationPolicyPrefKeeper::ApplyPolicyFromMap(
    ConfigurationPolicyType policy,
    Value* value,
    const PolicyToPreferenceMapEntry* map,
    int size) {
  for (int current = 0; current < size; ++current) {
    if (map[current].policy_type == policy) {
      DCHECK_EQ(map[current].value_type, value->GetType())
          << "mismatch in provided and expected policy value for preferences "
          << map[current].preference_path << ". expected = "
          << map[current].value_type << ", actual = "<< value->GetType();
      prefs_.SetValue(map[current].preference_path, value);
      return true;
    }
  }
  return false;
}

bool ConfigurationPolicyPrefKeeper::ApplyProxyPolicy(
    ConfigurationPolicyType policy,
    Value* value) {
  // We only collect the values until we have sufficient information when
  // FinalizeProxyPolicySettings() is called to determine whether the presented
  // values were correct and apply them in that case.
  if (policy == kPolicyProxyMode ||
      policy == kPolicyProxyServerMode ||
      policy == kPolicyProxyServer ||
      policy == kPolicyProxyPacUrl ||
      policy == kPolicyProxyBypassList) {
    delete proxy_policies_[policy];
    proxy_policies_[policy] = value;
    return true;
  }
  // We are not interested in this policy.
  return false;
}

bool ConfigurationPolicyPrefKeeper::ApplySyncPolicy(
    ConfigurationPolicyType policy, Value* value) {
  if (policy == kPolicySyncDisabled) {
    bool disable_sync;
    if (value->GetAsBoolean(&disable_sync) && disable_sync)
      prefs_.SetValue(prefs::kSyncManaged, value);
    else
      delete value;
    return true;
  }
  return false;
}

bool ConfigurationPolicyPrefKeeper::ApplyAutofillPolicy(
    ConfigurationPolicyType policy, Value* value) {
  if (policy == kPolicyAutoFillEnabled) {
    bool auto_fill_enabled;
    if (value->GetAsBoolean(&auto_fill_enabled) && !auto_fill_enabled)
      prefs_.SetValue(prefs::kAutofillEnabled,
                       Value::CreateBooleanValue(false));
    delete value;
    return true;
  }
  return false;
}

bool ConfigurationPolicyPrefKeeper::ApplyDownloadDirPolicy(
    ConfigurationPolicyType policy,
    Value* value) {
  // Replace the policy string which might contain some user variables to an
  // expanded string.
  if (policy == kPolicyDownloadDirectory) {
    // This policy is ignored on ChromeOS because the download path there is
    // fixed and can not be configured by the user.
#if !defined(OS_CHROMEOS)
    FilePath::StringType string_value;
    bool result = value->GetAsString(&string_value);
    DCHECK(result);
    FilePath::StringType expanded_value =
        policy::path_parser::ExpandPathVariables(string_value);
    prefs_.SetValue(prefs::kDownloadDefaultDirectory,
                    Value::CreateStringValue(expanded_value));
    prefs_.SetValue(prefs::kPromptForDownload,
                    Value::CreateBooleanValue(false));
#endif  // !defined(OS_CHROMEOS)
    delete value;
    return true;
  }
  // We are not interested in this policy.
  return false;
}

bool ConfigurationPolicyPrefKeeper::ApplyDiskCacheDirPolicy(
    ConfigurationPolicyType policy,
    Value* value) {
  // Replace the policy string which might contain some user variables to an
  // expanded string.
  if (policy == kPolicyDiskCacheDir) {
    FilePath::StringType string_value;
    bool result = value->GetAsString(&string_value);
    DCHECK(result);
    FilePath::StringType expanded_value =
        policy::path_parser::ExpandPathVariables(string_value);
    prefs_.SetValue(prefs::kDiskCacheDir,
                    Value::CreateStringValue(expanded_value));
    delete value;
    return true;
  }
  // We are not interested in this policy.
  return false;
}

bool ConfigurationPolicyPrefKeeper::ApplyFileSelectionDialogsPolicy(
    ConfigurationPolicyType policy,
    Value* value) {
  if (policy == kPolicyAllowFileSelectionDialogs) {
    prefs_.SetValue(prefs::kAllowFileSelectionDialogs, value);
    // If file-selection dialogs are not allowed we forbid the user to be
    // prompted for the download location, since this would end up in an Infobar
    // explaining that file-selection dialogs are forbidden anyways.
    bool allow_file_selection_dialogs = true;
    bool result = value->GetAsBoolean(&allow_file_selection_dialogs);
    DCHECK(result);
    if (!allow_file_selection_dialogs) {
      prefs_.SetValue(prefs::kPromptForDownload,
                      Value::CreateBooleanValue(false));
    }
    return true;
  }
  // We are not interested in this policy.
  return false;
}

bool ConfigurationPolicyPrefKeeper::ApplyDefaultSearchPolicy(
    ConfigurationPolicyType policy,
    Value* value) {
  // The DefaultSearchProviderEncodings policy has type list, but the related
  // preference has type string. Convert one into the other here, using
  // ';' as a separator.
  if (policy == kPolicyDefaultSearchProviderEncodings) {
    ListValue* list;
    if (!value->GetAsList(&list)) {
      NOTREACHED()
          << "mismatch in provided and expected policy value for preferences "
          << prefs::kDefaultSearchProviderEncodings << ". expected = "
          << Value::TYPE_LIST << ", actual = "<< value->GetType();
      return false;
    }
    ListValue::const_iterator iter(list->begin());
    ListValue::const_iterator end(list->end());
    std::string encodings;
    for (; iter != end; ++iter) {
      std::string s;
      if ((*iter)->GetAsString(&s)) {
        if (!encodings.empty())
          encodings.push_back(';');
        encodings.append(s);
      } else {
        NOTREACHED();
      }
    }
    // We own |value|.
    delete value;
    prefs_.SetValue(prefs::kDefaultSearchProviderEncodings,
                    Value::CreateStringValue(encodings));
    return true;
  }

  if (ApplyPolicyFromMap(policy, value, kDefaultSearchPolicyMap,
                         arraysize(kDefaultSearchPolicyMap))) {
    return true;
  }
  return false;
}

void ConfigurationPolicyPrefKeeper::EnsureStringPrefExists(
    const std::string& path) {
  std::string value;
  if (!prefs_.GetString(path, &value))
    prefs_.SetString(path, value);
}

namespace {

// Implementation of SearchTermsData just for validation.
class SearchTermsDataForValidation : public SearchTermsData {
 public:
  SearchTermsDataForValidation() {}

  // Implementation of SearchTermsData.
  virtual std::string GoogleBaseURLValue() const {
    return "http://www.google.com/";
  }
  virtual std::string GetApplicationLocale() const {
    return "en";
  }
#if defined(OS_WIN) && defined(GOOGLE_CHROME_BUILD)
  virtual string16 GetRlzParameterValue() const {
    return string16();
  }
#endif
 private:
  DISALLOW_COPY_AND_ASSIGN(SearchTermsDataForValidation);
};

}  // namespace

void ConfigurationPolicyPrefKeeper::FinalizeDefaultSearchPolicySettings() {
  bool enabled = true;
  if (prefs_.GetBoolean(prefs::kDefaultSearchProviderEnabled, &enabled) &&
      !enabled) {
    // If default search is disabled, we ignore the other fields.
    prefs_.SetString(prefs::kDefaultSearchProviderName, std::string());
    prefs_.SetString(prefs::kDefaultSearchProviderSearchURL, std::string());
    prefs_.SetString(prefs::kDefaultSearchProviderSuggestURL, std::string());
    prefs_.SetString(prefs::kDefaultSearchProviderIconURL, std::string());
    prefs_.SetString(prefs::kDefaultSearchProviderEncodings, std::string());
    prefs_.SetString(prefs::kDefaultSearchProviderKeyword, std::string());
    prefs_.SetString(prefs::kDefaultSearchProviderInstantURL, std::string());
    return;
  }
  std::string search_url;
  // The search URL is required.
  if (prefs_.GetString(prefs::kDefaultSearchProviderSearchURL, &search_url) &&
      !search_url.empty()) {
    SearchTermsDataForValidation search_terms_data;
    const TemplateURLRef search_url_ref(search_url, 0, 0);
    // It must support replacement (which implies it is valid).
    if (search_url_ref.SupportsReplacementUsingTermsData(search_terms_data)) {
      // The other entries are optional.  Just make sure that they are all
      // specified via policy, so that we don't use regular prefs.
      EnsureStringPrefExists(prefs::kDefaultSearchProviderSuggestURL);
      EnsureStringPrefExists(prefs::kDefaultSearchProviderIconURL);
      EnsureStringPrefExists(prefs::kDefaultSearchProviderEncodings);
      EnsureStringPrefExists(prefs::kDefaultSearchProviderKeyword);
      EnsureStringPrefExists(prefs::kDefaultSearchProviderInstantURL);

      // For the name, default to the host if not specified.
      std::string name;
      if (!prefs_.GetString(prefs::kDefaultSearchProviderName, &name) ||
          name.empty())
        prefs_.SetString(prefs::kDefaultSearchProviderName,
                          GURL(search_url).host());

      // And clear the IDs since these are not specified via policy.
      prefs_.SetString(prefs::kDefaultSearchProviderID, std::string());
      prefs_.SetString(prefs::kDefaultSearchProviderPrepopulateID,
                        std::string());
      return;
    }
  }
  // Required entries are not there.  Remove any related entries.
  RemovePreferencesOfMap(kDefaultSearchPolicyMap,
                         arraysize(kDefaultSearchPolicyMap));
}

void ConfigurationPolicyPrefKeeper::FinalizeProxyPolicySettings() {
  if (CheckProxySettings())
    ApplyProxySettings();

  STLDeleteContainerPairSecondPointers(proxy_policies_.begin(),
                                       proxy_policies_.end());
  proxy_policies_.clear();
}

bool ConfigurationPolicyPrefKeeper::CheckProxySettings() {
  bool mode = HasProxyPolicy(kPolicyProxyMode);
  bool server_mode = HasProxyPolicy(kPolicyProxyServerMode);  // deprecated
  bool server = HasProxyPolicy(kPolicyProxyServer);
  bool pac_url = HasProxyPolicy(kPolicyProxyPacUrl);
  bool bypass_list = HasProxyPolicy(kPolicyProxyBypassList);

  if ((server || pac_url || bypass_list) && !(mode || server_mode)) {
    LOG(WARNING) << "A centrally-administered policy defines proxy setting"
                 << " details without setting a proxy mode.";
    return false;
  }

  // If there's a server mode, convert it into a mode.
  std::string mode_value;
  if (mode) {
    if (server_mode)
      LOG(WARNING) << "Both ProxyMode and ProxyServerMode policies defined, "
                   << "ignoring ProxyMode.";
    if (!proxy_policies_[kPolicyProxyMode]->GetAsString(&mode_value)) {
      LOG(WARNING) << "Invalid ProxyMode value.";
      return false;
    }
  } else if (server_mode) {
    int server_mode_value;
    if (!proxy_policies_[kPolicyProxyServerMode]->GetAsInteger(
        &server_mode_value)) {
      LOG(WARNING) << "Invalid ProxyServerMode value.";
      return false;
    }

    switch (server_mode_value) {
      case kPolicyNoProxyServerMode:
        mode_value = ProxyPrefs::kDirectProxyModeName;
        break;
      case kPolicyAutoDetectProxyServerMode:
        mode_value = ProxyPrefs::kAutoDetectProxyModeName;
        break;
      case kPolicyManuallyConfiguredProxyServerMode:
        if (server && pac_url) {
          LOG(WARNING) << "A centrally-administered policy dictates that"
                       << " both fixed proxy servers and a .pac url. should"
                       << " be used for proxy configuration.";
          return false;
        }
        if (!server && !pac_url) {
          LOG(WARNING) << "A centrally-administered policy dictates that the"
                       << " proxy settings should use either fixed proxy"
                       << " servers or a .pac url, but specifies neither.";
          return false;
        }
        if (pac_url)
          mode_value = ProxyPrefs::kPacScriptProxyModeName;
        else
          mode_value = ProxyPrefs::kFixedServersProxyModeName;
        break;
      case kPolicyUseSystemProxyServerMode:
        mode_value = ProxyPrefs::kSystemProxyModeName;
        break;
      default:
        LOG(WARNING) << "Invalid proxy mode " << server_mode_value;
        return false;
    }
  }

  // If neither ProxyMode nor ProxyServerMode are specified, mode_value will be
  // empty and the proxy shouldn't be configured at all.
  if (mode_value.empty())
    return true;

  if (mode_value == ProxyPrefs::kDirectProxyModeName) {
    if (server || pac_url || bypass_list) {
      LOG(WARNING) << "A centrally-administered policy disables the use of"
                   << " a proxy but also specifies an explicit proxy"
                   << " configuration.";
      return false;
    }
  } else if (mode_value == ProxyPrefs::kAutoDetectProxyModeName) {
    if (server || bypass_list || pac_url) {
      LOG(WARNING) << "A centrally-administered policy dictates that a proxy"
                   << " shall be auto configured but specifies fixed proxy"
                   << " servers, a by-pass list or a .pac script URL.";
      return false;
    }
  } else if (mode_value == ProxyPrefs::kPacScriptProxyModeName) {
    if (server || bypass_list) {
      LOG(WARNING) << "A centrally-administered policy dictates that a .pac"
                   << " script URL should be used for proxy configuration but"
                   << " also specifies policies required only for fixed"
                   << " proxy servers.";
      return false;
    }
  } else if (mode_value == ProxyPrefs::kFixedServersProxyModeName) {
    if (pac_url) {
      LOG(WARNING) << "A centrally-administered policy dictates that"
                   << " fixed proxy servers should be used but also"
                   << " specifies a .pac script URL.";
      return false;
    }
  } else if (mode_value == ProxyPrefs::kSystemProxyModeName) {
    if (server || pac_url || bypass_list) {
      LOG(WARNING) << "A centrally-administered policy dictates that the"
                   << " system proxy settings should be used but also "
                   << " specifies an explicit proxy configuration.";
      return false;
    }
  } else {
    LOG(WARNING) << "Invalid proxy mode " << mode_value;
    return false;
  }
  return true;
}

void ConfigurationPolicyPrefKeeper::ApplyProxySettings() {
  ProxyPrefs::ProxyMode mode;
  if (HasProxyPolicy(kPolicyProxyMode)) {
    std::string string_mode;
    CHECK(proxy_policies_[kPolicyProxyMode]->GetAsString(&string_mode));
    if (!ProxyPrefs::StringToProxyMode(string_mode, &mode)) {
      LOG(WARNING) << "A centrally-administered policy specifies a value for "
                   << "the ProxyMode policy that isn't recognized.";
      return;
    }
  } else if (HasProxyPolicy(kPolicyProxyServerMode)) {
    int int_mode = 0;
    CHECK(proxy_policies_[kPolicyProxyServerMode]->GetAsInteger(&int_mode));
    switch (int_mode) {
      case kPolicyNoProxyServerMode:
        mode = ProxyPrefs::MODE_DIRECT;
        break;
      case kPolicyAutoDetectProxyServerMode:
        mode = ProxyPrefs::MODE_AUTO_DETECT;
        break;
      case kPolicyManuallyConfiguredProxyServerMode:
        mode = ProxyPrefs::MODE_FIXED_SERVERS;
        if (HasProxyPolicy(kPolicyProxyPacUrl))
          mode = ProxyPrefs::MODE_PAC_SCRIPT;
        break;
      case kPolicyUseSystemProxyServerMode:
        mode = ProxyPrefs::MODE_SYSTEM;
        break;
      default:
        mode = ProxyPrefs::MODE_DIRECT;
        NOTREACHED();
    }
  } else {
    return;
  }
  switch (mode) {
    case ProxyPrefs::MODE_DIRECT:
      prefs_.SetValue(prefs::kProxy, ProxyConfigDictionary::CreateDirect());
      break;
    case ProxyPrefs::MODE_AUTO_DETECT:
      prefs_.SetValue(prefs::kProxy, ProxyConfigDictionary::CreateAutoDetect());
      break;
    case ProxyPrefs::MODE_PAC_SCRIPT: {
      if (!HasProxyPolicy(kPolicyProxyPacUrl)) {
        LOG(WARNING) << "A centrally-administered policy specifies to use a "
                     << "PAC script, but doesn't supply the PAC script URL.";
        return;
      }
      std::string pac_url;
      proxy_policies_[kPolicyProxyPacUrl]->GetAsString(&pac_url);
      prefs_.SetValue(prefs::kProxy,
                      ProxyConfigDictionary::CreatePacScript(pac_url, false));
      break;
    }
    case ProxyPrefs::MODE_FIXED_SERVERS: {
      if (!HasProxyPolicy(kPolicyProxyServer)) {
        LOG(WARNING) << "A centrally-administered policy specifies to use a "
                     << "fixed server, but doesn't supply the server address.";
        return;
      }
      std::string proxy_server;
      proxy_policies_[kPolicyProxyServer]->GetAsString(&proxy_server);
      std::string bypass_list;
      if (HasProxyPolicy(kPolicyProxyBypassList))
        proxy_policies_[kPolicyProxyBypassList]->GetAsString(&bypass_list);
      prefs_.SetValue(prefs::kProxy,
                      ProxyConfigDictionary::CreateFixedServers(proxy_server,
                                                                bypass_list));
      break;
    }
    case ProxyPrefs::MODE_SYSTEM:
      prefs_.SetValue(prefs::kProxy,
                      ProxyConfigDictionary::CreateSystem());
      break;
    case ProxyPrefs::kModeCount:
      NOTREACHED();
  }
}

bool ConfigurationPolicyPrefKeeper::HasProxyPolicy(
    ConfigurationPolicyType policy) const {
  std::map<ConfigurationPolicyType, Value*>::const_iterator iter;
  iter = proxy_policies_.find(policy);
  std::string tmp;
  if (iter == proxy_policies_.end() ||
      !iter->second ||
      iter->second->IsType(Value::TYPE_NULL) ||
      (iter->second->IsType(Value::TYPE_STRING) &&
       iter->second->GetAsString(&tmp) &&
       tmp.empty())) {
    return false;
  }
  return true;
}

ConfigurationPolicyPrefStore::ConfigurationPolicyPrefStore(
    ConfigurationPolicyProvider* provider)
    : provider_(provider),
      initialization_complete_(false) {
  if (provider_) {
    // Read initial policy.
    policy_keeper_.reset(new ConfigurationPolicyPrefKeeper(provider));
    registrar_.Init(provider_, this);
    initialization_complete_ = provider->IsInitializationComplete();
  } else {
    initialization_complete_ = true;
  }
}

ConfigurationPolicyPrefStore::~ConfigurationPolicyPrefStore() {
}

void ConfigurationPolicyPrefStore::AddObserver(PrefStore::Observer* observer) {
  observers_.AddObserver(observer);
}

void ConfigurationPolicyPrefStore::RemoveObserver(
    PrefStore::Observer* observer) {
  observers_.RemoveObserver(observer);
}

bool ConfigurationPolicyPrefStore::IsInitializationComplete() const {
  return initialization_complete_;
}

PrefStore::ReadResult
ConfigurationPolicyPrefStore::GetValue(const std::string& key,
                                       const Value** value) const {
  if (policy_keeper_.get())
    return policy_keeper_->GetValue(key, value);

  return PrefStore::READ_NO_VALUE;
}

void ConfigurationPolicyPrefStore::OnUpdatePolicy() {
  Refresh();
}

void ConfigurationPolicyPrefStore::OnProviderGoingAway() {
  provider_ = NULL;
}

// static
ConfigurationPolicyPrefStore*
ConfigurationPolicyPrefStore::CreateManagedPlatformPolicyPrefStore() {
  BrowserPolicyConnector* connector =
      g_browser_process->browser_policy_connector();
  return new ConfigurationPolicyPrefStore(
      connector->GetManagedPlatformProvider());
}

// static
ConfigurationPolicyPrefStore*
ConfigurationPolicyPrefStore::CreateManagedCloudPolicyPrefStore(
    Profile* profile) {
  ConfigurationPolicyProvider* provider = NULL;
  if (profile) {
    // For user policy, return the profile's policy provider.
    provider = policy::ProfilePolicyConnectorFactory::GetForProfile(profile)->
        GetManagedCloudProvider();
  } else {
    // For device policy, return the provider of the browser process.
    BrowserPolicyConnector* connector =
        g_browser_process->browser_policy_connector();
    provider = connector->GetManagedCloudProvider();
  }
  return new ConfigurationPolicyPrefStore(provider);
}

// static
ConfigurationPolicyPrefStore*
ConfigurationPolicyPrefStore::CreateRecommendedPlatformPolicyPrefStore() {
  BrowserPolicyConnector* connector =
      g_browser_process->browser_policy_connector();
  return new ConfigurationPolicyPrefStore(
      connector->GetRecommendedPlatformProvider());
}

// static
ConfigurationPolicyPrefStore*
ConfigurationPolicyPrefStore::CreateRecommendedCloudPolicyPrefStore(
    Profile* profile) {
  ConfigurationPolicyProvider* provider = NULL;
  if (profile) {
    // For user policy, return the profile's policy provider.
    provider = policy::ProfilePolicyConnectorFactory::GetForProfile(profile)->
        GetRecommendedCloudProvider();
  } else {
    // For device policy, return the provider of the browser process.
    BrowserPolicyConnector* connector =
        g_browser_process->browser_policy_connector();
    provider = connector->GetRecommendedCloudProvider();
  }
  return new ConfigurationPolicyPrefStore(provider);
}

/* static */
const ConfigurationPolicyProvider::PolicyDefinitionList*
ConfigurationPolicyPrefStore::GetChromePolicyDefinitionList() {
  static ConfigurationPolicyProvider::PolicyDefinitionList::Entry entries[] = {
    { kPolicyHomepageLocation, Value::TYPE_STRING, key::kHomepageLocation },
    { kPolicyHomepageIsNewTabPage, Value::TYPE_BOOLEAN,
      key::kHomepageIsNewTabPage },
    { kPolicyRestoreOnStartup, Value::TYPE_INTEGER, key::kRestoreOnStartup },
    { kPolicyRestoreOnStartupURLs, Value::TYPE_LIST,
      key::kRestoreOnStartupURLs },
    { kPolicyDefaultSearchProviderEnabled, Value::TYPE_BOOLEAN,
      key::kDefaultSearchProviderEnabled },
    { kPolicyDefaultSearchProviderName, Value::TYPE_STRING,
      key::kDefaultSearchProviderName },
    { kPolicyDefaultSearchProviderKeyword, Value::TYPE_STRING,
      key::kDefaultSearchProviderKeyword },
    { kPolicyDefaultSearchProviderSearchURL, Value::TYPE_STRING,
      key::kDefaultSearchProviderSearchURL },
    { kPolicyDefaultSearchProviderSuggestURL, Value::TYPE_STRING,
      key::kDefaultSearchProviderSuggestURL },
    { kPolicyDefaultSearchProviderInstantURL, Value::TYPE_STRING,
      key::kDefaultSearchProviderInstantURL },
    { kPolicyDefaultSearchProviderIconURL, Value::TYPE_STRING,
      key::kDefaultSearchProviderIconURL },
    { kPolicyDefaultSearchProviderEncodings, Value::TYPE_LIST,
      key::kDefaultSearchProviderEncodings },
    { kPolicyProxyMode, Value::TYPE_STRING, key::kProxyMode },
    { kPolicyProxyServerMode, Value::TYPE_INTEGER, key::kProxyServerMode },
    { kPolicyProxyServer, Value::TYPE_STRING, key::kProxyServer },
    { kPolicyProxyPacUrl, Value::TYPE_STRING, key::kProxyPacUrl },
    { kPolicyProxyBypassList, Value::TYPE_STRING, key::kProxyBypassList },
    { kPolicyAlternateErrorPagesEnabled, Value::TYPE_BOOLEAN,
      key::kAlternateErrorPagesEnabled },
    { kPolicySearchSuggestEnabled, Value::TYPE_BOOLEAN,
      key::kSearchSuggestEnabled },
    { kPolicyDnsPrefetchingEnabled, Value::TYPE_BOOLEAN,
      key::kDnsPrefetchingEnabled },
    { kPolicyDisableSpdy, Value::TYPE_BOOLEAN, key::kDisableSpdy },
    { kPolicyDisabledSchemes, Value::TYPE_LIST, key::kDisabledSchemes },
    { kPolicySafeBrowsingEnabled, Value::TYPE_BOOLEAN,
      key::kSafeBrowsingEnabled },
    { kPolicyMetricsReportingEnabled, Value::TYPE_BOOLEAN,
      key::kMetricsReportingEnabled },
    { kPolicyPasswordManagerEnabled, Value::TYPE_BOOLEAN,
      key::kPasswordManagerEnabled },
    { kPolicyPasswordManagerAllowShowPasswords, Value::TYPE_BOOLEAN,
      key::kPasswordManagerAllowShowPasswords },
    { kPolicyAutoFillEnabled, Value::TYPE_BOOLEAN, key::kAutoFillEnabled },
    { kPolicyDisabledPlugins, Value::TYPE_LIST, key::kDisabledPlugins },
    { kPolicyDisabledPluginsExceptions, Value::TYPE_LIST,
      key::kDisabledPluginsExceptions },
    { kPolicyEnabledPlugins, Value::TYPE_LIST, key::kEnabledPlugins },
    { kPolicyApplicationLocaleValue, Value::TYPE_STRING,
      key::kApplicationLocaleValue },
    { kPolicySyncDisabled, Value::TYPE_BOOLEAN, key::kSyncDisabled },
    { kPolicyExtensionInstallWhitelist, Value::TYPE_LIST,
      key::kExtensionInstallWhitelist },
    { kPolicyExtensionInstallBlacklist, Value::TYPE_LIST,
      key::kExtensionInstallBlacklist },
    { kPolicyExtensionInstallForcelist, Value::TYPE_LIST,
      key::kExtensionInstallForcelist },
    { kPolicyShowHomeButton, Value::TYPE_BOOLEAN, key::kShowHomeButton },
    { kPolicyPrintingEnabled, Value::TYPE_BOOLEAN, key::kPrintingEnabled },
    { kPolicyJavascriptEnabled, Value::TYPE_BOOLEAN, key::kJavascriptEnabled },
    { kPolicyIncognitoEnabled, Value::TYPE_BOOLEAN, key::kIncognitoEnabled },
    { kPolicySavingBrowserHistoryDisabled, Value::TYPE_BOOLEAN,
      key::kSavingBrowserHistoryDisabled },
    { kPolicyClearSiteDataOnExit, Value::TYPE_BOOLEAN,
      key::kClearSiteDataOnExit },
    { kPolicyDeveloperToolsDisabled, Value::TYPE_BOOLEAN,
      key::kDeveloperToolsDisabled },
    { kPolicyBlockThirdPartyCookies, Value::TYPE_BOOLEAN,
      key::kBlockThirdPartyCookies },
    { kPolicyDefaultCookiesSetting, Value::TYPE_INTEGER,
      key::kDefaultCookiesSetting },
    { kPolicyDefaultImagesSetting, Value::TYPE_INTEGER,
      key::kDefaultImagesSetting },
    { kPolicyDefaultJavaScriptSetting, Value::TYPE_INTEGER,
      key::kDefaultJavaScriptSetting },
    { kPolicyDefaultPluginsSetting, Value::TYPE_INTEGER,
      key::kDefaultPluginsSetting },
    { kPolicyDefaultPopupsSetting, Value::TYPE_INTEGER,
      key::kDefaultPopupsSetting },
    { kPolicyDefaultNotificationSetting, Value::TYPE_INTEGER,
      key::kDefaultNotificationSetting },
    { kPolicyDefaultGeolocationSetting, Value::TYPE_INTEGER,
      key::kDefaultGeolocationSetting },
    { kPolicyCookiesAllowedForUrls, Value::TYPE_LIST,
      key::kCookiesAllowedForUrls },
    { kPolicyCookiesBlockedForUrls, Value::TYPE_LIST,
      key::kCookiesBlockedForUrls },
    { kPolicyCookiesSessionOnlyForUrls, Value::TYPE_LIST,
      key::kCookiesSessionOnlyForUrls },
    { kPolicyImagesAllowedForUrls, Value::TYPE_LIST,
      key::kImagesAllowedForUrls },
    { kPolicyImagesBlockedForUrls, Value::TYPE_LIST,
      key::kImagesBlockedForUrls },
    { kPolicyJavaScriptAllowedForUrls, Value::TYPE_LIST,
      key::kJavaScriptAllowedForUrls },
    { kPolicyJavaScriptBlockedForUrls, Value::TYPE_LIST,
      key::kJavaScriptBlockedForUrls },
    { kPolicyPluginsAllowedForUrls, Value::TYPE_LIST,
      key::kPluginsAllowedForUrls },
    { kPolicyPluginsBlockedForUrls, Value::TYPE_LIST,
      key::kPluginsBlockedForUrls },
    { kPolicyPopupsAllowedForUrls, Value::TYPE_LIST,
      key::kPopupsAllowedForUrls },
    { kPolicyPopupsBlockedForUrls, Value::TYPE_LIST,
      key::kPopupsBlockedForUrls },
    { kPolicyAuthSchemes, Value::TYPE_STRING, key::kAuthSchemes },
    { kPolicyDisableAuthNegotiateCnameLookup, Value::TYPE_BOOLEAN,
      key::kDisableAuthNegotiateCnameLookup },
    { kPolicyEnableAuthNegotiatePort, Value::TYPE_BOOLEAN,
      key::kEnableAuthNegotiatePort },
    { kPolicyAuthServerWhitelist, Value::TYPE_STRING,
      key::kAuthServerWhitelist },
    { kPolicyAuthNegotiateDelegateWhitelist, Value::TYPE_STRING,
      key::kAuthNegotiateDelegateWhitelist },
    { kPolicyGSSAPILibraryName, Value::TYPE_STRING,
      key::kGSSAPILibraryName },
    { kPolicyAllowCrossOriginAuthPrompt, Value::TYPE_BOOLEAN,
      key::kAllowCrossOriginAuthPrompt },
    { kPolicyDisable3DAPIs, Value::TYPE_BOOLEAN,
      key::kDisable3DAPIs },
    { kPolicyDisablePluginFinder, Value::TYPE_BOOLEAN,
      key::kDisablePluginFinder },
    { kPolicyPolicyRefreshRate, Value::TYPE_INTEGER,
      key::kPolicyRefreshRate },
    { kPolicyInstantEnabled, Value::TYPE_BOOLEAN, key::kInstantEnabled },
    { kPolicyDefaultBrowserSettingEnabled, Value::TYPE_BOOLEAN,
      key::kDefaultBrowserSettingEnabled },
    { kPolicyCloudPrintProxyEnabled, Value::TYPE_BOOLEAN,
      key::kCloudPrintProxyEnabled },
    { kPolicyDownloadDirectory, Value::TYPE_STRING,
      key::kDownloadDirectory },
    { kPolicyTranslateEnabled, Value::TYPE_BOOLEAN, key::kTranslateEnabled },
    { kPolicyAllowOutdatedPlugins, Value::TYPE_BOOLEAN,
      key::kAllowOutdatedPlugins },
    { kPolicyAlwaysAuthorizePlugins, Value::TYPE_BOOLEAN,
      key::kAlwaysAuthorizePlugins },
    { kPolicyBookmarkBarEnabled, Value::TYPE_BOOLEAN,
      key::kBookmarkBarEnabled },
    { kPolicyEditBookmarksEnabled, Value::TYPE_BOOLEAN,
      key::kEditBookmarksEnabled },
    { kPolicyAllowFileSelectionDialogs, Value::TYPE_BOOLEAN,
      key::kAllowFileSelectionDialogs },
    { kPolicyDiskCacheDir, Value::TYPE_STRING,
      key::kDiskCacheDir },

#if defined(OS_CHROMEOS)
    { kPolicyChromeOsLockOnIdleSuspend, Value::TYPE_BOOLEAN,
      key::kChromeOsLockOnIdleSuspend },
#endif
  };

  static ConfigurationPolicyProvider::PolicyDefinitionList policy_list = {
    entries,
    entries + arraysize(entries),
  };
  return &policy_list;
}

void ConfigurationPolicyPrefStore::Refresh() {
  if (!provider_)
    return;

  // Construct a new keeper, determine what changed and swap the keeper in.
  scoped_ptr<ConfigurationPolicyPrefKeeper> new_keeper(
      new ConfigurationPolicyPrefKeeper(provider_));
  std::vector<std::string> changed_prefs;
  new_keeper->GetDifferingPrefPaths(policy_keeper_.get(), &changed_prefs);
  policy_keeper_.reset(new_keeper.release());

  // Send out change notifications.
  for (std::vector<std::string>::const_iterator pref(changed_prefs.begin());
       pref != changed_prefs.end();
       ++pref) {
    FOR_EACH_OBSERVER(PrefStore::Observer, observers_,
                      OnPrefValueChanged(*pref));
  }

  // Update the initialization flag.
  if (!initialization_complete_ &&
      provider_->IsInitializationComplete()) {
    initialization_complete_ = true;
    FOR_EACH_OBSERVER(PrefStore::Observer, observers_,
                      OnInitializationCompleted(true));
  }
}

}  // namespace policy
