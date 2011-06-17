// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "app/test/data/resource.h"
#include "base/command_line.h"
#include "base/memory/scoped_ptr.h"
#include "base/values.h"
#include "chrome/browser/policy/configuration_policy_pref_store.h"
#include "chrome/browser/policy/mock_configuration_policy_provider.h"
#include "chrome/browser/prefs/browser_prefs.h"
#include "chrome/browser/prefs/command_line_pref_store.h"
#include "chrome/browser/prefs/pref_change_registrar.h"
#include "chrome/browser/prefs/pref_observer_mock.h"
#include "chrome/browser/prefs/pref_service_mock_builder.h"
#include "chrome/browser/prefs/pref_value_store.h"
#include "chrome/browser/prefs/proxy_config_dictionary.h"
#include "chrome/browser/prefs/testing_pref_store.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/testing_pref_service.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::_;
using testing::Mock;

// TODO(port): port this test to POSIX.
#if defined(OS_WIN)
TEST(PrefServiceTest, LocalizedPrefs) {
  TestingPrefService prefs;
  const char kBoolean[] = "boolean";
  const char kInteger[] = "integer";
  const char kString[] = "string";
  prefs.RegisterLocalizedBooleanPref(kBoolean, IDS_LOCALE_BOOL);
  prefs.RegisterLocalizedIntegerPref(kInteger, IDS_LOCALE_INT);
  prefs.RegisterLocalizedStringPref(kString, IDS_LOCALE_STRING);

  // The locale default should take preference over the user default.
  EXPECT_FALSE(prefs.GetBoolean(kBoolean));
  EXPECT_EQ(1, prefs.GetInteger(kInteger));
  EXPECT_EQ("hello", prefs.GetString(kString));

  prefs.SetBoolean(kBoolean, true);
  EXPECT_TRUE(prefs.GetBoolean(kBoolean));
  prefs.SetInteger(kInteger, 5);
  EXPECT_EQ(5, prefs.GetInteger(kInteger));
  prefs.SetString(kString, "foo");
  EXPECT_EQ("foo", prefs.GetString(kString));
}
#endif

TEST(PrefServiceTest, NoObserverFire) {
  TestingPrefService prefs;

  const char pref_name[] = "homepage";
  prefs.RegisterStringPref(pref_name, std::string());

  const char new_pref_value[] = "http://www.google.com/";
  PrefObserverMock obs;
  PrefChangeRegistrar registrar;
  registrar.Init(&prefs);
  registrar.Add(pref_name, &obs);

  // This should fire the checks in PrefObserverMock::Observe.
  const StringValue expected_value(new_pref_value);
  obs.Expect(&prefs, pref_name, &expected_value);
  prefs.SetString(pref_name, new_pref_value);
  Mock::VerifyAndClearExpectations(&obs);

  // Setting the pref to the same value should not set the pref value a second
  // time.
  EXPECT_CALL(obs, Observe(_, _, _)).Times(0);
  prefs.SetString(pref_name, new_pref_value);
  Mock::VerifyAndClearExpectations(&obs);

  // Clearing the pref should cause the pref to fire.
  const StringValue expected_default_value("");
  obs.Expect(&prefs, pref_name, &expected_default_value);
  prefs.ClearPref(pref_name);
  Mock::VerifyAndClearExpectations(&obs);

  // Clearing the pref again should not cause the pref to fire.
  EXPECT_CALL(obs, Observe(_, _, _)).Times(0);
  prefs.ClearPref(pref_name);
  Mock::VerifyAndClearExpectations(&obs);
}

TEST(PrefServiceTest, HasPrefPath) {
  TestingPrefService prefs;

  const char path[] = "fake.path";

  // Shouldn't initially have a path.
  EXPECT_FALSE(prefs.HasPrefPath(path));

  // Register the path. This doesn't set a value, so the path still shouldn't
  // exist.
  prefs.RegisterStringPref(path, std::string());
  EXPECT_FALSE(prefs.HasPrefPath(path));

  // Set a value and make sure we have a path.
  prefs.SetString(path, "blah");
  EXPECT_TRUE(prefs.HasPrefPath(path));
}

