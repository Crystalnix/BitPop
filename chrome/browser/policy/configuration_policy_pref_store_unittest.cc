// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/file_path.h"
#include "base/memory/ref_counted.h"
#include "chrome/browser/policy/configuration_policy_handler.h"
#include "chrome/browser/policy/configuration_policy_pref_store.h"
#include "chrome/browser/policy/mock_configuration_policy_provider.h"
#include "chrome/browser/prefs/incognito_mode_prefs.h"
#include "chrome/browser/prefs/proxy_config_dictionary.h"
#include "chrome/common/content_settings.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/pref_store_observer_mock.h"
#include "policy/policy_constants.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::_;
using testing::Mock;

namespace policy {

// Holds a set of test parameters, consisting of pref name and policy name.
class PolicyAndPref {
 public:
  PolicyAndPref(const char* policy_name, const char* pref_name)
      : policy_name_(policy_name),
        pref_name_(pref_name) {}

  const char* policy_name() const { return policy_name_; }
  const char* pref_name() const { return pref_name_; }

 private:
  const char* policy_name_;
  const char* pref_name_;
};

template<typename TESTBASE>
class ConfigurationPolicyPrefStoreTestBase : public TESTBASE {
 protected:
  ConfigurationPolicyPrefStoreTestBase()
      : provider_(),
        store_(new ConfigurationPolicyPrefStore(&provider_)) {}

