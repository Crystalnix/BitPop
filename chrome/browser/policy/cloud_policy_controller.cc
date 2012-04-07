// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/cloud_policy_controller.h"

#include <algorithm>
#include <string>

#include "base/bind.h"
#include "base/logging.h"
#include "base/metrics/histogram.h"
#include "base/rand_util.h"
#include "base/string_util.h"
#include "base/time.h"
#include "chrome/browser/policy/cloud_policy_cache_base.h"
#include "chrome/browser/policy/cloud_policy_constants.h"
#include "chrome/browser/policy/cloud_policy_subsystem.h"
#include "chrome/browser/policy/delayed_work_scheduler.h"
#include "chrome/browser/policy/device_management_service.h"
#include "chrome/browser/policy/device_token_fetcher.h"
#include "chrome/browser/policy/enterprise_metrics.h"
#include "chrome/browser/policy/policy_notifier.h"
#include "chrome/common/guid.h"

namespace policy {

namespace {

// The maximum ratio in percent of the policy refresh rate we use for adjusting
// the policy refresh time instant. The rationale is to avoid load spikes from
// many devices that were set up in sync for some reason.
const int kPolicyRefreshDeviationFactorPercent = 10;
// Maximum deviation we are willing to accept.
const int64 kPolicyRefreshDeviationMaxInMilliseconds = 30 * 60 * 1000;

// These are the base values for delays before retrying after an error. They
// will be doubled each time they are used.
const int64 kPolicyRefreshErrorDelayInMilliseconds =
    5 * 60 * 1000;  // 5 minutes.

// Default value for the policy refresh rate.
const int kPolicyRefreshRateInMilliseconds = 3 * 60 * 60 * 1000;  // 3 hours.

// Domain names that are known not to be managed.
// We don't register the device when such a user logs in.
const char* kNonManagedDomains[] = {
  "@googlemail.com",
  "@gmail.com"
};

// Checks the domain part of the given username against the list of known
// non-managed domain names. Returns false if |username| is empty or
// in a domain known not to be managed.
bool CanBeInManagedDomain(const std::string& username) {
  if (username.empty()) {
    // This means incognito user in case of ChromiumOS and
    // no logged-in user in case of Chromium (SigninService).
    return false;
  }
  for (size_t i = 0; i < arraysize(kNonManagedDomains); i++) {
    if (EndsWith(username, kNonManagedDomains[i], true)) {
      return false;
    }
  }
  return true;
}

// Records the UMA metric corresponding to |status|, if it represents an error.
// Also records that a fetch response was received.
void SampleErrorStatus(DeviceManagementStatus status) {
  UMA_HISTOGRAM_ENUMERATION(kMetricPolicy,
                            kMetricPolicyFetchResponseReceived,
                            kMetricPolicySize);
  int sample = -1;
  switch (status) {
    case DM_STATUS_SUCCESS:
      return;
    case DM_STATUS_SERVICE_POLICY_NOT_FOUND:
      sample = kMetricPolicyFetchNotFound;
      break;
    case DM_STATUS_SERVICE_DEVICE_NOT_FOUND:
      sample = kMetricPolicyFetchInvalidToken;
      break;
    case DM_STATUS_RESPONSE_DECODING_ERROR:
      sample = kMetricPolicyFetchBadResponse;
      break;
    case DM_STATUS_REQUEST_FAILED:
    case DM_STATUS_REQUEST_INVALID:
    case DM_STATUS_SERVICE_MANAGEMENT_TOKEN_INVALID:
      sample = kMetricPolicyFetchRequestFailed;
      break;
    case DM_STATUS_SERVICE_MANAGEMENT_NOT_SUPPORTED:
    case DM_STATUS_SERVICE_DEVICE_ID_CONFLICT:
    case DM_STATUS_SERVICE_INVALID_SERIAL_NUMBER:
    case DM_STATUS_TEMPORARY_UNAVAILABLE:
    case DM_STATUS_SERVICE_ACTIVATION_PENDING:
    case DM_STATUS_HTTP_STATUS_ERROR:
      sample = kMetricPolicyFetchServerFailed;
      break;
  }
  if (sample != -1)
    UMA_HISTOGRAM_ENUMERATION(kMetricPolicy, sample, kMetricPolicySize);
  else
    NOTREACHED();
}

}  // namespace

namespace em = enterprise_management;

CloudPolicyController::CloudPolicyController(
    DeviceManagementService* service,
    CloudPolicyCacheBase* cache,
    DeviceTokenFetcher* token_fetcher,
    CloudPolicyDataStore* data_store,
    PolicyNotifier* notifier) {
  Initialize(service,
             cache,
             token_fetcher,
             data_store,
             notifier,
             new DelayedWorkScheduler);
}

CloudPolicyController::~CloudPolicyController() {
  data_store_->RemoveObserver(this);
  scheduler_->CancelDelayedWork();
}

void CloudPolicyController::SetRefreshRate(int64 refresh_rate_milliseconds) {
  policy_refresh_rate_ms_ = refresh_rate_milliseconds;

  // Reschedule the refresh task if necessary.
  if (state_ == STATE_POLICY_VALID)
    SetState(STATE_POLICY_VALID);
}

void CloudPolicyController::Retry() {
  scheduler_->CancelDelayedWork();
  DoWork();
}

void CloudPolicyController::Reset() {
  SetState(STATE_TOKEN_UNAVAILABLE);
}

void CloudPolicyController::RefreshPolicies() {
  // This call must eventually trigger a notification to the cache.
  if (data_store_->device_token().empty()) {
    // The DMToken has to be fetched.
    if (ReadyToFetchToken()) {
      SetState(STATE_TOKEN_UNAVAILABLE);
    } else {
      // The controller doesn't have enough material to start a token fetch,
      // but observers of the cache are waiting for the refresh.
      SetState(STATE_TOKEN_UNMANAGED);
    }
  } else {
    // The token is valid, so the next step is to fetch policy.
    SetState(STATE_TOKEN_VALID);
  }
}

void CloudPolicyController::OnPolicyFetchCompleted(
    DeviceManagementStatus status,
    const em::DeviceManagementResponse& response) {
  if (status == DM_STATUS_SUCCESS && !response.has_policy_response()) {
    // Handled below.
    status = DM_STATUS_RESPONSE_DECODING_ERROR;
  }

  SampleErrorStatus(status);

  switch (status) {
    case DM_STATUS_SUCCESS: {
      const em::DevicePolicyResponse& policy_response(
          response.policy_response());
      if (policy_response.response_size() > 0) {
        if (policy_response.response_size() > 1) {
          LOG(WARNING) << "More than one policy in the response of the device "
                       << "management server, discarding.";
        }
        const em::PolicyFetchResponse& fetch_response(
            policy_response.response(0));
        if (!fetch_response.has_error_code() ||
            fetch_response.error_code() == dm_protocol::POLICY_FETCH_SUCCESS) {
          cache_->SetPolicy(fetch_response);
          SetState(STATE_POLICY_VALID);
        } else {
          UMA_HISTOGRAM_ENUMERATION(kMetricPolicy,
                                    kMetricPolicyFetchBadResponse,
                                    kMetricPolicySize);
          SetState(STATE_POLICY_UNAVAILABLE);
        }
      } else {
        UMA_HISTOGRAM_ENUMERATION(kMetricPolicy, kMetricPolicyFetchBadResponse,
                                  kMetricPolicySize);
        SetState(STATE_POLICY_UNAVAILABLE);
      }
      return;
    }
    case DM_STATUS_SERVICE_DEVICE_NOT_FOUND:
    case DM_STATUS_SERVICE_DEVICE_ID_CONFLICT:
    case DM_STATUS_SERVICE_MANAGEMENT_TOKEN_INVALID:
      LOG(WARNING) << "The device token was either invalid or unknown to the "
                   << "device manager, re-registering device.";
      // Will retry fetching a token but gracefully backing off.
      SetState(STATE_TOKEN_ERROR);
      return;
    case DM_STATUS_SERVICE_INVALID_SERIAL_NUMBER:
      VLOG(1) << "The device is no longer enlisted for the domain.";
      token_fetcher_->SetSerialNumberInvalidState();
      SetState(STATE_TOKEN_ERROR);
      return;
    case DM_STATUS_SERVICE_MANAGEMENT_NOT_SUPPORTED:
      VLOG(1) << "The device is no longer managed.";
      token_fetcher_->SetUnmanagedState();
      SetState(STATE_TOKEN_UNMANAGED);
      return;
    case DM_STATUS_SERVICE_POLICY_NOT_FOUND:
    case DM_STATUS_REQUEST_INVALID:
    case DM_STATUS_SERVICE_ACTIVATION_PENDING:
    case DM_STATUS_RESPONSE_DECODING_ERROR:
    case DM_STATUS_HTTP_STATUS_ERROR:
      VLOG(1) << "An error in the communication with the policy server occurred"
              << ", will retry in a few hours.";
      SetState(STATE_POLICY_UNAVAILABLE);
      return;
    case DM_STATUS_REQUEST_FAILED:
    case DM_STATUS_TEMPORARY_UNAVAILABLE:
      VLOG(1) << "A temporary error in the communication with the policy server"
              << " occurred.";
      // Will retry last operation but gracefully backing off.
      SetState(STATE_POLICY_ERROR);
      return;
  }

  NOTREACHED();
  SetState(STATE_POLICY_ERROR);
}

void CloudPolicyController::OnDeviceTokenChanged() {
  if (data_store_->device_token().empty())
    SetState(STATE_TOKEN_UNAVAILABLE);
  else
    SetState(STATE_TOKEN_VALID);
}

void CloudPolicyController::OnCredentialsChanged() {
  // This notification is only interesting if we don't have a device token.
  // If we already have a device token, that must be matching the current
  // user, because (1) we always recreate the policy subsystem after user
  // login (2) tokens are cached per user.
  if (data_store_->device_token().empty()) {
    notifier_->Inform(CloudPolicySubsystem::UNENROLLED,
                      CloudPolicySubsystem::NO_DETAILS,
                      PolicyNotifier::POLICY_CONTROLLER);
    effective_policy_refresh_error_delay_ms_ =
        kPolicyRefreshErrorDelayInMilliseconds;
    SetState(STATE_TOKEN_UNAVAILABLE);
  }
}

CloudPolicyController::CloudPolicyController(
    DeviceManagementService* service,
    CloudPolicyCacheBase* cache,
    DeviceTokenFetcher* token_fetcher,
    CloudPolicyDataStore* data_store,
    PolicyNotifier* notifier,
    DelayedWorkScheduler* scheduler) {
  Initialize(service,
             cache,
             token_fetcher,
             data_store,
             notifier,
             scheduler);
}

void CloudPolicyController::Initialize(
    DeviceManagementService* service,
    CloudPolicyCacheBase* cache,
    DeviceTokenFetcher* token_fetcher,
    CloudPolicyDataStore* data_store,
    PolicyNotifier* notifier,
    DelayedWorkScheduler* scheduler) {
  DCHECK(cache);

  service_ = service;
  cache_ = cache;
  token_fetcher_ = token_fetcher;
  data_store_ = data_store;
  notifier_ = notifier;
  state_ = STATE_TOKEN_UNAVAILABLE;
  policy_refresh_rate_ms_ = kPolicyRefreshRateInMilliseconds;
  effective_policy_refresh_error_delay_ms_ =
      kPolicyRefreshErrorDelayInMilliseconds;
  scheduler_.reset(scheduler);
  data_store_->AddObserver(this);
  if (!data_store_->device_token().empty())
    SetState(STATE_TOKEN_VALID);
  else
    SetState(STATE_TOKEN_UNAVAILABLE);
}

bool CloudPolicyController::ReadyToFetchToken() {
  return data_store_->token_cache_loaded() &&
         !data_store_->user_name().empty() &&
         data_store_->has_auth_token();
}

void CloudPolicyController::FetchToken() {
  if (ReadyToFetchToken()) {
    if (CanBeInManagedDomain(data_store_->user_name())) {
      // Generate a new random device id. (It'll only be kept if registration
      // succeeds.)
      data_store_->set_device_id(guid::GenerateGUID());
      token_fetcher_->FetchToken();
    } else {
      SetState(STATE_TOKEN_UNMANAGED);
    }
  } else {
    VLOG(1) << "Not ready to fetch DMToken yet, will try again later.";
  }
}

void CloudPolicyController::SendPolicyRequest() {
  DCHECK(!data_store_->device_token().empty());
  request_job_.reset(
      service_->CreateJob(DeviceManagementRequestJob::TYPE_POLICY_FETCH));
  request_job_->SetDMToken(data_store_->device_token());
  request_job_->SetClientID(data_store_->device_id());
  request_job_->SetUserAffiliation(data_store_->user_affiliation());

  em::DeviceManagementRequest* request = request_job_->GetRequest();
  em::PolicyFetchRequest* fetch_request =
      request->mutable_policy_request()->add_request();
  em::DeviceStatusReportRequest device_status;
  fetch_request->set_signature_type(em::PolicyFetchRequest::SHA1_RSA);
  fetch_request->set_policy_type(data_store_->policy_type());
  if (cache_->machine_id_missing() && !data_store_->machine_id().empty())
    fetch_request->set_machine_id(data_store_->machine_id());
  if (!cache_->is_unmanaged() &&
      !cache_->last_policy_refresh_time().is_null()) {
    base::TimeDelta timestamp =
        cache_->last_policy_refresh_time() - base::Time::UnixEpoch();
    fetch_request->set_timestamp(timestamp.InMilliseconds());
  }
  int key_version = 0;
  if (cache_->GetPublicKeyVersion(&key_version))
    fetch_request->set_public_key_version(key_version);

#if defined(OS_CHROMEOS)
  if (data_store_->device_status_collector()) {
    data_store_->device_status_collector()->GetStatus(
        request->mutable_device_status_report_request());
  }
#endif

  request_job_->Start(base::Bind(&CloudPolicyController::OnPolicyFetchCompleted,
                                 base::Unretained(this)));
  UMA_HISTOGRAM_ENUMERATION(kMetricPolicy, kMetricPolicyFetchRequested,
                            kMetricPolicySize);
}

void CloudPolicyController::DoWork() {
  switch (state_) {
    case STATE_TOKEN_UNAVAILABLE:
    case STATE_TOKEN_ERROR:
      FetchToken();
      return;
    case STATE_TOKEN_VALID:
    case STATE_POLICY_VALID:
    case STATE_POLICY_ERROR:
    case STATE_POLICY_UNAVAILABLE:
      SendPolicyRequest();
      return;
    case STATE_TOKEN_UNMANAGED:
      return;
  }

  NOTREACHED() << "Unhandled state" << state_;
}

void CloudPolicyController::SetState(
    CloudPolicyController::ControllerState new_state) {
  state_ = new_state;

  request_job_.reset();  // Stop any pending requests.

  base::Time now(base::Time::NowFromSystemTime());
  base::Time refresh_at;
  base::Time last_refresh(cache_->last_policy_refresh_time());
  if (last_refresh.is_null())
    last_refresh = now;

  // Determine when to take the next step.
  bool inform_notifier_done = false;
  switch (state_) {
    case STATE_TOKEN_UNMANAGED:
      notifier_->Inform(CloudPolicySubsystem::UNMANAGED,
                        CloudPolicySubsystem::NO_DETAILS,
                        PolicyNotifier::POLICY_CONTROLLER);
      break;
    case STATE_TOKEN_UNAVAILABLE:
      // The controller is not yet initialized and needs to immediately fetch
      // token and policy if present.
    case STATE_TOKEN_VALID:
      // Immediately try to fetch the token on initialization or policy after a
      // token update. Subsequent retries will respect the back-off strategy.
      refresh_at = now;
      // |notifier_| isn't informed about anything at this point, we wait for
      // the result of the next action first.
      break;
    case STATE_POLICY_VALID:
      // Delay is only reset if the policy fetch operation was successful. This
      // will ensure the server won't get overloaded with retries in case of
      // a bug on either side.
      effective_policy_refresh_error_delay_ms_ =
          kPolicyRefreshErrorDelayInMilliseconds;
      refresh_at =
          last_refresh + base::TimeDelta::FromMilliseconds(GetRefreshDelay());
      notifier_->Inform(CloudPolicySubsystem::SUCCESS,
                        CloudPolicySubsystem::NO_DETAILS,
                        PolicyNotifier::POLICY_CONTROLLER);
      break;
    case STATE_TOKEN_ERROR:
      notifier_->Inform(CloudPolicySubsystem::NETWORK_ERROR,
                        CloudPolicySubsystem::BAD_DMTOKEN,
                        PolicyNotifier::POLICY_CONTROLLER);
      inform_notifier_done = true;
    case STATE_POLICY_ERROR:
      if (!inform_notifier_done) {
        notifier_->Inform(CloudPolicySubsystem::NETWORK_ERROR,
                          CloudPolicySubsystem::POLICY_NETWORK_ERROR,
                          PolicyNotifier::POLICY_CONTROLLER);
      }
      refresh_at = now + base::TimeDelta::FromMilliseconds(
                             effective_policy_refresh_error_delay_ms_);
      effective_policy_refresh_error_delay_ms_ =
          std::min(effective_policy_refresh_error_delay_ms_ * 2,
                   policy_refresh_rate_ms_);
      break;
    case STATE_POLICY_UNAVAILABLE:
      effective_policy_refresh_error_delay_ms_ = policy_refresh_rate_ms_;
      refresh_at = now + base::TimeDelta::FromMilliseconds(
                             effective_policy_refresh_error_delay_ms_);
      notifier_->Inform(CloudPolicySubsystem::NETWORK_ERROR,
                        CloudPolicySubsystem::POLICY_NETWORK_ERROR,
                        PolicyNotifier::POLICY_CONTROLLER);
      break;
  }

  // Update the delayed work task.
  scheduler_->CancelDelayedWork();
  if (!refresh_at.is_null()) {
    int64 delay = std::max<int64>((refresh_at - now).InMilliseconds(), 0);
    scheduler_->PostDelayedWork(
        base::Bind(&CloudPolicyController::DoWork, base::Unretained(this)),
        delay);
  }

  // Inform the cache if a fetch attempt has completed. This happens if policy
  // has been succesfully fetched, or if token or policy fetching failed.
  if (state_ != STATE_TOKEN_UNAVAILABLE && state_ != STATE_TOKEN_VALID)
    cache_->SetFetchingDone();
}

int64 CloudPolicyController::GetRefreshDelay() {
  int64 deviation = (kPolicyRefreshDeviationFactorPercent *
                     policy_refresh_rate_ms_) / 100;
  deviation = std::min(deviation, kPolicyRefreshDeviationMaxInMilliseconds);
  return policy_refresh_rate_ms_ - base::RandGenerator(deviation + 1);
}

}  // namespace policy