TEST(PrefServiceTest, Observers) {
  const char pref_name[] = "homepage";

  TestingPrefService prefs;
  prefs.SetUserPref(pref_name, Value::CreateStringValue("http://www.cnn.com"));
  prefs.RegisterStringPref(pref_name, std::string());

  const char new_pref_value[] = "http://www.google.com/";
  const StringValue expected_new_pref_value(new_pref_value);
  PrefObserverMock obs;
  PrefChangeRegistrar registrar;
  registrar.Init(&prefs);
  registrar.Add(pref_name, &obs);

  // This should fire the checks in PrefObserverMock::Observe.
  obs.Expect(&prefs, pref_name, &expected_new_pref_value);
  prefs.SetString(pref_name, new_pref_value);
  Mock::VerifyAndClearExpectations(&obs);

  // Now try adding a second pref observer.
  const char new_pref_value2[] = "http://www.youtube.com/";
  const StringValue expected_new_pref_value2(new_pref_value2);
  PrefObserverMock obs2;
  obs.Expect(&prefs, pref_name, &expected_new_pref_value2);
  obs2.Expect(&prefs, pref_name, &expected_new_pref_value2);
  registrar.Add(pref_name, &obs2);
  // This should fire the checks in obs and obs2.
  prefs.SetString(pref_name, new_pref_value2);
  Mock::VerifyAndClearExpectations(&obs);
  Mock::VerifyAndClearExpectations(&obs2);

  // Make sure obs2 still works after removing obs.
  registrar.Remove(pref_name, &obs);
  EXPECT_CALL(obs, Observe(_, _, _)).Times(0);
  obs2.Expect(&prefs, pref_name, &expected_new_pref_value);
  // This should only fire the observer in obs2.
  prefs.SetString(pref_name, new_pref_value);
  Mock::VerifyAndClearExpectations(&obs);
  Mock::VerifyAndClearExpectations(&obs2);
}

// Make sure that if a preference changes type, so the wrong type is stored in
// the user pref file, it uses the correct fallback value instead.
TEST(PrefServiceTest, GetValueChangedType) {
  const int kTestValue = 10;
  TestingPrefService prefs;
  prefs.RegisterIntegerPref(prefs::kStabilityLaunchCount, kTestValue);

  // Check falling back to a recommended value.
  prefs.SetUserPref(prefs::kStabilityLaunchCount,
                    Value::CreateStringValue("not an integer"));
  const PrefService::Preference* pref =
      prefs.FindPreference(prefs::kStabilityLaunchCount);
  ASSERT_TRUE(pref);
  const Value* value = pref->GetValue();
  ASSERT_TRUE(value);
  EXPECT_EQ(Value::TYPE_INTEGER, value->GetType());
  int actual_int_value = -1;
  EXPECT_TRUE(value->GetAsInteger(&actual_int_value));
  EXPECT_EQ(kTestValue, actual_int_value);
}

void assertProxyMode(const ProxyConfigDictionary& dict,
                     ProxyPrefs::ProxyMode expected_mode) {
  ProxyPrefs::ProxyMode actual_mode;
  ASSERT_TRUE(dict.GetMode(&actual_mode));
  EXPECT_EQ(expected_mode, actual_mode);
}

void assertProxyServer(const ProxyConfigDictionary& dict,
                       const std::string& expected) {
  std::string actual;
  if (!expected.empty()) {
    ASSERT_TRUE(dict.GetProxyServer(&actual));
    EXPECT_EQ(expected, actual);
  } else {
    EXPECT_FALSE(dict.GetProxyServer(&actual));
  }
}

void assertPacUrl(const ProxyConfigDictionary& dict,
                  const std::string& expected) {
  std::string actual;
  if (!expected.empty()) {
    ASSERT_TRUE(dict.GetPacUrl(&actual));
    EXPECT_EQ(expected, actual);
  } else {
    EXPECT_FALSE(dict.GetPacUrl(&actual));
  }
}

void assertBypassList(const ProxyConfigDictionary& dict,
                      const std::string& expected) {
  std::string actual;
  if (!expected.empty()) {
    ASSERT_TRUE(dict.GetBypassList(&actual));
    EXPECT_EQ(expected, actual);
  } else {
    EXPECT_FALSE(dict.GetBypassList(&actual));
  }
}

void assertProxyModeWithoutParams(const ProxyConfigDictionary& dict,
                                  ProxyPrefs::ProxyMode proxy_mode) {
  assertProxyMode(dict, proxy_mode);
  assertProxyServer(dict, "");
  assertPacUrl(dict, "");
  assertBypassList(dict, "");
}

