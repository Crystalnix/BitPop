// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/browser_policy_connector.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "base/file_path.h"
#include "base/path_service.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/policy/cloud_policy_provider.h"
#include "chrome/browser/policy/cloud_policy_provider_impl.h"
#include "chrome/browser/policy/cloud_policy_subsystem.h"
#include "chrome/browser/policy/configuration_policy_provider.h"
#include "chrome/browser/policy/network_configuration_updater.h"
#include "chrome/browser/policy/user_policy_cache.h"
#include "chrome/browser/policy/user_policy_token_cache.h"
#include "chrome/browser/signin/token_service.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/net/gaia/gaia_constants.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_source.h"
#include "grit/generated_resources.h"
#include "policy/policy_constants.h"

#if defined(OS_WIN)
#include "chrome/browser/policy/configuration_policy_provider_win.h"
#elif defined(OS_MACOSX)
#include "chrome/browser/policy/configuration_policy_provider_mac.h"
#elif defined(OS_POSIX)
#include "chrome/browser/policy/config_dir_policy_provider.h"
#endif

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/cros/cros_library.h"
#include "chrome/browser/chromeos/system/statistics_provider.h"
#include "chrome/browser/policy/device_policy_cache.h"
#endif

using content::BrowserThread;

namespace policy {

namespace {

// Subdirectory in the user's profile for storing user policies.
const FilePath::CharType kPolicyDir[] = FILE_PATH_LITERAL("Device Management");
// File in the above directory for stroing user policy dmtokens.
const FilePath::CharType kTokenCacheFile[] = FILE_PATH_LITERAL("Token");
// File in the above directory for storing user policy data.
const FilePath::CharType kPolicyCacheFile[] = FILE_PATH_LITERAL("Policy");

// The following constants define delays applied before the initial policy fetch
// on startup. (So that displaying Chrome's GUI does not get delayed.)
// Delay in milliseconds from startup.
const int64 kServiceInitializationStartupDelay = 5000;

#if defined(OS_CHROMEOS)
// MachineInfo key names.
const char kMachineInfoSystemHwqual[] = "hardware_class";

// These are the machine serial number keys that we check in order until we
// find a non-empty serial number. The VPD spec says the serial number should be
// in the "serial_number" key for v2+ VPDs. However, we cannot check this first,
// since we'd get the "serial_number" value from the SMBIOS (yes, there's a name
// clash here!), which is different from the serial number we want and not
// actually per-device. So, we check the legacy keys first. If we find a
// serial number for these, we use it, otherwise we must be on a newer device
// that provides the correct data in "serial_number".
const char* kMachineInfoSerialNumberKeys[] = {
  "sn",            // ZGB
  "Product_S/N",   // Alex
  "Product_SN",    // Mario
  "serial_number"  // VPD v2+ devices
};
#endif

}  // namespace

BrowserPolicyConnector::BrowserPolicyConnector()
    : ALLOW_THIS_IN_INITIALIZER_LIST(weak_ptr_factory_(this)) {}

BrowserPolicyConnector::~BrowserPolicyConnector() {
  // Shutdown device cloud policy.
#if defined(OS_CHROMEOS)
  if (device_cloud_policy_subsystem_.get())
    device_cloud_policy_subsystem_->Shutdown();
  device_cloud_policy_subsystem_.reset();
  device_data_store_.reset();
#endif

  // Shutdown user cloud policy.
  if (user_cloud_policy_subsystem_.get())
    user_cloud_policy_subsystem_->Shutdown();
  user_cloud_policy_subsystem_.reset();
  user_policy_token_cache_.reset();
  user_data_store_.reset();
}

void BrowserPolicyConnector::Init() {
  managed_platform_provider_.reset(CreateManagedPlatformProvider());
  recommended_platform_provider_.reset(CreateRecommendedPlatformProvider());

  managed_cloud_provider_.reset(new CloudPolicyProviderImpl(
      this,
      GetChromePolicyDefinitionList(),
      POLICY_LEVEL_MANDATORY));
  recommended_cloud_provider_.reset(new CloudPolicyProviderImpl(
      this,
      GetChromePolicyDefinitionList(),
      POLICY_LEVEL_RECOMMENDED));

#if defined(OS_CHROMEOS)
  InitializeDevicePolicy();

  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kEnableONCPolicy)) {
    network_configuration_updater_.reset(
        new NetworkConfigurationUpdater(
            managed_cloud_provider_.get(),
            chromeos::CrosLibrary::Get()->GetNetworkLibrary()));
  }
#endif
}

