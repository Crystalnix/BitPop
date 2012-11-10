// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/cloud_policy_refresh_scheduler.h"

#include <algorithm>

#include "base/task_runner.h"
#include "chrome/browser/policy/cloud_policy_constants.h"
#include "chrome/common/chrome_notification_types.h"
#include "content/public/browser/notification_details.h"

namespace policy {

const int64 CloudPolicyRefreshScheduler::kUnmanagedRefreshDelayMs =
    24 * 60 * 60 * 1000;  // 1 day.
const int64 CloudPolicyRefreshScheduler::kInitialErrorRetryDelayMs =
    5 * 60 * 1000;  // 5 minutes.
const int64 CloudPolicyRefreshScheduler::kRefreshDelayMinMs =
    30 * 60 * 1000;  // 30 minutes.
const int64 CloudPolicyRefreshScheduler::kRefreshDelayMaxMs =
    24 * 60 * 60 * 1000;  // 1 day.

CloudPolicyRefreshScheduler::CloudPolicyRefreshScheduler(
    CloudPolicyClient* client,
    CloudPolicyStore* store,
    PrefService* prefs,
    const std::string& refresh_pref,
    const scoped_refptr<base::TaskRunner>& task_runner)
    : client_(client),
      store_(store),
      task_runner_(task_runner),
      error_retry_delay_ms_(kInitialErrorRetryDelayMs) {
  client_->AddObserver(this);
  store_->AddObserver(this);
  net::NetworkChangeNotifier::AddIPAddressObserver(this);

  refresh_delay_.Init(refresh_pref.c_str(), prefs, this);

  UpdateLastRefreshFromPolicy();
  ScheduleRefresh();
}

CloudPolicyRefreshScheduler::~CloudPolicyRefreshScheduler() {
  store_->RemoveObserver(this);
  client_->RemoveObserver(this);
  net::NetworkChangeNotifier::RemoveIPAddressObserver(this);
}

void CloudPolicyRefreshScheduler::OnPolicyFetched(CloudPolicyClient* client) {
  error_retry_delay_ms_ = kInitialErrorRetryDelayMs;

  // Schedule the next refresh.
  last_refresh_ = base::Time::NowFromSystemTime();
  ScheduleRefresh();
}

void CloudPolicyRefreshScheduler::OnRegistrationStateChanged(
    CloudPolicyClient* client) {
  error_retry_delay_ms_ = kInitialErrorRetryDelayMs;

  // The client might have registered, so trigger an immediate refresh.
  last_refresh_ = base::Time();
  ScheduleRefresh();
}

void CloudPolicyRefreshScheduler::OnClientError(CloudPolicyClient* client) {
  // Save the status for below.
  DeviceManagementStatus status = client_->status();

  // Schedule an error retry if applicable.
  last_refresh_ = base::Time::NowFromSystemTime();
  ScheduleRefresh();

  // Update the retry delay.
  if (client->is_registered() &&
      (status == DM_STATUS_REQUEST_FAILED ||
       status == DM_STATUS_TEMPORARY_UNAVAILABLE)) {
    error_retry_delay_ms_ = std::min(error_retry_delay_ms_ * 2,
                                     GetRefreshDelay());
  } else {
    error_retry_delay_ms_ = kInitialErrorRetryDelayMs;
  }
}

void CloudPolicyRefreshScheduler::OnStoreLoaded(CloudPolicyStore* store) {
  UpdateLastRefreshFromPolicy();

  // Re-schedule the next refresh in case the is_managed bit changed.
  ScheduleRefresh();
}

void CloudPolicyRefreshScheduler::OnStoreError(CloudPolicyStore* store) {
  // If |store_| fails, the is_managed bit that it provides may become stale.
  // The best guess in that situation is to assume is_managed didn't change and
  // continue using the stale information. Thus, no specific response to a store
  // error is required. NB: Changes to is_managed fire OnStoreLoaded().
}

void CloudPolicyRefreshScheduler::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  DCHECK_EQ(chrome::NOTIFICATION_PREF_CHANGED, type);
  DCHECK_EQ(refresh_delay_.GetPrefName(),
            *content::Details<std::string>(details).ptr());

