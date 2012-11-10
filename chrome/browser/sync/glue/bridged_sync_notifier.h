// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_GLUE_BRIDGED_SYNC_NOTIFIER_H_
#define CHROME_BROWSER_SYNC_GLUE_BRIDGED_SYNC_NOTIFIER_H_

#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "sync/notifier/sync_notifier.h"

namespace browser_sync {

class ChromeSyncNotificationBridge;

// A SyncNotifier implementation that wraps a ChromeSyncNotificationBridge
// and optionally a SyncNotifier delegate (which it takes ownership of).
// All SyncNotifier calls are passed straight through to the delegate,
// with the exception of AddObserver/RemoveObserver, which also result in
// the observer being registered/deregistered with the
// ChromeSyncNotificationBridge.
class BridgedSyncNotifier : public syncer::SyncNotifier {
 public:
  // Does not take ownership of |bridge|. Takes ownership of |delegate|.
  // |delegate| may be NULL.
  BridgedSyncNotifier(ChromeSyncNotificationBridge* bridge,
                      syncer::SyncNotifier* delegate);
  virtual ~BridgedSyncNotifier();

  // SyncNotifier implementation. Passes through all calls to the delegate.
  // RegisterHandler, UnregisterHandler, and UpdateRegisteredIds calls will
  // also be forwarded to the bridge.
  virtual void RegisterHandler(syncer::SyncNotifierObserver* handler) OVERRIDE;
  virtual void UpdateRegisteredIds(syncer::SyncNotifierObserver* handler,
                                   const syncer::ObjectIdSet& ids) OVERRIDE;
  virtual void UnregisterHandler(
      syncer::SyncNotifierObserver* handler) OVERRIDE;
  virtual void SetUniqueId(const std::string& unique_id) OVERRIDE;
  virtual void SetStateDeprecated(const std::string& state) OVERRIDE;
  virtual void UpdateCredentials(
      const std::string& email, const std::string& token) OVERRIDE;
  virtual void SendNotification(
      syncer::ModelTypeSet changed_types) OVERRIDE;

 private:
  // The notification bridge that we register the observers with.
  ChromeSyncNotificationBridge* bridge_;

  // The delegate we are wrapping.
  scoped_ptr<syncer::SyncNotifier> delegate_;
};

}  // namespace browser_sync

#endif  // CHROME_BROWSER_SYNC_GLUE_BRIDGED_SYNC_NOTIFIER_H_
