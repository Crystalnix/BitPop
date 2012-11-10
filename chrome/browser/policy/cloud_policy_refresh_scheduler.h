// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_POLICY_CLOUD_POLICY_REFRESH_SCHEDULER_H_
#define CHROME_BROWSER_POLICY_CLOUD_POLICY_REFRESH_SCHEDULER_H_

#include <string>

#include "base/basictypes.h"
#include "base/cancelable_callback.h"
#include "base/memory/ref_counted.h"
#include "base/time.h"
#include "chrome/browser/policy/cloud_policy_client.h"
#include "chrome/browser/policy/cloud_policy_store.h"
#include "chrome/browser/prefs/pref_member.h"
#include "content/public/browser/notification_observer.h"
#include "net/base/network_change_notifier.h"

class PrefService;

namespace base {
class TaskRunner;
}

namespace policy {

// Observes CloudPolicyClient and CloudPolicyStore to trigger periodic policy
// fetches and issue retries on error conditions.
class CloudPolicyRefreshScheduler
    : public CloudPolicyClient::Observer,
      public CloudPolicyStore::Observer,
      public content::NotificationObserver,
      public net::NetworkChangeNotifier::IPAddressObserver {
 public:
  // Refresh constants.
  static const int64 kUnmanagedRefreshDelayMs;
  static const int64 kInitialErrorRetryDelayMs;

  // Refresh delay bounds.
  static const int64 kRefreshDelayMinMs;
  static const int64 kRefreshDelayMaxMs;

  // |client|, |store| and |prefs| pointers must stay valid throughout the
  // lifetime of CloudPolicyRefreshScheduler.
  CloudPolicyRefreshScheduler(
      CloudPolicyClient* client,
      CloudPolicyStore* store,
      PrefService* prefs,
      const std::string& refresh_pref,
      const scoped_refptr<base::TaskRunner>& task_runner);
  virtual ~CloudPolicyRefreshScheduler();

  // CloudPolicyClient::Observer:
  virtual void OnPolicyFetched(CloudPolicyClient* client) OVERRIDE;
  virtual void OnRegistrationStateChanged(CloudPolicyClient* client) OVERRIDE;
  virtual void OnClientError(CloudPolicyClient* client) OVERRIDE;

  // CloudPolicyStore::Observer:
  virtual void OnStoreLoaded(CloudPolicyStore* store) OVERRIDE;
  virtual void OnStoreError(CloudPolicyStore* store) OVERRIDE;

  // content::NotificationObserver:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  // net::NetworkChangeNotifier::IPAddressObserver:
  virtual void OnIPAddressChanged() OVERRIDE;

 private:
  // Initializes |last_refresh_| to the policy timestamp from |store_| in case
  // there is policy present that indicates this client is not managed. This
  // results in policy fetches only to occur after the entire unmanaged refresh
  // delay expires, even over restarts. For managed clients, we want to trigger
  // a refresh on every restart.
  void UpdateLastRefreshFromPolicy();

  // Evaluates when the next refresh is pending and updates the callback to
  // execute that refresh at the appropriate time.
  void ScheduleRefresh();

  // Triggers a policy refresh.
  void PerformRefresh();

  // Schedules a policy refresh to happen after |delta_ms| milliseconds,
  // relative to |last_refresh_|.
  void RefreshAfter(int delta_ms);

  // Gets the refresh delay in milliseconds, clamped to the allowed bounds.
  int64 GetRefreshDelay();

  CloudPolicyClient* client_;
  CloudPolicyStore* store_;

  // For scheduling delayed tasks.
  const scoped_refptr<base::TaskRunner> task_runner_;

  // The delayed refresh callback.
  base::CancelableClosure refresh_callback_;

  // The last time a refresh callback completed.
  base::Time last_refresh_;

  // Error retry delay in milliseconds.
  int64 error_retry_delay_ms_;

  IntegerPrefMember refresh_delay_;

  DISALLOW_COPY_AND_ASSIGN(CloudPolicyRefreshScheduler);
};

}  // namespace policy

#endif  // CHROME_BROWSER_POLICY_CLOUD_POLICY_REFRESH_SCHEDULER_H_