ConfigurationPolicyProvider*
    BrowserPolicyConnector::GetManagedPlatformProvider() const {
  return managed_platform_provider_.get();
}

ConfigurationPolicyProvider*
    BrowserPolicyConnector::GetManagedCloudProvider() const {
  return managed_cloud_provider_.get();
}

ConfigurationPolicyProvider*
    BrowserPolicyConnector::GetRecommendedPlatformProvider() const {
  return recommended_platform_provider_.get();
}

ConfigurationPolicyProvider*
    BrowserPolicyConnector::GetRecommendedCloudProvider() const {
  return recommended_cloud_provider_.get();
}

void BrowserPolicyConnector::RegisterForDevicePolicy(
    const std::string& owner_email,
    const std::string& token,
    bool known_machine_id) {
#if defined(OS_CHROMEOS)
  if (device_data_store_.get()) {
    if (!device_data_store_->device_token().empty()) {
      LOG(ERROR) << "Device policy data store already has a DMToken; "
                 << "RegisterForDevicePolicy won't trigger a new registration.";
    }
    device_data_store_->set_user_name(owner_email);
    device_data_store_->set_known_machine_id(known_machine_id);
    device_data_store_->SetOAuthToken(token);
  }
#endif
}

bool BrowserPolicyConnector::IsEnterpriseManaged() {
#if defined(OS_CHROMEOS)
  return install_attributes_.get() && install_attributes_->IsEnterpriseDevice();
#else
  return false;
#endif
}

EnterpriseInstallAttributes::LockResult
    BrowserPolicyConnector::LockDevice(const std::string& user) {
#if defined(OS_CHROMEOS)
  if (install_attributes_.get())
    return install_attributes_->LockDevice(user);
#endif

  return EnterpriseInstallAttributes::LOCK_BACKEND_ERROR;
}

// static
std::string BrowserPolicyConnector::GetSerialNumber() {
  std::string serial_number;
#if defined(OS_CHROMEOS)
  chromeos::system::StatisticsProvider* provider =
      chromeos::system::StatisticsProvider::GetInstance();
  for (size_t i = 0; i < arraysize(kMachineInfoSerialNumberKeys); i++) {
    if (provider->GetMachineStatistic(kMachineInfoSerialNumberKeys[i],
                                      &serial_number) &&
        !serial_number.empty()) {
      break;
    }
  }
#endif
  return serial_number;
}

std::string BrowserPolicyConnector::GetEnterpriseDomain() {
#if defined(OS_CHROMEOS)
  if (install_attributes_.get())
    return install_attributes_->GetDomain();
#endif

  return std::string();
}

void BrowserPolicyConnector::ResetDevicePolicy() {
#if defined(OS_CHROMEOS)
  if (device_cloud_policy_subsystem_.get())
    device_cloud_policy_subsystem_->Reset();
#endif
}

void BrowserPolicyConnector::FetchCloudPolicy() {
#if defined(OS_CHROMEOS)
  if (device_cloud_policy_subsystem_.get())
    device_cloud_policy_subsystem_->RefreshPolicies();
  if (user_cloud_policy_subsystem_.get())
    user_cloud_policy_subsystem_->RefreshPolicies();
#endif
}

void BrowserPolicyConnector::RefreshPolicies() {
  if (managed_platform_provider_.get())
    managed_platform_provider_->RefreshPolicies();
  if (recommended_platform_provider_.get())
    recommended_platform_provider_->RefreshPolicies();
  if (managed_cloud_provider_.get())
    managed_cloud_provider_->RefreshPolicies();
  if (recommended_cloud_provider_.get())
    recommended_cloud_provider_->RefreshPolicies();
}

