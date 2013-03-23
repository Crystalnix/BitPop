// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_POLICY_CLOUD_POLICY_CACHE_BASE_H_
#define CHROME_BROWSER_POLICY_CLOUD_POLICY_CACHE_BASE_H_

#include "base/observer_list.h"
#include "base/threading/non_thread_safe.h"
#include "base/time.h"
#include "chrome/browser/policy/cloud_policy_subsystem.h"
#include "chrome/browser/policy/policy_map.h"
#include "chrome/browser/policy/proto/device_management_backend.pb.h"

namespace policy {

class PolicyNotifier;

// Caches policy information, as set by calls to |SetPolicy()|, persists
// it to disk or session_manager (depending on subclass implementation),
// and makes it available via policy providers.
class CloudPolicyCacheBase : public base::NonThreadSafe {
 public:
  class Observer {
   public:
    virtual ~Observer() {}
    virtual void OnCacheUpdate(CloudPolicyCacheBase*) = 0;
  };

  CloudPolicyCacheBase();
  virtual ~CloudPolicyCacheBase();

  void set_policy_notifier(PolicyNotifier* notifier) {
    notifier_ = notifier;
  }

  // Loads persisted policy information.
  virtual void Load() = 0;

  // Resets the policy information. Returns true if |policy| was accepted and
  // stored.
  virtual bool SetPolicy(
      const enterprise_management::PolicyFetchResponse& policy) = 0;

  virtual void SetUnmanaged() = 0;

  // Invoked whenever an attempt to fetch policy has been completed. The fetch
  // may or may not have suceeded. This can be triggered by failed attempts to
  // fetch oauth tokens, register with dmserver or fetch policy.
  virtual void SetFetchingDone();

  bool is_unmanaged() const {
    return is_unmanaged_;
  }

  // Returns the time at which the policy was last fetched.
  base::Time last_policy_refresh_time() const {
    return last_policy_refresh_time_;
  }

  bool machine_id_missing() const {
    return machine_id_missing_;
  }

  // Get the version of the encryption key currently used for decoding policy.
  // Returns true if the version is available, in which case |version| is filled
  // in.
  bool GetPublicKeyVersion(int* version);

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // Accessor for the underlying PolicyMap.
  const PolicyMap* policy() { return &policies_; }

  // Resets the cache, clearing the policy currently stored in memory and the
  // last refresh time.
  void Reset();

  // true if the cache contains data that is ready to be served as policies.
  // This usually means that the local policy storage has been loaded.
  // Note that Profile creation will block until the cache is ready.
  // On enrolled devices and for users of the enrolled domain, the cache only
  // becomes ready after a user policy fetch is completed.
  bool IsReady();

 protected:
  // Wraps public key version and validity.
  struct PublicKeyVersion {
    int version;
    bool valid;
  };

  // Decodes the given |policy| using |DecodePolicyResponse()|, applies the
  // contents to |policies_|, and notifies observers.
  // |timestamp| returns the timestamp embedded in |policy|, callers can pass
  // NULL if they don't care. |check_for_timestamp_validity| tells this method
  // to discard policy data with a timestamp from the future.
  // Returns true upon success.
  bool SetPolicyInternal(
      const enterprise_management::PolicyFetchResponse& policy,
      base::Time* timestamp,
      bool check_for_timestamp_validity);

  void SetUnmanagedInternal(const base::Time& timestamp);

  // Indicates that initialization is now complete. Observers will be notified.
  void SetReady();

  // Decodes |policy_data|, populating |mandatory| and |recommended| with
  // the results.
  virtual bool DecodePolicyData(
      const enterprise_management::PolicyData& policy_data,
      PolicyMap* policies) = 0;

  // Decodes a PolicyFetchResponse into two PolicyMaps and a timestamp.
  // Also performs verification, returns NULL if any check fails.
  bool DecodePolicyResponse(
      const enterprise_management::PolicyFetchResponse& policy_response,
      PolicyMap* policies,
      base::Time* timestamp,
      PublicKeyVersion* public_key_version);

  // Notifies observers if the cache IsReady().
  void NotifyObservers();

  void InformNotifier(CloudPolicySubsystem::PolicySubsystemState state,
                      CloudPolicySubsystem::ErrorDetails error_details);

  void set_last_policy_refresh_time(base::Time timestamp) {
    last_policy_refresh_time_ = timestamp;
  }

 private:
  friend class DevicePolicyCacheTest;
  friend class UserPolicyCacheTest;
  friend class MockCloudPolicyCache;

  // Policy key-value information.
  PolicyMap policies_;

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

  // Flag indicating whether the server claims that a valid machine identifier
  // is missing on the server side. Read directly from the policy blob.
  bool machine_id_missing_;

  // Currently used public key version, if available.
  PublicKeyVersion public_key_version_;

  // Cache observers that are registered with this cache.
  ObserverList<Observer, true> observer_list_;

  DISALLOW_COPY_AND_ASSIGN(CloudPolicyCacheBase);
};

}  // namespace policy

#endif  // CHROME_BROWSER_POLICY_CLOUD_POLICY_CACHE_BASE_H_
