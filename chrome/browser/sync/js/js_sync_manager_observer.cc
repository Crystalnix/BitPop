// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/js/js_sync_manager_observer.h"

#include <cstddef>

#include "base/location.h"
#include "base/logging.h"
#include "base/string_number_conversions.h"
#include "base/values.h"
#include "chrome/browser/sync/internal_api/change_record.h"
#include "chrome/browser/sync/js/js_arg_list.h"
#include "chrome/browser/sync/js/js_event_details.h"
#include "chrome/browser/sync/js/js_event_handler.h"
#include "chrome/browser/sync/sessions/session_state.h"
#include "chrome/browser/sync/syncable/model_type.h"

namespace browser_sync {

using browser_sync::SyncProtocolError;

JsSyncManagerObserver::JsSyncManagerObserver() {}

JsSyncManagerObserver::~JsSyncManagerObserver() {}

void JsSyncManagerObserver::SetJsEventHandler(
    const WeakHandle<JsEventHandler>& event_handler) {
  event_handler_ = event_handler;
}

void JsSyncManagerObserver::OnSyncCycleCompleted(
    const sessions::SyncSessionSnapshot* snapshot) {
  if (!event_handler_.IsInitialized()) {
    return;
  }
  DictionaryValue details;
  details.Set("snapshot", snapshot->ToValue());
  HandleJsEvent(FROM_HERE, "onSyncCycleCompleted", JsEventDetails(&details));
}

void JsSyncManagerObserver::OnAuthError(
    const GoogleServiceAuthError& auth_error) {
  if (!event_handler_.IsInitialized()) {
    return;
  }
  DictionaryValue details;
  details.Set("authError", auth_error.ToValue());
  HandleJsEvent(FROM_HERE, "onAuthError", JsEventDetails(&details));
}

void JsSyncManagerObserver::OnUpdatedToken(const std::string& token) {
  if (!event_handler_.IsInitialized()) {
    return;
  }
  DictionaryValue details;
  details.SetString("token", "<redacted>");
  HandleJsEvent(FROM_HERE, "onUpdatedToken", JsEventDetails(&details));
}

void JsSyncManagerObserver::OnPassphraseRequired(
    sync_api::PassphraseRequiredReason reason,
    const sync_pb::EncryptedData& pending_keys) {
  if (!event_handler_.IsInitialized()) {
    return;
  }
  DictionaryValue details;
  details.SetString("reason",
                     sync_api::PassphraseRequiredReasonToString(reason));
  HandleJsEvent(FROM_HERE, "onPassphraseRequired", JsEventDetails(&details));
}

void JsSyncManagerObserver::OnPassphraseAccepted() {
  if (!event_handler_.IsInitialized()) {
    return;
  }
  DictionaryValue details;
  HandleJsEvent(FROM_HERE, "onPassphraseAccepted", JsEventDetails(&details));
}

void JsSyncManagerObserver::OnBootstrapTokenUpdated(
    const std::string& boostrap_token) {
  if (!event_handler_.IsInitialized()) {
    return;
  }
  DictionaryValue details;
  details.SetString("bootstrapToken", "<redacted>");
  HandleJsEvent(FROM_HERE, "OnBootstrapTokenUpdated", JsEventDetails(&details));
}

void JsSyncManagerObserver::OnEncryptedTypesChanged(
    syncable::ModelTypeSet encrypted_types,
    bool encrypt_everything) {
  if (!event_handler_.IsInitialized()) {
    return;
  }
  DictionaryValue details;
  details.Set("encryptedTypes",
               syncable::ModelTypeSetToValue(encrypted_types));
  details.SetBoolean("encryptEverything", encrypt_everything);
  HandleJsEvent(FROM_HERE,
                "onEncryptedTypesChanged", JsEventDetails(&details));
}

void JsSyncManagerObserver::OnEncryptionComplete() {
  if (!event_handler_.IsInitialized()) {
    return;
  }
  DictionaryValue details;
  HandleJsEvent(FROM_HERE, "onEncryptionComplete", JsEventDetails());
}

void JsSyncManagerObserver::OnActionableError(
    const SyncProtocolError& sync_error) {
  if (!event_handler_.IsInitialized()) {
    return;
  }
  DictionaryValue details;
  details.Set("syncError",  sync_error.ToValue());
  HandleJsEvent(FROM_HERE, "onActionableError",
                JsEventDetails(&details));
}

void JsSyncManagerObserver::OnInitializationComplete(
    const WeakHandle<JsBackend>& js_backend,
    bool success) {
  if (!event_handler_.IsInitialized()) {
    return;
  }
  // Ignore the |js_backend| argument; it's not really convertible to
  // JSON anyway.
  HandleJsEvent(FROM_HERE, "onInitializationComplete", JsEventDetails());
}

void JsSyncManagerObserver::OnStopSyncingPermanently() {
  if (!event_handler_.IsInitialized()) {
    return;
  }
  HandleJsEvent(FROM_HERE, "onStopSyncingPermanently", JsEventDetails());
}

void JsSyncManagerObserver::OnClearServerDataSucceeded() {
  if (!event_handler_.IsInitialized()) {
    return;
  }
  HandleJsEvent(FROM_HERE, "onClearServerDataSucceeded", JsEventDetails());
}

void JsSyncManagerObserver::OnClearServerDataFailed() {
  if (!event_handler_.IsInitialized()) {
    return;
  }
  HandleJsEvent(FROM_HERE, "onClearServerDataFailed", JsEventDetails());
}

void JsSyncManagerObserver::HandleJsEvent(
    const tracked_objects::Location& from_here,
    const std::string& name, const JsEventDetails& details) {
  if (!event_handler_.IsInitialized()) {
    NOTREACHED();
    return;
  }
  event_handler_.Call(from_here,
                      &JsEventHandler::HandleJsEvent, name, details);
}

}  // namespace browser_sync
