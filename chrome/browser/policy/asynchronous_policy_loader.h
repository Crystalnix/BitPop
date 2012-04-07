// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_POLICY_ASYNCHRONOUS_POLICY_LOADER_H_
#define CHROME_BROWSER_POLICY_ASYNCHRONOUS_POLICY_LOADER_H_
#pragma once

#include "base/callback.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time.h"
#include "chrome/browser/policy/asynchronous_policy_provider.h"
#include "chrome/browser/policy/configuration_policy_provider.h"
#include "chrome/browser/policy/policy_map.h"

class MessageLoop;

namespace policy {

// Used by the implementation of asynchronous policy provider to manage the
// tasks on the FILE thread that do the heavy lifting of loading policies.
class AsynchronousPolicyLoader
    : public base::RefCountedThreadSafe<AsynchronousPolicyLoader> {
 public:
  AsynchronousPolicyLoader(AsynchronousPolicyProvider::Delegate* delegate,
                           int reload_interval_minutes);

  // Triggers initial policy load, and installs |callback| as the callback to
  // invoke on policy updates.
  virtual void Init(const base::Closure& callback);

  // Reloads policy, sending notification of changes if necessary. Must be
  // called on the FILE thread. When |force| is true, the loader should do an
  // immediate full reload.
  virtual void Reload(bool force);

  // Stops any pending reload tasks. Updates callbacks won't be performed
  // anymore once the loader is stopped.
  virtual void Stop();

  const PolicyMap& policy() const { return policy_; }

 protected:
  // AsynchronousPolicyLoader objects should only be deleted by
  // RefCountedThreadSafe.
  friend class base::RefCountedThreadSafe<AsynchronousPolicyLoader>;
  virtual ~AsynchronousPolicyLoader();

  // Schedules a call to UpdatePolicy on |origin_loop_|. Takes ownership of
  // |new_policy|.
  void PostUpdatePolicyTask(PolicyMap* new_policy);

  AsynchronousPolicyProvider::Delegate* delegate() {
    return delegate_.get();
  }

  // Performs start operations that must be performed on the FILE thread.
  virtual void InitOnFileThread();

  // Performs stop operations that must be performed on the FILE thread.
  virtual void StopOnFileThread();

  // Schedules a reload task to run when |delay| expires. Must be called on the
  // FILE thread.
  void ScheduleReloadTask(const base::TimeDelta& delay);

  // Schedules a reload task to run after the number of minutes specified
  // in |reload_interval_minutes_|. Must be called on the FILE thread.
  void ScheduleFallbackReloadTask();

  void CancelReloadTask();

  // Invoked from the reload task on the FILE thread.
  void ReloadFromTask();

 private:
  friend class AsynchronousPolicyLoaderTest;

  // Finishes loader initialization after the threading system has been fully
  // intialized.
  void InitAfterFileThreadAvailable();

  // Replaces the existing policy to value map with a new one, sending
  // notification to the observers if there is a policy change. Must be called
  // on |origin_loop_| so that it's safe to call back into the provider, which
  // is not thread-safe. Takes ownership of |new_policy|.
  void UpdatePolicy(PolicyMap* new_policy);

  // Provides the low-level mechanics for loading policy.
  scoped_ptr<AsynchronousPolicyProvider::Delegate> delegate_;

  // Current policy.
  PolicyMap policy_;

  // Used to create and invalidate WeakPtrs on the FILE thread. These are only
  // used to post reload tasks that can be cancelled.
  base::WeakPtrFactory<AsynchronousPolicyLoader> weak_ptr_factory_;

  // The interval at which a policy reload will be triggered as a fallback.
  const base::TimeDelta  reload_interval_;

  // The message loop on which this object was constructed. Recorded so that
  // it's possible to call back into the non thread safe provider to fire the
  // notification.
  MessageLoop* origin_loop_;

  // True if Stop has been called.
  bool stopped_;

  // Callback to invoke on policy updates.
  base::Closure updates_callback_;

  DISALLOW_COPY_AND_ASSIGN(AsynchronousPolicyLoader);
};

}  // namespace policy

#endif  // CHROME_BROWSER_POLICY_ASYNCHRONOUS_POLICY_LOADER_H_