  ScheduleRefresh();
}

void CloudPolicyRefreshScheduler::OnIPAddressChanged() {
  if (client_->status() == DM_STATUS_REQUEST_FAILED)
    RefreshAfter(0);
}

void CloudPolicyRefreshScheduler::UpdateLastRefreshFromPolicy() {
  if (store_->has_policy() && !store_->is_managed() &&
      last_refresh_.is_null()) {
    last_refresh_ =
        base::Time::UnixEpoch() +
        base::TimeDelta::FromMilliseconds(store_->policy()->timestamp());
  }
}

void CloudPolicyRefreshScheduler::ScheduleRefresh() {
  // If the client isn't registered, there is nothing to do.
  if (!client_->is_registered()) {
    refresh_callback_.Cancel();
    return;
  }

  // If there is a registration, go by the client's status. That will tell us
  // what the appropriate refresh delay should be.
  switch (client_->status()) {
    case DM_STATUS_SUCCESS:
      if (store_->is_managed())
        RefreshAfter(GetRefreshDelay());
      else
        RefreshAfter(kUnmanagedRefreshDelayMs);
      return;
    case DM_STATUS_SERVICE_ACTIVATION_PENDING:
    case DM_STATUS_SERVICE_POLICY_NOT_FOUND:
      RefreshAfter(GetRefreshDelay());
      return;
    case DM_STATUS_REQUEST_FAILED:
    case DM_STATUS_TEMPORARY_UNAVAILABLE:
      RefreshAfter(error_retry_delay_ms_);
      return;
    case DM_STATUS_REQUEST_INVALID:
    case DM_STATUS_HTTP_STATUS_ERROR:
    case DM_STATUS_RESPONSE_DECODING_ERROR:
    case DM_STATUS_SERVICE_MANAGEMENT_NOT_SUPPORTED:
      RefreshAfter(kUnmanagedRefreshDelayMs);
      return;
    case DM_STATUS_SERVICE_MANAGEMENT_TOKEN_INVALID:
    case DM_STATUS_SERVICE_DEVICE_NOT_FOUND:
    case DM_STATUS_SERVICE_INVALID_SERIAL_NUMBER:
    case DM_STATUS_SERVICE_DEVICE_ID_CONFLICT:
    case DM_STATUS_MISSING_LICENSES:
      // Need a re-registration, no use in retrying.
      return;
  }

  NOTREACHED() << "Invalid client status " << client_->status();
  RefreshAfter(kUnmanagedRefreshDelayMs);
}

void CloudPolicyRefreshScheduler::PerformRefresh() {
  if (client_->is_registered()) {
    // Update |last_refresh_| so another fetch isn't triggered inadvertently.
    last_refresh_ = base::Time::NowFromSystemTime();

    // The result of this operation will be reported through a callback, at
    // which point the next refresh will be scheduled.
    client_->FetchPolicy();
    return;
  }

  // This should never happen, as the registration change should have been
  // handled via OnRegistrationStateChanged().
  NOTREACHED();
}

void CloudPolicyRefreshScheduler::RefreshAfter(int delta_ms) {
  base::TimeDelta delta(base::TimeDelta::FromMilliseconds(delta_ms));
  refresh_callback_.Cancel();

  // Schedule the callback.
  base::TimeDelta delay =
      std::max((last_refresh_ + delta) - base::Time::NowFromSystemTime(),
               base::TimeDelta());
  refresh_callback_.Reset(
      base::Bind(&CloudPolicyRefreshScheduler::PerformRefresh,
                 base::Unretained(this)));
  task_runner_->PostDelayedTask(FROM_HERE, refresh_callback_.callback(), delay);
}

int64 CloudPolicyRefreshScheduler::GetRefreshDelay() {
  return std::min(std::max<int64>(refresh_delay_.GetValue(),
                                  kRefreshDelayMinMs),
                  kRefreshDelayMaxMs);
}

}  // namespace policy
