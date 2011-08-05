// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/command_line.h"
#include "base/message_loop.h"
#include "base/path_service.h"
#include "base/scoped_temp_dir.h"
#include "base/system_monitor/system_monitor.h"
#include "base/values.h"
#include "chrome/browser/prefs/browser_prefs.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/testing_browser_process_test.h"
#include "chrome/test/testing_pref_service.h"
#include "content/browser/browser_thread.h"
#include "content/common/notification_service.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {
// This global variable is used to check that value returned to different
// observers is the same.
Profile* g_created_profile;

}  // namespace

class ProfileManagerTest : public TestingBrowserProcessTest {
 protected:
  ProfileManagerTest()
      : ui_thread_(BrowserThread::UI, &message_loop_),
        file_thread_(BrowserThread::FILE, &message_loop_),
        profile_manager_(new ProfileManagerWithoutInit),
        local_state_(testing_browser_process_.get()) {
  }

  virtual void SetUp() {
    // Create a new temporary directory, and store the path
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
  }

  virtual void TearDown() {
    profile_manager_.reset();
  }

  class MockObserver : public ProfileManagerObserver {
   public:
    MOCK_METHOD1(OnProfileCreated, void(Profile* profile));
  };

  // The path to temporary directory used to contain the test operations.
  ScopedTempDir temp_dir_;

  MessageLoopForUI message_loop_;
  BrowserThread ui_thread_;
  BrowserThread file_thread_;

  base::SystemMonitor system_monitor_dummy_;

  // Also will test profile deletion.
  scoped_ptr<ProfileManager> profile_manager_;

  ScopedTestingLocalState local_state_;
};

TEST_F(ProfileManagerTest, GetProfile) {
  FilePath dest_path = temp_dir_.path();
  dest_path = dest_path.Append(FILE_PATH_LITERAL("New Profile"));

  Profile* profile;

  // Successfully create a profile.
  profile = profile_manager_->GetProfile(dest_path);
  EXPECT_TRUE(profile);

  // The profile already exists when we call GetProfile. Just load it.
  EXPECT_EQ(profile, profile_manager_->GetProfile(dest_path));
}

TEST_F(ProfileManagerTest, DefaultProfileDir) {
  CommandLine *cl = CommandLine::ForCurrentProcess();
  std::string profile_dir("my_user");

  cl->AppendSwitch(switches::kTestType);

  FilePath expected_default =
      FilePath().AppendASCII(chrome::kNotSignedInProfile);
  EXPECT_EQ(expected_default.value(),
            profile_manager_->GetCurrentProfileDir().value());
}

#if defined(OS_CHROMEOS)
// This functionality only exists on Chrome OS.
TEST_F(ProfileManagerTest, LoggedInProfileDir) {
  CommandLine *cl = CommandLine::ForCurrentProcess();
  std::string profile_dir("my_user");

  cl->AppendSwitchASCII(switches::kLoginProfile, profile_dir);
  cl->AppendSwitch(switches::kTestType);

  FilePath expected_default =
      FilePath().AppendASCII(chrome::kNotSignedInProfile);
  EXPECT_EQ(expected_default.value(),
            profile_manager_->GetCurrentProfileDir().value());

  profile_manager_->Observe(NotificationType::LOGIN_USER_CHANGED,
                           NotificationService::AllSources(),
                           NotificationService::NoDetails());
  FilePath expected_logged_in(profile_dir);
  EXPECT_EQ(expected_logged_in.value(),
            profile_manager_->GetCurrentProfileDir().value());
  VLOG(1) << temp_dir_.path().Append(
      profile_manager_->GetCurrentProfileDir()).value();
}

#endif

