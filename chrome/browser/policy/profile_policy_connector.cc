// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <string>

#include "base/command_line.h"
#include "base/file_util.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/policy/browser_policy_connector.h"
#include "chrome/browser/policy/cloud_policy_subsystem.h"
#include "chrome/browser/policy/configuration_policy_pref_store.h"
#include "chrome/browser/policy/profile_policy_connector.h"
#include "chrome/browser/policy/user_policy_cache.h"
#include "chrome/browser/policy/user_policy_identity_strategy.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_switches.h"

namespace {

const FilePath::CharType kPolicyDir[] = FILE_PATH_LITERAL("Device Management");
const FilePath::CharType kTokenCacheFile[] = FILE_PATH_LITERAL("Token");
const FilePath::CharType kPolicyCacheFile[] = FILE_PATH_LITERAL("Policy");

const int64 kServiceInitializationStartupDelay = 2000;

}  // namespace

namespace policy {

ProfilePolicyConnector::ProfilePolicyConnector(Profile* profile)
    : profile_(profile) {
  CommandLine* command_line = CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kDeviceManagementUrl)) {
    FilePath policy_cache_dir(profile_->GetPath());
    policy_cache_dir = policy_cache_dir.Append(kPolicyDir);

    identity_strategy_.reset(new UserPolicyIdentityStrategy(
        profile_,
        policy_cache_dir.Append(kTokenCacheFile)));
    cloud_policy_subsystem_.reset(new CloudPolicySubsystem(
        identity_strategy_.get(),
        new UserPolicyCache(policy_cache_dir.Append(kPolicyCacheFile))));

    BrowserPolicyConnector* browser_connector =
        g_browser_process->browser_policy_connector();

    managed_cloud_provider_.reset(new MergingPolicyProvider(
        browser_connector->GetManagedCloudProvider(),
        cloud_policy_subsystem_->GetManagedPolicyProvider()));
    recommended_cloud_provider_.reset(new MergingPolicyProvider(
        browser_connector->GetRecommendedCloudProvider(),
        cloud_policy_subsystem_->GetRecommendedPolicyProvider()));
  }
}

ProfilePolicyConnector::~ProfilePolicyConnector() {
  managed_cloud_provider_.reset();
  recommended_cloud_provider_.reset();
  cloud_policy_subsystem_.reset();
  identity_strategy_.reset();
}

void ProfilePolicyConnector::ScheduleServiceInitialization(
    int64 delay_milliseconds) {
  if (cloud_policy_subsystem_.get())
    cloud_policy_subsystem_->ScheduleServiceInitialization(delay_milliseconds);
}

void ProfilePolicyConnector::Initialize() {
  if (identity_strategy_.get())
    identity_strategy_->LoadTokenCache();
  if (cloud_policy_subsystem_.get())
    cloud_policy_subsystem_->Initialize(profile_->GetPrefs(),
                                        kServiceInitializationStartupDelay);
}

void ProfilePolicyConnector::Shutdown() {
  if (cloud_policy_subsystem_.get())
    cloud_policy_subsystem_->Shutdown();
}

ConfigurationPolicyProvider*
    ProfilePolicyConnector::GetManagedCloudProvider() {
  return managed_cloud_provider_.get();
}

ConfigurationPolicyProvider*
    ProfilePolicyConnector::GetRecommendedCloudProvider() {
  return recommended_cloud_provider_.get();
}

MergingPolicyProvider::MergingPolicyProvider(
    ConfigurationPolicyProvider* browser_policy_provider,
    ConfigurationPolicyProvider* profile_policy_provider)
    : ConfigurationPolicyProvider(
          ConfigurationPolicyPrefStore::GetChromePolicyDefinitionList()),
      browser_policy_provider_(browser_policy_provider),
      profile_policy_provider_(profile_policy_provider),
      browser_registrar_(new ConfigurationPolicyObserverRegistrar()),
      profile_registrar_(new ConfigurationPolicyObserverRegistrar()) {
  if (browser_policy_provider_)
    browser_registrar_->Init(browser_policy_provider_, this);
  if (profile_policy_provider_)
    profile_registrar_->Init(profile_policy_provider_, this);
}

MergingPolicyProvider::~MergingPolicyProvider() {
  if (browser_policy_provider_ || profile_policy_provider_) {
    FOR_EACH_OBSERVER(ConfigurationPolicyProvider::Observer,
                      observer_list_, OnProviderGoingAway());
  }
}

bool MergingPolicyProvider::Provide(ConfigurationPolicyStoreInterface* store) {
  // First, apply the profile policies and observe if interesting policies
  // have been applied.
  ObservingPolicyStoreInterface observe(store);
  bool rv = true;
  if (profile_policy_provider_)
    rv = profile_policy_provider_->Provide(&observe);

  // Now apply policies from the browser provider, if they were not applied
  // by the profile provider.
  // Currently, these include only the proxy settings.
  if (browser_policy_provider_) {
    FilteringPolicyStoreInterface filter(store,
                                         !observe.IsProxyPolicyApplied());
    rv = rv && browser_policy_provider_->Provide(&filter);
  }

  return rv;
}

void MergingPolicyProvider::AddObserver(
    ConfigurationPolicyProvider::Observer* observer) {
  observer_list_.AddObserver(observer);
}

void MergingPolicyProvider::RemoveObserver(
    ConfigurationPolicyProvider::Observer* observer) {
  observer_list_.RemoveObserver(observer);
}

void MergingPolicyProvider::OnUpdatePolicy() {
  FOR_EACH_OBSERVER(ConfigurationPolicyProvider::Observer,
                    observer_list_, OnUpdatePolicy());
}

void MergingPolicyProvider::OnProviderGoingAway() {
  if (browser_policy_provider_ || profile_policy_provider_) {
    FOR_EACH_OBSERVER(ConfigurationPolicyProvider::Observer,
                      observer_list_, OnProviderGoingAway());
    browser_registrar_.reset();
    profile_registrar_.reset();
    browser_policy_provider_ = NULL;
    profile_policy_provider_ = NULL;
  }
}

}  // namespace policy
