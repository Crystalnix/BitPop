// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/device_status_collector.h"

#include "base/bind.h"
#include "base/callback.h"
#include "base/string_number_conversions.h"
#include "chrome/browser/chromeos/cros_settings.h"
#include "chrome/browser/chromeos/cros_settings_names.h"
#include "chrome/browser/chromeos/system/statistics_provider.h"
#include "chrome/browser/policy/proto/device_management_backend.pb.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/prefs/scoped_user_pref_update.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/chrome_version_info.h"

using base::Time;
using base::TimeDelta;
using chromeos::VersionLoader;

namespace em = enterprise_management;

namespace {
// How many seconds of inactivity triggers the idle state.
const unsigned int kIdleStateThresholdSeconds = 300;

// The maximum number of time periods stored in the local state.
const unsigned int kMaxStoredActivePeriods = 500;

// Stores a list of timestamps representing device active periods.
const char* const kPrefDeviceActivePeriods = "device_status.active_periods";

bool GetTimestamp(const ListValue* list, int index, int64* out_value) {
  std::string string_value;
  if (list->GetString(index, &string_value))
    return base::StringToInt64(string_value, out_value);
  return false;
}

}  // namespace

namespace policy {

DeviceStatusCollector::DeviceStatusCollector(
    PrefService* local_state,
    chromeos::system::StatisticsProvider* provider)
    : max_stored_active_periods_(kMaxStoredActivePeriods),
      local_state_(local_state),
      last_idle_check_(Time()),
      last_idle_state_(IDLE_STATE_UNKNOWN),
      statistics_provider_(provider),
      report_version_info_(false),
      report_activity_times_(false),
      report_boot_mode_(false) {
  timer_.Start(FROM_HERE,
               TimeDelta::FromSeconds(
                   DeviceStatusCollector::kPollIntervalSeconds),
               this, &DeviceStatusCollector::CheckIdleState);

  cros_settings_ = chromeos::CrosSettings::Get();

  // Watch for changes to the individual policies that control what the status
  // reports contain.
  cros_settings_->AddSettingsObserver(chromeos::kReportDeviceVersionInfo, this);
  cros_settings_->AddSettingsObserver(chromeos::kReportDeviceActivityTimes,
                                      this);
  cros_settings_->AddSettingsObserver(chromeos::kReportDeviceBootMode, this);

  // Fetch the current values of the policies.
  UpdateReportingSettings();

  // Get the the OS and firmware version info.
  version_loader_.GetVersion(&consumer_,
                             base::Bind(&DeviceStatusCollector::OnOSVersion,
                                        base::Unretained(this)),
                             VersionLoader::VERSION_FULL);
  version_loader_.GetFirmware(&consumer_,
                              base::Bind(&DeviceStatusCollector::OnOSFirmware,
                                         base::Unretained(this)));
}

DeviceStatusCollector::~DeviceStatusCollector() {
  cros_settings_->RemoveSettingsObserver(chromeos::kReportDeviceVersionInfo,
                                         this);
  cros_settings_->RemoveSettingsObserver(chromeos::kReportDeviceActivityTimes,
                                         this);
  cros_settings_->RemoveSettingsObserver(chromeos::kReportDeviceBootMode, this);
}

// static
void DeviceStatusCollector::RegisterPrefs(PrefService* local_state) {
  local_state->RegisterListPref(kPrefDeviceActivePeriods, new ListValue);
}

void DeviceStatusCollector::CheckIdleState() {
  CalculateIdleState(kIdleStateThresholdSeconds,
      base::Bind(&DeviceStatusCollector::IdleStateCallback,
                 base::Unretained(this)));
}

void DeviceStatusCollector::UpdateReportingSettings() {
  // Attempt to fetch the current value of the reporting settings.
  // If trusted values are not available, register this function to be called
  // back when they are available.
  bool is_trusted = cros_settings_->GetTrusted(
      chromeos::kReportDeviceVersionInfo,
      base::Bind(&DeviceStatusCollector::UpdateReportingSettings,
                 base::Unretained(this)));
  if (is_trusted) {
    cros_settings_->GetBoolean(
        chromeos::kReportDeviceVersionInfo, &report_version_info_);
    cros_settings_->GetBoolean(
        chromeos::kReportDeviceActivityTimes, &report_activity_times_);
    cros_settings_->GetBoolean(
        chromeos::kReportDeviceBootMode, &report_boot_mode_);
  }
}

Time DeviceStatusCollector::GetCurrentTime() {
  return Time::Now();
}

void DeviceStatusCollector::AddActivePeriod(Time start, Time end) {
  // Maintain the list of active periods in a local_state pref.
  ListPrefUpdate update(local_state_, kPrefDeviceActivePeriods);
  ListValue* active_periods = update.Get();

  // Cap the number of active periods that we store.
  if (active_periods->GetSize() >= 2 * max_stored_active_periods_)
    return;

  Time epoch = Time::UnixEpoch();
  int64 start_timestamp = (start - epoch).InMilliseconds();
  Value* end_value = new StringValue(
      base::Int64ToString((end - epoch).InMilliseconds()));

  int list_size = active_periods->GetSize();
  DCHECK(list_size % 2 == 0);

  // Check if this period can be combined with the previous one.
  if (list_size > 0 && last_idle_state_ == IDLE_STATE_ACTIVE) {
    int64 last_period_end;
    if (GetTimestamp(active_periods, list_size - 1, &last_period_end) &&
        last_period_end == start_timestamp) {
      active_periods->Set(list_size - 1, end_value);
      return;
    }
  }
  // Add a new period to the list.
  active_periods->Append(
      new StringValue(base::Int64ToString(start_timestamp)));
  active_periods->Append(end_value);
}

void DeviceStatusCollector::IdleStateCallback(IdleState state) {
  // Do nothing if device activity reporting is disabled.
  if (!report_activity_times_)
    return;

  Time now = GetCurrentTime();

  if (state == IDLE_STATE_ACTIVE) {
    unsigned int poll_interval = DeviceStatusCollector::kPollIntervalSeconds;

    // If it's been too long since the last report, assume that the system was
    // in standby, and only count a single interval of activity.
    if ((now - last_idle_check_).InSeconds() >= (2 * poll_interval))
      AddActivePeriod(now - TimeDelta::FromSeconds(poll_interval), now);
    else
      AddActivePeriod(last_idle_check_, now);
  }
  last_idle_check_ = now;
  last_idle_state_ = state;
}

void DeviceStatusCollector::GetActivityTimes(
    em::DeviceStatusReportRequest* request) {
  const ListValue* active_periods =
      local_state_->GetList(kPrefDeviceActivePeriods);
  em::TimePeriod* time_period;

  DCHECK(active_periods->GetSize() % 2 == 0);

  int period_count = active_periods->GetSize() / 2;
  for (int i = 0; i < period_count; i++) {
    int64 start, end;

    if (!GetTimestamp(active_periods, 2 * i, &start) ||
        !GetTimestamp(active_periods, 2 * i + 1, &end) ||
        end < start) {
      // Something is amiss -- bail out.
      NOTREACHED();
      break;
    }
    time_period = request->add_active_time();
    time_period->set_start_timestamp(start);
    time_period->set_end_timestamp(end);
  }
  ListPrefUpdate update(local_state_, kPrefDeviceActivePeriods);
  update.Get()->Clear();
}

void DeviceStatusCollector::GetVersionInfo(
    em::DeviceStatusReportRequest* request) {
  chrome::VersionInfo version_info;
  request->set_browser_version(version_info.Version());
  request->set_os_version(os_version_);
  request->set_firmware_version(firmware_version_);
}

void DeviceStatusCollector::GetBootMode(
    em::DeviceStatusReportRequest* request) {
  std::string dev_switch_mode;
  if (statistics_provider_->GetMachineStatistic(
      "devsw_boot", &dev_switch_mode)) {
    if (dev_switch_mode == "1")
      request->set_boot_mode("Dev");
    else if (dev_switch_mode == "0")
      request->set_boot_mode("Verified");
  }
}

void DeviceStatusCollector::GetStatus(em::DeviceStatusReportRequest* request) {
  if (report_activity_times_)
    GetActivityTimes(request);

  if (report_version_info_)
    GetVersionInfo(request);

  if (report_boot_mode_)
    GetBootMode(request);
}

void DeviceStatusCollector::OnOSVersion(VersionLoader::Handle handle,
                                        std::string version) {
  os_version_ = version;
}

void DeviceStatusCollector::OnOSFirmware(VersionLoader::Handle handle,
                                         std::string version) {
  firmware_version_ = version;
}

void DeviceStatusCollector::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  if (type == chrome::NOTIFICATION_SYSTEM_SETTING_CHANGED)
    UpdateReportingSettings();
  else
    NOTREACHED();
}

}  // namespace policy
