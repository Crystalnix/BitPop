// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <fstream>
#include <vector>

#include "base/file_util.h"
#include "base/path_service.h"
#include "base/md5.h"
#include "base/scoped_temp_dir.h"
#include "base/string16.h"
#include "base/string_util.h"
#include "base/win/scoped_comptr.h"
#include "base/win/windows_version.h"
#include "chrome/installer/util/browser_distribution.h"
#include "chrome/installer/util/master_preferences.h"
#include "chrome/installer/util/shell_util.h"
#include "chrome/installer/util/util_constants.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {
bool VerifyChromeShortcut(const std::wstring& exe_path,
                          const std::wstring& shortcut,
                          const std::wstring& description,
                          int icon_index) {
  base::win::ScopedComPtr<IShellLink> i_shell_link;
  base::win::ScopedComPtr<IPersistFile> i_persist_file;

  // Get pointer to the IShellLink interface
  bool failed = FAILED(i_shell_link.CreateInstance(CLSID_ShellLink, NULL,
                                                   CLSCTX_INPROC_SERVER));
  EXPECT_FALSE(failed) << "Failed to get IShellLink";
  if (failed)
    return false;

  // Query IShellLink for the IPersistFile interface
  failed = FAILED(i_persist_file.QueryFrom(i_shell_link));
  EXPECT_FALSE(failed) << "Failed to get IPersistFile";
  if (failed)
    return false;

  failed = FAILED(i_persist_file->Load(shortcut.c_str(), 0));
  EXPECT_FALSE(failed) << "Failed to load shortcut " << shortcut.c_str();
  if (failed)
    return false;

  wchar_t long_path[MAX_PATH] = {0};
  wchar_t short_path[MAX_PATH] = {0};
  failed = ((::GetLongPathName(exe_path.c_str(), long_path, MAX_PATH) == 0) ||
            (::GetShortPathName(exe_path.c_str(), short_path, MAX_PATH) == 0));
  EXPECT_FALSE(failed) << "Failed to get long and short path names for "
                       << exe_path;
  if (failed)
    return false;

  wchar_t file_path[MAX_PATH] = {0};
  failed = ((FAILED(i_shell_link->GetPath(file_path, MAX_PATH, NULL,
                                          SLGP_UNCPRIORITY))) ||
            ((FilePath(file_path) != FilePath(long_path)) &&
             (FilePath(file_path) != FilePath(short_path))));
  EXPECT_FALSE(failed) << "File path " << file_path << " did not match with "
                       << exe_path;
  if (failed)
    return false;

  wchar_t desc[MAX_PATH] = {0};
  failed = ((FAILED(i_shell_link->GetDescription(desc, MAX_PATH))) ||
            (std::wstring(desc) != std::wstring(description)));
  EXPECT_FALSE(failed) << "Description " << desc << " did not match with "
                       << description;
  if (failed)
    return false;

  wchar_t icon_path[MAX_PATH] = {0};
  int index = 0;
  failed = ((FAILED(i_shell_link->GetIconLocation(icon_path, MAX_PATH,
                                                  &index))) ||
            ((FilePath(file_path) != FilePath(long_path)) &&
             (FilePath(file_path) != FilePath(short_path))) ||
            (index != icon_index));
  EXPECT_FALSE(failed);
  if (failed)
    return false;

  return true;
}

class ShellUtilTestWithDirAndDist : public testing::Test {
 protected:
  virtual void SetUp() {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    dist_ = BrowserDistribution::GetDistribution();
    ASSERT_TRUE(dist_ != NULL);
  }

  BrowserDistribution* dist_;

  ScopedTempDir temp_dir_;
};
};

