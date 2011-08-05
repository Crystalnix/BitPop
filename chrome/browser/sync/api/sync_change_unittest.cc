// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/api/sync_change.h"

#include <string>

#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "chrome/browser/sync/protocol/preference_specifics.pb.h"
#include "chrome/browser/sync/protocol/proto_value_conversions.h"

using browser_sync::EntitySpecificsToValue;

// Ordered list of SyncChange's.
typedef std::vector<SyncChange> SyncChangeList;

namespace {

typedef testing::Test SyncChangeTest;

TEST_F(SyncChangeTest, LocalDelete) {
  SyncChange::SyncChangeType change_type = SyncChange::ACTION_DELETE;
  std::string tag = "client_tag";
  SyncChange e(change_type,
              SyncData::CreateLocalData(tag));
  EXPECT_EQ(change_type, e.change_type());
  EXPECT_EQ(tag, e.sync_data().GetTag());
  EXPECT_EQ(syncable::UNSPECIFIED, e.sync_data().GetDataType());
}

TEST_F(SyncChangeTest, LocalUpdate) {
  SyncChange::SyncChangeType change_type = SyncChange::ACTION_UPDATE;
  sync_pb::EntitySpecifics specifics;
  sync_pb::PreferenceSpecifics* pref_specifics =
      specifics.MutableExtension(sync_pb::preference);
  pref_specifics->set_name("test");
  std::string tag = "client_tag";
  SyncChange e(change_type,
              SyncData::CreateLocalData(tag, specifics));
  EXPECT_EQ(change_type, e.change_type());
  EXPECT_EQ(tag, e.sync_data().GetTag());
  EXPECT_EQ(syncable::PREFERENCES, e.sync_data().GetDataType());
  scoped_ptr<DictionaryValue> ref_spec(EntitySpecificsToValue(specifics));
  scoped_ptr<DictionaryValue> e_spec(EntitySpecificsToValue(
      e.sync_data().GetSpecifics()));
  EXPECT_TRUE(ref_spec->Equals(e_spec.get()));
}

TEST_F(SyncChangeTest, LocalAdd) {
  SyncChange::SyncChangeType change_type = SyncChange::ACTION_ADD;
  sync_pb::EntitySpecifics specifics;
  sync_pb::PreferenceSpecifics* pref_specifics =
      specifics.MutableExtension(sync_pb::preference);
  pref_specifics->set_name("test");
  std::string tag = "client_tag";
  SyncChange e(change_type,
              SyncData::CreateLocalData(tag, specifics));
  EXPECT_EQ(change_type, e.change_type());
  EXPECT_EQ(tag, e.sync_data().GetTag());
  EXPECT_EQ(syncable::PREFERENCES, e.sync_data().GetDataType());
  scoped_ptr<DictionaryValue> ref_spec(EntitySpecificsToValue(specifics));
  scoped_ptr<DictionaryValue> e_spec(EntitySpecificsToValue(
      e.sync_data().GetSpecifics()));
  EXPECT_TRUE(ref_spec->Equals(e_spec.get()));
}

TEST_F(SyncChangeTest, SyncerChanges) {
  SyncChangeList change_list;

  // Create an update.
  sync_pb::EntitySpecifics update_specifics;
  sync_pb::PreferenceSpecifics* pref_specifics =
      update_specifics.MutableExtension(sync_pb::preference);
  pref_specifics->set_name("update");
  change_list.push_back(SyncChange(
      SyncChange::ACTION_UPDATE,
      SyncData::CreateRemoteData(update_specifics)));

  // Create an add.
  sync_pb::EntitySpecifics add_specifics;
  pref_specifics =
      add_specifics.MutableExtension(sync_pb::preference);
  pref_specifics->set_name("add");
  change_list.push_back(SyncChange(
      SyncChange::ACTION_ADD,
      SyncData::CreateRemoteData(add_specifics)));

  // Create a delete.
  sync_pb::EntitySpecifics delete_specifics;
  pref_specifics =
      delete_specifics.MutableExtension(sync_pb::preference);
  pref_specifics->set_name("add");
  change_list.push_back(SyncChange(
      SyncChange::ACTION_DELETE,
      SyncData::CreateRemoteData(delete_specifics)));

  ASSERT_EQ(3U, change_list.size());

  // Verify update.
  SyncChange e = change_list[0];
  EXPECT_EQ(SyncChange::ACTION_UPDATE, e.change_type());
  EXPECT_EQ(syncable::PREFERENCES, e.sync_data().GetDataType());
  scoped_ptr<DictionaryValue> ref_spec(EntitySpecificsToValue(
      update_specifics));
  scoped_ptr<DictionaryValue> e_spec(EntitySpecificsToValue(
      e.sync_data().GetSpecifics()));
  EXPECT_TRUE(ref_spec->Equals(e_spec.get()));

  // Verify add.
  e = change_list[1];
  EXPECT_EQ(SyncChange::ACTION_ADD, e.change_type());
  EXPECT_EQ(syncable::PREFERENCES, e.sync_data().GetDataType());
  ref_spec.reset(EntitySpecificsToValue(add_specifics));
  e_spec.reset(EntitySpecificsToValue(e.sync_data().GetSpecifics()));
  EXPECT_TRUE(ref_spec->Equals(e_spec.get()));

  // Verify delete.
  e = change_list[2];
  EXPECT_EQ(SyncChange::ACTION_DELETE, e.change_type());
  EXPECT_EQ(syncable::PREFERENCES, e.sync_data().GetDataType());
  ref_spec.reset(EntitySpecificsToValue(delete_specifics));
  e_spec.reset(EntitySpecificsToValue(e.sync_data().GetSpecifics()));
  EXPECT_TRUE(ref_spec->Equals(e_spec.get()));
}

}  // namespace
