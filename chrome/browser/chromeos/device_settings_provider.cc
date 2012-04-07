// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/device_settings_provider.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/callback.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "base/string_util.h"
#include "base/threading/thread_restrictions.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chromeos/cros/cros_library.h"
#include "chrome/browser/chromeos/cros/network_library.h"
#include "chrome/browser/chromeos/cros_settings.h"
#include "chrome/browser/chromeos/cros_settings_names.h"
#include "chrome/browser/chromeos/login/ownership_service.h"
#include "chrome/browser/chromeos/login/signed_settings_cache.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/policy/proto/chrome_device_policy.pb.h"
#include "chrome/browser/prefs/pref_value_map.h"
#include "chrome/browser/ui/options/options_util.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/installer/util/google_update_settings.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_service.h"

using google::protobuf::RepeatedPtrField;

namespace em = enterprise_management;

namespace chromeos {

namespace {

const char* kBooleanSettings[] = {
  kAccountsPrefAllowNewUser,
  kAccountsPrefAllowGuest,
  kAccountsPrefShowUserNamesOnSignIn,
  kSignedDataRoamingEnabled,
  kStatsReportingPref,
  kReportDeviceVersionInfo,
  kReportDeviceActivityTimes,
  kReportDeviceBootMode
};

const char* kStringSettings[] = {
  kDeviceOwner,
  kReleaseChannel,
  kSettingProxyEverywhere
};

const char* kListSettings[] = {
  kAccountsPrefUsers
};

// Upper bound for number of retries to fetch a signed setting.
static const int kNumRetriesLimit = 9;

// Legacy policy file location. Used to detect migration from pre v12 ChormeOS.
const char kLegacyPolicyFile[] = "/var/lib/whitelist/preferences";

bool IsControlledBooleanSetting(const std::string& pref_path) {
  const char** end = kBooleanSettings + arraysize(kBooleanSettings);
  return std::find(kBooleanSettings, end, pref_path) != end;
}

bool IsControlledStringSetting(const std::string& pref_path) {
  const char** end = kStringSettings + arraysize(kStringSettings);
  return std::find(kStringSettings, end, pref_path) != end;
}

bool IsControlledListSetting(const std::string& pref_path) {
  const char** end = kListSettings + arraysize(kListSettings);
  return std::find(kListSettings, end, pref_path) != end;
}

bool IsControlledSetting(const std::string& pref_path) {
  return (IsControlledBooleanSetting(pref_path) ||
          IsControlledStringSetting(pref_path) ||
          IsControlledListSetting(pref_path));
}

bool HasOldMetricsFile() {
  // TODO(pastarmovj): Remove this once migration is not needed anymore.
  // If the value is not set we should try to migrate legacy consent file.
  // Loading consent file state causes us to do blocking IO on UI thread.
  // Temporarily allow it until we fix http://crbug.com/62626
  base::ThreadRestrictions::ScopedAllowIO allow_io;
  return GoogleUpdateSettings::GetCollectStatsConsent();
}

}  // namespace

DeviceSettingsProvider::DeviceSettingsProvider(
    const NotifyObserversCallback& notify_cb)
    : CrosSettingsProvider(notify_cb),
      ownership_status_(OwnershipService::GetSharedInstance()->GetStatus(true)),
      migration_helper_(new SignedSettingsMigrationHelper()),
      retries_left_(kNumRetriesLimit),
      trusted_(false) {
  // Register for notification when ownership is taken so that we can update
  // the |ownership_status_| and reload if needed.
  registrar_.Add(this, chrome::NOTIFICATION_OWNER_KEY_FETCH_ATTEMPT_SUCCEEDED,
                 content::NotificationService::AllSources());
  // Make sure we have at least the cache data immediately.
  RetrieveCachedData();
  // Start prefetching preferences.
  Reload();
}

DeviceSettingsProvider::~DeviceSettingsProvider() {
}

void DeviceSettingsProvider::Reload() {
  // While fetching we can't trust the cache anymore.
  trusted_ = false;
  if (ownership_status_ == OwnershipService::OWNERSHIP_NONE) {
    RetrieveCachedData();
  } else {
    // Retrieve the real data.
    SignedSettingsHelper::Get()->StartRetrievePolicyOp(
        base::Bind(&DeviceSettingsProvider::OnRetrievePolicyCompleted,
                   base::Unretained(this)));
  }
}

void DeviceSettingsProvider::DoSet(const std::string& path,
                                   const base::Value& in_value) {
  if (!UserManager::Get()->current_user_is_owner() &&
      ownership_status_ != OwnershipService::OWNERSHIP_NONE) {
    LOG(WARNING) << "Changing settings from non-owner, setting=" << path;

    // Revert UI change.
    NotifyObservers(path);
    return;
  }

  if (IsControlledSetting(path)) {
    pending_changes_.push_back(PendingQueueElement(path, in_value.DeepCopy()));
    if (pending_changes_.size() == 1)
      SetInPolicy();
  } else {
    NOTREACHED() << "Try to set unhandled cros setting " << path;
  }
}

void DeviceSettingsProvider::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  if (type == chrome::NOTIFICATION_OWNER_KEY_FETCH_ATTEMPT_SUCCEEDED) {
    // Reload the policy blob once the owner key has been loaded or updated.
    ownership_status_ = OwnershipService::OWNERSHIP_TAKEN;
    Reload();
  }
}

