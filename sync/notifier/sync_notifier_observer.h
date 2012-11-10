// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYNC_NOTIFIER_SYNC_NOTIFIER_OBSERVER_H_
#define SYNC_NOTIFIER_SYNC_NOTIFIER_OBSERVER_H_

#include "sync/notifier/object_id_payload_map.h"
#include "sync/notifier/notifications_disabled_reason.h"

namespace syncer {

enum IncomingNotificationSource {
  // The server is notifying us that one or more datatypes have stale data.
  REMOTE_NOTIFICATION,
  // A chrome datatype is requesting an optimistic refresh of its data.
  LOCAL_NOTIFICATION,
};

class SyncNotifierObserver {
 public:
  // Called when notifications are enabled.
  virtual void OnNotificationsEnabled() = 0;

  // Called when notifications are disabled, with the reason in
  // |reason|.
  virtual void OnNotificationsDisabled(
      NotificationsDisabledReason reason) = 0;

  // Called when a notification is received.  The per-id payloads
  // are in |type_payloads| and the source is in |source|.
  virtual void OnIncomingNotification(
      const ObjectIdPayloadMap& id_payloads,
      IncomingNotificationSource source) = 0;

 protected:
  virtual ~SyncNotifierObserver() {}
};

}  // namespace syncer

#endif  // SYNC_NOTIFIER_SYNC_NOTIFIER_OBSERVER_H_
