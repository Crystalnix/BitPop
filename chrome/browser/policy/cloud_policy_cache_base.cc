// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/cloud_policy_cache_base.h"

#include <string>

#include "base/logging.h"
#include "base/metrics/histogram.h"
#include "chrome/browser/policy/enterprise_metrics.h"
#include "chrome/browser/policy/policy_notifier.h"

namespace em = enterprise_management;

namespace policy {

CloudPolicyCacheBase::CloudPolicyCacheBase()
    : notifier_(NULL),
      initialization_complete_(false),
      is_unmanaged_(false),
      machine_id_missing_(false) {
  public_key_version_.version = 0;
  public_key_version_.valid = false;
}

CloudPolicyCacheBase::~CloudPolicyCacheBase() {
  FOR_EACH_OBSERVER(Observer, observer_list_, OnCacheGoingAway(this));
}

void CloudPolicyCacheBase::SetFetchingDone() {
  // NotifyObservers only fires notifications if the cache is ready.
  NotifyObservers();
}

void CloudPolicyCacheBase::AddObserver(Observer* observer) {
  observer_list_.AddObserver(observer);
}

void CloudPolicyCacheBase::RemoveObserver(Observer* observer) {
  observer_list_.RemoveObserver(observer);
}

void CloudPolicyCacheBase::Reset() {
  last_policy_refresh_time_ = base::Time();
  is_unmanaged_ = false;
  policies_.Clear();
  public_key_version_.version = 0;
  public_key_version_.valid = false;
  InformNotifier(CloudPolicySubsystem::UNENROLLED,
                 CloudPolicySubsystem::NO_DETAILS);
}

bool CloudPolicyCacheBase::IsReady() {
  return initialization_complete_;
}

bool CloudPolicyCacheBase::GetPublicKeyVersion(int* version) {
  if (public_key_version_.valid)
    *version = public_key_version_.version;

  return public_key_version_.valid;
}

bool CloudPolicyCacheBase::SetPolicyInternal(
    const em::PolicyFetchResponse& policy,
    base::Time* timestamp,
    bool check_for_timestamp_validity) {
  DCHECK(CalledOnValidThread());
  is_unmanaged_ = false;
  PolicyMap policies;
  base::Time temp_timestamp;
  PublicKeyVersion temp_public_key_version;
  bool ok = DecodePolicyResponse(policy, &policies,
                                 &temp_timestamp, &temp_public_key_version);
  if (!ok) {
    LOG(WARNING) << "Decoding policy data failed.";
    UMA_HISTOGRAM_ENUMERATION(kMetricPolicy, kMetricPolicyFetchInvalidPolicy,
                              kMetricPolicySize);
    return false;
  }
  if (timestamp) {
    *timestamp = temp_timestamp;
  }
  if (check_for_timestamp_validity &&
      temp_timestamp > base::Time::NowFromSystemTime()) {
    LOG(WARNING) << "Rejected policy data, file is from the future.";
    UMA_HISTOGRAM_ENUMERATION(kMetricPolicy,
                              kMetricPolicyFetchTimestampInFuture,
                              kMetricPolicySize);
    return false;
  }
  public_key_version_.version = temp_public_key_version.version;
  public_key_version_.valid = temp_public_key_version.valid;

  if (policies_.Equals(policies)) {
    UMA_HISTOGRAM_ENUMERATION(kMetricPolicy, kMetricPolicyFetchNotModified,
                              kMetricPolicySize);
  }

  policies_.Swap(&policies);

  InformNotifier(CloudPolicySubsystem::SUCCESS,
                 CloudPolicySubsystem::NO_DETAILS);
  return true;
}

void CloudPolicyCacheBase::SetUnmanagedInternal(const base::Time& timestamp) {
  is_unmanaged_ = true;
  public_key_version_.valid = false;
  policies_.Clear();
  last_policy_refresh_time_ = timestamp;
}

void CloudPolicyCacheBase::SetReady() {
  initialization_complete_ = true;
  NotifyObservers();
}

bool CloudPolicyCacheBase::DecodePolicyResponse(
    const em::PolicyFetchResponse& policy_response,
    PolicyMap* policies,
    base::Time* timestamp,
    PublicKeyVersion* public_key_version) {
  std::string data = policy_response.policy_data();
  em::PolicyData policy_data;
  if (!policy_data.ParseFromString(data)) {
    LOG(WARNING) << "Failed to parse PolicyData protobuf.";
    return false;
  }
  if (timestamp) {
    *timestamp = base::Time::UnixEpoch() +
                 base::TimeDelta::FromMilliseconds(policy_data.timestamp());
  }
  if (public_key_version) {
    public_key_version->valid = policy_data.has_public_key_version();
    if (public_key_version->valid)
      public_key_version->version = policy_data.public_key_version();
  }
  machine_id_missing_ = policy_data.valid_serial_number_missing();

  return DecodePolicyData(policy_data, policies);
}

void CloudPolicyCacheBase::NotifyObservers() {
  if (IsReady())
    FOR_EACH_OBSERVER(Observer, observer_list_, OnCacheUpdate(this));
}

void CloudPolicyCacheBase::InformNotifier(
    CloudPolicySubsystem::PolicySubsystemState state,
    CloudPolicySubsystem::ErrorDetails error_details) {
  // TODO(jkummerow): To obsolete this NULL-check, make all uses of
  // UserPolicyCache explicitly set a notifier using |set_policy_notifier()|.
  if (notifier_)
    notifier_->Inform(state, error_details, PolicyNotifier::POLICY_CACHE);
}

}  // namespace policy