TEST(PrefServiceTest, ProxyPolicyOverridesCommandLineOptions) {
  CommandLine command_line(CommandLine::NO_PROGRAM);
  command_line.AppendSwitchASCII(switches::kProxyBypassList, "123");
  command_line.AppendSwitchASCII(switches::kProxyServer, "789");
  scoped_ptr<policy::MockConfigurationPolicyProvider> provider(
      new policy::MockConfigurationPolicyProvider());
  Value* mode_name = Value::CreateStringValue(
      ProxyPrefs::kFixedServersProxyModeName);
  provider->AddPolicy(policy::kPolicyProxyMode, mode_name);
  provider->AddPolicy(policy::kPolicyProxyBypassList,
                      Value::CreateStringValue("abc"));
  provider->AddPolicy(policy::kPolicyProxyServer,
                      Value::CreateStringValue("ghi"));

  // First verify that command-line options are set correctly when
  // there is no policy in effect.
  PrefServiceMockBuilder builder;
  builder.WithCommandLine(&command_line);
  scoped_ptr<PrefService> prefs(builder.Create());
  browser::RegisterUserPrefs(prefs.get());
  ProxyConfigDictionary dict(prefs->GetDictionary(prefs::kProxy));
  assertProxyMode(dict, ProxyPrefs::MODE_FIXED_SERVERS);
  assertProxyServer(dict, "789");
  assertPacUrl(dict, "");
  assertBypassList(dict, "123");

  // Try a second time time with the managed PrefStore in place, the
  // manual proxy policy should have removed all traces of the command
  // line and replaced them with the policy versions.
  builder.WithCommandLine(&command_line);
  builder.WithManagedPlatformProvider(provider.get());
  scoped_ptr<PrefService> prefs2(builder.Create());
  browser::RegisterUserPrefs(prefs2.get());
  ProxyConfigDictionary dict2(prefs2->GetDictionary(prefs::kProxy));
  assertProxyMode(dict2, ProxyPrefs::MODE_FIXED_SERVERS);
  assertProxyServer(dict2, "ghi");
  assertPacUrl(dict2, "");
  assertBypassList(dict2, "abc");
}

TEST(PrefServiceTest, ProxyPolicyOverridesUnrelatedCommandLineOptions) {
  CommandLine command_line(CommandLine::NO_PROGRAM);
  command_line.AppendSwitchASCII(switches::kProxyBypassList, "123");
  command_line.AppendSwitchASCII(switches::kProxyServer, "789");
  scoped_ptr<policy::MockConfigurationPolicyProvider> provider(
      new policy::MockConfigurationPolicyProvider());
  Value* mode_name = Value::CreateStringValue(
      ProxyPrefs::kAutoDetectProxyModeName);
  provider->AddPolicy(policy::kPolicyProxyMode, mode_name);

  // First verify that command-line options are set correctly when
  // there is no policy in effect.
  PrefServiceMockBuilder builder;
  builder.WithCommandLine(&command_line);
  scoped_ptr<PrefService> prefs(builder.Create());
  browser::RegisterUserPrefs(prefs.get());
  ProxyConfigDictionary dict(prefs->GetDictionary(prefs::kProxy));
  assertProxyMode(dict, ProxyPrefs::MODE_FIXED_SERVERS);
  assertProxyServer(dict, "789");
  assertPacUrl(dict, "");
  assertBypassList(dict, "123");

  // Try a second time time with the managed PrefStore in place, the
  // no proxy policy should have removed all traces of the command
  // line proxy settings, even though they were not the specific one
  // set in policy.
  builder.WithCommandLine(&command_line);
  builder.WithManagedPlatformProvider(provider.get());
  scoped_ptr<PrefService> prefs2(builder.Create());
  browser::RegisterUserPrefs(prefs2.get());
  ProxyConfigDictionary dict2(prefs2->GetDictionary(prefs::kProxy));
  assertProxyModeWithoutParams(dict2, ProxyPrefs::MODE_AUTO_DETECT);
}

TEST(PrefServiceTest, ProxyPolicyOverridesCommandLineNoProxy) {
  CommandLine command_line(CommandLine::NO_PROGRAM);
  command_line.AppendSwitch(switches::kNoProxyServer);
  scoped_ptr<policy::MockConfigurationPolicyProvider> provider(
      new policy::MockConfigurationPolicyProvider());
  Value* mode_name = Value::CreateStringValue(
      ProxyPrefs::kAutoDetectProxyModeName);
  provider->AddPolicy(policy::kPolicyProxyMode, mode_name);

  // First verify that command-line options are set correctly when
  // there is no policy in effect.
  PrefServiceMockBuilder builder;
  builder.WithCommandLine(&command_line);
  scoped_ptr<PrefService> prefs(builder.Create());
  browser::RegisterUserPrefs(prefs.get());
  ProxyConfigDictionary dict(prefs->GetDictionary(prefs::kProxy));
  assertProxyModeWithoutParams(dict, ProxyPrefs::MODE_DIRECT);

  // Try a second time time with the managed PrefStore in place, the
  // auto-detect should be overridden. The default pref store must be
  // in place with the appropriate default value for this to work.
  builder.WithCommandLine(&command_line);
  builder.WithManagedPlatformProvider(provider.get());
  scoped_ptr<PrefService> prefs2(builder.Create());
  browser::RegisterUserPrefs(prefs2.get());
  ProxyConfigDictionary dict2(prefs2->GetDictionary(prefs::kProxy));
  assertProxyModeWithoutParams(dict2, ProxyPrefs::MODE_AUTO_DETECT);
}

