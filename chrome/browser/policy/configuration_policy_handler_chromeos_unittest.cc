// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/configuration_policy_handler_chromeos.h"

#include "chrome/browser/policy/policy_error_map.h"
#include "chrome/browser/policy/policy_map.h"
#include "policy/policy_constants.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace policy {

TEST(NetworkConfigurationPolicyHandlerTest, Empty) {
  PolicyMap policy_map;
  NetworkConfigurationPolicyHandler handler(
      key::kOpenNetworkConfiguration,
      chromeos::NetworkUIData::ONC_SOURCE_USER_POLICY);
  PolicyErrorMap errors;
  EXPECT_TRUE(handler.CheckPolicySettings(policy_map, &errors));
  EXPECT_TRUE(errors.GetErrors(key::kOpenNetworkConfiguration).empty());
}

TEST(NetworkConfigurationPolicyHandlerTest, ValidONC) {
  const std::string kTestONC(
      "{"
      "  \"NetworkConfigurations\": [{"
      "    \"GUID\": \"{485d6076-dd44-6b6d-69787465725f5045}\","
      "    \"Type\": \"WiFi\","
      "    \"WiFi\": {"
      "      \"Security\": \"WEP\","
      "      \"SSID\": \"ssid\","
      "      \"Passphrase\": \"pass\","
      "    }"
      "  }]"
      "}");

  PolicyMap policy_map;
  policy_map.Set(key::kOpenNetworkConfiguration,
                 POLICY_LEVEL_MANDATORY,
                 POLICY_SCOPE_USER,
                 Value::CreateStringValue(kTestONC));
  NetworkConfigurationPolicyHandler handler(
      key::kOpenNetworkConfiguration,
      chromeos::NetworkUIData::ONC_SOURCE_USER_POLICY);
  PolicyErrorMap errors;
  EXPECT_TRUE(handler.CheckPolicySettings(policy_map, &errors));
  EXPECT_TRUE(errors.GetErrors(key::kOpenNetworkConfiguration).empty());
}

TEST(NetworkConfigurationPolicyHandlerTest, WrongType) {
  PolicyMap policy_map;
  policy_map.Set(key::kOpenNetworkConfiguration,
                 POLICY_LEVEL_MANDATORY,
                 POLICY_SCOPE_USER,
                 Value::CreateBooleanValue(false));
  NetworkConfigurationPolicyHandler handler(
      key::kOpenNetworkConfiguration,
      chromeos::NetworkUIData::ONC_SOURCE_USER_POLICY);
  PolicyErrorMap errors;
  EXPECT_FALSE(handler.CheckPolicySettings(policy_map, &errors));
  EXPECT_FALSE(errors.GetErrors(key::kOpenNetworkConfiguration).empty());
}

TEST(NetworkConfigurationPolicyHandlerTest, JSONParseError) {
  const std::string kTestONC("I'm not proper JSON!");
  PolicyMap policy_map;
  policy_map.Set(key::kOpenNetworkConfiguration,
                 POLICY_LEVEL_MANDATORY,
                 POLICY_SCOPE_USER,
                 Value::CreateStringValue(kTestONC));
  NetworkConfigurationPolicyHandler handler(
      key::kOpenNetworkConfiguration,
      chromeos::NetworkUIData::ONC_SOURCE_USER_POLICY);
  PolicyErrorMap errors;
  EXPECT_FALSE(handler.CheckPolicySettings(policy_map, &errors));
  EXPECT_FALSE(errors.GetErrors(key::kOpenNetworkConfiguration).empty());
}

TEST(NetworkConfigurationPolicyHandlerTest, Sanitization) {
  const std::string kTestONC(
      "{"
      "  \"NetworkConfigurations\": [{"
      "    \"GUID\": \"{485d6076-dd44-6b6d-69787465725f5045}\","
      "    \"Type\": \"WiFi\","
      "    \"WiFi\": {"
      "      \"Security\": \"WEP\","
      "      \"SSID\": \"ssid\","
      "      \"Passphrase\": \"pass\","
      "    }"
      "  }]"
      "}");

  PolicyMap policy_map;
  policy_map.Set(key::kOpenNetworkConfiguration,
                 POLICY_LEVEL_MANDATORY,
                 POLICY_SCOPE_USER,
                 Value::CreateStringValue(kTestONC));
  NetworkConfigurationPolicyHandler handler(
      key::kOpenNetworkConfiguration,
      chromeos::NetworkUIData::ONC_SOURCE_USER_POLICY);
  PolicyErrorMap errors;
  handler.PrepareForDisplaying(&policy_map);
  const Value* sanitized = policy_map.GetValue(key::kOpenNetworkConfiguration);
  ASSERT_TRUE(sanitized);
  std::string sanitized_onc;
  EXPECT_TRUE(sanitized->GetAsString(&sanitized_onc));
  EXPECT_FALSE(sanitized_onc.empty());
  EXPECT_EQ(std::string::npos, sanitized_onc.find("pass"));
}

}  // namespace policy