const em::PolicyData DeviceSettingsProvider::policy() const {
  return policy_;
}

void DeviceSettingsProvider::RetrieveCachedData() {
  // If there is no owner yet, this function will pull the policy cache from the
  // temp storage and use that instead.
  em::PolicyData policy;
  if (!signed_settings_cache::Retrieve(&policy,
                                       g_browser_process->local_state())) {
    VLOG(1) << "Can't retrieve temp store possibly not created yet.";
    // Prepare empty data for the case we don't have temp cache yet.
    policy.set_policy_type(kDevicePolicyType);
    em::ChromeDeviceSettingsProto pol;
    policy.set_policy_value(pol.SerializeAsString());
  }

  policy_ = policy;
  UpdateValuesCache();
}

void DeviceSettingsProvider::SetInPolicy() {
  if (pending_changes_.empty()) {
    NOTREACHED();
    return;
  }

  const std::string& prop = pending_changes_[0].first;
  base::Value* value = pending_changes_[0].second;
  if (prop == kDeviceOwner) {
    // Just store it in the memory cache without trusted checks or persisting.
    std::string owner;
    if (value->GetAsString(&owner)) {
      policy_.set_username(owner);
      // In this case the |value_cache_| takes the ownership of |value|.
      values_cache_.SetValue(prop, value);
      NotifyObservers(prop);
      // We can't trust this value anymore until we reload the real username.
      trusted_ = false;
      pending_changes_.erase(pending_changes_.begin());
      if (!pending_changes_.empty())
        SetInPolicy();
    } else {
      NOTREACHED();
    }
    return;
  }

  if (!RequestTrustedEntity()) {
    // Otherwise we should first reload and apply on top of that.
    SignedSettingsHelper::Get()->StartRetrievePolicyOp(
            base::Bind(&DeviceSettingsProvider::FinishSetInPolicy,
                       base::Unretained(this)));
    return;
  }

  trusted_ = false;
  em::PolicyData data = policy();
  em::ChromeDeviceSettingsProto pol;
  pol.ParseFromString(data.policy_value());
  if (prop == kAccountsPrefAllowNewUser) {
    em::AllowNewUsersProto* allow = pol.mutable_allow_new_users();
    bool allow_value;
    if (value->GetAsBoolean(&allow_value))
      allow->set_allow_new_users(allow_value);
    else
      NOTREACHED();
  } else if (prop == kAccountsPrefAllowGuest) {
    em::GuestModeEnabledProto* guest = pol.mutable_guest_mode_enabled();
    bool guest_value;
    if (value->GetAsBoolean(&guest_value))
      guest->set_guest_mode_enabled(guest_value);
    else
      NOTREACHED();
  } else if (prop == kAccountsPrefShowUserNamesOnSignIn) {
    em::ShowUserNamesOnSigninProto* show = pol.mutable_show_user_names();
    bool show_value;
    if (value->GetAsBoolean(&show_value))
      show->set_show_user_names(show_value);
    else
      NOTREACHED();
  } else if (prop == kSignedDataRoamingEnabled) {
    em::DataRoamingEnabledProto* roam = pol.mutable_data_roaming_enabled();
    bool roaming_value = false;
    if (value->GetAsBoolean(&roaming_value))
      roam->set_data_roaming_enabled(roaming_value);
    else
      NOTREACHED();
    ApplyRoamingSetting(roaming_value);
  } else if (prop == kSettingProxyEverywhere) {
    // TODO(cmasone): NOTIMPLEMENTED() once http://crosbug.com/13052 is fixed.
    std::string proxy_value;
    if (value->GetAsString(&proxy_value)) {
      bool success =
          pol.mutable_device_proxy_settings()->ParseFromString(proxy_value);
      DCHECK(success);
    } else {
      NOTREACHED();
    }
  } else if (prop == kReleaseChannel) {
    em::ReleaseChannelProto* release_channel = pol.mutable_release_channel();
    std::string channel_value;
    if (value->GetAsString(&channel_value))
      release_channel->set_release_channel(channel_value);
    else
      NOTREACHED();
  } else if (prop == kStatsReportingPref) {
    em::MetricsEnabledProto* metrics = pol.mutable_metrics_enabled();
    bool metrics_value = false;
    if (value->GetAsBoolean(&metrics_value))
      metrics->set_metrics_enabled(metrics_value);
    else
      NOTREACHED();
    ApplyMetricsSetting(false, metrics_value);
  } else if (prop == kAccountsPrefUsers) {
    em::UserWhitelistProto* whitelist_proto = pol.mutable_user_whitelist();
    whitelist_proto->clear_user_whitelist();
    base::ListValue& users = static_cast<base::ListValue&>(*value);
    for (base::ListValue::const_iterator i = users.begin();
         i != users.end(); ++i) {
      std::string email;
      if ((*i)->GetAsString(&email))
        whitelist_proto->add_user_whitelist(email.c_str());
    }
  } else {
    // kReportDeviceVersionInfo, kReportDeviceActivityTimes, and
    // kReportDeviceBootMode do not support being set in the policy, since
    // they are not intended to be user-controlled.
    NOTREACHED();
  }
  data.set_policy_value(pol.SerializeAsString());
  // Set the cache to the updated value.
  policy_ = data;
  UpdateValuesCache();

  if (!signed_settings_cache::Store(data, g_browser_process->local_state()))
    LOG(ERROR) << "Couldn't store to the temp storage.";

  if (ownership_status_ == OwnershipService::OWNERSHIP_TAKEN) {
    em::PolicyFetchResponse policy_envelope;
    policy_envelope.set_policy_data(policy_.SerializeAsString());
    SignedSettingsHelper::Get()->StartStorePolicyOp(
        policy_envelope,
        base::Bind(&DeviceSettingsProvider::OnStorePolicyCompleted,
                   base::Unretained(this)));
  } else {
    // OnStorePolicyCompleted won't get called in this case so proceed with any
    // pending operations immediately.
    delete pending_changes_[0].second;
    pending_changes_.erase(pending_changes_.begin());
    if (!pending_changes_.empty())
      SetInPolicy();
  }
}

