// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sync/internal_api/js_sync_manager_observer.h"

#include "base/basictypes.h"
#include "base/location.h"
#include "base/message_loop.h"
#include "base/values.h"
#include "sync/internal_api/public/base/model_type.h"
#include "sync/internal_api/public/sessions/sync_session_snapshot.h"
#include "sync/internal_api/public/util/sync_string_conversions.h"
#include "sync/internal_api/public/util/weak_handle.h"
#include "sync/js/js_event_details.h"
#include "sync/js/js_test_util.h"
#include "sync/protocol/sync_protocol_error.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace syncer {
namespace {

using ::testing::InSequence;
using ::testing::StrictMock;

class JsSyncManagerObserverTest : public testing::Test {
 protected:
  JsSyncManagerObserverTest() {
    js_sync_manager_observer_.SetJsEventHandler(
        mock_js_event_handler_.AsWeakHandle());
  }

 private:
  // This must be destroyed after the member variables below in order
  // for WeakHandles to be destroyed properly.
  MessageLoop message_loop_;

 protected:
  StrictMock<MockJsEventHandler> mock_js_event_handler_;
  JsSyncManagerObserver js_sync_manager_observer_;

  void PumpLoop() {
    message_loop_.RunAllPending();
  }
};

TEST_F(JsSyncManagerObserverTest, NoArgNotifiations) {
  InSequence dummy;

  EXPECT_CALL(mock_js_event_handler_,
              HandleJsEvent("onStopSyncingPermanently",
                            HasDetails(JsEventDetails())));
  EXPECT_CALL(mock_js_event_handler_,
              HandleJsEvent("onEncryptionComplete",
                            HasDetails(JsEventDetails())));

  js_sync_manager_observer_.OnStopSyncingPermanently();
  js_sync_manager_observer_.OnEncryptionComplete();
  PumpLoop();
}

TEST_F(JsSyncManagerObserverTest, OnInitializationComplete) {
  DictionaryValue expected_details;
  syncer::ModelTypeSet restored_types;
  restored_types.Put(BOOKMARKS);
  restored_types.Put(NIGORI);
  expected_details.Set("restoredTypes", ModelTypeSetToValue(restored_types));

  EXPECT_CALL(mock_js_event_handler_,
              HandleJsEvent("onInitializationComplete",
                            HasDetailsAsDictionary(expected_details)));

  js_sync_manager_observer_.OnInitializationComplete(
      WeakHandle<JsBackend>(), true, restored_types);
  PumpLoop();
}

TEST_F(JsSyncManagerObserverTest, OnSyncCycleCompleted) {
  ModelTypePayloadMap download_progress_markers;
  sessions::SyncSessionSnapshot snapshot(sessions::ModelNeutralState(),
                                         false,
                                         ModelTypeSet(),
                                         download_progress_markers,
                                         false,
                                         true,
                                         8,
                                         5,
                                         2,
                                         7,
                                         sessions::SyncSourceInfo(),
                                         false,
                                         0,
                                         base::Time::Now(),
                                         false);
  DictionaryValue expected_details;
  expected_details.Set("snapshot", snapshot.ToValue());

  EXPECT_CALL(mock_js_event_handler_,
              HandleJsEvent("onSyncCycleCompleted",
                           HasDetailsAsDictionary(expected_details)));

  js_sync_manager_observer_.OnSyncCycleCompleted(snapshot);
  PumpLoop();
}

TEST_F(JsSyncManagerObserverTest, OnActionableError) {
  SyncProtocolError sync_error;
  sync_error.action = CLEAR_USER_DATA_AND_RESYNC;
  sync_error.error_type = TRANSIENT_ERROR;
  DictionaryValue expected_details;
  expected_details.Set("syncError", sync_error.ToValue());

  EXPECT_CALL(mock_js_event_handler_,
              HandleJsEvent("onActionableError",
                           HasDetailsAsDictionary(expected_details)));

  js_sync_manager_observer_.OnActionableError(sync_error);
  PumpLoop();
}


TEST_F(JsSyncManagerObserverTest, OnConnectionStatusChange) {
  const ConnectionStatus kStatus = CONNECTION_AUTH_ERROR;
  DictionaryValue expected_details;
  expected_details.SetString("status",
                             ConnectionStatusToString(kStatus));

  EXPECT_CALL(mock_js_event_handler_,
              HandleJsEvent("onConnectionStatusChange",
                            HasDetailsAsDictionary(expected_details)));

  js_sync_manager_observer_.OnConnectionStatusChange(kStatus);
  PumpLoop();
}

TEST_F(JsSyncManagerObserverTest, OnPassphraseRequired) {
  InSequence dummy;

  DictionaryValue reason_passphrase_not_required_details;
  DictionaryValue reason_encryption_details;
  DictionaryValue reason_decryption_details;

  reason_passphrase_not_required_details.SetString(
      "reason",
      PassphraseRequiredReasonToString(REASON_PASSPHRASE_NOT_REQUIRED));
  reason_encryption_details.SetString(
      "reason",
      PassphraseRequiredReasonToString(REASON_ENCRYPTION));
  reason_decryption_details.SetString(
      "reason",
      PassphraseRequiredReasonToString(REASON_DECRYPTION));

  EXPECT_CALL(mock_js_event_handler_,
              HandleJsEvent("onPassphraseRequired",
                           HasDetailsAsDictionary(
                               reason_passphrase_not_required_details)));
  EXPECT_CALL(mock_js_event_handler_,
              HandleJsEvent("onPassphraseRequired",
                           HasDetailsAsDictionary(reason_encryption_details)));
  EXPECT_CALL(mock_js_event_handler_,
              HandleJsEvent("onPassphraseRequired",
                           HasDetailsAsDictionary(reason_decryption_details)));

  js_sync_manager_observer_.OnPassphraseRequired(
      REASON_PASSPHRASE_NOT_REQUIRED,
      sync_pb::EncryptedData());
  js_sync_manager_observer_.OnPassphraseRequired(REASON_ENCRYPTION,
                                                 sync_pb::EncryptedData());
  js_sync_manager_observer_.OnPassphraseRequired(REASON_DECRYPTION,
                                                 sync_pb::EncryptedData());
  PumpLoop();
}

TEST_F(JsSyncManagerObserverTest, SensitiveNotifiations) {
  DictionaryValue redacted_token_details;
  redacted_token_details.SetString("token", "<redacted>");
  DictionaryValue redacted_bootstrap_token_details;
  redacted_bootstrap_token_details.SetString("bootstrapToken", "<redacted>");

  EXPECT_CALL(mock_js_event_handler_,
              HandleJsEvent("onUpdatedToken",
                           HasDetailsAsDictionary(redacted_token_details)));
  EXPECT_CALL(mock_js_event_handler_,
              HandleJsEvent(
                  "OnBootstrapTokenUpdated",
                  HasDetailsAsDictionary(redacted_bootstrap_token_details)));

  js_sync_manager_observer_.OnUpdatedToken("sensitive_token");
  js_sync_manager_observer_.OnBootstrapTokenUpdated("sensitive_token");
  PumpLoop();
}

TEST_F(JsSyncManagerObserverTest, OnEncryptedTypesChanged) {
  DictionaryValue expected_details;
  ListValue* encrypted_type_values = new ListValue();
  const bool encrypt_everything = false;
  expected_details.Set("encryptedTypes", encrypted_type_values);
  expected_details.SetBoolean("encryptEverything", encrypt_everything);
  ModelTypeSet encrypted_types;

  for (int i = FIRST_REAL_MODEL_TYPE; i < MODEL_TYPE_COUNT; ++i) {
    ModelType type = ModelTypeFromInt(i);
    encrypted_types.Put(type);
    encrypted_type_values->Append(Value::CreateStringValue(
        ModelTypeToString(type)));
  }

  EXPECT_CALL(mock_js_event_handler_,
              HandleJsEvent("onEncryptedTypesChanged",
                            HasDetailsAsDictionary(expected_details)));

  js_sync_manager_observer_.OnEncryptedTypesChanged(
      encrypted_types, encrypt_everything);
  PumpLoop();
}

}  // namespace
}  // namespace syncer
