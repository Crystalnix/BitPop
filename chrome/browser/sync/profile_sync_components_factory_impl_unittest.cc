// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "base/command_line.h"
#include "base/file_path.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "chrome/browser/sync/glue/data_type_controller.h"
#include "chrome/browser/sync/profile_sync_components_factory_impl.h"
#include "chrome/browser/sync/profile_sync_service.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/test/base/testing_profile.h"
#include "content/test/test_browser_thread.h"
#include "testing/gtest/include/gtest/gtest.h"

using browser_sync::DataTypeController;
using content::BrowserThread;

class ProfileSyncComponentsFactoryImplTest : public testing::Test {
 protected:
  ProfileSyncComponentsFactoryImplTest()
      : ui_thread_(BrowserThread::UI, &message_loop_) {}

  virtual void SetUp() {
    profile_.reset(new TestingProfile());
    FilePath program_path(FILE_PATH_LITERAL("chrome.exe"));
    command_line_.reset(new CommandLine(program_path));
  }

  // Returns the collection of default datatypes.
  static std::vector<syncable::ModelType> DefaultDatatypes() {
    std::vector<syncable::ModelType> datatypes;
    datatypes.push_back(syncable::BOOKMARKS);
    datatypes.push_back(syncable::PREFERENCES);
    datatypes.push_back(syncable::AUTOFILL);
    datatypes.push_back(syncable::THEMES);
    datatypes.push_back(syncable::EXTENSIONS);
    datatypes.push_back(syncable::APPS);
    datatypes.push_back(syncable::APP_NOTIFICATIONS);
    datatypes.push_back(syncable::AUTOFILL_PROFILE);
    datatypes.push_back(syncable::PASSWORDS);
    datatypes.push_back(syncable::TYPED_URLS);
    datatypes.push_back(syncable::SEARCH_ENGINES);
    return datatypes;
  }

  // Returns the number of default datatypes.
  static size_t DefaultDatatypesCount() {
    return DefaultDatatypes().size();
  }

  // Asserts that all the default datatypes are in |map|, except
  // for |exception_type|, which unless it is UNDEFINED, is asserted to
  // not be in |map|.
  static void CheckDefaultDatatypesInMapExcept(
      DataTypeController::StateMap* map,
      syncable::ModelType exception_type) {
    std::vector<syncable::ModelType> defaults = DefaultDatatypes();
    std::vector<syncable::ModelType>::iterator iter;
    for (iter = defaults.begin(); iter != defaults.end(); ++iter) {
      if (exception_type != syncable::UNSPECIFIED && exception_type == *iter)
        EXPECT_EQ(0U, map->count(*iter))
            << *iter << " found in dataypes map, shouldn't be there.";
      else
        EXPECT_EQ(1U, map->count(*iter))
            << *iter << " not found in datatypes map";
    }
  }

  // Asserts that if you apply the command line switch |cmd_switch|,
  // all types are enabled except for |type|, which is disabled.
  void TestSwitchDisablesType(const char* cmd_switch,
                              syncable::ModelType type) {
    command_line_->AppendSwitch(cmd_switch);
    scoped_ptr<ProfileSyncService> pss(
        new ProfileSyncService(
            new ProfileSyncComponentsFactoryImpl(profile_.get(),
                                                 command_line_.get()),
            profile_.get(),
            NULL,
            ProfileSyncService::MANUAL_START));
    pss->factory()->RegisterDataTypes(pss.get());
    DataTypeController::StateMap controller_states;
    pss->GetDataTypeControllerStates(&controller_states);
    EXPECT_EQ(DefaultDatatypesCount() - 1, controller_states.size());
    CheckDefaultDatatypesInMapExcept(&controller_states, type);
  }

  MessageLoop message_loop_;
  content::TestBrowserThread ui_thread_;
  scoped_ptr<Profile> profile_;
  scoped_ptr<CommandLine> command_line_;
};

TEST_F(ProfileSyncComponentsFactoryImplTest, CreatePSSDefault) {
  scoped_ptr<ProfileSyncService> pss(
      new ProfileSyncService(
          new ProfileSyncComponentsFactoryImpl(profile_.get(),
                                               command_line_.get()),
      profile_.get(),
      NULL,
      ProfileSyncService::MANUAL_START));
  pss->factory()->RegisterDataTypes(pss.get());
  DataTypeController::StateMap controller_states;
  pss->GetDataTypeControllerStates(&controller_states);
  EXPECT_EQ(DefaultDatatypesCount(), controller_states.size());
  CheckDefaultDatatypesInMapExcept(&controller_states, syncable::UNSPECIFIED);
}

TEST_F(ProfileSyncComponentsFactoryImplTest, CreatePSSDisableAutofill) {
  TestSwitchDisablesType(switches::kDisableSyncAutofill,
                         syncable::AUTOFILL);
}

TEST_F(ProfileSyncComponentsFactoryImplTest, CreatePSSDisableBookmarks) {
  TestSwitchDisablesType(switches::kDisableSyncBookmarks,
                         syncable::BOOKMARKS);
}

TEST_F(ProfileSyncComponentsFactoryImplTest, CreatePSSDisablePreferences) {
  TestSwitchDisablesType(switches::kDisableSyncPreferences,
                         syncable::PREFERENCES);
}

TEST_F(ProfileSyncComponentsFactoryImplTest, CreatePSSDisableThemes) {
  TestSwitchDisablesType(switches::kDisableSyncThemes,
                         syncable::THEMES);
}

TEST_F(ProfileSyncComponentsFactoryImplTest, CreatePSSDisableExtensions) {
  TestSwitchDisablesType(switches::kDisableSyncExtensions,
                         syncable::EXTENSIONS);
}

TEST_F(ProfileSyncComponentsFactoryImplTest, CreatePSSDisableApps) {
  TestSwitchDisablesType(switches::kDisableSyncApps,
                         syncable::APPS);
}

TEST_F(ProfileSyncComponentsFactoryImplTest, CreatePSSDisableAutofillProfile) {
  TestSwitchDisablesType(switches::kDisableSyncAutofillProfile,
                         syncable::AUTOFILL_PROFILE);
}

TEST_F(ProfileSyncComponentsFactoryImplTest, CreatePSSDisablePasswords) {
  TestSwitchDisablesType(switches::kDisableSyncPasswords,
                         syncable::PASSWORDS);
}