void BrowserPolicyConnector::ScheduleServiceInitialization(
    int64 delay_milliseconds) {
  if (user_cloud_policy_subsystem_.get()) {
    user_cloud_policy_subsystem_->
        ScheduleServiceInitialization(delay_milliseconds);
  }
#if defined(OS_CHROMEOS)
  if (device_cloud_policy_subsystem_.get()) {
    device_cloud_policy_subsystem_->
        ScheduleServiceInitialization(delay_milliseconds);
  }
#endif
}
void BrowserPolicyConnector::InitializeUserPolicy(
    const std::string& user_name,
    bool wait_for_policy_fetch) {
  // Throw away the old backend.
  user_cloud_policy_subsystem_.reset();
  user_policy_token_cache_.reset();
  user_data_store_.reset();
  token_service_ = NULL;
  registrar_.RemoveAll();

  CommandLine* command_line = CommandLine::ForCurrentProcess();

  FilePath policy_dir;
  PathService::Get(chrome::DIR_USER_DATA, &policy_dir);
#if defined(OS_CHROMEOS)
  policy_dir = policy_dir.Append(
      command_line->GetSwitchValuePath(switches::kLoginProfile));
#endif

  if (command_line->HasSwitch(switches::kDeviceManagementUrl)) {
    FilePath policy_cache_dir = policy_dir.Append(kPolicyDir);

    UserPolicyCache* user_policy_cache =
        new UserPolicyCache(policy_cache_dir.Append(kPolicyCacheFile),
                            wait_for_policy_fetch);
    user_data_store_.reset(CloudPolicyDataStore::CreateForUserPolicies());
    user_policy_token_cache_.reset(
        new UserPolicyTokenCache(user_data_store_.get(),
                                 policy_cache_dir.Append(kTokenCacheFile)));

    // Prepending user caches meaning they will take precedence of device policy
    // caches.
    managed_cloud_provider_->PrependCache(user_policy_cache);
    recommended_cloud_provider_->PrependCache(user_policy_cache);
    user_cloud_policy_subsystem_.reset(new CloudPolicySubsystem(
        user_data_store_.get(),
        user_policy_cache));

    // Initiate the DM-Token load.
    user_policy_token_cache_->Load();

    user_data_store_->set_user_name(user_name);
    user_data_store_->set_user_affiliation(GetUserAffiliation(user_name));

    int64 delay =
        wait_for_policy_fetch ? 0 : kServiceInitializationStartupDelay;
    user_cloud_policy_subsystem_->CompleteInitialization(
        prefs::kUserPolicyRefreshRate,
        delay);
  }
}

void BrowserPolicyConnector::SetUserPolicyTokenService(
    TokenService* token_service) {
  token_service_ = token_service;
  registrar_.Add(this,
                 chrome::NOTIFICATION_TOKEN_AVAILABLE,
                 content::Source<TokenService>(token_service_));

  if (token_service_->HasTokenForService(
          GaiaConstants::kDeviceManagementService)) {
    user_data_store_->SetGaiaToken(token_service_->GetTokenForService(
        GaiaConstants::kDeviceManagementService));
  }
}

void BrowserPolicyConnector::RegisterForUserPolicy(
    const std::string& oauth_token) {
  if (oauth_token.empty()) {
    // An attempt to fetch the dm service oauth token has failed. Notify
    // the user policy cache of this, so that a potential blocked login
    // proceeds without waiting for user policy.
    if (user_cloud_policy_subsystem_.get()) {
      user_cloud_policy_subsystem_->GetCloudPolicyCacheBase()->
          SetFetchingDone();
    }
  } else {
    if (user_data_store_.get())
      user_data_store_->SetOAuthToken(oauth_token);
  }
}

const CloudPolicyDataStore*
    BrowserPolicyConnector::GetDeviceCloudPolicyDataStore() const {
#if defined(OS_CHROMEOS)
  return device_data_store_.get();
#else
  return NULL;
#endif
}

const CloudPolicyDataStore*
    BrowserPolicyConnector::GetUserCloudPolicyDataStore() const {
  return user_data_store_.get();
}

const ConfigurationPolicyHandlerList*
    BrowserPolicyConnector::GetHandlerList() const {
  return &handler_list_;
}

UserAffiliation BrowserPolicyConnector::GetUserAffiliation(
    const std::string& user_name) {
#if defined(OS_CHROMEOS)
  if (install_attributes_.get()) {
    size_t pos = user_name.find('@');
    if (pos != std::string::npos &&
        user_name.substr(pos + 1) == install_attributes_->GetDomain()) {
      return USER_AFFILIATION_MANAGED;
    }
  }
#endif

  return USER_AFFILIATION_NONE;
}

void BrowserPolicyConnector::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (type == chrome::NOTIFICATION_TOKEN_AVAILABLE) {
    const TokenService* token_source =
        content::Source<const TokenService>(source).ptr();
    DCHECK_EQ(token_service_, token_source);
    const TokenService::TokenAvailableDetails* token_details =
        content::Details<const TokenService::TokenAvailableDetails>(details).
            ptr();
    if (token_details->service() == GaiaConstants::kDeviceManagementService) {
      if (user_data_store_.get()) {
        user_data_store_->SetGaiaToken(token_details->token());
      }
    }
  } else {
    NOTREACHED();
  }
}

