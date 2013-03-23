// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "base/json/json_file_value_serializer.h"
#include "base/message_loop.h"
#include "base/path_service.h"
#include "base/string_util.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/management_policy.h"
#include "chrome/browser/extensions/test_extension_system.h"
#include "chrome/browser/ui/webui/extensions/extension_settings_handler.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/test_browser_thread.h"
#include "testing/gtest/include/gtest/gtest.h"

using extensions::Extension;

class ExtensionUITest : public testing::Test {
 public:
  ExtensionUITest()
      : ui_thread_(content::BrowserThread::UI, &message_loop_),
        file_thread_(content::BrowserThread::FILE, &message_loop_) {}

 protected:
  virtual void SetUp() OVERRIDE {
    // Create an ExtensionService and ManagementPolicy to inject into the
    // ExtensionSettingsHandler.
    profile_.reset(new TestingProfile());
    extensions::TestExtensionSystem* system =
        static_cast<extensions::TestExtensionSystem*>(
            extensions::ExtensionSystem::Get(profile_.get()));
    extension_service_ = system->CreateExtensionService(
        CommandLine::ForCurrentProcess(), FilePath(), false);
    management_policy_ = system->management_policy();

    handler_.reset(new ExtensionSettingsHandler(extension_service_,
                                                management_policy_));
  }

  virtual void TearDown() OVERRIDE {
    handler_.reset();
    profile_.reset();
    // Execute any pending deletion tasks.
    message_loop_.RunUntilIdle();
  }

  static DictionaryValue* DeserializeJSONTestData(const FilePath& path,
      std::string *error) {
    Value* value;

    JSONFileValueSerializer serializer(path);
    value = serializer.Deserialize(NULL, error);

    return static_cast<DictionaryValue*>(value);
  }

  DictionaryValue* CreateExtensionDetailViewFromPath(
      const FilePath& extension_path,
      const std::vector<ExtensionPage>& pages,
      Extension::Location location) {
    std::string error;

    FilePath manifest_path = extension_path.Append(
        Extension::kManifestFilename);
    scoped_ptr<DictionaryValue> extension_data(DeserializeJSONTestData(
        manifest_path, &error));
    EXPECT_EQ("", error);

    scoped_refptr<Extension> extension(Extension::Create(
        extension_path, location, *extension_data, Extension::REQUIRE_KEY,
        &error));
    EXPECT_TRUE(extension.get());
    EXPECT_EQ("", error);

    return handler_->CreateExtensionDetailValue(extension.get(), pages, NULL);
  }

  void CompareExpectedAndActualOutput(
      const FilePath& extension_path,
      const std::vector<ExtensionPage>& pages,
      const FilePath& expected_output_path) {
    std::string error;

    scoped_ptr<DictionaryValue> expected_output_data(DeserializeJSONTestData(
        expected_output_path, &error));
    EXPECT_EQ("", error);

    // Produce test output.
    scoped_ptr<DictionaryValue> actual_output_data(
        CreateExtensionDetailViewFromPath(
            extension_path, pages, Extension::INVALID));

    // Compare the outputs.
    // Ignore unknown fields in the actual output data.
    std::string paths_details = " - expected (" +
        expected_output_path.MaybeAsASCII() + ") vs. actual (" +
        extension_path.MaybeAsASCII() + ")";
    for (DictionaryValue::key_iterator key = expected_output_data->begin_keys();
        key != expected_output_data->end_keys();
        ++key) {
      Value* expected_value = NULL;
      Value* actual_value = NULL;
      EXPECT_TRUE(expected_output_data->Get(*key, &expected_value)) <<
          *key + " is missing" + paths_details;
      EXPECT_TRUE(actual_output_data->Get(*key, &actual_value)) <<
          *key + " is missing" + paths_details;
      if (expected_value == NULL) {
        EXPECT_EQ(NULL, actual_value) << *key + paths_details;
      } else {
        EXPECT_TRUE(expected_value->Equals(actual_value)) << *key +
            paths_details;
      }
    }
  }

  MessageLoop message_loop_;
  content::TestBrowserThread ui_thread_;
  content::TestBrowserThread file_thread_;
  scoped_ptr<TestingProfile> profile_;
  ExtensionService* extension_service_;
  extensions::ManagementPolicy* management_policy_;
  scoped_ptr<ExtensionSettingsHandler> handler_;
};

