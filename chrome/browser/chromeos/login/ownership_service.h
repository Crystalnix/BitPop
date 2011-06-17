// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_OWNERSHIP_SERVICE_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_OWNERSHIP_SERVICE_H_
#pragma once

#include <string>
#include <vector>

#include "base/callback.h"
#include "base/synchronization/lock.h"
#include "chrome/browser/chromeos/login/owner_key_utils.h"
#include "chrome/browser/chromeos/login/owner_manager.h"
#include "chrome/browser/policy/proto/device_management_backend.pb.h"
#include "content/browser/browser_thread.h"
#include "content/common/notification_observer.h"
#include "content/common/notification_registrar.h"
#include "content/common/notification_service.h"

namespace base {
template <typename T> struct DefaultLazyInstanceTraits;
}

namespace em = enterprise_management;
namespace chromeos {

class OwnershipService : public NotificationObserver {
 public:
  enum Status {
    // Listed in upgrade order.
    OWNERSHIP_UNKNOWN = 0,
    OWNERSHIP_NONE,
    OWNERSHIP_TAKEN
  };

  // Returns the singleton instance of the OwnershipService.
  static OwnershipService* GetSharedInstance();
  virtual ~OwnershipService();

  // Called after FILE thread is created to prefetch ownership status and avoid
  // blocking on UI thread.
  void Prewarm();

  // Owner settings are being re-implemented as a single, signed protobuf
  // that is stored by the session manager.  Thus, to write a setting, you
  // need to have the existing policy, update it, re-sign it, and then have
  // it stored.  This could be done by requesting the policy every time, or
  // by caching it and updating it upon every successful store.
  // Caching is faster and easier, so we'll do that.  These are the
  // getters/setters for the cached policy.
  virtual void set_cached_policy(const em::PolicyData& pol);
  virtual bool has_cached_policy();
  virtual const em::PolicyData& cached_policy();

  // Sets a new owner key. This will _not_ load the key material from disk, but
  // rather update Chrome's in-memory copy of the key. |callback| will be
  // invoked once the operation completes.
  virtual void StartUpdateOwnerKey(const std::vector<uint8>& new_key,
                                   OwnerManager::KeyUpdateDelegate* d);

  // If the device has been owned already, posts a task to the FILE thread to
  // fetch the public key off disk.
  //
  // Sends out a OWNER_KEY_FETCH_ATTEMPT_SUCCESS notification on success,
  // OWNER_KEY_FETCH_ATTEMPT_FAILED on failure.
  virtual void StartLoadOwnerKeyAttempt();

  // Initiate an attempt to sign |data| with |private_key_|.  Will call
  // d->OnKeyOpComplete() when done.  Upon success, the signature will be passed
  // as the |payload| argument to d->OnKeyOpComplete().
  //
  // If you call this on a well-known thread, you'll be called back on that
  // thread.  Otherwise, you'll get called back on the UI thread.
  virtual void StartSigningAttempt(const std::string& data,
                                   OwnerManager::Delegate* d);

  // Initiate an attempt to verify that |signature| is valid over |data| with
  // |public_key_|.  When the attempt is completed, an appropriate KeyOpCode
  // will be passed to d->OnKeyOpComplete().
  //
  // If you call this on a well-known thread, you'll be called back on that
  // thread.  Otherwise, you'll get called back on the UI thread.
  virtual void StartVerifyAttempt(const std::string& data,
                                  const std::vector<uint8>& signature,
                                  OwnerManager::Delegate* d);

  // This method must be run on the FILE thread.
  virtual bool CurrentUserIsOwner();

  // This method should be run on FILE thread.
  // Note: not static, for better mocking.
  virtual bool IsAlreadyOwned();

  // This method can be run either on FILE or UI threads.  If |blocking| flag
  // is specified then it is guaranteed to return either OWNERSHIP_NONE or
  // OWNERSHIP_TAKEN (and not OWNERSHIP_UNKNOWN), however in this case it may
  // occasionally block doing i/o.
  virtual Status GetStatus(bool blocking);

 protected:
  OwnershipService();

  // NotificationObserver implementation.
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

 private:
  friend struct base::DefaultLazyInstanceTraits<OwnershipService>;
  friend class OwnershipServiceTest;

  // Task posted on FILE thread on startup to prefetch ownership status.
  void FetchStatus();

  // Sets ownership status. May be called on either thread.
  void SetStatus(Status new_status);

  static void UpdateOwnerKey(OwnershipService* service,
                             const BrowserThread::ID thread_id,
                             const std::vector<uint8>& new_key,
                             OwnerManager::KeyUpdateDelegate* d);
  static void TryLoadOwnerKeyAttempt(OwnershipService* service);
  static void TrySigningAttempt(OwnershipService* service,
                                const BrowserThread::ID thread_id,
                                const std::string& data,
                                OwnerManager::Delegate* d);
  static void TryVerifyAttempt(OwnershipService* service,
                               const BrowserThread::ID thread_id,
                               const std::string& data,
                               const std::vector<uint8>& signature,
                               OwnerManager::Delegate* d);
  static void FailAttempt(OwnerManager::Delegate* d);

  OwnerManager* manager() { return manager_.get(); }

  scoped_refptr<OwnerManager> manager_;
  scoped_refptr<OwnerKeyUtils> utils_;
  scoped_ptr<em::PolicyData> policy_;
  NotificationRegistrar notification_registrar_;
  volatile Status ownership_status_;
  base::Lock ownership_status_lock_;
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_OWNERSHIP_SERVICE_H_