// Test that we can open archives successfully.
TEST_F(ShellUtilTestWithDirAndDist, UpdateChromeShortcutTest) {
  // Create an executable in test path by copying ourself to it.
  wchar_t exe_full_path_str[MAX_PATH];
  EXPECT_FALSE(::GetModuleFileName(NULL, exe_full_path_str, MAX_PATH) == 0);
  FilePath exe_full_path(exe_full_path_str);

  FilePath exe_path = temp_dir_.path().AppendASCII("setup.exe");
  EXPECT_TRUE(file_util::CopyFile(exe_full_path, exe_path));

  FilePath shortcut_path = temp_dir_.path().AppendASCII("shortcut.lnk");
  const std::wstring description(L"dummy description");
  EXPECT_TRUE(ShellUtil::UpdateChromeShortcut(
      dist_,
      exe_path.value(),
      shortcut_path.value(),
      L"",
      description,
      exe_path.value(),
      dist_->GetIconIndex(),
      ShellUtil::SHORTCUT_CREATE_ALWAYS));
  EXPECT_TRUE(VerifyChromeShortcut(exe_path.value(),
                                   shortcut_path.value(),
                                   description, 0));

  // Now specify an icon index in master prefs and make sure it works.
  FilePath prefs_path = temp_dir_.path().AppendASCII(
      installer::kDefaultMasterPrefs);
  std::ofstream file;
  file.open(prefs_path.value().c_str());
  ASSERT_TRUE(file.is_open());
  file <<
"{"
" \"distribution\":{"
"   \"chrome_shortcut_icon_index\" : 1"
" }"
"}";
  file.close();
  ASSERT_TRUE(file_util::Delete(shortcut_path, false));
  EXPECT_TRUE(ShellUtil::UpdateChromeShortcut(
      dist_,
      exe_path.value(),
      shortcut_path.value(),
      L"",
      description,
      exe_path.value(),
      dist_->GetIconIndex(),
      ShellUtil::SHORTCUT_CREATE_ALWAYS));
  EXPECT_TRUE(VerifyChromeShortcut(exe_path.value(),
                                   shortcut_path.value(),
                                   description, 1));

  // Now change only description to update shortcut and make sure icon index
  // doesn't change.
  const std::wstring description2(L"dummy description 2");
  EXPECT_TRUE(ShellUtil::UpdateChromeShortcut(dist_,
                                              exe_path.value(),
                                              shortcut_path.value(),
                                              L"",
                                              description2,
                                              exe_path.value(),
                                              dist_->GetIconIndex(),
                                              ShellUtil::SHORTCUT_NO_OPTIONS));
  EXPECT_TRUE(VerifyChromeShortcut(exe_path.value(),
                                   shortcut_path.value(),
                                   description2, 1));
}

