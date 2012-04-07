// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_POLICY_CLOUD_POLICY_CONTROLLER_H_
#define CHROME_BROWSER_POLICY_CLOUD_POLICY_CONTROLLER_H_
#pragma once

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/policy/cloud_policy_constants.h"
#include "chrome/browser/policy/cloud_policy_data_store.h"

namespace enterprise_management {
class DeviceManagementResponse;
}

namespace policy {

class CloudPolicyCacheBase;
class DelayedWorkScheduler;
class DeviceManagementRequestJob;
class DeviceManagementService;
class DeviceTokenFetcher;
class PolicyNotifier;

// Coordinates the actions of DeviceTokenFetcher, CloudPolicyDataStore,
// and CloudPolicyCache: calls their methods and listens to their
// callbacks/notifications.
class CloudPolicyController : public CloudPolicyDataStore::Observer {
 public:
  // All parameters are weak pointers.
  CloudPolicyController(DeviceManagementService* service,
                        CloudPolicyCacheBase* cache,
                        DeviceTokenFetcher* token_fetcher,
                        CloudPolicyDataStore* data_store,
                        PolicyNotifier* notifier);
  virtual ~CloudPolicyController();

  // Sets the refresh rate at which to re-fetch policy information.
  void SetRefreshRate(int64 refresh_rate_milliseconds);

  // Triggers an immediate retry of the current operation.
  void Retry();

  // Stops any pending activity and resets the controller to unenrolled state.
  void Reset();

  // Attempts to fetch policies again, if possible. The cache is notified that
  // a fetch was attempted.
  void RefreshPolicies();

  // Policy request response handler.
  void OnPolicyFetchCompleted(
      DeviceManagementStatus status,
      const enterprise_management::DeviceManagementResponse& response);

  // CloudPolicyDataStore::Observer implementation:
  virtual void OnDeviceTokenChanged() OVERRIDE;
  virtual void OnCredentialsChanged() OVERRIDE;

 private:
  // Indicates the current state the controller is in.
  enum ControllerState {
    // The controller is initializing, policy information not yet available.
    STATE_TOKEN_UNAVAILABLE,
    // The device is not managed. Should retry fetching the token after delay.
    STATE_TOKEN_UNMANAGED,
    // The token is not valid and should be refetched with exponential back-off.
    STATE_TOKEN_ERROR,
    // The token is valid, but policy is yet to be fetched.
    STATE_TOKEN_VALID,
    // Policy information is available and valid.
    STATE_POLICY_VALID,
    // The service returned an error when requesting policy, will retry.
    STATE_POLICY_ERROR,
    // The service returned an error that is not going to go away soon.
    STATE_POLICY_UNAVAILABLE
  };

  friend class CloudPolicyControllerTest;
  friend class TestingCloudPolicySubsystem;

  // More configurable constructor for use by test cases.
  // Takes ownership of |scheduler|; the other parameters are weak pointers.
  CloudPolicyController(DeviceManagementService* service,
                        CloudPolicyCacheBase* cache,
                        DeviceTokenFetcher* token_fetcher,
                        CloudPolicyDataStore* data_store,
                        PolicyNotifier* notifier,
                        DelayedWorkScheduler* scheduler);

  // Called by constructors to perform shared initialization.
  void Initialize(DeviceManagementService* service,
                  CloudPolicyCacheBase* cache,
                  DeviceTokenFetcher* token_fetcher,
                  CloudPolicyDataStore* data_store,
                  PolicyNotifier* notifier,
                  DelayedWorkScheduler* scheduler);

  // Checks if the controller is ready to fetch the DMToken.
  bool ReadyToFetchToken();

  // Asks the token fetcher to fetch a new token.
  void FetchToken();

  // Sends a request to the device management backend to fetch policy if one
  // isn't already outstanding.
  void SendPolicyRequest();

  // Called back from |scheduler_|.
  // Performs whatever action is required in the current state,
  // e.g. refreshing policy.
  void DoWork();

  // Switches to a new state and triggers any appropriate actions.
  void SetState(ControllerState new_state);

  // Computes the policy refresh delay to use.
  int64 GetRefreshDelay();

  DeviceManagementService* service_;
  CloudPolicyCacheBase* cache_;
  CloudPolicyDataStore* data_store_;
  DeviceTokenFetcher* token_fetcher_;
  scoped_ptr<DeviceManagementRequestJob> request_job_;
  ControllerState state_;
  PolicyNotifier* notifier_;

  int64 policy_refresh_rate_ms_;
  int64 effective_policy_refresh_error_delay_ms_;

  scoped_ptr<DelayedWorkScheduler> scheduler_;

  DISALLOW_COPY_AND_ASSIGN(CloudPolicyController);
};

}  // namespace policy

#endif  // CHROME_BROWSER_POLICY_CLOUD_POLICY_CONTROLLER_H_