  MockConfigurationPolicyProvider provider_;
  scoped_refptr<ConfigurationPolicyPrefStore> store_;
};

// Test cases for list-valued policy settings.
class ConfigurationPolicyPrefStoreListTest
    : public ConfigurationPolicyPrefStoreTestBase<
                 testing::TestWithParam<PolicyAndPref> > {
};

TEST_P(ConfigurationPolicyPrefStoreListTest, GetDefault) {
  EXPECT_EQ(PrefStore::READ_NO_VALUE,
            store_->GetValue(GetParam().pref_name(), NULL));
}

TEST_P(ConfigurationPolicyPrefStoreListTest, SetValue) {
  ListValue* in_value = new ListValue();
  in_value->Append(Value::CreateStringValue("test1"));
  in_value->Append(Value::CreateStringValue("test2,"));
  provider_.AddMandatoryPolicy(GetParam().policy_name(), in_value);
  store_->OnUpdatePolicy(&provider_);
  const Value* value = NULL;
  EXPECT_EQ(PrefStore::READ_OK,
            store_->GetValue(GetParam().pref_name(), &value));
  ASSERT_TRUE(value);
  EXPECT_TRUE(in_value->Equals(value));
}

INSTANTIATE_TEST_CASE_P(
    ConfigurationPolicyPrefStoreListTestInstance,
    ConfigurationPolicyPrefStoreListTest,
    testing::Values(
        PolicyAndPref(key::kRestoreOnStartupURLs,
                      prefs::kURLsToRestoreOnStartup),
        PolicyAndPref(key::kExtensionInstallWhitelist,
                      prefs::kExtensionInstallAllowList),
        PolicyAndPref(key::kExtensionInstallBlacklist,
                      prefs::kExtensionInstallDenyList),
        PolicyAndPref(key::kDisabledPlugins,
                      prefs::kPluginsDisabledPlugins),
        PolicyAndPref(key::kDisabledPluginsExceptions,
                      prefs::kPluginsDisabledPluginsExceptions),
        PolicyAndPref(key::kEnabledPlugins,
                      prefs::kPluginsEnabledPlugins),
        PolicyAndPref(key::kDisabledSchemes,
                      prefs::kDisabledSchemes),
        PolicyAndPref(key::kAutoSelectCertificateForUrls,
                      prefs::kManagedAutoSelectCertificateForUrls),
        PolicyAndPref(key::kURLBlacklist,
                      prefs::kUrlBlacklist),
        PolicyAndPref(key::kURLWhitelist,
                      prefs::kUrlWhitelist)));

// Test cases for string-valued policy settings.
class ConfigurationPolicyPrefStoreStringTest
    : public ConfigurationPolicyPrefStoreTestBase<
                 testing::TestWithParam<PolicyAndPref> > {
};

TEST_P(ConfigurationPolicyPrefStoreStringTest, GetDefault) {
  EXPECT_EQ(PrefStore::READ_NO_VALUE,
            store_->GetValue(GetParam().pref_name(), NULL));
}

TEST_P(ConfigurationPolicyPrefStoreStringTest, SetValue) {
  provider_.AddMandatoryPolicy(GetParam().policy_name(),
                               Value::CreateStringValue("http://chromium.org"));
  store_->OnUpdatePolicy(&provider_);
  const Value* value = NULL;
  EXPECT_EQ(PrefStore::READ_OK,
            store_->GetValue(GetParam().pref_name(), &value));
  ASSERT_TRUE(value);
  EXPECT_TRUE(StringValue("http://chromium.org").Equals(value));
}

INSTANTIATE_TEST_CASE_P(
    ConfigurationPolicyPrefStoreStringTestInstance,
    ConfigurationPolicyPrefStoreStringTest,
    testing::Values(
        PolicyAndPref(key::kHomepageLocation,
                      prefs::kHomePage),
        PolicyAndPref(key::kApplicationLocaleValue,
                      prefs::kApplicationLocale),
        PolicyAndPref(key::kAuthSchemes,
                      prefs::kAuthSchemes),
        PolicyAndPref(key::kAuthServerWhitelist,
                      prefs::kAuthServerWhitelist),
        PolicyAndPref(key::kAuthNegotiateDelegateWhitelist,
                      prefs::kAuthNegotiateDelegateWhitelist),
        PolicyAndPref(key::kGSSAPILibraryName,
                      prefs::kGSSAPILibraryName),
        PolicyAndPref(key::kDiskCacheDir,
                      prefs::kDiskCacheDir)));

#if !defined(OS_CHROMEOS)
INSTANTIATE_TEST_CASE_P(
    ConfigurationPolicyPrefStoreDownloadDirectoryInstance,
    ConfigurationPolicyPrefStoreStringTest,
    testing::Values(PolicyAndPref(key::kDownloadDirectory,
                                  prefs::kDownloadDefaultDirectory)));
#endif  // !defined(OS_CHROMEOS)

// Test cases for boolean-valued policy settings.
class ConfigurationPolicyPrefStoreBooleanTest
    : public ConfigurationPolicyPrefStoreTestBase<
                 testing::TestWithParam<PolicyAndPref> > {
};

TEST_P(ConfigurationPolicyPrefStoreBooleanTest, GetDefault) {
  EXPECT_EQ(PrefStore::READ_NO_VALUE,
            store_->GetValue(GetParam().pref_name(), NULL));
}

TEST_P(ConfigurationPolicyPrefStoreBooleanTest, SetValue) {
  provider_.AddMandatoryPolicy(GetParam().policy_name(),
                               Value::CreateBooleanValue(false));
  store_->OnUpdatePolicy(&provider_);
  const Value* value = NULL;
  EXPECT_EQ(PrefStore::READ_OK,
            store_->GetValue(GetParam().pref_name(), &value));
  ASSERT_TRUE(value);
  bool boolean_value = true;
  bool result = value->GetAsBoolean(&boolean_value);
  ASSERT_TRUE(result);
  EXPECT_FALSE(boolean_value);

  provider_.AddMandatoryPolicy(GetParam().policy_name(),
                               Value::CreateBooleanValue(true));
  store_->OnUpdatePolicy(&provider_);
  value = NULL;
  EXPECT_EQ(PrefStore::READ_OK,
            store_->GetValue(GetParam().pref_name(), &value));
  boolean_value = false;
  result = value->GetAsBoolean(&boolean_value);
  ASSERT_TRUE(result);
  EXPECT_TRUE(boolean_value);
}

INSTANTIATE_TEST_CASE_P(
    ConfigurationPolicyPrefStoreBooleanTestInstance,
    ConfigurationPolicyPrefStoreBooleanTest,
    testing::Values(
        PolicyAndPref(key::kHomepageIsNewTabPage,
                      prefs::kHomePageIsNewTabPage),
        PolicyAndPref(key::kAlternateErrorPagesEnabled,
                      prefs::kAlternateErrorPagesEnabled),
        PolicyAndPref(key::kSearchSuggestEnabled,
                      prefs::kSearchSuggestEnabled),
        PolicyAndPref(key::kDnsPrefetchingEnabled,
                      prefs::kNetworkPredictionEnabled),
        PolicyAndPref(key::kDisableSpdy,
                      prefs::kDisableSpdy),
        PolicyAndPref(key::kSafeBrowsingEnabled,
                      prefs::kSafeBrowsingEnabled),
        PolicyAndPref(key::kMetricsReportingEnabled,
                      prefs::kMetricsReportingEnabled),
        PolicyAndPref(key::kPasswordManagerEnabled,
                      prefs::kPasswordManagerEnabled),
        PolicyAndPref(key::kPasswordManagerAllowShowPasswords,
                      prefs::kPasswordManagerAllowShowPasswords),
        PolicyAndPref(key::kShowHomeButton,
                      prefs::kShowHomeButton),
        PolicyAndPref(key::kPrintingEnabled,
                      prefs::kPrintingEnabled),
        PolicyAndPref(key::kRemoteAccessHostFirewallTraversal,
                      prefs::kRemoteAccessHostFirewallTraversal),
        PolicyAndPref(key::kCloudPrintProxyEnabled,
                      prefs::kCloudPrintProxyEnabled),
        PolicyAndPref(key::kCloudPrintSubmitEnabled,
                      prefs::kCloudPrintSubmitEnabled),
        PolicyAndPref(key::kSavingBrowserHistoryDisabled,
                      prefs::kSavingBrowserHistoryDisabled),
        PolicyAndPref(key::kEnableOriginBoundCerts,
                      prefs::kEnableOriginBoundCerts),
        PolicyAndPref(key::kDisableSSLRecordSplitting,
                      prefs::kDisableSSLRecordSplitting),
        PolicyAndPref(key::kDisableAuthNegotiateCnameLookup,
                      prefs::kDisableAuthNegotiateCnameLookup),
        PolicyAndPref(key::kEnableAuthNegotiatePort,
                      prefs::kEnableAuthNegotiatePort),
        PolicyAndPref(key::kInstantEnabled,
                      prefs::kInstantEnabled),
        PolicyAndPref(key::kDisablePluginFinder,
                      prefs::kDisablePluginFinder),
        PolicyAndPref(key::kClearSiteDataOnExit,
                      prefs::kClearSiteDataOnExit),
        PolicyAndPref(key::kDefaultBrowserSettingEnabled,
                      prefs::kDefaultBrowserSettingEnabled),
        PolicyAndPref(key::kDisable3DAPIs,
                      prefs::kDisable3DAPIs),
        PolicyAndPref(key::kTranslateEnabled,
                      prefs::kEnableTranslate),
        PolicyAndPref(key::kAllowOutdatedPlugins,
                      prefs::kPluginsAllowOutdated),
        PolicyAndPref(key::kAlwaysAuthorizePlugins,
                      prefs::kPluginsAlwaysAuthorize),
        PolicyAndPref(key::kBookmarkBarEnabled,
                      prefs::kShowBookmarkBar),
        PolicyAndPref(key::kEditBookmarksEnabled,
                      prefs::kEditBookmarksEnabled),
        PolicyAndPref(key::kAllowFileSelectionDialogs,
                      prefs::kAllowFileSelectionDialogs),
        PolicyAndPref(key::kAllowCrossOriginAuthPrompt,
                      prefs::kAllowCrossOriginAuthPrompt),
        PolicyAndPref(key::kImportBookmarks,
                      prefs::kImportBookmarks),
        PolicyAndPref(key::kImportHistory,
                      prefs::kImportHistory),
        PolicyAndPref(key::kImportHomepage,
                      prefs::kImportHomepage),
        PolicyAndPref(key::kImportSearchEngine,
                      prefs::kImportSearchEngine),
        PolicyAndPref(key::kImportSavedPasswords,
                      prefs::kImportSavedPasswords),
        PolicyAndPref(key::kEnableMemoryInfo,
                      prefs::kEnableMemoryInfo),
        PolicyAndPref(key::kDisablePrintPreview,
                      prefs::kPrintPreviewDisabled),
        PolicyAndPref(key::kDeveloperToolsDisabled,
                      prefs::kDevToolsDisabled)));

#if defined(OS_CHROMEOS)
INSTANTIATE_TEST_CASE_P(
    CrosConfigurationPolicyPrefStoreBooleanTestInstance,
    ConfigurationPolicyPrefStoreBooleanTest,
    testing::Values(
        PolicyAndPref(key::kChromeOsLockOnIdleSuspend,
                      prefs::kEnableScreenLock)));
#endif  // defined(OS_CHROMEOS)

// Test cases for integer-valued policy settings.
class ConfigurationPolicyPrefStoreIntegerTest
    : public ConfigurationPolicyPrefStoreTestBase<
                 testing::TestWithParam<PolicyAndPref> > {
};

TEST_P(ConfigurationPolicyPrefStoreIntegerTest, GetDefault) {
  EXPECT_EQ(PrefStore::READ_NO_VALUE,
            store_->GetValue(GetParam().pref_name(), NULL));
}

TEST_P(ConfigurationPolicyPrefStoreIntegerTest, SetValue) {
  provider_.AddMandatoryPolicy(GetParam().policy_name(),
                               Value::CreateIntegerValue(2));
  store_->OnUpdatePolicy(&provider_);
  const Value* value = NULL;
  EXPECT_EQ(PrefStore::READ_OK,
            store_->GetValue(GetParam().pref_name(), &value));
  EXPECT_TRUE(base::FundamentalValue(2).Equals(value));
}

INSTANTIATE_TEST_CASE_P(
    ConfigurationPolicyPrefStoreIntegerTestInstance,
    ConfigurationPolicyPrefStoreIntegerTest,
    testing::Values(
        PolicyAndPref(key::kDefaultCookiesSetting,
                      prefs::kManagedDefaultCookiesSetting),
        PolicyAndPref(key::kDefaultImagesSetting,
                      prefs::kManagedDefaultImagesSetting),
        PolicyAndPref(key::kDefaultPluginsSetting,
                      prefs::kManagedDefaultPluginsSetting),
        PolicyAndPref(key::kDefaultPopupsSetting,
                      prefs::kManagedDefaultPopupsSetting),
        PolicyAndPref(key::kDefaultNotificationsSetting,
                      prefs::kManagedDefaultNotificationsSetting),
        PolicyAndPref(key::kDefaultGeolocationSetting,
                      prefs::kManagedDefaultGeolocationSetting),
        PolicyAndPref(key::kRestoreOnStartup,
                      prefs::kRestoreOnStartup),
        PolicyAndPref(key::kDiskCacheSize,
                      prefs::kDiskCacheSize),
        PolicyAndPref(key::kMediaCacheSize,
                      prefs::kMediaCacheSize),
        PolicyAndPref(key::kPolicyRefreshRate,
                      prefs::kUserPolicyRefreshRate),
        PolicyAndPref(key::kMaxConnectionsPerProxy,
                      prefs::kMaxConnectionsPerProxy)));

// Test cases for the proxy policy settings.
class ConfigurationPolicyPrefStoreProxyTest : public testing::Test {
 protected:
  // Verify that all the proxy prefs are set to the specified expected values.
  static void VerifyProxyPrefs(
      const ConfigurationPolicyPrefStore& store,
      const std::string& expected_proxy_server,
      const std::string& expected_proxy_pac_url,
      const std::string& expected_proxy_bypass_list,
      const ProxyPrefs::ProxyMode& expected_proxy_mode) {
    const Value* value = NULL;
    ASSERT_EQ(PrefStore::READ_OK,
              store.GetValue(prefs::kProxy, &value));
    ASSERT_EQ(Value::TYPE_DICTIONARY, value->GetType());
    ProxyConfigDictionary dict(static_cast<const DictionaryValue*>(value));
    std::string s;
    if (expected_proxy_server.empty()) {
      EXPECT_FALSE(dict.GetProxyServer(&s));
    } else {
      ASSERT_TRUE(dict.GetProxyServer(&s));
      EXPECT_EQ(expected_proxy_server, s);
    }
    if (expected_proxy_pac_url.empty()) {
      EXPECT_FALSE(dict.GetPacUrl(&s));
    } else {
      ASSERT_TRUE(dict.GetPacUrl(&s));
      EXPECT_EQ(expected_proxy_pac_url, s);
    }
    if (expected_proxy_bypass_list.empty()) {
      EXPECT_FALSE(dict.GetBypassList(&s));
    } else {
      ASSERT_TRUE(dict.GetBypassList(&s));
      EXPECT_EQ(expected_proxy_bypass_list, s);
    }
    ProxyPrefs::ProxyMode mode;
    ASSERT_TRUE(dict.GetMode(&mode));
    EXPECT_EQ(expected_proxy_mode, mode);
  }
};

TEST_F(ConfigurationPolicyPrefStoreProxyTest, ManualOptions) {
  MockConfigurationPolicyProvider provider;
  provider.AddMandatoryPolicy(
      key::kProxyBypassList,
      Value::CreateStringValue("http://chromium.org/override"));
  provider.AddMandatoryPolicy(key::kProxyServer,
                              Value::CreateStringValue("chromium.org"));
  provider.AddMandatoryPolicy(
      key::kProxyServerMode,
      Value::CreateIntegerValue(
          ProxyPolicyHandler::PROXY_MANUALLY_CONFIGURED_PROXY_SERVER_MODE));

  scoped_refptr<ConfigurationPolicyPrefStore> store(
      new ConfigurationPolicyPrefStore(&provider));
  VerifyProxyPrefs(
      *store, "chromium.org", "", "http://chromium.org/override",
      ProxyPrefs::MODE_FIXED_SERVERS);
}

TEST_F(ConfigurationPolicyPrefStoreProxyTest, ManualOptionsReversedApplyOrder) {
  MockConfigurationPolicyProvider provider;
  provider.AddMandatoryPolicy(
      key::kProxyServerMode,
      Value::CreateIntegerValue(
          ProxyPolicyHandler::PROXY_MANUALLY_CONFIGURED_PROXY_SERVER_MODE));
  provider.AddMandatoryPolicy(
      key::kProxyBypassList,
      Value::CreateStringValue("http://chromium.org/override"));
  provider.AddMandatoryPolicy(
      key::kProxyServer,
      Value::CreateStringValue("chromium.org"));
  scoped_refptr<ConfigurationPolicyPrefStore> store(
      new ConfigurationPolicyPrefStore(&provider));
  VerifyProxyPrefs(
      *store, "chromium.org", "", "http://chromium.org/override",
      ProxyPrefs::MODE_FIXED_SERVERS);
}

TEST_F(ConfigurationPolicyPrefStoreProxyTest, ManualOptionsInvalid) {
  MockConfigurationPolicyProvider provider;
  provider.AddMandatoryPolicy(
      key::kProxyServerMode,
      Value::CreateIntegerValue(
          ProxyPolicyHandler::PROXY_MANUALLY_CONFIGURED_PROXY_SERVER_MODE));

  scoped_refptr<ConfigurationPolicyPrefStore> store(
      new ConfigurationPolicyPrefStore(&provider));
  const Value* value = NULL;
  EXPECT_EQ(PrefStore::READ_NO_VALUE, store->GetValue(prefs::kProxy, &value));
}


TEST_F(ConfigurationPolicyPrefStoreProxyTest, NoProxyServerMode) {
  MockConfigurationPolicyProvider provider;
  provider.AddMandatoryPolicy(
      key::kProxyServerMode,
      Value::CreateIntegerValue(ProxyPolicyHandler::PROXY_SERVER_MODE));

  scoped_refptr<ConfigurationPolicyPrefStore> store(
      new ConfigurationPolicyPrefStore(&provider));
  VerifyProxyPrefs(*store, "", "", "", ProxyPrefs::MODE_DIRECT);
}

TEST_F(ConfigurationPolicyPrefStoreProxyTest, NoProxyModeName) {
  MockConfigurationPolicyProvider provider;
  provider.AddMandatoryPolicy(
      key::kProxyMode,
      Value::CreateStringValue(ProxyPrefs::kDirectProxyModeName));

  scoped_refptr<ConfigurationPolicyPrefStore> store(
      new ConfigurationPolicyPrefStore(&provider));
  VerifyProxyPrefs(*store, "", "", "", ProxyPrefs::MODE_DIRECT);
}

TEST_F(ConfigurationPolicyPrefStoreProxyTest, AutoDetectProxyServerMode) {
  MockConfigurationPolicyProvider provider;
  provider.AddMandatoryPolicy(
      key::kProxyServerMode,
      Value::CreateIntegerValue(
          ProxyPolicyHandler::PROXY_AUTO_DETECT_PROXY_SERVER_MODE));

  scoped_refptr<ConfigurationPolicyPrefStore> store(
      new ConfigurationPolicyPrefStore(&provider));
  VerifyProxyPrefs(*store, "", "", "", ProxyPrefs::MODE_AUTO_DETECT);
}

TEST_F(ConfigurationPolicyPrefStoreProxyTest, AutoDetectProxyModeName) {
  MockConfigurationPolicyProvider provider;
  provider.AddMandatoryPolicy(
      key::kProxyMode,
      Value::CreateStringValue(ProxyPrefs::kAutoDetectProxyModeName));

  scoped_refptr<ConfigurationPolicyPrefStore> store(
      new ConfigurationPolicyPrefStore(&provider));
  VerifyProxyPrefs(*store, "", "", "", ProxyPrefs::MODE_AUTO_DETECT);
}

TEST_F(ConfigurationPolicyPrefStoreProxyTest, PacScriptProxyMode) {
  MockConfigurationPolicyProvider provider;
  provider.AddMandatoryPolicy(
      key::kProxyPacUrl,
      Value::CreateStringValue("http://short.org/proxy.pac"));
  provider.AddMandatoryPolicy(
      key::kProxyMode,
      Value::CreateStringValue(ProxyPrefs::kPacScriptProxyModeName));

  scoped_refptr<ConfigurationPolicyPrefStore> store(
      new ConfigurationPolicyPrefStore(&provider));
  VerifyProxyPrefs(*store, "", "http://short.org/proxy.pac", "",
                   ProxyPrefs::MODE_PAC_SCRIPT);
}

TEST_F(ConfigurationPolicyPrefStoreProxyTest, PacScriptProxyModeInvalid) {
  MockConfigurationPolicyProvider provider;
  provider.AddMandatoryPolicy(
      key::kProxyMode,
      Value::CreateStringValue(ProxyPrefs::kPacScriptProxyModeName));

  scoped_refptr<ConfigurationPolicyPrefStore> store(
      new ConfigurationPolicyPrefStore(&provider));
  const Value* value = NULL;
  EXPECT_EQ(PrefStore::READ_NO_VALUE, store->GetValue(prefs::kProxy, &value));
}

// Regression test for http://crbug.com/78016, CPanel returns empty strings
// for unset properties.
TEST_F(ConfigurationPolicyPrefStoreProxyTest, PacScriptProxyModeBug78016) {
  MockConfigurationPolicyProvider provider;
  provider.AddMandatoryPolicy(key::kProxyServer,
                              Value::CreateStringValue(""));
  provider.AddMandatoryPolicy(
      key::kProxyPacUrl,
      Value::CreateStringValue("http://short.org/proxy.pac"));
  provider.AddMandatoryPolicy(
      key::kProxyMode,
      Value::CreateStringValue(ProxyPrefs::kPacScriptProxyModeName));

  scoped_refptr<ConfigurationPolicyPrefStore> store(
      new ConfigurationPolicyPrefStore(&provider));
  VerifyProxyPrefs(*store, "", "http://short.org/proxy.pac", "",
                   ProxyPrefs::MODE_PAC_SCRIPT);
}

TEST_F(ConfigurationPolicyPrefStoreProxyTest, UseSystemProxyServerMode) {
  MockConfigurationPolicyProvider provider;
  provider.AddMandatoryPolicy(
      key::kProxyServerMode,
      Value::CreateIntegerValue(
          ProxyPolicyHandler::PROXY_USE_SYSTEM_PROXY_SERVER_MODE));

  scoped_refptr<ConfigurationPolicyPrefStore> store(
      new ConfigurationPolicyPrefStore(&provider));
  VerifyProxyPrefs(*store, "", "", "", ProxyPrefs::MODE_SYSTEM);
}

TEST_F(ConfigurationPolicyPrefStoreProxyTest, UseSystemProxyMode) {
  MockConfigurationPolicyProvider provider;
  provider.AddMandatoryPolicy(
      key::kProxyMode,
      Value::CreateStringValue(ProxyPrefs::kSystemProxyModeName));

  scoped_refptr<ConfigurationPolicyPrefStore> store(
      new ConfigurationPolicyPrefStore(&provider));
  VerifyProxyPrefs(*store, "", "", "", ProxyPrefs::MODE_SYSTEM);
}

TEST_F(ConfigurationPolicyPrefStoreProxyTest,
       ProxyModeOverridesProxyServerMode) {
  MockConfigurationPolicyProvider provider;
  provider.AddMandatoryPolicy(
      key::kProxyServerMode,
      Value::CreateIntegerValue(ProxyPolicyHandler::PROXY_SERVER_MODE));
  provider.AddMandatoryPolicy(
      key::kProxyMode,
      Value::CreateStringValue(ProxyPrefs::kAutoDetectProxyModeName));

  scoped_refptr<ConfigurationPolicyPrefStore> store(
      new ConfigurationPolicyPrefStore(&provider));
  VerifyProxyPrefs(*store, "", "", "", ProxyPrefs::MODE_AUTO_DETECT);
}

TEST_F(ConfigurationPolicyPrefStoreProxyTest, ProxyInvalid) {
  for (int i = 0; i < ProxyPolicyHandler::MODE_COUNT; ++i) {
    MockConfigurationPolicyProvider provider;
    provider.AddMandatoryPolicy(key::kProxyServerMode,
                                Value::CreateIntegerValue(i));
    // No mode expects all three parameters being set.
    provider.AddMandatoryPolicy(
        key::kProxyPacUrl,
        Value::CreateStringValue("http://short.org/proxy.pac"));
    provider.AddMandatoryPolicy(
        key::kProxyBypassList,
        Value::CreateStringValue("http://chromium.org/override"));
    provider.AddMandatoryPolicy(key::kProxyServer,
                                Value::CreateStringValue("chromium.org"));

    scoped_refptr<ConfigurationPolicyPrefStore> store(
        new ConfigurationPolicyPrefStore(&provider));
    const Value* value = NULL;
    EXPECT_EQ(PrefStore::READ_NO_VALUE,
              store->GetValue(prefs::kProxy, &value));
  }
}

class ConfigurationPolicyPrefStoreDefaultSearchTest : public testing::Test {
};

// Checks that if the policy for default search is valid, i.e. there's a
// search URL, that all the elements have been given proper defaults.
TEST_F(ConfigurationPolicyPrefStoreDefaultSearchTest, MinimallyDefined) {
  const char* const search_url = "http://test.com/search?t={searchTerms}";
  MockConfigurationPolicyProvider provider;
  provider.AddMandatoryPolicy(key::kDefaultSearchProviderEnabled,
                              Value::CreateBooleanValue(true));
  provider.AddMandatoryPolicy(key::kDefaultSearchProviderSearchURL,
                              Value::CreateStringValue(search_url));

  scoped_refptr<ConfigurationPolicyPrefStore> store(
      new ConfigurationPolicyPrefStore(&provider));

  const Value* value = NULL;
  EXPECT_EQ(PrefStore::READ_OK,
            store->GetValue(prefs::kDefaultSearchProviderSearchURL, &value));
  EXPECT_TRUE(StringValue(search_url).Equals(value));

  EXPECT_EQ(PrefStore::READ_OK,
            store->GetValue(prefs::kDefaultSearchProviderName, &value));
  EXPECT_TRUE(StringValue("test.com").Equals(value));

  EXPECT_EQ(PrefStore::READ_OK,
            store->GetValue(prefs::kDefaultSearchProviderKeyword, &value));
  EXPECT_TRUE(StringValue(std::string()).Equals(value));

  EXPECT_EQ(PrefStore::READ_OK,
            store->GetValue(prefs::kDefaultSearchProviderSuggestURL, &value));
  EXPECT_TRUE(StringValue(std::string()).Equals(value));

  EXPECT_EQ(PrefStore::READ_OK,
            store->GetValue(prefs::kDefaultSearchProviderIconURL, &value));
  EXPECT_TRUE(StringValue(std::string()).Equals(value));

  EXPECT_EQ(PrefStore::READ_OK,
            store->GetValue(prefs::kDefaultSearchProviderEncodings, &value));
  EXPECT_TRUE(StringValue(std::string()).Equals(value));

  EXPECT_EQ(PrefStore::READ_OK,
            store->GetValue(prefs::kDefaultSearchProviderInstantURL, &value));
  EXPECT_TRUE(StringValue(std::string()).Equals(value));
}

// Checks that for a fully defined search policy, all elements have been
// read properly.
TEST_F(ConfigurationPolicyPrefStoreDefaultSearchTest, FullyDefined) {
  const char* const search_url = "http://test.com/search?t={searchTerms}";
  const char* const suggest_url = "http://test.com/sugg?={searchTerms}";
  const char* const icon_url = "http://test.com/icon.jpg";
  const char* const name = "MyName";
  const char* const keyword = "MyKeyword";
  ListValue* encodings = new ListValue();
  encodings->Append(Value::CreateStringValue("UTF-16"));
  encodings->Append(Value::CreateStringValue("UTF-8"));
  MockConfigurationPolicyProvider provider;
  provider.AddMandatoryPolicy(key::kDefaultSearchProviderEnabled,
                              Value::CreateBooleanValue(true));
  provider.AddMandatoryPolicy(key::kDefaultSearchProviderSearchURL,
                              Value::CreateStringValue(search_url));
  provider.AddMandatoryPolicy(key::kDefaultSearchProviderName,
                              Value::CreateStringValue(name));
  provider.AddMandatoryPolicy(key::kDefaultSearchProviderKeyword,
                              Value::CreateStringValue(keyword));
  provider.AddMandatoryPolicy(key::kDefaultSearchProviderSuggestURL,
                              Value::CreateStringValue(suggest_url));
  provider.AddMandatoryPolicy(key::kDefaultSearchProviderIconURL,
                              Value::CreateStringValue(icon_url));
  provider.AddMandatoryPolicy(key::kDefaultSearchProviderEncodings, encodings);

  scoped_refptr<ConfigurationPolicyPrefStore> store(
      new ConfigurationPolicyPrefStore(&provider));

  const Value* value = NULL;
  EXPECT_EQ(PrefStore::READ_OK,
            store->GetValue(prefs::kDefaultSearchProviderSearchURL, &value));
  EXPECT_TRUE(StringValue(search_url).Equals(value));

  EXPECT_EQ(PrefStore::READ_OK,
            store->GetValue(prefs::kDefaultSearchProviderName, &value));
  EXPECT_TRUE(StringValue(name).Equals(value));

  EXPECT_EQ(PrefStore::READ_OK,
            store->GetValue(prefs::kDefaultSearchProviderKeyword, &value));
  EXPECT_TRUE(StringValue(keyword).Equals(value));

  EXPECT_EQ(PrefStore::READ_OK,
            store->GetValue(prefs::kDefaultSearchProviderSuggestURL, &value));
  EXPECT_TRUE(StringValue(suggest_url).Equals(value));

  EXPECT_EQ(PrefStore::READ_OK,
            store->GetValue(prefs::kDefaultSearchProviderIconURL, &value));
  EXPECT_TRUE(StringValue(icon_url).Equals(value));

  EXPECT_EQ(PrefStore::READ_OK,
            store->GetValue(prefs::kDefaultSearchProviderEncodings, &value));
  EXPECT_TRUE(StringValue("UTF-16;UTF-8").Equals(value));
}

// Checks that if the default search policy is missing, that no elements of the
// default search policy will be present.
TEST_F(ConfigurationPolicyPrefStoreDefaultSearchTest, MissingUrl) {
  const char* const suggest_url = "http://test.com/sugg?t={searchTerms}";
  const char* const icon_url = "http://test.com/icon.jpg";
  const char* const name = "MyName";
  const char* const keyword = "MyKeyword";
  ListValue* encodings = new ListValue();
  encodings->Append(Value::CreateStringValue("UTF-16"));
  encodings->Append(Value::CreateStringValue("UTF-8"));
  MockConfigurationPolicyProvider provider;
  provider.AddMandatoryPolicy(key::kDefaultSearchProviderEnabled,
                              Value::CreateBooleanValue(true));
  provider.AddMandatoryPolicy(key::kDefaultSearchProviderName,
                              Value::CreateStringValue(name));
  provider.AddMandatoryPolicy(key::kDefaultSearchProviderKeyword,
                              Value::CreateStringValue(keyword));
  provider.AddMandatoryPolicy(key::kDefaultSearchProviderSuggestURL,
                              Value::CreateStringValue(suggest_url));
  provider.AddMandatoryPolicy(key::kDefaultSearchProviderIconURL,
                              Value::CreateStringValue(icon_url));
  provider.AddMandatoryPolicy(key::kDefaultSearchProviderEncodings, encodings);

  scoped_refptr<ConfigurationPolicyPrefStore> store(
      new ConfigurationPolicyPrefStore(&provider));

  EXPECT_EQ(PrefStore::READ_NO_VALUE,
            store->GetValue(prefs::kDefaultSearchProviderSearchURL, NULL));
  EXPECT_EQ(PrefStore::READ_NO_VALUE,
            store->GetValue(prefs::kDefaultSearchProviderName, NULL));
  EXPECT_EQ(PrefStore::READ_NO_VALUE,
            store->GetValue(prefs::kDefaultSearchProviderKeyword, NULL));
  EXPECT_EQ(PrefStore::READ_NO_VALUE,
            store->GetValue(prefs::kDefaultSearchProviderSuggestURL, NULL));
  EXPECT_EQ(PrefStore::READ_NO_VALUE,
            store->GetValue(prefs::kDefaultSearchProviderIconURL, NULL));
  EXPECT_EQ(PrefStore::READ_NO_VALUE,
            store->GetValue(prefs::kDefaultSearchProviderEncodings, NULL));
}

// Checks that if the default search policy is invalid, that no elements of the
// default search policy will be present.
TEST_F(ConfigurationPolicyPrefStoreDefaultSearchTest, Invalid) {
  const char* const bad_search_url = "http://test.com/noSearchTerms";
  const char* const suggest_url = "http://test.com/sugg?t={searchTerms}";
  const char* const icon_url = "http://test.com/icon.jpg";
  const char* const name = "MyName";
  const char* const keyword = "MyKeyword";
  ListValue* encodings = new ListValue();
  encodings->Append(Value::CreateStringValue("UTF-16"));
  encodings->Append(Value::CreateStringValue("UTF-8"));
  MockConfigurationPolicyProvider provider;
  provider.AddMandatoryPolicy(key::kDefaultSearchProviderEnabled,
                              Value::CreateBooleanValue(true));
  provider.AddMandatoryPolicy(key::kDefaultSearchProviderSearchURL,
                              Value::CreateStringValue(bad_search_url));
  provider.AddMandatoryPolicy(key::kDefaultSearchProviderName,
                              Value::CreateStringValue(name));
  provider.AddMandatoryPolicy(key::kDefaultSearchProviderKeyword,
                              Value::CreateStringValue(keyword));
  provider.AddMandatoryPolicy(key::kDefaultSearchProviderSuggestURL,
                              Value::CreateStringValue(suggest_url));
  provider.AddMandatoryPolicy(key::kDefaultSearchProviderIconURL,
                              Value::CreateStringValue(icon_url));
  provider.AddMandatoryPolicy(key::kDefaultSearchProviderEncodings, encodings);

  scoped_refptr<ConfigurationPolicyPrefStore> store(
      new ConfigurationPolicyPrefStore(&provider));

  EXPECT_EQ(PrefStore::READ_NO_VALUE,
            store->GetValue(prefs::kDefaultSearchProviderSearchURL, NULL));
  EXPECT_EQ(PrefStore::READ_NO_VALUE,
            store->GetValue(prefs::kDefaultSearchProviderName, NULL));
  EXPECT_EQ(PrefStore::READ_NO_VALUE,
            store->GetValue(prefs::kDefaultSearchProviderKeyword, NULL));
  EXPECT_EQ(PrefStore::READ_NO_VALUE,
            store->GetValue(prefs::kDefaultSearchProviderSuggestURL, NULL));
  EXPECT_EQ(PrefStore::READ_NO_VALUE,
            store->GetValue(prefs::kDefaultSearchProviderIconURL, NULL));
  EXPECT_EQ(PrefStore::READ_NO_VALUE,
            store->GetValue(prefs::kDefaultSearchProviderEncodings, NULL));
}

// Tests Incognito mode availability preference setting.
class ConfigurationPolicyPrefStoreIncognitoModeTest : public testing::Test {
 protected:
  static const int kIncognitoModeAvailabilityNotSet = -1;