TEST_F(ExtensionUITest, GenerateExtensionsJSONData) {
  FilePath data_test_dir_path, extension_path, expected_output_path;
  EXPECT_TRUE(PathService::Get(chrome::DIR_TEST_DATA, &data_test_dir_path));

  // Test Extension1
  extension_path = data_test_dir_path.AppendASCII("extensions")
      .AppendASCII("good")
      .AppendASCII("Extensions")
      .AppendASCII("behllobkkfkfnphdnhnkndlbkcpglgmj")
      .AppendASCII("1.0.0.0");

  std::vector<ExtensionPage> pages;
  pages.push_back(ExtensionPage(
      GURL("chrome-extension://behllobkkfkfnphdnhnkndlbkcpglgmj/bar.html"),
      42, 88, false));
  pages.push_back(ExtensionPage(
      GURL("chrome-extension://behllobkkfkfnphdnhnkndlbkcpglgmj/dog.html"),
      0, 0, false));

  expected_output_path = data_test_dir_path.AppendASCII("extensions")
      .AppendASCII("ui")
      .AppendASCII("create_extension_detail_value_expected_output")
      .AppendASCII("good-extension1.json");

  CompareExpectedAndActualOutput(extension_path, pages, expected_output_path);

#if !defined(OS_CHROMEOS)
  // Test Extension2
  extension_path = data_test_dir_path.AppendASCII("extensions")
      .AppendASCII("good")
      .AppendASCII("Extensions")
      .AppendASCII("hpiknbiabeeppbpihjehijgoemciehgk")
      .AppendASCII("2");

  expected_output_path = data_test_dir_path.AppendASCII("extensions")
      .AppendASCII("ui")
      .AppendASCII("create_extension_detail_value_expected_output")
      .AppendASCII("good-extension2.json");

  // It's OK to have duplicate URLs, so long as the IDs are different.
  pages[1].url = pages[0].url;

  CompareExpectedAndActualOutput(extension_path, pages, expected_output_path);
#endif

  // Test Extension3
  extension_path = data_test_dir_path.AppendASCII("extensions")
      .AppendASCII("good")
      .AppendASCII("Extensions")
      .AppendASCII("bjafgdebaacbbbecmhlhpofkepfkgcpa")
      .AppendASCII("1.0");

  expected_output_path = data_test_dir_path.AppendASCII("extensions")
      .AppendASCII("ui")
      .AppendASCII("create_extension_detail_value_expected_output")
      .AppendASCII("good-extension3.json");

  pages.clear();

  CompareExpectedAndActualOutput(extension_path, pages, expected_output_path);
}

// Test that using Extension::LOAD for the extension location triggers the
// correct values in the details, including location, order, and allow_reload.
TEST_F(ExtensionUITest, LocationLoadPropagation) {
  FilePath data_test_dir_path, extension_path;
  EXPECT_TRUE(PathService::Get(chrome::DIR_TEST_DATA, &data_test_dir_path));

  extension_path = data_test_dir_path.AppendASCII("extensions")
      .AppendASCII("good")
      .AppendASCII("Extensions")
      .AppendASCII("behllobkkfkfnphdnhnkndlbkcpglgmj")
      .AppendASCII("1.0.0.0");

  std::vector<ExtensionPage> pages;

  scoped_ptr<DictionaryValue> extension_details(
      CreateExtensionDetailViewFromPath(
          extension_path, pages, Extension::LOAD));

  bool ui_allow_reload = false;
  bool ui_is_unpacked = false;
  FilePath::StringType ui_path;

  EXPECT_TRUE(extension_details->GetBoolean("allow_reload", &ui_allow_reload));
  EXPECT_TRUE(extension_details->GetBoolean("isUnpacked", &ui_is_unpacked));
  EXPECT_TRUE(extension_details->GetString("path", &ui_path));
  EXPECT_EQ(true, ui_allow_reload);
  EXPECT_EQ(true, ui_is_unpacked);
  EXPECT_EQ(extension_path, FilePath(ui_path));
}

// Test that using Extension::EXTERNAL_PREF for the extension location triggers
// the correct values in the details, including location, order, and
// allow_reload.  Contrast to Extension::LOAD, which has somewhat different
// values.
TEST_F(ExtensionUITest, LocationExternalPrefPropagation) {
  FilePath data_test_dir_path, extension_path;
  EXPECT_TRUE(PathService::Get(chrome::DIR_TEST_DATA, &data_test_dir_path));

  extension_path = data_test_dir_path.AppendASCII("extensions")
      .AppendASCII("good")
      .AppendASCII("Extensions")
      .AppendASCII("behllobkkfkfnphdnhnkndlbkcpglgmj")
      .AppendASCII("1.0.0.0");

  std::vector<ExtensionPage> pages;

  scoped_ptr<DictionaryValue> extension_details(
      CreateExtensionDetailViewFromPath(
          extension_path, pages, Extension::EXTERNAL_PREF));

  bool ui_allow_reload = true;
  bool ui_is_unpacked = true;
  FilePath::StringType ui_path;

  EXPECT_TRUE(extension_details->GetBoolean("allow_reload", &ui_allow_reload));
  EXPECT_TRUE(extension_details->GetBoolean("isUnpacked", &ui_is_unpacked));
  EXPECT_FALSE(extension_details->GetString("path", &ui_path));
  EXPECT_FALSE(ui_allow_reload);
  EXPECT_FALSE(ui_is_unpacked);
}

// Test that the extension path is correctly propagated into the extension
// details.
TEST_F(ExtensionUITest, PathPropagation) {
  FilePath data_test_dir_path, extension_path;
  EXPECT_TRUE(PathService::Get(chrome::DIR_TEST_DATA, &data_test_dir_path));

  extension_path = data_test_dir_path.AppendASCII("extensions")
      .AppendASCII("good")
      .AppendASCII("Extensions")
      .AppendASCII("behllobkkfkfnphdnhnkndlbkcpglgmj")
      .AppendASCII("1.0.0.0");

  std::vector<ExtensionPage> pages;

  scoped_ptr<DictionaryValue> extension_details(
      CreateExtensionDetailViewFromPath(
          extension_path, pages, Extension::LOAD));

  FilePath::StringType ui_path;

  EXPECT_TRUE(extension_details->GetString("path", &ui_path));
  EXPECT_EQ(extension_path, FilePath(ui_path));
}