TEST(PrefServiceTest, ProxyPolicyOverridesCommandLineAutoDetect) {
  CommandLine command_line(CommandLine::NO_PROGRAM);
  command_line.AppendSwitch(switches::kProxyAutoDetect);
  scoped_ptr<policy::MockConfigurationPolicyProvider> provider(
      new policy::MockConfigurationPolicyProvider());
  Value* mode_name = Value::CreateStringValue(
      ProxyPrefs::kDirectProxyModeName);
  provider->AddPolicy(policy::kPolicyProxyMode, mode_name);

  // First verify that the auto-detect is set if there is no managed
  // PrefStore.
  PrefServiceMockBuilder builder;
  builder.WithCommandLine(&command_line);
  scoped_ptr<PrefService> prefs(builder.Create());
  browser::RegisterUserPrefs(prefs.get());
  ProxyConfigDictionary dict(prefs->GetDictionary(prefs::kProxy));
  assertProxyModeWithoutParams(dict, ProxyPrefs::MODE_AUTO_DETECT);

  // Try a second time time with the managed PrefStore in place, the
  // auto-detect should be overridden. The default pref store must be
  // in place with the appropriate default value for this to work.
  builder.WithCommandLine(&command_line);
  builder.WithManagedPlatformProvider(provider.get());
  scoped_ptr<PrefService> prefs2(builder.Create());
  browser::RegisterUserPrefs(prefs2.get());
  ProxyConfigDictionary dict2(prefs2->GetDictionary(prefs::kProxy));
  assertProxyModeWithoutParams(dict2, ProxyPrefs::MODE_DIRECT);
}

class PrefServiceSetValueTest : public testing::Test {
 protected:
  static const char kName[];
  static const char kValue[];

  TestingPrefService prefs_;
  PrefObserverMock observer_;
};

const char PrefServiceSetValueTest::kName[] = "name";
const char PrefServiceSetValueTest::kValue[] = "value";

TEST_F(PrefServiceSetValueTest, SetStringValue) {
  const char default_string[] = "default";
  const StringValue default_value(default_string);
  prefs_.RegisterStringPref(kName, default_string);

  PrefChangeRegistrar registrar;
  registrar.Init(&prefs_);
  registrar.Add(kName, &observer_);

  // Changing the controlling store from default to user triggers notification.
  observer_.Expect(&prefs_, kName, &default_value);
  prefs_.Set(kName, default_value);
  Mock::VerifyAndClearExpectations(&observer_);

  EXPECT_CALL(observer_, Observe(_, _, _)).Times(0);
  prefs_.Set(kName, default_value);
  Mock::VerifyAndClearExpectations(&observer_);

  StringValue new_value(kValue);
  observer_.Expect(&prefs_, kName, &new_value);
  prefs_.Set(kName, new_value);
  Mock::VerifyAndClearExpectations(&observer_);
}

TEST_F(PrefServiceSetValueTest, SetDictionaryValue) {
  prefs_.RegisterDictionaryPref(kName);
  PrefChangeRegistrar registrar;
  registrar.Init(&prefs_);
  registrar.Add(kName, &observer_);

  EXPECT_CALL(observer_, Observe(_, _, _)).Times(0);
  prefs_.RemoveUserPref(kName);
  Mock::VerifyAndClearExpectations(&observer_);

  DictionaryValue new_value;
  new_value.SetString(kName, kValue);
  observer_.Expect(&prefs_, kName, &new_value);
  prefs_.Set(kName, new_value);
  Mock::VerifyAndClearExpectations(&observer_);

  EXPECT_CALL(observer_, Observe(_, _, _)).Times(0);
  prefs_.Set(kName, new_value);
  Mock::VerifyAndClearExpectations(&observer_);

  DictionaryValue empty;
  observer_.Expect(&prefs_, kName, &empty);
  prefs_.Set(kName, empty);
  Mock::VerifyAndClearExpectations(&observer_);
}

TEST_F(PrefServiceSetValueTest, SetListValue) {
  prefs_.RegisterListPref(kName);
  PrefChangeRegistrar registrar;
  registrar.Init(&prefs_);
  registrar.Add(kName, &observer_);

  EXPECT_CALL(observer_, Observe(_, _, _)).Times(0);
  prefs_.RemoveUserPref(kName);
  Mock::VerifyAndClearExpectations(&observer_);

  ListValue new_value;
  new_value.Append(Value::CreateStringValue(kValue));
  observer_.Expect(&prefs_, kName, &new_value);
  prefs_.Set(kName, new_value);
  Mock::VerifyAndClearExpectations(&observer_);

  EXPECT_CALL(observer_, Observe(_, _, _)).Times(0);
  prefs_.Set(kName, new_value);
  Mock::VerifyAndClearExpectations(&observer_);

  ListValue empty;
  observer_.Expect(&prefs_, kName, &empty);
  prefs_.Set(kName, empty);
  Mock::VerifyAndClearExpectations(&observer_);
}