  enum ObsoleteIncognitoEnabledValue {
    INCOGNITO_ENABLED_UNKNOWN,
    INCOGNITO_ENABLED_TRUE,
    INCOGNITO_ENABLED_FALSE
  };

  void SetPolicies(ObsoleteIncognitoEnabledValue incognito_enabled,
                   int availability) {
    if (incognito_enabled != INCOGNITO_ENABLED_UNKNOWN) {
      provider_.AddMandatoryPolicy(
          key::kIncognitoEnabled,
          Value::CreateBooleanValue(
              incognito_enabled == INCOGNITO_ENABLED_TRUE));
    }
    if (availability >= 0) {
      provider_.AddMandatoryPolicy(key::kIncognitoModeAvailability,
                                   Value::CreateIntegerValue(availability));
    }
    store_ = new ConfigurationPolicyPrefStore(&provider_);
  }

  void VerifyValues(IncognitoModePrefs::Availability availability) {
    const Value* value = NULL;
    EXPECT_EQ(PrefStore::READ_OK,
              store_->GetValue(prefs::kIncognitoModeAvailability, &value));
    EXPECT_TRUE(base::FundamentalValue(availability).Equals(value));
  }

  MockConfigurationPolicyProvider provider_;
  scoped_refptr<ConfigurationPolicyPrefStore> store_;
};

// The following testcases verify that if the obsolete IncognitoEnabled
// policy is not set, the IncognitoModeAvailability values should be copied
// from IncognitoModeAvailability policy to pref "as is".
TEST_F(ConfigurationPolicyPrefStoreIncognitoModeTest,
       NoObsoletePolicyAndIncognitoEnabled) {
  SetPolicies(INCOGNITO_ENABLED_UNKNOWN, IncognitoModePrefs::ENABLED);
  VerifyValues(IncognitoModePrefs::ENABLED);
}

TEST_F(ConfigurationPolicyPrefStoreIncognitoModeTest,
       NoObsoletePolicyAndIncognitoDisabled) {
  SetPolicies(INCOGNITO_ENABLED_UNKNOWN, IncognitoModePrefs::DISABLED);
  VerifyValues(IncognitoModePrefs::DISABLED);
}

TEST_F(ConfigurationPolicyPrefStoreIncognitoModeTest,
       NoObsoletePolicyAndIncognitoForced) {
  SetPolicies(INCOGNITO_ENABLED_UNKNOWN, IncognitoModePrefs::FORCED);
  VerifyValues(IncognitoModePrefs::FORCED);
}

TEST_F(ConfigurationPolicyPrefStoreIncognitoModeTest,
       NoObsoletePolicyAndNoIncognitoAvailability) {
  SetPolicies(INCOGNITO_ENABLED_UNKNOWN, kIncognitoModeAvailabilityNotSet);
  const Value* value = NULL;
  EXPECT_EQ(PrefStore::READ_NO_VALUE,
            store_->GetValue(prefs::kIncognitoModeAvailability, &value));
}

// Checks that if the obsolete IncognitoEnabled policy is set, if sets
// the IncognitoModeAvailability preference only in case
// the IncognitoModeAvailability policy is not specified.
TEST_F(ConfigurationPolicyPrefStoreIncognitoModeTest,
       ObsoletePolicyDoesNotAffectAvailabilityEnabled) {
  SetPolicies(INCOGNITO_ENABLED_FALSE, IncognitoModePrefs::ENABLED);
  VerifyValues(IncognitoModePrefs::ENABLED);
}

TEST_F(ConfigurationPolicyPrefStoreIncognitoModeTest,
       ObsoletePolicyDoesNotAffectAvailabilityForced) {
  SetPolicies(INCOGNITO_ENABLED_TRUE, IncognitoModePrefs::FORCED);
  VerifyValues(IncognitoModePrefs::FORCED);
}

TEST_F(ConfigurationPolicyPrefStoreIncognitoModeTest,
       ObsoletePolicySetsPreferenceToEnabled) {
  SetPolicies(INCOGNITO_ENABLED_TRUE, kIncognitoModeAvailabilityNotSet);
  VerifyValues(IncognitoModePrefs::ENABLED);
}

TEST_F(ConfigurationPolicyPrefStoreIncognitoModeTest,
       ObsoletePolicySetsPreferenceToDisabled) {
  SetPolicies(INCOGNITO_ENABLED_FALSE, kIncognitoModeAvailabilityNotSet);
  VerifyValues(IncognitoModePrefs::DISABLED);
}

// Test cases for the Sync policy setting.
class ConfigurationPolicyPrefStoreSyncTest
    : public ConfigurationPolicyPrefStoreTestBase<testing::Test> {
};

TEST_F(ConfigurationPolicyPrefStoreSyncTest, Default) {
  EXPECT_EQ(PrefStore::READ_NO_VALUE,
            store_->GetValue(prefs::kSyncManaged, NULL));
}

TEST_F(ConfigurationPolicyPrefStoreSyncTest, Enabled) {
  provider_.AddMandatoryPolicy(key::kSyncDisabled,
                               Value::CreateBooleanValue(false));
  store_->OnUpdatePolicy(&provider_);
  // Enabling Sync should not set the pref.
  EXPECT_EQ(PrefStore::READ_NO_VALUE,
            store_->GetValue(prefs::kSyncManaged, NULL));
}

TEST_F(ConfigurationPolicyPrefStoreSyncTest, Disabled) {
  provider_.AddMandatoryPolicy(key::kSyncDisabled,
                               Value::CreateBooleanValue(true));
  store_->OnUpdatePolicy(&provider_);
  // Sync should be flagged as managed.
  const Value* value = NULL;
  EXPECT_EQ(PrefStore::READ_OK, store_->GetValue(prefs::kSyncManaged, &value));
  ASSERT_TRUE(value);
  bool sync_managed = false;
  bool result = value->GetAsBoolean(&sync_managed);
  ASSERT_TRUE(result);
  EXPECT_TRUE(sync_managed);
}

// Test cases for how the DownloadDirectory and AllowFileSelectionDialogs policy
// influence the PromptForDownload preference.
class ConfigurationPolicyPrefStorePromptDownloadTest
    : public ConfigurationPolicyPrefStoreTestBase<testing::Test> {
};

TEST_F(ConfigurationPolicyPrefStorePromptDownloadTest, Default) {
  EXPECT_EQ(PrefStore::READ_NO_VALUE,
            store_->GetValue(prefs::kPromptForDownload, NULL));
}

#if !defined(OS_CHROMEOS)
TEST_F(ConfigurationPolicyPrefStorePromptDownloadTest, SetDownloadDirectory) {
  EXPECT_EQ(PrefStore::READ_NO_VALUE,
            store_->GetValue(prefs::kPromptForDownload, NULL));
  provider_.AddMandatoryPolicy(key::kDownloadDirectory,
                               Value::CreateStringValue(""));
  store_->OnUpdatePolicy(&provider_);

  // Setting a DownloadDirectory should disable the PromptForDownload pref.
  const Value* value = NULL;
  EXPECT_EQ(PrefStore::READ_OK, store_->GetValue(prefs::kPromptForDownload,
                                                 &value));
  ASSERT_TRUE(value);
  bool prompt_for_download = true;
  bool result = value->GetAsBoolean(&prompt_for_download);
  ASSERT_TRUE(result);
  EXPECT_FALSE(prompt_for_download);
}
#endif  // !defined(OS_CHROMEOS)

TEST_F(ConfigurationPolicyPrefStorePromptDownloadTest,
       EnableFileSelectionDialogs) {
  EXPECT_EQ(PrefStore::READ_NO_VALUE,
            store_->GetValue(prefs::kPromptForDownload, NULL));
  provider_.AddMandatoryPolicy(key::kAllowFileSelectionDialogs,
                               Value::CreateBooleanValue(true));
  store_->OnUpdatePolicy(&provider_);

  // Allowing file-selection dialogs should not influence the PromptForDownload
  // pref.
  EXPECT_EQ(PrefStore::READ_NO_VALUE,
            store_->GetValue(prefs::kPromptForDownload, NULL));
}

TEST_F(ConfigurationPolicyPrefStorePromptDownloadTest,
       DisableFileSelectionDialogs) {
  EXPECT_EQ(PrefStore::READ_NO_VALUE,
            store_->GetValue(prefs::kPromptForDownload, NULL));
  provider_.AddMandatoryPolicy(key::kAllowFileSelectionDialogs,
                               Value::CreateBooleanValue(false));
  store_->OnUpdatePolicy(&provider_);

  // Disabling file-selection dialogs should disable the PromptForDownload pref.
  const Value* value = NULL;
  EXPECT_EQ(PrefStore::READ_OK, store_->GetValue(prefs::kPromptForDownload,
                                                 &value));
  ASSERT_TRUE(value);
  bool prompt_for_download = true;
  bool result = value->GetAsBoolean(&prompt_for_download);
  ASSERT_TRUE(result);
  EXPECT_FALSE(prompt_for_download);
}

// Test cases for the Autofill policy setting.
class ConfigurationPolicyPrefStoreAutofillTest
    : public ConfigurationPolicyPrefStoreTestBase<testing::Test> {
};

TEST_F(ConfigurationPolicyPrefStoreAutofillTest, Default) {
  EXPECT_EQ(PrefStore::READ_NO_VALUE,
            store_->GetValue(prefs::kAutofillEnabled, NULL));
}

TEST_F(ConfigurationPolicyPrefStoreAutofillTest, Enabled) {
  provider_.AddMandatoryPolicy(key::kAutoFillEnabled,
                               Value::CreateBooleanValue(true));
  store_->OnUpdatePolicy(&provider_);
  // Enabling Autofill should not set the pref.
  EXPECT_EQ(PrefStore::READ_NO_VALUE,
            store_->GetValue(prefs::kAutofillEnabled, NULL));
}

TEST_F(ConfigurationPolicyPrefStoreAutofillTest, Disabled) {
  provider_.AddMandatoryPolicy(key::kAutoFillEnabled,
                               Value::CreateBooleanValue(false));
  store_->OnUpdatePolicy(&provider_);
  // Disabling Autofill should switch the pref to managed.
  const Value* value = NULL;
  EXPECT_EQ(PrefStore::READ_OK,
            store_->GetValue(prefs::kAutofillEnabled, &value));
  ASSERT_TRUE(value);
  bool autofill_enabled = true;
  bool result = value->GetAsBoolean(&autofill_enabled);
  ASSERT_TRUE(result);
  EXPECT_FALSE(autofill_enabled);
}

// Exercises the policy refresh mechanism.
class ConfigurationPolicyPrefStoreRefreshTest
    : public ConfigurationPolicyPrefStoreTestBase<testing::Test> {
 protected:
  virtual void SetUp() {
    store_->AddObserver(&observer_);
  }

  virtual void TearDown() {
    store_->RemoveObserver(&observer_);
  }

  PrefStoreObserverMock observer_;
};

TEST_F(ConfigurationPolicyPrefStoreRefreshTest, Refresh) {
  const Value* value = NULL;
  EXPECT_EQ(PrefStore::READ_NO_VALUE,
            store_->GetValue(prefs::kHomePage, NULL));

  EXPECT_CALL(observer_, OnPrefValueChanged(prefs::kHomePage)).Times(1);
  provider_.AddMandatoryPolicy(
      key::kHomepageLocation,
      Value::CreateStringValue("http://www.chromium.org"));
  store_->OnUpdatePolicy(&provider_);
  Mock::VerifyAndClearExpectations(&observer_);
  EXPECT_EQ(PrefStore::READ_OK,
            store_->GetValue(prefs::kHomePage, &value));
  EXPECT_TRUE(StringValue("http://www.chromium.org").Equals(value));

  EXPECT_CALL(observer_, OnPrefValueChanged(_)).Times(0);
  store_->OnUpdatePolicy(&provider_);
  Mock::VerifyAndClearExpectations(&observer_);

  EXPECT_CALL(observer_, OnPrefValueChanged(prefs::kHomePage)).Times(1);
  provider_.RemovePolicy(key::kHomepageLocation);
  store_->OnUpdatePolicy(&provider_);
  Mock::VerifyAndClearExpectations(&observer_);
  EXPECT_EQ(PrefStore::READ_NO_VALUE,
            store_->GetValue(prefs::kHomePage, NULL));
}

TEST_F(ConfigurationPolicyPrefStoreRefreshTest, Initialization) {
  EXPECT_FALSE(store_->IsInitializationComplete());

  EXPECT_CALL(observer_, OnInitializationCompleted(true)).Times(1);

  provider_.SetInitializationComplete(true);
  EXPECT_FALSE(store_->IsInitializationComplete());

  store_->OnUpdatePolicy(&provider_);
  Mock::VerifyAndClearExpectations(&observer_);
  EXPECT_TRUE(store_->IsInitializationComplete());
}

// Tests for policies that don't quite fit the previous patterns.
class ConfigurationPolicyPrefStoreOthersTest
    : public ConfigurationPolicyPrefStoreTestBase<testing::Test> {
};

TEST_F(ConfigurationPolicyPrefStoreOthersTest, JavascriptEnabled) {
  // This is a boolean policy, but affects an integer preference.
  EXPECT_EQ(PrefStore::READ_NO_VALUE,
            store_->GetValue(prefs::kManagedDefaultJavaScriptSetting, NULL));
  provider_.AddMandatoryPolicy(key::kJavascriptEnabled,
                               Value::CreateBooleanValue(true));
  store_->OnUpdatePolicy(&provider_);
  EXPECT_EQ(PrefStore::READ_NO_VALUE,
            store_->GetValue(prefs::kManagedDefaultJavaScriptSetting, NULL));
  provider_.AddMandatoryPolicy(key::kJavascriptEnabled,
                               Value::CreateBooleanValue(false));
  store_->OnUpdatePolicy(&provider_);
  const Value* value = NULL;
  EXPECT_EQ(PrefStore::READ_OK,
            store_->GetValue(prefs::kManagedDefaultJavaScriptSetting, &value));
  EXPECT_TRUE(base::FundamentalValue(CONTENT_SETTING_BLOCK).Equals(value));
}

TEST_F(ConfigurationPolicyPrefStoreOthersTest, JavascriptEnabledOverridden) {
  EXPECT_EQ(PrefStore::READ_NO_VALUE,
            store_->GetValue(prefs::kManagedDefaultJavaScriptSetting, NULL));
  provider_.AddMandatoryPolicy(key::kJavascriptEnabled,
                               Value::CreateBooleanValue(false));
  store_->OnUpdatePolicy(&provider_);
  const Value* value = NULL;
  EXPECT_EQ(PrefStore::READ_OK,
            store_->GetValue(prefs::kManagedDefaultJavaScriptSetting, &value));
  EXPECT_TRUE(base::FundamentalValue(CONTENT_SETTING_BLOCK).Equals(value));
  // DefaultJavaScriptSetting overrides JavascriptEnabled.
  provider_.AddMandatoryPolicy(
      key::kDefaultJavaScriptSetting,
      Value::CreateIntegerValue(CONTENT_SETTING_ALLOW));
  store_->OnUpdatePolicy(&provider_);
  EXPECT_EQ(PrefStore::READ_OK,
            store_->GetValue(prefs::kManagedDefaultJavaScriptSetting, &value));
  EXPECT_TRUE(base::FundamentalValue(CONTENT_SETTING_ALLOW).Equals(value));
}

}  // namespace policy