TEST_F(ShellUtilTestWithDirAndDist, CreateChromeDesktopShortcutTest) {
  // Run this test on Vista+ only if we are running elevated.
  if (base::win::GetVersion() > base::win::VERSION_XP && !IsUserAnAdmin()) {
    LOG(ERROR) << "Must be admin to run this test on Vista+";
    return;
  }

  // Create an executable in test path by copying ourself to it.
  wchar_t exe_full_path_str[MAX_PATH];
  EXPECT_FALSE(::GetModuleFileName(NULL, exe_full_path_str, MAX_PATH) == 0);
  FilePath exe_full_path(exe_full_path_str);

  FilePath exe_path = temp_dir_.path().AppendASCII("setup.exe");
  EXPECT_TRUE(file_util::CopyFile(exe_full_path, exe_path));

  const std::wstring description(L"dummy description");

  FilePath user_desktop_path;
  EXPECT_TRUE(ShellUtil::GetDesktopPath(false, &user_desktop_path));
  FilePath system_desktop_path;
  EXPECT_TRUE(ShellUtil::GetDesktopPath(true, &system_desktop_path));

  std::wstring shortcut_name;
  EXPECT_TRUE(ShellUtil::GetChromeShortcutName(dist_, false, L"",
                                               &shortcut_name));

  std::wstring default_profile_shortcut_name;
  const std::wstring default_profile_user_name = L"Minsk";
  EXPECT_TRUE(ShellUtil::GetChromeShortcutName(dist_, false,
                                               default_profile_user_name,
                                               &default_profile_shortcut_name));

  std::wstring second_profile_shortcut_name;
  const std::wstring second_profile_user_name = L"Pinsk";
  EXPECT_TRUE(ShellUtil::GetChromeShortcutName(dist_, false,
                                               second_profile_user_name,
                                               &second_profile_shortcut_name));

  FilePath user_shortcut_path = user_desktop_path.Append(shortcut_name);
  FilePath system_shortcut_path = system_desktop_path.Append(shortcut_name);
  FilePath default_profile_shortcut_path = user_desktop_path.Append(
      default_profile_shortcut_name);
  FilePath second_profile_shortcut_path = user_desktop_path.Append(
      second_profile_shortcut_name);

  // Test simple creation of a user-level shortcut.
  EXPECT_TRUE(ShellUtil::CreateChromeDesktopShortcut(
      dist_,
      exe_path.value(),
      description,
      L"",
      L"",
      exe_path.value(),
      dist_->GetIconIndex(),
      ShellUtil::CURRENT_USER,
      ShellUtil::SHORTCUT_CREATE_ALWAYS));
  EXPECT_TRUE(VerifyChromeShortcut(exe_path.value(),
                                   user_shortcut_path.value(),
                                   description,
                                   0));
  EXPECT_TRUE(ShellUtil::RemoveChromeDesktopShortcut(
      dist_,
      ShellUtil::CURRENT_USER,
      ShellUtil::SHORTCUT_NO_OPTIONS));

  // Test simple creation of a system-level shortcut.
  EXPECT_TRUE(ShellUtil::CreateChromeDesktopShortcut(
      dist_,
      exe_path.value(),
      description,
      L"",
      L"",
      exe_path.value(),
      dist_->GetIconIndex(),
      ShellUtil::SYSTEM_LEVEL,
      ShellUtil::SHORTCUT_CREATE_ALWAYS));
  EXPECT_TRUE(VerifyChromeShortcut(exe_path.value(),
                                   system_shortcut_path.value(),
                                   description,
                                   0));
  EXPECT_TRUE(ShellUtil::RemoveChromeDesktopShortcut(
      dist_,
      ShellUtil::SYSTEM_LEVEL,
      ShellUtil::SHORTCUT_NO_OPTIONS));

  // Test creation of a user-level shortcut when a system-level shortcut
  // is already present (should fail).
  EXPECT_TRUE(ShellUtil::CreateChromeDesktopShortcut(
      dist_,
      exe_path.value(),
      description,
      L"",
      L"",
      exe_path.value(),
      dist_->GetIconIndex(),
      ShellUtil::SYSTEM_LEVEL,
      ShellUtil::SHORTCUT_CREATE_ALWAYS));
  EXPECT_FALSE(ShellUtil::CreateChromeDesktopShortcut(
      dist_,
      exe_path.value(),
      description,
      L"",
      L"",
      exe_path.value(),
      dist_->GetIconIndex(),
      ShellUtil::CURRENT_USER,
      ShellUtil::SHORTCUT_CREATE_ALWAYS));
  EXPECT_TRUE(VerifyChromeShortcut(exe_path.value(),
                                   system_shortcut_path.value(),
                                   description,
                                   0));
  EXPECT_FALSE(file_util::PathExists(user_shortcut_path));
  EXPECT_TRUE(ShellUtil::RemoveChromeDesktopShortcut(
      dist_,
      ShellUtil::SYSTEM_LEVEL,
      ShellUtil::SHORTCUT_NO_OPTIONS));

  // Test creation of a system-level shortcut when a user-level shortcut
  // is already present (should succeed).
  EXPECT_TRUE(ShellUtil::CreateChromeDesktopShortcut(
      dist_,
      exe_path.value(),
      description,
      L"",
      L"",
      exe_path.value(),
      dist_->GetIconIndex(),
      ShellUtil::CURRENT_USER,
      ShellUtil::SHORTCUT_CREATE_ALWAYS));
  EXPECT_TRUE(ShellUtil::CreateChromeDesktopShortcut(
      dist_,
      exe_path.value(),
      description,
      L"",
      L"",
      exe_path.value(),
      dist_->GetIconIndex(),
      ShellUtil::SYSTEM_LEVEL,
      ShellUtil::SHORTCUT_CREATE_ALWAYS));
  EXPECT_TRUE(VerifyChromeShortcut(exe_path.value(),
                                   user_shortcut_path.value(),
                                   description,
                                   0));
  EXPECT_TRUE(VerifyChromeShortcut(exe_path.value(),
                                   system_shortcut_path.value(),
                                   description,
                                   0));
  EXPECT_TRUE(ShellUtil::RemoveChromeDesktopShortcut(
      dist_,
      ShellUtil::CURRENT_USER,
      ShellUtil::SHORTCUT_NO_OPTIONS));
  EXPECT_TRUE(ShellUtil::RemoveChromeDesktopShortcut(
      dist_,
      ShellUtil::SYSTEM_LEVEL,
      ShellUtil::SHORTCUT_NO_OPTIONS));

  // Test creation of two profile-specific shortcuts (these are always
  // user-level).
  EXPECT_TRUE(ShellUtil::CreateChromeDesktopShortcut(
      dist_,
      exe_path.value(),
      description,
      default_profile_user_name,
      L"--profile-directory=\"Default\"",
      exe_path.value(),
      dist_->GetIconIndex(),
      ShellUtil::CURRENT_USER,
      ShellUtil::SHORTCUT_CREATE_ALWAYS));
  EXPECT_TRUE(VerifyChromeShortcut(exe_path.value(),
                                   default_profile_shortcut_path.value(),
                                   description,
                                   0));
  EXPECT_TRUE(ShellUtil::CreateChromeDesktopShortcut(
      dist_,
      exe_path.value(),
      description,
      second_profile_user_name,
      L"--profile-directory=\"Profile 1\"",
      exe_path.value(),
      dist_->GetIconIndex(),
      ShellUtil::CURRENT_USER,
      ShellUtil::SHORTCUT_CREATE_ALWAYS));
  EXPECT_TRUE(VerifyChromeShortcut(exe_path.value(),
                                   second_profile_shortcut_path.value(),
                                   description,
                                   0));
  std::vector<string16> profile_names;
  profile_names.push_back(default_profile_shortcut_name);
  profile_names.push_back(second_profile_shortcut_name);
  EXPECT_TRUE(ShellUtil::RemoveChromeDesktopShortcutsWithAppendedNames(
      profile_names));
}

