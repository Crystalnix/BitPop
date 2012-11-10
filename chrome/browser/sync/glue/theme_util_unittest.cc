// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/glue/theme_util.h"

#include "base/file_path.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_manifest_constants.h"
#include "chrome/test/base/testing_profile.h"
#include "sync/protocol/theme_specifics.pb.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using extensions::Extension;

namespace browser_sync {

namespace {

using ::testing::AnyNumber;
using ::testing::Return;

class MockThemeService : public ThemeService {
 public:
  MOCK_METHOD0(SetNativeTheme, void());
  MOCK_METHOD0(UseDefaultTheme, void());
  MOCK_CONST_METHOD0(GetThemeID, std::string());
};

ProfileKeyedService* BuildMockThemeService(Profile* profile) {
  return new MockThemeService;
}

class SyncThemeUtilTest : public testing::Test {
 protected:
  MockThemeService* BuildForProfile(Profile* profile) {
    return static_cast<MockThemeService*>(
        ThemeServiceFactory::GetInstance()->SetTestingFactoryAndUse(
            profile, BuildMockThemeService));
  }
};

scoped_refptr<Extension> MakeThemeExtension(const FilePath& extension_path,
                                            const std::string& name,
                                            const std::string& update_url) {
  DictionaryValue source;
  source.SetString(extension_manifest_keys::kName, name);
  source.Set(extension_manifest_keys::kTheme, new DictionaryValue());
  source.SetString(extension_manifest_keys::kUpdateURL, update_url);
  source.SetString(extension_manifest_keys::kVersion, "0.0.0.0");
  std::string error;
  scoped_refptr<Extension> extension = Extension::Create(
      extension_path, Extension::INTERNAL, source, Extension::NO_FLAGS, &error);
  EXPECT_TRUE(extension);
  EXPECT_EQ("", error);
  return extension;
}

TEST_F(SyncThemeUtilTest, AreThemeSpecificsEqualHelper) {
  sync_pb::ThemeSpecifics a, b;
  EXPECT_TRUE(AreThemeSpecificsEqualHelper(a, b, false));
  EXPECT_TRUE(AreThemeSpecificsEqualHelper(a, b, true));

  // Custom vs. non-custom.

  a.set_use_custom_theme(true);
  EXPECT_FALSE(AreThemeSpecificsEqualHelper(a, b, false));
  EXPECT_FALSE(AreThemeSpecificsEqualHelper(a, b, true));

  // Custom theme equality.

  b.set_use_custom_theme(true);
  EXPECT_TRUE(AreThemeSpecificsEqualHelper(a, b, false));
  EXPECT_TRUE(AreThemeSpecificsEqualHelper(a, b, true));

  a.set_custom_theme_id("id");
  EXPECT_FALSE(AreThemeSpecificsEqualHelper(a, b, false));
  EXPECT_FALSE(AreThemeSpecificsEqualHelper(a, b, true));

  b.set_custom_theme_id("id");
  EXPECT_TRUE(AreThemeSpecificsEqualHelper(a, b, false));
  EXPECT_TRUE(AreThemeSpecificsEqualHelper(a, b, true));

  a.set_custom_theme_update_url("http://update.url");
  EXPECT_TRUE(AreThemeSpecificsEqualHelper(a, b, false));
  EXPECT_TRUE(AreThemeSpecificsEqualHelper(a, b, true));

  a.set_custom_theme_name("name");
  EXPECT_TRUE(AreThemeSpecificsEqualHelper(a, b, false));
  EXPECT_TRUE(AreThemeSpecificsEqualHelper(a, b, true));

  // Non-custom theme equality.

  a.set_use_custom_theme(false);
  b.set_use_custom_theme(false);
  EXPECT_TRUE(AreThemeSpecificsEqualHelper(a, b, false));
  EXPECT_TRUE(AreThemeSpecificsEqualHelper(a, b, true));

  a.set_use_system_theme_by_default(true);
  EXPECT_TRUE(AreThemeSpecificsEqualHelper(a, b, false));
  EXPECT_FALSE(AreThemeSpecificsEqualHelper(a, b, true));

  b.set_use_system_theme_by_default(true);
  EXPECT_TRUE(AreThemeSpecificsEqualHelper(a, b, false));
  EXPECT_TRUE(AreThemeSpecificsEqualHelper(a, b, true));
}

TEST_F(SyncThemeUtilTest, SetCurrentThemeDefaultTheme) {
  sync_pb::ThemeSpecifics theme_specifics;
  TestingProfile profile;
  MockThemeService* mock_theme_service = BuildForProfile(&profile);

  EXPECT_CALL(*mock_theme_service, UseDefaultTheme()).Times(1);

  SetCurrentThemeFromThemeSpecifics(theme_specifics, &profile);
}

TEST_F(SyncThemeUtilTest, SetCurrentThemeSystemTheme) {
  sync_pb::ThemeSpecifics theme_specifics;
  theme_specifics.set_use_system_theme_by_default(true);

  TestingProfile profile;
  MockThemeService* mock_theme_service = BuildForProfile(&profile);

  EXPECT_CALL(*mock_theme_service, SetNativeTheme()).Times(1);

  SetCurrentThemeFromThemeSpecifics(theme_specifics, &profile);
}

// TODO(akalin): Make ExtensionService/ExtensionUpdater testable
// enough to be able to write a unittest for SetCurrentTheme for a
// custom theme.

TEST_F(SyncThemeUtilTest, GetThemeSpecificsHelperNoCustomTheme) {
  sync_pb::ThemeSpecifics theme_specifics;
  theme_specifics.set_use_custom_theme(true);
  theme_specifics.set_use_system_theme_by_default(true);
  theme_specifics.set_custom_theme_name("name");
  theme_specifics.set_custom_theme_id("id");
  theme_specifics.set_custom_theme_update_url("updateurl");
  GetThemeSpecificsFromCurrentThemeHelper(NULL, false, false,
                                          &theme_specifics);

  EXPECT_TRUE(theme_specifics.has_use_custom_theme());
  EXPECT_FALSE(theme_specifics.use_custom_theme());
  // Should be preserved since we passed in false for
  // is_system_theme_distinct_from_current_theme.
  EXPECT_TRUE(theme_specifics.use_system_theme_by_default());
  EXPECT_FALSE(theme_specifics.has_custom_theme_name());
  EXPECT_FALSE(theme_specifics.has_custom_theme_id());
  EXPECT_FALSE(theme_specifics.has_custom_theme_update_url());
}

TEST_F(SyncThemeUtilTest, GetThemeSpecificsHelperNoCustomThemeDistinct) {
  sync_pb::ThemeSpecifics theme_specifics;
  theme_specifics.set_use_custom_theme(true);
  theme_specifics.set_custom_theme_name("name");
  theme_specifics.set_custom_theme_id("id");
  theme_specifics.set_custom_theme_update_url("updateurl");
  GetThemeSpecificsFromCurrentThemeHelper(NULL, true, false,
                                          &theme_specifics);

  EXPECT_TRUE(theme_specifics.has_use_custom_theme());
  EXPECT_FALSE(theme_specifics.use_custom_theme());
  // Should be set since we passed in true for
  // is_system_theme_distinct_from_current_theme.
  EXPECT_TRUE(theme_specifics.has_use_system_theme_by_default());
  EXPECT_FALSE(theme_specifics.use_system_theme_by_default());
  EXPECT_FALSE(theme_specifics.has_custom_theme_name());
  EXPECT_FALSE(theme_specifics.has_custom_theme_id());
  EXPECT_FALSE(theme_specifics.has_custom_theme_update_url());
}

namespace {
#if defined(OS_WIN)
const FilePath::CharType kExtensionFilePath[] = FILE_PATH_LITERAL("c:\\foo");
#elif defined(OS_POSIX)
const FilePath::CharType kExtensionFilePath[] = FILE_PATH_LITERAL("/oo");
#endif
}  // namespace

TEST_F(SyncThemeUtilTest, GetThemeSpecificsHelperCustomTheme) {
  sync_pb::ThemeSpecifics theme_specifics;
  theme_specifics.set_use_custom_theme(false);
  theme_specifics.set_use_system_theme_by_default(true);
  FilePath file_path(kExtensionFilePath);
  const std::string kThemeName("name");
  const std::string kThemeUpdateUrl("http://update.url/foo");
  scoped_refptr<Extension> extension(
      MakeThemeExtension(file_path, kThemeName, kThemeUpdateUrl));
  GetThemeSpecificsFromCurrentThemeHelper(extension.get(), false, false,
                                          &theme_specifics);

  EXPECT_TRUE(theme_specifics.use_custom_theme());
  EXPECT_TRUE(theme_specifics.use_system_theme_by_default());
  EXPECT_EQ(kThemeName, theme_specifics.custom_theme_name());
  EXPECT_EQ(extension->id(), theme_specifics.custom_theme_id());
  EXPECT_EQ(kThemeUpdateUrl, theme_specifics.custom_theme_update_url());
}

TEST_F(SyncThemeUtilTest, GetThemeSpecificsHelperCustomThemeDistinct) {
  sync_pb::ThemeSpecifics theme_specifics;
  theme_specifics.set_use_custom_theme(false);
  FilePath file_path(kExtensionFilePath);
  const std::string kThemeName("name");
  const std::string kThemeUpdateUrl("http://update.url/foo");
  scoped_refptr<Extension> extension(
      MakeThemeExtension(file_path, kThemeName, kThemeUpdateUrl));
  GetThemeSpecificsFromCurrentThemeHelper(extension.get(), true, false,
                                          &theme_specifics);

  EXPECT_TRUE(theme_specifics.use_custom_theme());
  EXPECT_TRUE(theme_specifics.has_use_system_theme_by_default());
  EXPECT_FALSE(theme_specifics.use_system_theme_by_default());
  EXPECT_EQ(kThemeName, theme_specifics.custom_theme_name());
  EXPECT_EQ(extension->id(), theme_specifics.custom_theme_id());
  EXPECT_EQ(kThemeUpdateUrl, theme_specifics.custom_theme_update_url());
}

TEST_F(SyncThemeUtilTest, SetCurrentThemeIfNecessaryDefaultThemeNotNecessary) {
  TestingProfile profile;
  MockThemeService* mock_theme_service = BuildForProfile(&profile);

  EXPECT_CALL(*mock_theme_service, GetThemeID()).WillRepeatedly(Return(
      ThemeService::kDefaultThemeID));
  EXPECT_CALL(*mock_theme_service, UseDefaultTheme()).Times(AnyNumber());

  // TODO(akalin): Mock out call to GetPrefs() under TOOLKIT_GTK.

  sync_pb::ThemeSpecifics theme_specifics;
  SetCurrentThemeFromThemeSpecificsIfNecessary(theme_specifics,
                                               &profile);
}

}  // namespace

}  // namespace browser_sync