void BrowserPolicyConnector::InitializeDevicePolicy() {
#if defined(OS_CHROMEOS)
  // Throw away the old backend.
  device_cloud_policy_subsystem_.reset();
  device_data_store_.reset();

  CommandLine* command_line = CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kEnableDevicePolicy)) {
    device_data_store_.reset(CloudPolicyDataStore::CreateForDevicePolicies());
    chromeos::CryptohomeLibrary* cryptohome =
        chromeos::CrosLibrary::Get()->GetCryptohomeLibrary();
    install_attributes_.reset(new EnterpriseInstallAttributes(cryptohome));
    DevicePolicyCache* device_policy_cache =
        new DevicePolicyCache(device_data_store_.get(),
                              install_attributes_.get());

    managed_cloud_provider_->AppendCache(device_policy_cache);
    recommended_cloud_provider_->AppendCache(device_policy_cache);

    device_cloud_policy_subsystem_.reset(new CloudPolicySubsystem(
        device_data_store_.get(),
        device_policy_cache));

    // Initialize the subsystem once the message loops are spinning.
    MessageLoop::current()->PostTask(
        FROM_HERE,
        base::Bind(&BrowserPolicyConnector::CompleteInitialization,
                   weak_ptr_factory_.GetWeakPtr()));
  }
#endif
}

void BrowserPolicyConnector::CompleteInitialization() {
#if defined(OS_CHROMEOS)
  if (device_cloud_policy_subsystem_.get()) {
    // Read serial number and machine model. This must be done before we call
    // CompleteInitialization() below such that the serial number is available
    // for re-submission in case we're doing serial number recovery.
    if (device_data_store_->machine_id().empty() ||
        device_data_store_->machine_model().empty()) {
      chromeos::system::StatisticsProvider* provider =
          chromeos::system::StatisticsProvider::GetInstance();

      std::string machine_model;
      if (!provider->GetMachineStatistic(kMachineInfoSystemHwqual,
                                         &machine_model)) {
        LOG(ERROR) << "Failed to get machine model.";
      }

      std::string machine_id = GetSerialNumber();
      if (machine_id.empty())
        LOG(ERROR) << "Failed to get machine serial number.";

      device_data_store_->set_machine_id(machine_id);
      device_data_store_->set_machine_model(machine_model);
    }

    device_cloud_policy_subsystem_->CompleteInitialization(
        prefs::kDevicePolicyRefreshRate,
        kServiceInitializationStartupDelay);
  }
  device_data_store_->set_device_status_collector(
      new DeviceStatusCollector(
          g_browser_process->local_state(),
          chromeos::system::StatisticsProvider::GetInstance()));
#endif
}

// static
ConfigurationPolicyProvider*
    BrowserPolicyConnector::CreateManagedPlatformProvider() {
  const PolicyDefinitionList* policy_list = GetChromePolicyDefinitionList();
#if defined(OS_WIN)
  return new ConfigurationPolicyProviderWin(policy_list,
                                            policy::kRegistryMandatorySubKey,
                                            POLICY_LEVEL_MANDATORY);
#elif defined(OS_MACOSX)
  return new ConfigurationPolicyProviderMac(policy_list,
                                            POLICY_LEVEL_MANDATORY);
#elif defined(OS_POSIX)
  FilePath config_dir_path;
  if (PathService::Get(chrome::DIR_POLICY_FILES, &config_dir_path)) {
    return new ConfigDirPolicyProvider(
        policy_list,
        POLICY_LEVEL_MANDATORY,
        POLICY_SCOPE_MACHINE,
        config_dir_path.Append(FILE_PATH_LITERAL("managed")));
  } else {
    return NULL;
  }
#else
  return NULL;
#endif
}

// static
ConfigurationPolicyProvider*
    BrowserPolicyConnector::CreateRecommendedPlatformProvider() {
  const PolicyDefinitionList* policy_list = GetChromePolicyDefinitionList();
#if defined(OS_WIN)
  return new ConfigurationPolicyProviderWin(policy_list,
                                            policy::kRegistryRecommendedSubKey,
                                            POLICY_LEVEL_RECOMMENDED);
#elif defined(OS_MACOSX)
  return new ConfigurationPolicyProviderMac(policy_list,
                                            POLICY_LEVEL_RECOMMENDED);
#elif defined(OS_POSIX)
  FilePath config_dir_path;
  if (PathService::Get(chrome::DIR_POLICY_FILES, &config_dir_path)) {
    return new ConfigDirPolicyProvider(
        policy_list,
        POLICY_LEVEL_RECOMMENDED,
        POLICY_SCOPE_MACHINE,
        config_dir_path.Append(FILE_PATH_LITERAL("recommended")));
  } else {
    return NULL;
  }
#else
  return NULL;
#endif
}

}  // namespace policy