TEST_F(ProfileManagerTest, RegisterProfileName) {
  // Create a test profile.
  FilePath dest_path1 = temp_dir_.path();
  std::string dir_base("New Profile 1");
  dest_path1 = dest_path1.Append(FILE_PATH_LITERAL("New Profile 1"));
  Profile* profile1;
  profile1 = profile_manager_->GetProfile(dest_path1);
  ASSERT_TRUE(profile1);

  // Simulate registration of the profile name (normally triggered by the
  // change of the kGoogleServicesUsername preference):
  std::string testname("testname");
  profile1->GetPrefs()->SetString(prefs::kGoogleServicesUsername, testname);
  profile_manager_->RegisterProfileName(profile1);

  // Profile name should be associated with the directory in Local State.
  std::string stored_profile_name;
  const DictionaryValue* path_map = static_cast<const DictionaryValue*>(
      local_state_.Get()->GetUserPref(prefs::kProfileDirectoryMap));
  path_map->GetString(dir_base, &stored_profile_name);
  ASSERT_STREQ(testname.c_str(), stored_profile_name.c_str());
}

TEST_F(ProfileManagerTest, CreateAndUseTwoProfiles) {
  FilePath dest_path1 = temp_dir_.path();
  dest_path1 = dest_path1.Append(FILE_PATH_LITERAL("New Profile 1"));

  FilePath dest_path2 = temp_dir_.path();
  dest_path2 = dest_path2.Append(FILE_PATH_LITERAL("New Profile 2"));

  Profile* profile1;
  Profile* profile2;

  // Successfully create the profiles.
  profile1 = profile_manager_->GetProfile(dest_path1);
  ASSERT_TRUE(profile1);

  profile2 = profile_manager_->GetProfile(dest_path2);
  ASSERT_TRUE(profile2);

  // Force lazy-init of some profile services to simulate use.
  EXPECT_TRUE(profile1->GetHistoryService(Profile::EXPLICIT_ACCESS));
  EXPECT_TRUE(profile1->GetBookmarkModel());
  EXPECT_TRUE(profile2->GetBookmarkModel());
  EXPECT_TRUE(profile2->GetHistoryService(Profile::EXPLICIT_ACCESS));

  // Make sure any pending tasks run before we destroy the profiles.
  message_loop_.RunAllPending();

  profile_manager_.reset();

  // Make sure history cleans up correctly.
  message_loop_.RunAllPending();
}

// Tests asynchronous profile creation mechanism.
TEST_F(ProfileManagerTest, CreateProfileAsync) {
  FilePath dest_path =
      temp_dir_.path().Append(FILE_PATH_LITERAL("New Profile"));

  MockObserver mock_observer;
  EXPECT_CALL(mock_observer, OnProfileCreated(testing::NotNull())).Times(1);

  profile_manager_->CreateProfileAsync(dest_path, &mock_observer);

  message_loop_.RunAllPending();
}

MATCHER(SameNotNull, "The same non-NULL value for all cals.") {
  if (!g_created_profile)
    g_created_profile = arg;
  return g_created_profile == arg;
}

TEST_F(ProfileManagerTest, CreateProfileAsyncMultipleRequests) {
  FilePath dest_path =
      temp_dir_.path().Append(FILE_PATH_LITERAL("New Profile"));

  g_created_profile = NULL;

  MockObserver mock_observer1;
  EXPECT_CALL(mock_observer1, OnProfileCreated(SameNotNull())).Times(1);
  MockObserver mock_observer2;
  EXPECT_CALL(mock_observer2, OnProfileCreated(SameNotNull())).Times(1);
  MockObserver mock_observer3;
  EXPECT_CALL(mock_observer3, OnProfileCreated(SameNotNull())).Times(1);

  profile_manager_->CreateProfileAsync(dest_path, &mock_observer1);
  profile_manager_->CreateProfileAsync(dest_path, &mock_observer2);
  profile_manager_->CreateProfileAsync(dest_path, &mock_observer3);

  message_loop_.RunAllPending();
}

TEST_F(ProfileManagerTest, CreateProfilesAsync) {
  FilePath dest_path1 =
      temp_dir_.path().Append(FILE_PATH_LITERAL("New Profile 1"));
  FilePath dest_path2 =
      temp_dir_.path().Append(FILE_PATH_LITERAL("New Profile 2"));

  MockObserver mock_observer;
  EXPECT_CALL(mock_observer, OnProfileCreated(testing::NotNull())).Times(2);

  profile_manager_->CreateProfileAsync(dest_path1, &mock_observer);
  profile_manager_->CreateProfileAsync(dest_path2, &mock_observer);

  message_loop_.RunAllPending();
}
