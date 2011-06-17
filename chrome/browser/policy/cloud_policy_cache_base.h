// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_POLICY_CLOUD_POLICY_CACHE_BASE_H_
#define CHROME_BROWSER_POLICY_CLOUD_POLICY_CACHE_BASE_H_
#pragma once

#include "base/gtest_prod_util.h"
#include "base/memory/scoped_ptr.h"
#include "base/observer_list.h"
#include "base/threading/non_thread_safe.h"
#include "base/time.h"
#include "chrome/browser/policy/cloud_policy_subsystem.h"
#include "chrome/browser/policy/configuration_policy_provider.h"
#include "chrome/browser/policy/policy_map.h"
#include "chrome/browser/policy/proto/device_management_backend.pb.h"

namespace policy {

class PolicyMap;
class PolicyNotifier;

namespace em = enterprise_management;

// Caches policy information, as set by calls to |SetPolicy()|, persists
// it to disk or session_manager (depending on subclass implementation),
// and makes it available via policy providers.
class CloudPolicyCacheBase : public base::NonThreadSafe {
 public:
  // Used to distinguish mandatory from recommended policies.
  enum PolicyLevel {
    // Policy is forced upon the user and should always take effect.
    POLICY_LEVEL_MANDATORY,
    // The value is just a recommendation that the user may override.
    POLICY_LEVEL_RECOMMENDED,
  };

  CloudPolicyCacheBase();
  virtual ~CloudPolicyCacheBase();

  void set_policy_notifier(PolicyNotifier* notifier) {
    notifier_ = notifier;
  }

  // Loads persisted policy information.
  virtual void Load() = 0;

  // Resets the policy information.
  virtual void SetPolicy(const em::PolicyFetchResponse& policy) = 0;

  ConfigurationPolicyProvider* GetManagedPolicyProvider();
  ConfigurationPolicyProvider* GetRecommendedPolicyProvider();

  virtual void SetUnmanaged() = 0;
  bool is_unmanaged() const {
    return is_unmanaged_;
  }

  // Returns the time at which the policy was last fetched.
  base::Time last_policy_refresh_time() const {
    return last_policy_refresh_time_;
  }

  // Get the version of the encryption key currently used for decoding policy.
  // Returns true if the version is available, in which case |version| is filled
  // in.
  bool GetPublicKeyVersion(int* version);

 protected:
  // Wraps public key version and validity.
  struct PublicKeyVersion {
    int version;
    bool valid;
  };

  // Decodes the given |policy| using |DecodePolicyResponse()|, applies the
  // contents to |{mandatory,recommended}_policy_|, and notifies observers.
  // |timestamp| returns the timestamp embedded in |policy|, callers can pass
  // NULL if they don't care. |check_for_timestamp_validity| tells this method
  // to discard policy data with a timestamp from the future.
  // Returns true upon success.
  bool SetPolicyInternal(const em::PolicyFetchResponse& policy,
                         base::Time* timestamp,
                         bool check_for_timestamp_validity);

  void SetUnmanagedInternal(const base::Time& timestamp);

  // Decodes |policy_data|, populating |mandatory| and |recommended| with
  // the results.
  virtual bool DecodePolicyData(const em::PolicyData& policy_data,
                                PolicyMap* mandatory,
                                PolicyMap* recommended) = 0;

  // Decodes a PolicyFetchResponse into two PolicyMaps and a timestamp.
  // Also performs verification, returns NULL if any check fails.
  bool DecodePolicyResponse(const em::PolicyFetchResponse& policy_response,
                            PolicyMap* mandatory,
                            PolicyMap* recommended,
                            base::Time* timestamp,
                            PublicKeyVersion* public_key_version);

  void InformNotifier(CloudPolicySubsystem::PolicySubsystemState state,
                      CloudPolicySubsystem::ErrorDetails error_details);

  // See comment for |initialization_complete_|.
  bool initialization_complete() {
    return initialization_complete_;
  }

  void set_last_policy_refresh_time(base::Time timestamp) {
    last_policy_refresh_time_ = timestamp;
  }

 private:
  class CloudPolicyProvider;

  friend class DevicePolicyCacheTest;
  friend class UserPolicyCacheTest;

  // Policy key-value information.
  PolicyMap mandatory_policy_;
  PolicyMap recommended_policy_;

  // Policy providers.
  scoped_ptr<ConfigurationPolicyProvider> managed_policy_provider_;
  scoped_ptr<ConfigurationPolicyProvider> recommended_policy_provider_;

  PolicyNotifier* notifier_;

  // The time at which the policy was last refreshed. Is updated both upon
  // successful and unsuccessful refresh attempts.
  base::Time last_policy_refresh_time_;

  // Whether initialization has been completed. This is the case when we have
  // valid policy, learned that the device is unmanaged or ran into
  // unrecoverable errors.
  bool initialization_complete_;

  // Whether the the server has indicated this device is unmanaged.
  bool is_unmanaged_;

  // Currently used public key version, if available.
  PublicKeyVersion public_key_version_;

  // Provider observers that are registered with this cache's providers.
  ObserverList<ConfigurationPolicyProvider::Observer, true> observer_list_;

  DISALLOW_COPY_AND_ASSIGN(CloudPolicyCacheBase);
};

}  // namespace policy

#endif  // CHROME_BROWSER_POLICY_CLOUD_POLICY_CACHE_BASE_H_
