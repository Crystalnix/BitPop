// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/cloud_policy_provider.h"

#include "base/basictypes.h"
#include "base/values.h"
#include "chrome/browser/policy/browser_policy_connector.h"
#include "chrome/browser/policy/cloud_policy_cache_base.h"
#include "chrome/browser/policy/cloud_policy_provider_impl.h"
#include "chrome/browser/policy/configuration_policy_provider.h"
#include "chrome/browser/policy/mock_configuration_policy_provider.h"
#include "policy/policy_constants.h"
#include "testing/gmock/include/gmock/gmock.h"

using testing::Mock;
using testing::_;

namespace em = enterprise_management;

namespace policy {

namespace {

// Utility function for tests.
void SetPolicy(PolicyMap* map, const char* policy, Value* value) {
  map->Set(policy, POLICY_LEVEL_MANDATORY, POLICY_SCOPE_USER, value);
}

}  // namespace

class MockCloudPolicyCache : public CloudPolicyCacheBase {
 public:
  MockCloudPolicyCache() {}
  virtual ~MockCloudPolicyCache() {}

  // CloudPolicyCacheBase implementation.
  void Load() OVERRIDE {}
  void SetPolicy(const em::PolicyFetchResponse& policy) OVERRIDE {}
  bool DecodePolicyData(const em::PolicyData& policy_data,
                        PolicyMap* policies) OVERRIDE {
    return true;
  }

  void SetUnmanaged() OVERRIDE {
    is_unmanaged_ = true;
  }

  PolicyMap* mutable_policy() {
    return &policies_;
  }

  void set_initialized(bool initialized) {
    initialization_complete_ = initialized;
  }

  void Set(const char *name, Value* value) {
    policies_.Set(name, POLICY_LEVEL_MANDATORY, POLICY_SCOPE_USER, value);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(MockCloudPolicyCache);
};

class CloudPolicyProviderTest : public testing::Test {
 protected:
  void CreateCloudPolicyProvider() {
    cloud_policy_provider_.reset(
        new CloudPolicyProviderImpl(&browser_policy_connector_,
                                    GetChromePolicyDefinitionList(),
                                    POLICY_LEVEL_MANDATORY));
  }

  // Appends the caches to a provider and then provides the policies to
  // |result|.
  void RunCachesThroughProvider(MockCloudPolicyCache caches[], int n,
                                PolicyMap* result) {
    CloudPolicyProviderImpl provider(
        &browser_policy_connector_,
        GetChromePolicyDefinitionList(),
        POLICY_LEVEL_MANDATORY);
    for (int i = 0; i < n; i++) {
      provider.AppendCache(&caches[i]);
    }
    provider.Provide(result);
  }

  void CombineTwoPolicyMaps(const PolicyMap& base,
                            const PolicyMap& overlay,
                            PolicyMap* out_map) {
    MockCloudPolicyCache caches[2];
    caches[0].mutable_policy()->CopyFrom(base);
    caches[0].set_initialized(true);
    caches[1].mutable_policy()->CopyFrom(overlay);
    caches[1].set_initialized(true);
    RunCachesThroughProvider(caches, 2, out_map);
  }

  void FixDeprecatedPolicies(PolicyMap* policies) {
    CloudPolicyProviderImpl::FixDeprecatedPolicies(policies);
  }

  scoped_ptr<CloudPolicyProviderImpl> cloud_policy_provider_;