TEST_F(ShellUtilTestWithDirAndDist, BuildAppModelIdBasic) {
  std::vector<string16> components;
  const string16 base_app_id(dist_->GetBaseAppId());
  components.push_back(base_app_id);
  ASSERT_EQ(base_app_id, ShellUtil::BuildAppModelId(components));
}

TEST_F(ShellUtilTestWithDirAndDist, BuildAppModelIdManySmall) {
  std::vector<string16> components;
  const string16 suffixed_app_id(dist_->GetBaseAppId().append(L".gab"));
  components.push_back(suffixed_app_id);
  components.push_back(L"Default");
  components.push_back(L"Test");
  ASSERT_EQ(suffixed_app_id + L".Default.Test",
            ShellUtil::BuildAppModelId(components));
}

TEST_F(ShellUtilTestWithDirAndDist, BuildAppModelIdLongUsernameNormalProfile) {
  std::vector<string16> components;
  const string16 long_appname(
      L"Chrome.a_user_who_has_a_crazy_long_name_with_some_weird@symbols_in_it_"
      L"that_goes_over_64_characters");
  components.push_back(long_appname);
  components.push_back(L"Default");
  ASSERT_EQ(L"Chrome.a_user_wer_64_characters.Default",
            ShellUtil::BuildAppModelId(components));
}

TEST_F(ShellUtilTestWithDirAndDist, BuildAppModelIdLongEverything) {
  std::vector<string16> components;
  const string16 long_appname(
      L"Chrome.a_user_who_has_a_crazy_long_name_with_some_weird@symbols_in_it_"
      L"that_goes_over_64_characters");
  components.push_back(long_appname);
  components.push_back(
      L"A_crazy_profile_name_not_even_sure_whether_that_is_possible");
  const string16 constructed_app_id(ShellUtil::BuildAppModelId(components));
  ASSERT_LE(constructed_app_id.length(), installer::kMaxAppModelIdLength);
  ASSERT_EQ(L"Chrome.a_user_wer_64_characters.A_crazy_profilethat_is_possible",
            constructed_app_id);
}

TEST(ShellUtilTest, GetUserSpecificRegistrySuffix) {
  string16 suffix;
  ASSERT_TRUE(ShellUtil::GetUserSpecificRegistrySuffix(&suffix));
  ASSERT_TRUE(StartsWith(suffix, L".", true));
  ASSERT_EQ(27, suffix.length());
  ASSERT_TRUE(ContainsOnlyChars(suffix.substr(1),
                                L"ABCDEFGHIJKLMNOPQRSTUVWXYZ234567"));
}

TEST(ShellUtilTest, GetOldUserSpecificRegistrySuffix) {
  string16 suffix;
  ASSERT_TRUE(ShellUtil::GetOldUserSpecificRegistrySuffix(&suffix));
  ASSERT_TRUE(StartsWith(suffix, L".", true));

  wchar_t user_name[256];
  DWORD size = arraysize(user_name);
  ASSERT_NE(0, ::GetUserName(user_name, &size));
  ASSERT_GE(size, 1U);
  ASSERT_STREQ(user_name, suffix.substr(1).c_str());
}

TEST(ShellUtilTest, ByteArrayToBase32) {
  // Tests from http://tools.ietf.org/html/rfc4648#section-10.
  const unsigned char test_array[] = { 'f', 'o', 'o', 'b', 'a', 'r' };

  const string16 expected[] = { L"", L"MY", L"MZXQ", L"MZXW6", L"MZXW6YQ",
                                L"MZXW6YTB", L"MZXW6YTBOI"};

  // Run the tests, with one more letter in the input every pass.
  for (int i = 0; i < arraysize(expected); ++i) {
    ASSERT_EQ(expected[i],
              ShellUtil::ByteArrayToBase32(test_array, i));
  }
}