void DeviceSettingsProvider::FinishSetInPolicy(
    SignedSettings::ReturnCode code,
    const em::PolicyFetchResponse& policy) {
  if (code != SignedSettings::SUCCESS) {
    LOG(ERROR) << "Can't serialize to policy error code: " << code;
    Reload();
    return;
  }
  // Update the internal caches and set the trusted flag to true so that we
  // can pass the trustedness check in the second call to SetInPolicy.
  OnRetrievePolicyCompleted(code, policy);

  SetInPolicy();
}

void DeviceSettingsProvider::UpdateValuesCache() {
  const em::PolicyData data = policy();
  PrefValueMap new_values_cache;

  if (data.has_username() && !data.has_request_token())
    new_values_cache.SetString(kDeviceOwner, data.username());

  em::ChromeDeviceSettingsProto pol;
  pol.ParseFromString(data.policy_value());

  // For all our boolean settings the following is applicable:
  // true is default permissive value and false is safe prohibitive value.
  // Exception: kSignedDataRoamingEnabled which has default value of false.
  if (pol.has_allow_new_users() &&
      pol.allow_new_users().has_allow_new_users() &&
      pol.allow_new_users().allow_new_users()) {
    // New users allowed, user_whitelist() ignored.
    new_values_cache.SetBoolean(kAccountsPrefAllowNewUser, true);
  } else if (!pol.has_user_whitelist()) {
    // If we have the allow_new_users bool, and it is true, we honor that above.
    // In all other cases (don't have it, have it and it is set to false, etc),
    // We will honor the user_whitelist() if it is there and populated.
    // Otherwise we default to allowing new users.
    new_values_cache.SetBoolean(kAccountsPrefAllowNewUser, true);
  } else {
    new_values_cache.SetBoolean(kAccountsPrefAllowNewUser,
                             pol.user_whitelist().user_whitelist_size() == 0);
  }

  new_values_cache.SetBoolean(
      kAccountsPrefAllowGuest,
      !pol.has_guest_mode_enabled() ||
      !pol.guest_mode_enabled().has_guest_mode_enabled() ||
      pol.guest_mode_enabled().guest_mode_enabled());

  new_values_cache.SetBoolean(
      kAccountsPrefShowUserNamesOnSignIn,
      !pol.has_show_user_names() ||
      !pol.show_user_names().has_show_user_names() ||
      pol.show_user_names().show_user_names());

  new_values_cache.SetBoolean(
      kSignedDataRoamingEnabled,
      pol.has_data_roaming_enabled() &&
      pol.data_roaming_enabled().has_data_roaming_enabled() &&
      pol.data_roaming_enabled().data_roaming_enabled());

  // TODO(cmasone): NOTIMPLEMENTED() once http://crosbug.com/13052 is fixed.
  std::string serialized;
  if (pol.has_device_proxy_settings() &&
      pol.device_proxy_settings().SerializeToString(&serialized)) {
    new_values_cache.SetString(kSettingProxyEverywhere, serialized);
  }

  if (!pol.has_release_channel() ||
      !pol.release_channel().has_release_channel()) {
    // Default to an invalid channel (will be ignored).
    new_values_cache.SetString(kReleaseChannel, "");
  } else {
    new_values_cache.SetString(kReleaseChannel,
                               pol.release_channel().release_channel());
  }

  if (pol.has_metrics_enabled()) {
    new_values_cache.SetBoolean(kStatsReportingPref,
                                pol.metrics_enabled().metrics_enabled());
  } else {
    new_values_cache.SetBoolean(kStatsReportingPref, HasOldMetricsFile());
  }

  base::ListValue* list = new base::ListValue();
  const em::UserWhitelistProto& whitelist_proto = pol.user_whitelist();
  const RepeatedPtrField<std::string>& whitelist =
      whitelist_proto.user_whitelist();
  for (RepeatedPtrField<std::string>::const_iterator it = whitelist.begin();
       it != whitelist.end(); ++it) {
    list->Append(base::Value::CreateStringValue(*it));
  }
  new_values_cache.SetValue(kAccountsPrefUsers, list);

  if (pol.has_device_reporting()) {
    if (pol.device_reporting().has_report_version_info()) {
      new_values_cache.SetBoolean(kReportDeviceVersionInfo,
          pol.device_reporting().report_version_info());
    }
    // TODO(dubroy): Re-add device activity time policy here when the UI
    // to notify the user has been implemented (http://crosbug.com/26252).
    if (pol.device_reporting().has_report_boot_mode()) {
      new_values_cache.SetBoolean(kReportDeviceBootMode,
          pol.device_reporting().report_boot_mode());
    }
  }

  // Collect all notifications but send them only after we have swapped the
  // cache so that if somebody actually reads the cache will be already valid.
  std::vector<std::string> notifications;
  // Go through the new values and verify in the old ones.
  PrefValueMap::iterator iter = new_values_cache.begin();
  for (; iter != new_values_cache.end(); ++iter) {
    const base::Value* old_value;
    if (!values_cache_.GetValue(iter->first, &old_value) ||
        !old_value->Equals(iter->second)) {
      notifications.push_back(iter->first);
    }
  }
  // Now check for values that have been removed from the policy blob.
  for (iter = values_cache_.begin(); iter != values_cache_.end(); ++iter) {
    const base::Value* value;
    if (!new_values_cache.GetValue(iter->first, &value))
      notifications.push_back(iter->first);
  }
  // Swap and notify.
  values_cache_.Swap(&new_values_cache);
  for (size_t i = 0; i < notifications.size(); ++i)
    NotifyObservers(notifications[i]);
}