 private:
  BrowserPolicyConnector browser_policy_connector_;
};

// Proxy setting distributed over multiple caches.
TEST_F(CloudPolicyProviderTest,
       ProxySettingDistributedOverMultipleCaches) {
  // There are proxy_policy_count()+1 = 6 caches and they are mixed together by
  // one instance of CloudPolicyProvider. The first cache has some policies but
  // no proxy-related ones. The following caches have each one proxy-policy set.
  const int n = 6;
  MockCloudPolicyCache caches[n];

  // Prepare |cache[0]| to serve some non-proxy policies.
  caches[0].Set(key::kShowHomeButton, Value::CreateBooleanValue(true));
  caches[0].Set(key::kIncognitoEnabled, Value::CreateBooleanValue(true));
  caches[0].Set(key::kTranslateEnabled, Value::CreateBooleanValue(true));
  caches[0].set_initialized(true);

  // Prepare the other caches to serve one proxy-policy each.
  caches[1].Set(key::kProxyMode, Value::CreateStringValue("cache 1"));
  caches[1].set_initialized(true);
  caches[2].Set(key::kProxyServerMode, Value::CreateIntegerValue(2));
  caches[2].set_initialized(true);
  caches[3].Set(key::kProxyServer, Value::CreateStringValue("cache 3"));
  caches[3].set_initialized(true);
  caches[4].Set(key::kProxyPacUrl, Value::CreateStringValue("cache 4"));
  caches[4].set_initialized(true);
  caches[5].Set(key::kProxyMode, Value::CreateStringValue("cache 5"));
  caches[5].set_initialized(true);

  PolicyMap policies;
  RunCachesThroughProvider(caches, n, &policies);

  // Verify expectations.
  EXPECT_TRUE(policies.Get(key::kProxyMode) == NULL);
  EXPECT_TRUE(policies.Get(key::kProxyServerMode) == NULL);
  EXPECT_TRUE(policies.Get(key::kProxyServer) == NULL);
  EXPECT_TRUE(policies.Get(key::kProxyPacUrl) == NULL);

  const Value* value = policies.GetValue(key::kProxySettings);
  ASSERT_TRUE(value != NULL);
  ASSERT_TRUE(value->IsType(Value::TYPE_DICTIONARY));
  const DictionaryValue* settings = static_cast<const DictionaryValue*>(value);
  std::string mode;
  EXPECT_TRUE(settings->GetString(key::kProxyMode, &mode));
  EXPECT_EQ("cache 1", mode);

  base::FundamentalValue expected(true);
  EXPECT_TRUE(base::Value::Equals(&expected,
                                  policies.GetValue(key::kShowHomeButton)));
  EXPECT_TRUE(base::Value::Equals(&expected,
                                  policies.GetValue(key::kIncognitoEnabled)));
  EXPECT_TRUE(base::Value::Equals(&expected,
                                  policies.GetValue(key::kTranslateEnabled)));
}

// Combining two PolicyMaps.
TEST_F(CloudPolicyProviderTest, CombineTwoPolicyMapsSame) {
  PolicyMap A, B, C;
  SetPolicy(&A, key::kHomepageLocation,
            Value::CreateStringValue("http://www.chromium.org"));
  SetPolicy(&B, key::kHomepageLocation,
            Value::CreateStringValue("http://www.google.com"));
  SetPolicy(&A, key::kApplicationLocaleValue, Value::CreateStringValue("hu"));
  SetPolicy(&B, key::kApplicationLocaleValue, Value::CreateStringValue("us"));
  SetPolicy(&A, key::kDevicePolicyRefreshRate, new base::FundamentalValue(100));
  SetPolicy(&B, key::kDevicePolicyRefreshRate, new base::FundamentalValue(200));
  CombineTwoPolicyMaps(A, B, &C);
  EXPECT_TRUE(A.Equals(C));
}

TEST_F(CloudPolicyProviderTest, CombineTwoPolicyMapsEmpty) {
  PolicyMap A, B, C;
  CombineTwoPolicyMaps(A, B, &C);
  EXPECT_TRUE(C.empty());
}

TEST_F(CloudPolicyProviderTest, CombineTwoPolicyMapsPartial) {
  PolicyMap A, B, C;

  SetPolicy(&A, key::kHomepageLocation,
            Value::CreateStringValue("http://www.chromium.org"));
  SetPolicy(&B, key::kHomepageLocation,
            Value::CreateStringValue("http://www.google.com"));
  SetPolicy(&B, key::kApplicationLocaleValue, Value::CreateStringValue("us"));
  SetPolicy(&A, key::kDevicePolicyRefreshRate, new base::FundamentalValue(100));
  SetPolicy(&B, key::kDevicePolicyRefreshRate, new base::FundamentalValue(200));
  CombineTwoPolicyMaps(A, B, &C);

  const Value* value;
  std::string string_value;
  int int_value;
  value = C.GetValue(key::kHomepageLocation);
  ASSERT_TRUE(NULL != value);
  EXPECT_TRUE(value->GetAsString(&string_value));
  EXPECT_EQ("http://www.chromium.org", string_value);
  value = C.GetValue(key::kApplicationLocaleValue);
  ASSERT_TRUE(NULL != value);
  EXPECT_TRUE(value->GetAsString(&string_value));
  EXPECT_EQ("us", string_value);
  value = C.GetValue(key::kDevicePolicyRefreshRate);
  ASSERT_TRUE(NULL != value);
  EXPECT_TRUE(value->GetAsInteger(&int_value));
  EXPECT_EQ(100, int_value);
}

TEST_F(CloudPolicyProviderTest, CombineTwoPolicyMapsProxies) {
  const int a_value = 1;
  const int b_value = -1;
  PolicyMap A, B, C;

  SetPolicy(&A, key::kProxyMode, Value::CreateIntegerValue(a_value));
  SetPolicy(&B, key::kProxyServerMode, Value::CreateIntegerValue(b_value));
  SetPolicy(&B, key::kProxyServer, Value::CreateIntegerValue(b_value));
  SetPolicy(&B, key::kProxyPacUrl, Value::CreateIntegerValue(b_value));
  SetPolicy(&B, key::kProxyBypassList, Value::CreateIntegerValue(b_value));

  CombineTwoPolicyMaps(A, B, &C);

  FixDeprecatedPolicies(&A);
  FixDeprecatedPolicies(&B);
  EXPECT_TRUE(A.Equals(C));
  EXPECT_FALSE(B.Equals(C));
}

TEST_F(CloudPolicyProviderTest, RefreshPolicies) {
  CreateCloudPolicyProvider();
  MockCloudPolicyCache cache0;
  MockCloudPolicyCache cache1;
  MockCloudPolicyCache cache2;
  MockConfigurationPolicyObserver observer;
  ConfigurationPolicyObserverRegistrar registrar;
  registrar.Init(cloud_policy_provider_.get(), &observer);

  // OnUpdatePolicy is called when the provider doesn't have any caches.
  EXPECT_CALL(observer, OnUpdatePolicy(cloud_policy_provider_.get())).Times(1);
  cloud_policy_provider_->RefreshPolicies();
  Mock::VerifyAndClearExpectations(&observer);

  // OnUpdatePolicy is called when all the caches have updated.
  EXPECT_CALL(observer, OnUpdatePolicy(cloud_policy_provider_.get())).Times(2);
  cloud_policy_provider_->AppendCache(&cache0);
  cloud_policy_provider_->AppendCache(&cache1);
  Mock::VerifyAndClearExpectations(&observer);

  EXPECT_CALL(observer, OnUpdatePolicy(cloud_policy_provider_.get())).Times(0);
  cloud_policy_provider_->RefreshPolicies();
  Mock::VerifyAndClearExpectations(&observer);

  EXPECT_CALL(observer, OnUpdatePolicy(cloud_policy_provider_.get())).Times(0);
  // Updating just one of the caches is not enough.
  cloud_policy_provider_->OnCacheUpdate(&cache0);
  Mock::VerifyAndClearExpectations(&observer);

  EXPECT_CALL(observer, OnUpdatePolicy(cloud_policy_provider_.get())).Times(0);
  // This cache wasn't available when RefreshPolicies was called, so it isn't
  // required to fire the update.
  cloud_policy_provider_->AppendCache(&cache2);
  Mock::VerifyAndClearExpectations(&observer);

  EXPECT_CALL(observer, OnUpdatePolicy(cloud_policy_provider_.get())).Times(1);
  cloud_policy_provider_->OnCacheUpdate(&cache1);
  Mock::VerifyAndClearExpectations(&observer);

  EXPECT_CALL(observer, OnUpdatePolicy(cloud_policy_provider_.get())).Times(0);
  cloud_policy_provider_->RefreshPolicies();
  Mock::VerifyAndClearExpectations(&observer);

  EXPECT_CALL(observer, OnUpdatePolicy(cloud_policy_provider_.get())).Times(0);
  cloud_policy_provider_->OnCacheUpdate(&cache0);
  cloud_policy_provider_->OnCacheUpdate(&cache1);
  Mock::VerifyAndClearExpectations(&observer);

  // If a cache refreshes more than once, the provider should still wait for
  // the others before firing the update.
  EXPECT_CALL(observer, OnUpdatePolicy(cloud_policy_provider_.get())).Times(0);
  cloud_policy_provider_->OnCacheUpdate(&cache0);
  Mock::VerifyAndClearExpectations(&observer);

  // Fire updates if one of the required caches goes away while waiting.
  EXPECT_CALL(observer, OnUpdatePolicy(cloud_policy_provider_.get())).Times(1);
  cloud_policy_provider_->OnCacheGoingAway(&cache2);
  Mock::VerifyAndClearExpectations(&observer);
}

}  // namespace policy
