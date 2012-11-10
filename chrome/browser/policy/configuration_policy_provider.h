// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_POLICY_CONFIGURATION_POLICY_PROVIDER_H_
#define CHROME_BROWSER_POLICY_CONFIGURATION_POLICY_PROVIDER_H_

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "base/observer_list.h"
#include "chrome/browser/policy/policy_bundle.h"

namespace policy {

// A mostly-abstract super class for platform-specific policy providers.
// Platform-specific policy providers (Windows Group Policy, gconf,
// etc.) should implement a subclass of this class.
class ConfigurationPolicyProvider {
 public:
  class Observer {
   public:
    virtual ~Observer();
    virtual void OnUpdatePolicy(ConfigurationPolicyProvider* provider) = 0;
    virtual void OnProviderGoingAway(ConfigurationPolicyProvider* provider);
  };

  ConfigurationPolicyProvider();

  virtual ~ConfigurationPolicyProvider();

  // Returns the current PolicyBundle.
  const PolicyBundle& policies() const { return policy_bundle_; }

  // Check whether this provider has completed initialization. This is used to
  // detect whether initialization is done in case providers implementations
  // need to do asynchronous operations for initialization.
  virtual bool IsInitializationComplete() const;

  // Asks the provider to refresh its policies. All the updates caused by this
  // call will be visible on the next call of OnUpdatePolicy on the observers,
  // which are guaranteed to happen even if the refresh fails.
  // It is possible that OnProviderGoingAway is called first though, and
  // OnUpdatePolicy won't be called if that happens.
  virtual void RefreshPolicies() = 0;

 protected:
  // Subclasses must invoke this to update the policies currently served by
  // this provider. UpdatePolicy() takes ownership of |policies|.
  // The observers are notified after the policies are updated.
  void UpdatePolicy(scoped_ptr<PolicyBundle> bundle);

 private:
  friend class ConfigurationPolicyObserverRegistrar;

  virtual void AddObserver(Observer* observer);
  virtual void RemoveObserver(Observer* observer);

  // The policies currently configured at this provider.
  PolicyBundle policy_bundle_;

  ObserverList<Observer, true> observer_list_;

  DISALLOW_COPY_AND_ASSIGN(ConfigurationPolicyProvider);
};

// Manages observers for a ConfigurationPolicyProvider. Is used to register
// observers, and automatically removes them upon destruction.
// Implementation detail: to avoid duplicate bookkeeping of registered
// observers, this registrar class acts as a proxy for notifications (since it
// needs to register itself anyway to get OnProviderGoingAway notifications).
class ConfigurationPolicyObserverRegistrar
    : ConfigurationPolicyProvider::Observer {
 public:
  ConfigurationPolicyObserverRegistrar();
  virtual ~ConfigurationPolicyObserverRegistrar();
  void Init(ConfigurationPolicyProvider* provider,
            ConfigurationPolicyProvider::Observer* observer);

  // ConfigurationPolicyProvider::Observer implementation:
  virtual void OnUpdatePolicy(ConfigurationPolicyProvider* provider) OVERRIDE;
  virtual void OnProviderGoingAway(
      ConfigurationPolicyProvider* provider) OVERRIDE;

  ConfigurationPolicyProvider* provider() { return provider_; }

 private:
  ConfigurationPolicyProvider* provider_;
  ConfigurationPolicyProvider::Observer* observer_;

  DISALLOW_COPY_AND_ASSIGN(ConfigurationPolicyObserverRegistrar);
};

}  // namespace policy

#endif  // CHROME_BROWSER_POLICY_CONFIGURATION_POLICY_PROVIDER_H_