void DeviceSettingsProvider::ApplyMetricsSetting(bool use_file,
                                                 bool new_value) const {
  // TODO(pastarmovj): Remove this once migration is not needed anymore.
  // If the value is not set we should try to migrate legacy consent file.
  if (use_file) {
    new_value = HasOldMetricsFile();
    // Make sure the values will get eventually written to the policy file.
    migration_helper_->AddMigrationValue(
        kStatsReportingPref, base::Value::CreateBooleanValue(new_value));
    migration_helper_->MigrateValues();
    LOG(INFO) << "No metrics policy set will revert to checking "
                 << "consent file which is "
                 << (new_value ? "on." : "off.");
  }
  VLOG(1) << "Metrics policy is being set to : " << new_value
          << "(use file : " << use_file << ")";
  // TODO(pastarmovj): Remove this once we don't need to regenerate the
  // consent file for the GUID anymore.
  OptionsUtil::ResolveMetricsReportingEnabled(new_value);
}

void DeviceSettingsProvider::ApplyRoamingSetting(bool new_value) const {
  NetworkLibrary* cros = CrosLibrary::Get()->GetNetworkLibrary();
  const NetworkDevice* cellular = cros->FindCellularDevice();
  if (cellular) {
    bool device_value = cellular->data_roaming_allowed();
    if (!device_value && cros->IsCellularAlwaysInRoaming()) {
      // If operator requires roaming always enabled, ignore supplied value
      // and set data roaming allowed in true always.
      cros->SetCellularDataRoamingAllowed(true);
    } else if (device_value != new_value) {
      cros->SetCellularDataRoamingAllowed(new_value);
    }
  }
}

