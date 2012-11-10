// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/app_sync_data.h"

#include "sync/protocol/app_specifics.pb.h"
#include "sync/protocol/sync.pb.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace extensions {

namespace {

const char kValidId[] = "abcdefghijklmnopabcdefghijklmnop";
const char kName[] = "MyExtension";
const char kValidVersion[] = "0.0.0.0";
const char kValidUpdateUrl[] =
    "http://clients2.google.com/service/update2/crx";
const char kOAuthClientId[] = "1234abcd";

}  // namespace

class AppSyncDataTest : public testing::Test {
 public:
  AppSyncDataTest() {}
  virtual ~AppSyncDataTest() {}

  void SetRequiredExtensionValues(
      sync_pb::ExtensionSpecifics* extension_specifics) {
    extension_specifics->set_id(kValidId);
    extension_specifics->set_update_url(kValidUpdateUrl);
    extension_specifics->set_version(kValidVersion);
    extension_specifics->set_enabled(false);
    extension_specifics->set_incognito_enabled(true);
    extension_specifics->set_name(kName);
  }
};

TEST_F(AppSyncDataTest, SyncDataToExtensionSyncDataForApp) {
  sync_pb::EntitySpecifics entity;
  sync_pb::AppSpecifics* app_specifics = entity.mutable_app();
  app_specifics->set_app_launch_ordinal(
      StringOrdinal::CreateInitialOrdinal().ToString());
  app_specifics->set_page_ordinal(
      StringOrdinal::CreateInitialOrdinal().ToString());
  sync_pb::AppNotificationSettings* notif_settings =
      app_specifics->mutable_notification_settings();
  notif_settings->set_oauth_client_id(kOAuthClientId);
  notif_settings->set_disabled(true);

  SetRequiredExtensionValues(app_specifics->mutable_extension());

  syncer::SyncData sync_data =
      syncer::SyncData::CreateLocalData("sync_tag", "non_unique_title", entity);

  AppSyncData app_sync_data(sync_data);
  EXPECT_EQ(app_specifics->app_launch_ordinal(),
            app_sync_data.app_launch_ordinal().ToString());
  EXPECT_EQ(app_specifics->page_ordinal(),
            app_sync_data.page_ordinal().ToString());
  EXPECT_EQ(notif_settings->oauth_client_id(),
            app_sync_data.notifications_client_id());
  EXPECT_EQ(notif_settings->disabled(),
            app_sync_data.notifications_disabled());
}



TEST_F(AppSyncDataTest, ExtensionSyncDataToSyncDataForApp) {
  sync_pb::EntitySpecifics entity;
  sync_pb::AppSpecifics* input_specifics = entity.mutable_app();
  input_specifics->set_app_launch_ordinal(
      StringOrdinal::CreateInitialOrdinal().ToString());
  input_specifics->set_page_ordinal(
      StringOrdinal::CreateInitialOrdinal().ToString());
  sync_pb::AppNotificationSettings* notif_settings =
      input_specifics->mutable_notification_settings();
  notif_settings->set_oauth_client_id(kOAuthClientId);
  notif_settings->set_disabled(true);

  SetRequiredExtensionValues(input_specifics->mutable_extension());

  syncer::SyncData sync_data =
      syncer::SyncData::CreateLocalData("sync_tag", "non_unique_title", entity);
  AppSyncData app_sync_data(sync_data);

  syncer::SyncData output_sync_data = app_sync_data.GetSyncData();
  EXPECT_TRUE(sync_data.GetSpecifics().has_app());
  const sync_pb::AppSpecifics& output_specifics =
      output_sync_data.GetSpecifics().app();
  EXPECT_EQ(input_specifics->SerializeAsString(),
            output_specifics.SerializeAsString());
}

// Ensures that invalid StringOrdinals don't break ExtensionSyncData.
TEST_F(AppSyncDataTest, ExtensionSyncDataInvalidOrdinal) {
  sync_pb::EntitySpecifics entity;
  sync_pb::AppSpecifics* app_specifics = entity.mutable_app();
  // Set the ordinals as invalid.
  app_specifics->set_app_launch_ordinal("");
  app_specifics->set_page_ordinal("");

  sync_pb::AppNotificationSettings* notif_settings =
      app_specifics->mutable_notification_settings();
  notif_settings->set_oauth_client_id(kOAuthClientId);
  notif_settings->set_disabled(true);

  SetRequiredExtensionValues(app_specifics->mutable_extension());

  syncer::SyncData sync_data =
      syncer::SyncData::CreateLocalData("sync_tag", "non_unique_title", entity);

  // There should be no issue loading the sync data.
  AppSyncData app_sync_data(sync_data);
  app_sync_data.GetSyncData();
}

}  // namespace extensions
