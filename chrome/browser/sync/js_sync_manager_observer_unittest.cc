// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/js_sync_manager_observer.h"

#include <cstddef>

#include "base/basictypes.h"
#include "base/values.h"
#include "chrome/browser/sync/engine/syncapi.h"
#include "chrome/browser/sync/js_arg_list.h"
#include "chrome/browser/sync/js_test_util.h"
#include "chrome/browser/sync/sessions/session_state.h"
#include "chrome/browser/sync/syncable/model_type.h"
#include "chrome/test/sync/engine/test_user_share.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace browser_sync {
namespace {

using ::testing::InSequence;
using ::testing::StrictMock;

class JsSyncManagerObserverTest : public testing::Test {
 protected:
  JsSyncManagerObserverTest() : sync_manager_observer_(&mock_router_) {}

  StrictMock<MockJsEventRouter> mock_router_;
  JsSyncManagerObserver sync_manager_observer_;
};

TEST_F(JsSyncManagerObserverTest, NoArgNotifiations) {
  InSequence dummy;

  EXPECT_CALL(mock_router_,
              RouteJsEvent("onInitializationComplete",
                           HasArgs(JsArgList()), NULL));
  EXPECT_CALL(mock_router_,
              RouteJsEvent("onPassphraseFailed",
                           HasArgs(JsArgList()), NULL));

  EXPECT_CALL(mock_router_,
              RouteJsEvent("onStopSyncingPermanently",
                           HasArgs(JsArgList()), NULL));
  EXPECT_CALL(mock_router_,
              RouteJsEvent("onClearServerDataSucceeded",
                           HasArgs(JsArgList()), NULL));
  EXPECT_CALL(mock_router_,
              RouteJsEvent("onClearServerDataFailed",
                           HasArgs(JsArgList()), NULL));

  sync_manager_observer_.OnInitializationComplete();
  sync_manager_observer_.OnPassphraseFailed();
  sync_manager_observer_.OnStopSyncingPermanently();
  sync_manager_observer_.OnClearServerDataSucceeded();
  sync_manager_observer_.OnClearServerDataFailed();
}

TEST_F(JsSyncManagerObserverTest, OnChangesComplete) {
  InSequence dummy;

  for (int i = syncable::FIRST_REAL_MODEL_TYPE;
       i < syncable::MODEL_TYPE_COUNT; ++i) {
    const std::string& model_type_str =
        syncable::ModelTypeToString(syncable::ModelTypeFromInt(i));
    ListValue expected_args;
    expected_args.Append(Value::CreateStringValue(model_type_str));
    EXPECT_CALL(mock_router_,
                RouteJsEvent("onChangesComplete",
                             HasArgsAsList(expected_args), NULL));
  }

  for (int i = syncable::FIRST_REAL_MODEL_TYPE;
       i < syncable::MODEL_TYPE_COUNT; ++i) {
    sync_manager_observer_.OnChangesComplete(syncable::ModelTypeFromInt(i));
  }
}

TEST_F(JsSyncManagerObserverTest, OnSyncCycleCompleted) {
  std::string download_progress_markers[syncable::MODEL_TYPE_COUNT];
  sessions::SyncSessionSnapshot snapshot(sessions::SyncerStatus(),
                                         sessions::ErrorCounters(),
                                         100,
                                         false,
                                         syncable::ModelTypeBitSet(),
                                         download_progress_markers,
                                         false,
                                         true,
                                         100,
                                         5,
                                         false,
                                         sessions::SyncSourceInfo());
  ListValue expected_args;
  expected_args.Append(snapshot.ToValue());

  EXPECT_CALL(mock_router_,
              RouteJsEvent("onSyncCycleCompleted",
                           HasArgsAsList(expected_args), NULL));

  sync_manager_observer_.OnSyncCycleCompleted(&snapshot);
}

TEST_F(JsSyncManagerObserverTest, OnAuthError) {
  GoogleServiceAuthError error(GoogleServiceAuthError::TWO_FACTOR);
  ListValue expected_args;
  expected_args.Append(error.ToValue());

  EXPECT_CALL(mock_router_,
              RouteJsEvent("onAuthError",
                           HasArgsAsList(expected_args), NULL));

  sync_manager_observer_.OnAuthError(error);
}

TEST_F(JsSyncManagerObserverTest, OnPassphraseRequired) {
  InSequence dummy;

  ListValue true_args, false_args;
  true_args.Append(Value::CreateBooleanValue(true));
  false_args.Append(Value::CreateBooleanValue(false));

  EXPECT_CALL(mock_router_,
              RouteJsEvent("onPassphraseRequired",
                           HasArgsAsList(false_args), NULL));
  EXPECT_CALL(mock_router_,
              RouteJsEvent("onPassphraseRequired",
                           HasArgsAsList(true_args), NULL));

  sync_manager_observer_.OnPassphraseRequired(false);
  sync_manager_observer_.OnPassphraseRequired(true);
}

TEST_F(JsSyncManagerObserverTest, SensitiveNotifiations) {
  ListValue redacted_args;
  redacted_args.Append(Value::CreateStringValue("<redacted>"));

  EXPECT_CALL(mock_router_,
              RouteJsEvent("onUpdatedToken",
                           HasArgsAsList(redacted_args), NULL));
  EXPECT_CALL(mock_router_,
              RouteJsEvent("onPassphraseAccepted",
                           HasArgsAsList(redacted_args), NULL));

  sync_manager_observer_.OnUpdatedToken("sensitive_token");
  sync_manager_observer_.OnPassphraseAccepted("sensitive_token");
}

TEST_F(JsSyncManagerObserverTest, OnEncryptionComplete) {
  ListValue expected_args;
  ListValue* encrypted_type_values = new ListValue();
  syncable::ModelTypeSet encrypted_types;

  expected_args.Append(encrypted_type_values);
  for (int i = syncable::FIRST_REAL_MODEL_TYPE;
       i < syncable::MODEL_TYPE_COUNT; ++i) {
    syncable::ModelType type = syncable::ModelTypeFromInt(i);
    encrypted_types.insert(type);
    encrypted_type_values->Append(Value::CreateStringValue(
        syncable::ModelTypeToString(type)));
  }

  EXPECT_CALL(mock_router_,
              RouteJsEvent("onEncryptionComplete",
                           HasArgsAsList(expected_args), NULL));

  sync_manager_observer_.OnEncryptionComplete(encrypted_types);
}

TEST_F(JsSyncManagerObserverTest, OnMigrationNeededForTypes) {
  ListValue expected_args;
  ListValue* type_values = new ListValue();
  syncable::ModelTypeSet types;

  expected_args.Append(type_values);
  for (int i = syncable::FIRST_REAL_MODEL_TYPE;
       i < syncable::MODEL_TYPE_COUNT; ++i) {
    syncable::ModelType type = syncable::ModelTypeFromInt(i);
    types.insert(type);
    type_values->Append(Value::CreateStringValue(
        syncable::ModelTypeToString(type)));
  }

  EXPECT_CALL(mock_router_,
              RouteJsEvent("onMigrationNeededForTypes",
                           HasArgsAsList(expected_args), NULL));

  sync_manager_observer_.OnMigrationNeededForTypes(types);
}

namespace {

// Makes a node of the given model type.  Returns the id of the
// newly-created node.
int64 MakeNode(sync_api::UserShare* share, syncable::ModelType model_type) {
  sync_api::WriteTransaction trans(share);
  sync_api::ReadNode root_node(&trans);
  root_node.InitByRootLookup();
  sync_api::WriteNode node(&trans);
  EXPECT_TRUE(node.InitUniqueByCreation(
      model_type, root_node,
      syncable::ModelTypeToString(model_type)));
  node.SetIsFolder(false);
  return node.GetId();
}

}  // namespace

TEST_F(JsSyncManagerObserverTest, OnChangesApplied) {
  InSequence dummy;

  TestUserShare test_user_share;
  test_user_share.SetUp();

  // We don't test with passwords as that requires additional setup.

  // Build a list of example ChangeRecords.
  sync_api::SyncManager::ChangeRecord changes[syncable::MODEL_TYPE_COUNT];
  for (int i = syncable::AUTOFILL_PROFILE;
       i < syncable::MODEL_TYPE_COUNT; ++i) {
    changes[i].id =
        MakeNode(test_user_share.user_share(), syncable::ModelTypeFromInt(i));
    switch (i % 3) {
      case 0:
        changes[i].action =
            sync_api::SyncManager::ChangeRecord::ACTION_ADD;
        break;
      case 1:
        changes[i].action =
            sync_api::SyncManager::ChangeRecord::ACTION_UPDATE;
        break;
      default:
        changes[i].action =
            sync_api::SyncManager::ChangeRecord::ACTION_DELETE;
        break;
    }
    {
      sync_api::ReadTransaction trans(test_user_share.user_share());
      sync_api::ReadNode node(&trans);
      EXPECT_TRUE(node.InitByIdLookup(changes[i].id));
      changes[i].specifics = node.GetEntry()->Get(syncable::SPECIFICS);
    }
  }

  // For each i, we call OnChangesApplied() with the first arg equal
  // to i cast to ModelType and the second argument with the changes
  // starting from changes[i].

  // Set expectations for each data type.
  for (int i = syncable::AUTOFILL_PROFILE;
       i < syncable::MODEL_TYPE_COUNT; ++i) {
    const std::string& model_type_str =
        syncable::ModelTypeToString(syncable::ModelTypeFromInt(i));
    ListValue expected_args;
    expected_args.Append(Value::CreateStringValue(model_type_str));
    ListValue* expected_changes = new ListValue();
    expected_args.Append(expected_changes);
    for (int j = i; j < syncable::MODEL_TYPE_COUNT; ++j) {
      sync_api::ReadTransaction trans(test_user_share.user_share());
      expected_changes->Append(changes[j].ToValue(&trans));
    }
    EXPECT_CALL(mock_router_,
                RouteJsEvent("onChangesApplied",
                             HasArgsAsList(expected_args), NULL));
  }

  // Fire OnChangesApplied() for each data type.
  for (int i = syncable::AUTOFILL_PROFILE;
       i < syncable::MODEL_TYPE_COUNT; ++i) {
    sync_api::ReadTransaction trans(test_user_share.user_share());
    sync_manager_observer_.OnChangesApplied(syncable::ModelTypeFromInt(i),
                                            &trans, &changes[i],
                                            syncable::MODEL_TYPE_COUNT - i);
  }

  test_user_share.TearDown();
}

}  // namespace
}  // namespace browser_sync