void DeviceSettingsProvider::ApplySideEffects() const {
  const em::PolicyData data = policy();
  em::ChromeDeviceSettingsProto pol;
  pol.ParseFromString(data.policy_value());
  // First migrate metrics settings as needed.
  if (pol.has_metrics_enabled())
    ApplyMetricsSetting(false, pol.metrics_enabled().metrics_enabled());
  else
    ApplyMetricsSetting(true, false);
  // Next set the roaming setting as needed.
  ApplyRoamingSetting(pol.has_data_roaming_enabled() ?
      pol.data_roaming_enabled().data_roaming_enabled() : false);
}

bool DeviceSettingsProvider::MitigateMissingPolicy() {
  // As this code runs only in exceptional cases it's fine to allow I/O here.
  base::ThreadRestrictions::ScopedAllowIO allow_io;
  FilePath legacy_policy_file(kLegacyPolicyFile);
  // Check if legacy file exists but is not writable to avoid possible
  // attack of creating this file through chronos (although this should be
  // not possible in root owned location), but better be safe than sorry.
  // TODO(pastarmovj): Remove this workaround once we have proper checking
  // for policy corruption or when Cr48 is phased out the very latest.
  // See: http://crosbug.com/24916.
  if (file_util::PathExists(legacy_policy_file) &&
      !file_util::PathIsWritable(legacy_policy_file)) {
    // We are in pre 11 dev upgrading to post 17 version mode.
    LOG(ERROR) << "Detected system upgraded from ChromeOS 11 or older with "
               << "missing policies. Switching to migration policy mode "
               << "until the owner logs in to regenerate the policy data.";
    // In this situation we should pretend we have policy even though we
    // don't until the owner logs in and restores the policy blob.
    values_cache_.SetBoolean(kAccountsPrefAllowNewUser, true);
    values_cache_.SetBoolean(kAccountsPrefAllowGuest, true);
    trusted_ = true;
    // Make sure we will recreate the policy once the owner logs in.
    // Any value not in this list will be left to the default which is fine as
    // we repopulate the whitelist with the owner and any other possible every
    // time the user enables whitelist filtering on the UI.
    migration_helper_->AddMigrationValue(
        kAccountsPrefAllowNewUser, base::Value::CreateBooleanValue(true));
    migration_helper_->MigrateValues();
    // The last step is to pretend we loaded policy correctly and call everyone.
    for (size_t i = 0; i < callbacks_.size(); ++i)
      callbacks_[i].Run();
    callbacks_.clear();
    return true;
  }
  return false;
}

