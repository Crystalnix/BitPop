// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/configuration_policy_reader.h"

#include <map>
#include <string>
#include <vector>

#include "base/logging.h"
#include "base/stl_util.h"
#include "base/string16.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/policy/browser_policy_connector.h"
#include "chrome/browser/policy/configuration_policy_pref_store.h"
#include "chrome/browser/policy/policy_error_map.h"
#include "chrome/browser/policy/policy_map.h"
#include "policy/policy_constants.h"

namespace policy {

// This class functions as a container for policy status information used by the
// ConfigurationPolicyReader class. It obtains policy values from a
// ConfigurationPolicyProvider and maps them to their status information.
class ConfigurationPolicyStatusKeeper {
 public:
  explicit ConfigurationPolicyStatusKeeper(
      ConfigurationPolicyProvider* provider);
  virtual ~ConfigurationPolicyStatusKeeper();

  // Returns a pointer to a DictionaryValue containing the status information
  // of the policy |policy|. The caller acquires ownership of the returned
  // value. Returns NULL if no such policy is stored in this keeper.
  DictionaryValue* GetPolicyStatus(const char* policy) const;

 private:
  typedef std::map<std::string, PolicyStatusInfo*> PolicyStatusMap;

  // Calls Provide() on the passed in |provider| to get policy values.
  void GetPoliciesFromProvider(ConfigurationPolicyProvider* provider);

  // Mapping from policy name to PolicyStatusInfo.
  PolicyStatusMap policy_map_;