const base::Value* DeviceSettingsProvider::Get(const std::string& path) const {
  if (IsControlledSetting(path)) {
    const base::Value* value;
    if (values_cache_.GetValue(path, &value))
      return value;
  } else {
    NOTREACHED() << "Trying to get non cros setting.";
  }

  return NULL;
}

bool DeviceSettingsProvider::GetTrusted(const std::string& path,
                                        const base::Closure& callback) {
  if (!IsControlledSetting(path)) {
    NOTREACHED();
    return true;
  }

  if (RequestTrustedEntity()) {
    return true;
  } else {
    if (!callback.is_null())
      callbacks_.push_back(callback);
    return false;
  }
}

bool DeviceSettingsProvider::HandlesSetting(const std::string& path) const {
  return IsControlledSetting(path);
}

bool DeviceSettingsProvider::RequestTrustedEntity() {
  if (ownership_status_ == OwnershipService::OWNERSHIP_NONE)
    return true;
  return trusted_;
}

void DeviceSettingsProvider::OnStorePolicyCompleted(
    SignedSettings::ReturnCode code) {
  // In any case reload the policy cache to now.
  if (code != SignedSettings::SUCCESS)
    Reload();
  else
    trusted_ = true;

  // Clear the finished task and proceed with any other stores that could be
  // pending by now.
  delete pending_changes_[0].second;
  pending_changes_.erase(pending_changes_.begin());
  if (!pending_changes_.empty())
    SetInPolicy();
}

void DeviceSettingsProvider::OnRetrievePolicyCompleted(
    SignedSettings::ReturnCode code,
    const em::PolicyFetchResponse& policy_data) {
  VLOG(1) << "OnRetrievePolicyCompleted. Error code: " << code
          << ", trusted : " << trusted_ << ", status : " << ownership_status_;
  switch (code) {
    case SignedSettings::SUCCESS: {
      DCHECK(policy_data.has_policy_data());
      policy_.ParseFromString(policy_data.policy_data());
      signed_settings_cache::Store(policy(),
                                   g_browser_process->local_state());
      UpdateValuesCache();
      trusted_ = true;
      for (size_t i = 0; i < callbacks_.size(); ++i)
        callbacks_[i].Run();
      callbacks_.clear();
      // TODO(pastarmovj): Make those side effects responsibility of the
      // respective subsystems.
      ApplySideEffects();
      break;
    }
    case SignedSettings::NOT_FOUND:
      // Verify if we don't have to mitigate pre Chrome 12 machine here and if
      // needed do the magic.
      if (MitigateMissingPolicy())
        break;
    case SignedSettings::KEY_UNAVAILABLE: {
      if (ownership_status_ != OwnershipService::OWNERSHIP_TAKEN)
        NOTREACHED() << "No policies present yet, will use the temp storage.";
      break;
    }
    case SignedSettings::BAD_SIGNATURE:
    case SignedSettings::OPERATION_FAILED: {
      LOG(ERROR) << "Failed to retrieve cros policies. Reason:" << code;
      if (retries_left_ > 0) {
        retries_left_ -= 1;
        Reload();
        return;
      }
      LOG(ERROR) << "No retries left";
      break;
    }
  }
}

}  // namespace chromeos