  DISALLOW_COPY_AND_ASSIGN(ConfigurationPolicyStatusKeeper);
};

// ConfigurationPolicyStatusKeeper
ConfigurationPolicyStatusKeeper::ConfigurationPolicyStatusKeeper(
    ConfigurationPolicyProvider* provider) {
  GetPoliciesFromProvider(provider);
}

ConfigurationPolicyStatusKeeper::~ConfigurationPolicyStatusKeeper() {
  STLDeleteContainerPairSecondPointers(policy_map_.begin(), policy_map_.end());
  policy_map_.clear();
}

DictionaryValue* ConfigurationPolicyStatusKeeper::GetPolicyStatus(
    const char* policy) const {
  PolicyStatusMap::const_iterator entry = policy_map_.find(policy);
  return entry != policy_map_.end() ?
      (entry->second)->GetDictionaryValue() : NULL;
}

void ConfigurationPolicyStatusKeeper::GetPoliciesFromProvider(
    ConfigurationPolicyProvider* provider) {
  PolicyMap policies;
  if (!provider->Provide(&policies))
    DLOG(WARNING) << "Failed to get policy from provider.";

  PolicyErrorMap errors;
  const ConfigurationPolicyHandlerList* handler_list =
      g_browser_process->browser_policy_connector()->GetHandlerList();
  handler_list->ApplyPolicySettings(policies, NULL, &errors);
  handler_list->PrepareForDisplaying(&policies);

  PolicyMap::const_iterator policy = policies.begin();
  for ( ; policy != policies.end(); ++policy) {
    string16 name = ASCIIToUTF16(policy->first);
    const PolicyMap::Entry& entry = policy->second;
    string16 error_message = errors.GetErrors(policy->first);
    PolicyStatusInfo::PolicyStatus status =
        error_message.empty() ? PolicyStatusInfo::ENFORCED
                              : PolicyStatusInfo::FAILED;
    policy_map_[policy->first] = new PolicyStatusInfo(name,
                                                      entry.scope,
                                                      entry.level,
                                                      entry.value->DeepCopy(),
                                                      status,
                                                      error_message);
  }
}

// ConfigurationPolicyReader
ConfigurationPolicyReader::ConfigurationPolicyReader(
    ConfigurationPolicyProvider* provider)
    : provider_(provider) {
  if (provider_) {
    // Read initial policy.
    policy_keeper_.reset(new ConfigurationPolicyStatusKeeper(provider_));
    registrar_.Init(provider_, this);
  }
}

ConfigurationPolicyReader::~ConfigurationPolicyReader() {
}

void ConfigurationPolicyReader::OnUpdatePolicy(
    ConfigurationPolicyProvider* provider) {
  Refresh();
}

void ConfigurationPolicyReader::OnProviderGoingAway(
    ConfigurationPolicyProvider* provider) {
  provider_ = NULL;
}

void ConfigurationPolicyReader::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void ConfigurationPolicyReader::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

// static
ConfigurationPolicyReader*
    ConfigurationPolicyReader::CreateManagedPlatformPolicyReader() {
  BrowserPolicyConnector* connector =
      g_browser_process->browser_policy_connector();
  return new ConfigurationPolicyReader(connector->GetManagedPlatformProvider());
}

// static
ConfigurationPolicyReader*
    ConfigurationPolicyReader::CreateManagedCloudPolicyReader() {
  BrowserPolicyConnector* connector =
      g_browser_process->browser_policy_connector();
  return new ConfigurationPolicyReader(connector->GetManagedCloudProvider());
}

// static
ConfigurationPolicyReader*
    ConfigurationPolicyReader::CreateRecommendedPlatformPolicyReader() {
  BrowserPolicyConnector* connector =
      g_browser_process->browser_policy_connector();
  return new ConfigurationPolicyReader(
      connector->GetRecommendedPlatformProvider());
}

// static
ConfigurationPolicyReader*
    ConfigurationPolicyReader::CreateRecommendedCloudPolicyReader() {
  BrowserPolicyConnector* connector =
      g_browser_process->browser_policy_connector();
  return new ConfigurationPolicyReader(
      connector->GetRecommendedCloudProvider());
}

DictionaryValue* ConfigurationPolicyReader::GetPolicyStatus(
    const char* policy) const {
  if (policy_keeper_.get())
    return policy_keeper_->GetPolicyStatus(policy);
  return NULL;
}

ConfigurationPolicyReader::ConfigurationPolicyReader()
    : provider_(NULL),
      policy_keeper_(NULL) {
}

void ConfigurationPolicyReader::Refresh() {
  if (!provider_)
    return;
  policy_keeper_.reset(new ConfigurationPolicyStatusKeeper(provider_));
  FOR_EACH_OBSERVER(Observer, observers_, OnPolicyValuesChanged());
}

// PolicyStatus
PolicyStatus::PolicyStatus(ConfigurationPolicyReader* managed_platform,
                           ConfigurationPolicyReader* managed_cloud,
                           ConfigurationPolicyReader* recommended_platform,
                           ConfigurationPolicyReader* recommended_cloud)
    : managed_platform_(managed_platform),
      managed_cloud_(managed_cloud),
      recommended_platform_(recommended_platform),
      recommended_cloud_(recommended_cloud) {
}

PolicyStatus::~PolicyStatus() {
}

void PolicyStatus::AddObserver(Observer* observer) const {
  managed_platform_->AddObserver(observer);
  managed_cloud_->AddObserver(observer);
  recommended_platform_->AddObserver(observer);
  recommended_cloud_->AddObserver(observer);
}

void PolicyStatus::RemoveObserver(Observer* observer) const {
  managed_platform_->RemoveObserver(observer);
  managed_cloud_->RemoveObserver(observer);
  recommended_platform_->RemoveObserver(observer);
  recommended_cloud_->RemoveObserver(observer);
}

ListValue* PolicyStatus::GetPolicyStatusList(bool* any_policies_set) const {
  ListValue* result = new ListValue();
  std::vector<DictionaryValue*> unset_policies;

  *any_policies_set = false;
  const PolicyDefinitionList* supported_policies =
      GetChromePolicyDefinitionList();
  const PolicyDefinitionList::Entry* policy = supported_policies->begin;
  for ( ; policy != supported_policies->end; ++policy) {
    if (!AddPolicyFromReaders(policy->name, result)) {
      PolicyStatusInfo info(ASCIIToUTF16(policy->name),
                            POLICY_SCOPE_USER,
                            POLICY_LEVEL_MANDATORY,
                            Value::CreateNullValue(),
                            PolicyStatusInfo::STATUS_UNDEFINED,
                            string16());
      unset_policies.push_back(info.GetDictionaryValue());
    } else {
      *any_policies_set = true;
    }
  }

  // Add policies that weren't actually sent from providers to list.
  std::vector<DictionaryValue*>::const_iterator info = unset_policies.begin();
  for ( ; info != unset_policies.end(); ++info)
    result->Append(*info);

  return result;
}

bool PolicyStatus::AddPolicyFromReaders(
    const char* policy, ListValue* list) const {
  DictionaryValue* mp_policy =
      managed_platform_->GetPolicyStatus(policy);
  DictionaryValue* mc_policy =
      managed_cloud_->GetPolicyStatus(policy);
  DictionaryValue* rp_policy =
      recommended_platform_->GetPolicyStatus(policy);
  DictionaryValue* rc_policy =
      recommended_cloud_->GetPolicyStatus(policy);

  bool added_policy = mp_policy || mc_policy || rp_policy || rc_policy;

  if (mp_policy)
    list->Append(mp_policy);
  if (mc_policy)
    list->Append(mc_policy);
  if (rp_policy)
    list->Append(rp_policy);
  if (rc_policy)
    list->Append(rc_policy);

  return added_policy;
}

} // namespace policy
