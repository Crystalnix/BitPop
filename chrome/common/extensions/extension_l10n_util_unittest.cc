// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/file_path.h"
#include "base/file_util.h"
#include "base/memory/linked_ptr.h"
#include "base/memory/scoped_ptr.h"
#include "base/path_service.h"
#include "base/scoped_temp_dir.h"
#include "base/values.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_constants.h"
#include "chrome/common/extensions/extension_l10n_util.h"
#include "chrome/common/extensions/extension_message_bundle.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/l10n/l10n_util.h"

namespace errors = extension_manifest_errors;
namespace keys = extension_manifest_keys;

namespace {

TEST(ExtensionL10nUtil, GetValidLocalesEmptyLocaleFolder) {
  ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());

  FilePath src_path = temp.path().Append(Extension::kLocaleFolder);
  ASSERT_TRUE(file_util::CreateDirectory(src_path));

  std::string error;
  std::set<std::string> locales;
  EXPECT_FALSE(extension_l10n_util::GetValidLocales(src_path,
                                                    &locales,
                                                    &error));

  EXPECT_TRUE(locales.empty());
}

TEST(ExtensionL10nUtil, GetValidLocalesWithValidLocaleNoMessagesFile) {
  ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());

  FilePath src_path = temp.path().Append(Extension::kLocaleFolder);
  ASSERT_TRUE(file_util::CreateDirectory(src_path));
  ASSERT_TRUE(file_util::CreateDirectory(src_path.AppendASCII("sr")));

  std::string error;
  std::set<std::string> locales;
  EXPECT_FALSE(extension_l10n_util::GetValidLocales(src_path,
                                                    &locales,
                                                    &error));

  EXPECT_TRUE(locales.empty());
}

TEST(ExtensionL10nUtil, GetValidLocalesWithUnsupportedLocale) {
  ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());

  FilePath src_path = temp.path().Append(Extension::kLocaleFolder);
  ASSERT_TRUE(file_util::CreateDirectory(src_path));
  // Supported locale.
  FilePath locale_1 = src_path.AppendASCII("sr");
  ASSERT_TRUE(file_util::CreateDirectory(locale_1));
  std::string data("whatever");
  ASSERT_TRUE(file_util::WriteFile(
      locale_1.Append(Extension::kMessagesFilename),
      data.c_str(), data.length()));
  // Unsupported locale.
  ASSERT_TRUE(file_util::CreateDirectory(src_path.AppendASCII("xxx_yyy")));

  std::string error;
  std::set<std::string> locales;
  EXPECT_TRUE(extension_l10n_util::GetValidLocales(src_path,
                                                   &locales,
                                                   &error));

  EXPECT_FALSE(locales.empty());
  EXPECT_TRUE(locales.find("sr") != locales.end());
  EXPECT_FALSE(locales.find("xxx_yyy") != locales.end());
}

TEST(ExtensionL10nUtil, GetValidLocalesWithValidLocalesAndMessagesFile) {
  FilePath install_dir;
  ASSERT_TRUE(PathService::Get(chrome::DIR_TEST_DATA, &install_dir));
  install_dir = install_dir.AppendASCII("extensions")
      .AppendASCII("good")
      .AppendASCII("Extensions")
      .AppendASCII("behllobkkfkfnphdnhnkndlbkcpglgmj")
      .AppendASCII("1.0.0.0")
      .Append(Extension::kLocaleFolder);

  std::string error;
  std::set<std::string> locales;
  EXPECT_TRUE(extension_l10n_util::GetValidLocales(install_dir,
                                                   &locales,
                                                   &error));
  EXPECT_EQ(3U, locales.size());
  EXPECT_TRUE(locales.find("sr") != locales.end());
  EXPECT_TRUE(locales.find("en") != locales.end());
  EXPECT_TRUE(locales.find("en_US") != locales.end());
}

TEST(ExtensionL10nUtil, LoadMessageCatalogsValidFallback) {
  FilePath install_dir;
  ASSERT_TRUE(PathService::Get(chrome::DIR_TEST_DATA, &install_dir));
  install_dir = install_dir.AppendASCII("extensions")
      .AppendASCII("good")
      .AppendASCII("Extensions")
      .AppendASCII("behllobkkfkfnphdnhnkndlbkcpglgmj")
      .AppendASCII("1.0.0.0")
      .Append(Extension::kLocaleFolder);

  std::string error;
  std::set<std::string> locales;
  EXPECT_TRUE(extension_l10n_util::GetValidLocales(install_dir,
                                                   &locales,
                                                   &error));

  scoped_ptr<ExtensionMessageBundle> bundle(
      extension_l10n_util::LoadMessageCatalogs(
          install_dir, "sr", "en_US", locales, &error));
  ASSERT_FALSE(NULL == bundle.get());
  EXPECT_TRUE(error.empty());
  EXPECT_EQ("Color", bundle->GetL10nMessage("color"));
  EXPECT_EQ("Not in the US or GB.", bundle->GetL10nMessage("not_in_US_or_GB"));
}

TEST(ExtensionL10nUtil, LoadMessageCatalogsMissingFiles) {
  ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());

  FilePath src_path = temp.path().Append(Extension::kLocaleFolder);
  ASSERT_TRUE(file_util::CreateDirectory(src_path));

  std::set<std::string> valid_locales;
  valid_locales.insert("sr");
  valid_locales.insert("en");
  std::string error;
  EXPECT_TRUE(NULL == extension_l10n_util::LoadMessageCatalogs(src_path,
                                                               "en",
                                                               "sr",
                                                               valid_locales,
                                                               &error));
  EXPECT_FALSE(error.empty());
}

TEST(ExtensionL10nUtil, LoadMessageCatalogsBadJSONFormat) {
  ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());

  FilePath src_path = temp.path().Append(Extension::kLocaleFolder);
  ASSERT_TRUE(file_util::CreateDirectory(src_path));

  FilePath locale = src_path.AppendASCII("sr");
  ASSERT_TRUE(file_util::CreateDirectory(locale));

  std::string data = "{ \"name\":";
  ASSERT_TRUE(
      file_util::WriteFile(locale.Append(Extension::kMessagesFilename),
                           data.c_str(), data.length()));

  std::set<std::string> valid_locales;
  valid_locales.insert("sr");
  valid_locales.insert("en_US");
  std::string error;
  EXPECT_TRUE(NULL == extension_l10n_util::LoadMessageCatalogs(src_path,
                                                              "en_US",
                                                              "sr",
                                                              valid_locales,
                                                              &error));
  EXPECT_EQ("Line: 1, column: 10, Syntax error.", error);
}

TEST(ExtensionL10nUtil, LoadMessageCatalogsDuplicateKeys) {
  ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());

  FilePath src_path = temp.path().Append(Extension::kLocaleFolder);
  ASSERT_TRUE(file_util::CreateDirectory(src_path));

  FilePath locale_1 = src_path.AppendASCII("en");
  ASSERT_TRUE(file_util::CreateDirectory(locale_1));

  std::string data =
    "{ \"name\": { \"message\": \"something\" }, "
    "\"name\": { \"message\": \"something else\" } }";
  ASSERT_TRUE(
      file_util::WriteFile(locale_1.Append(Extension::kMessagesFilename),
                           data.c_str(), data.length()));

  FilePath locale_2 = src_path.AppendASCII("sr");
  ASSERT_TRUE(file_util::CreateDirectory(locale_2));

  ASSERT_TRUE(
      file_util::WriteFile(locale_2.Append(Extension::kMessagesFilename),
                           data.c_str(), data.length()));

  std::set<std::string> valid_locales;
  valid_locales.insert("sr");
  valid_locales.insert("en");
  std::string error;
  // JSON parser hides duplicates. We are going to get only one key/value
  // pair at the end.
  scoped_ptr<ExtensionMessageBundle> message_bundle(
      extension_l10n_util::LoadMessageCatalogs(src_path,
                                               "en",
                                               "sr",
                                               valid_locales,
                                               &error));
  EXPECT_TRUE(NULL != message_bundle.get());
  EXPECT_TRUE(error.empty());
}

// Caller owns the returned object.
ExtensionMessageBundle* CreateManifestBundle() {
  linked_ptr<DictionaryValue> catalog(new DictionaryValue);

  DictionaryValue* name_tree = new DictionaryValue();
  name_tree->SetString("message", "name");
  catalog->Set("name", name_tree);

  DictionaryValue* description_tree = new DictionaryValue();
  description_tree->SetString("message", "description");
  catalog->Set("description", description_tree);

  DictionaryValue* action_title_tree = new DictionaryValue();
  action_title_tree->SetString("message", "action title");
  catalog->Set("title", action_title_tree);

  DictionaryValue* omnibox_keyword_tree = new DictionaryValue();
  omnibox_keyword_tree->SetString("message", "omnibox keyword");
  catalog->Set("omnibox_keyword", omnibox_keyword_tree);

  DictionaryValue* file_handler_title_tree = new DictionaryValue();
  file_handler_title_tree->SetString("message", "file handler title");
  catalog->Set("file_handler_title", file_handler_title_tree);

  std::vector<linked_ptr<DictionaryValue> > catalogs;
  catalogs.push_back(catalog);

  std::string error;
  ExtensionMessageBundle* bundle =
      ExtensionMessageBundle::Create(catalogs, &error);
  EXPECT_TRUE(bundle);
  EXPECT_TRUE(error.empty());

  return bundle;
}

TEST(ExtensionL10nUtil, LocalizeEmptyManifest) {
  DictionaryValue manifest;
  std::string error;
  scoped_ptr<ExtensionMessageBundle> messages(CreateManifestBundle());

  EXPECT_FALSE(
      extension_l10n_util::LocalizeManifest(*messages, &manifest, &error));
  EXPECT_EQ(errors::kInvalidName, error);
}

TEST(ExtensionL10nUtil, LocalizeManifestWithoutNameMsgAndEmptyDescription) {
  DictionaryValue manifest;
  manifest.SetString(keys::kName, "no __MSG");
  std::string error;
  scoped_ptr<ExtensionMessageBundle> messages(CreateManifestBundle());

  EXPECT_TRUE(
      extension_l10n_util::LocalizeManifest(*messages, &manifest, &error));

  std::string result;
  ASSERT_TRUE(manifest.GetString(keys::kName, &result));
  EXPECT_EQ("no __MSG", result);

  EXPECT_FALSE(manifest.HasKey(keys::kDescription));

  EXPECT_TRUE(error.empty());
}

TEST(ExtensionL10nUtil, LocalizeManifestWithNameMsgAndEmptyDescription) {
  DictionaryValue manifest;
  manifest.SetString(keys::kName, "__MSG_name__");
  std::string error;
  scoped_ptr<ExtensionMessageBundle> messages(CreateManifestBundle());

  EXPECT_TRUE(
      extension_l10n_util::LocalizeManifest(*messages, &manifest, &error));

  std::string result;
  ASSERT_TRUE(manifest.GetString(keys::kName, &result));
  EXPECT_EQ("name", result);

  EXPECT_FALSE(manifest.HasKey(keys::kDescription));

  EXPECT_TRUE(error.empty());
}

TEST(ExtensionL10nUtil, LocalizeManifestWithBadNameMsg) {
  DictionaryValue manifest;
  manifest.SetString(keys::kName, "__MSG_name_is_bad__");
  manifest.SetString(keys::kDescription, "__MSG_description__");
  std::string error;
  scoped_ptr<ExtensionMessageBundle> messages(CreateManifestBundle());

  EXPECT_FALSE(
      extension_l10n_util::LocalizeManifest(*messages, &manifest, &error));

  std::string result;
  ASSERT_TRUE(manifest.GetString(keys::kName, &result));
  EXPECT_EQ("__MSG_name_is_bad__", result);

  ASSERT_TRUE(manifest.GetString(keys::kDescription, &result));
  EXPECT_EQ("__MSG_description__", result);

  EXPECT_EQ("Variable __MSG_name_is_bad__ used but not defined.", error);
}

TEST(ExtensionL10nUtil, LocalizeManifestWithNameDescriptionDefaultTitleMsgs) {
  DictionaryValue manifest;
  manifest.SetString(keys::kName, "__MSG_name__");
  manifest.SetString(keys::kDescription, "__MSG_description__");
  std::string action_title(keys::kBrowserAction);
  action_title.append(".");
  action_title.append(keys::kPageActionDefaultTitle);
  manifest.SetString(action_title, "__MSG_title__");

  std::string error;
  scoped_ptr<ExtensionMessageBundle> messages(CreateManifestBundle());

  EXPECT_TRUE(
      extension_l10n_util::LocalizeManifest(*messages, &manifest, &error));

  std::string result;
  ASSERT_TRUE(manifest.GetString(keys::kName, &result));
  EXPECT_EQ("name", result);

  ASSERT_TRUE(manifest.GetString(keys::kDescription, &result));
  EXPECT_EQ("description", result);

  ASSERT_TRUE(manifest.GetString(action_title, &result));
  EXPECT_EQ("action title", result);

  EXPECT_TRUE(error.empty());
}

TEST(ExtensionL10nUtil, LocalizeManifestWithNameDescriptionOmniboxMsgs) {
  DictionaryValue manifest;
  manifest.SetString(keys::kName, "__MSG_name__");
  manifest.SetString(keys::kDescription, "__MSG_description__");
  manifest.SetString(keys::kOmniboxKeyword, "__MSG_omnibox_keyword__");

  std::string error;
  scoped_ptr<ExtensionMessageBundle> messages(CreateManifestBundle());

  EXPECT_TRUE(
      extension_l10n_util::LocalizeManifest(*messages, &manifest, &error));

  std::string result;
  ASSERT_TRUE(manifest.GetString(keys::kName, &result));
  EXPECT_EQ("name", result);

  ASSERT_TRUE(manifest.GetString(keys::kDescription, &result));
  EXPECT_EQ("description", result);

  ASSERT_TRUE(manifest.GetString(keys::kOmniboxKeyword, &result));
  EXPECT_EQ("omnibox keyword", result);

  EXPECT_TRUE(error.empty());
}

TEST(ExtensionL10nUtil, LocalizeManifestWithNameDescriptionFileHandlerTitle) {
  DictionaryValue manifest;
  manifest.SetString(keys::kName, "__MSG_name__");
  manifest.SetString(keys::kDescription, "__MSG_description__");
  ListValue* handlers = new ListValue();
  manifest.Set(keys::kFileBrowserHandlers, handlers);
  DictionaryValue* handler = new DictionaryValue();
  handlers->Append(handler);
  handler->SetString(keys::kPageActionDefaultTitle,
                     "__MSG_file_handler_title__");

  std::string error;
  scoped_ptr<ExtensionMessageBundle> messages(CreateManifestBundle());

  EXPECT_TRUE(
      extension_l10n_util::LocalizeManifest(*messages, &manifest, &error));

  std::string result;
  ASSERT_TRUE(manifest.GetString(keys::kName, &result));
  EXPECT_EQ("name", result);

  ASSERT_TRUE(manifest.GetString(keys::kDescription, &result));
  EXPECT_EQ("description", result);

  ASSERT_TRUE(handler->GetString(keys::kPageActionDefaultTitle, &result));
  EXPECT_EQ("file handler title", result);

  EXPECT_TRUE(error.empty());
}

// Try with NULL manifest.
TEST(ExtensionL10nUtil, ShouldRelocalizeManifestWithNullManifest) {
  ExtensionInfo info(NULL, "", FilePath(), Extension::LOAD);

  EXPECT_FALSE(extension_l10n_util::ShouldRelocalizeManifest(info));
}

// Try with default and current locales missing.
TEST(ExtensionL10nUtil, ShouldRelocalizeManifestEmptyManifest) {
  DictionaryValue manifest;
  ExtensionInfo info(&manifest, "", FilePath(), Extension::LOAD);

  EXPECT_FALSE(extension_l10n_util::ShouldRelocalizeManifest(info));
}

// Try with missing current_locale.
TEST(ExtensionL10nUtil, ShouldRelocalizeManifestWithDefaultLocale) {
  DictionaryValue manifest;
  manifest.SetString(keys::kDefaultLocale, "en_US");

  ExtensionInfo info(&manifest, "", FilePath(), Extension::LOAD);

  EXPECT_TRUE(extension_l10n_util::ShouldRelocalizeManifest(info));
}

// Try with missing default_locale.
TEST(ExtensionL10nUtil, ShouldRelocalizeManifestWithCurrentLocale) {
  DictionaryValue manifest;
  manifest.SetString(keys::kCurrentLocale,
                     extension_l10n_util::CurrentLocaleOrDefault());

  ExtensionInfo info(&manifest, "", FilePath(), Extension::LOAD);

  EXPECT_FALSE(extension_l10n_util::ShouldRelocalizeManifest(info));
}

// Try with all data present, but with same current_locale as system locale.
TEST(ExtensionL10nUtil, ShouldRelocalizeManifestSameCurrentLocale) {
  DictionaryValue manifest;
  manifest.SetString(keys::kDefaultLocale, "en_US");
  manifest.SetString(keys::kCurrentLocale,
                     extension_l10n_util::CurrentLocaleOrDefault());

  ExtensionInfo info(&manifest, "", FilePath(), Extension::LOAD);

  EXPECT_FALSE(extension_l10n_util::ShouldRelocalizeManifest(info));
}

// Try with all data present, but with different current_locale.
TEST(ExtensionL10nUtil, ShouldRelocalizeManifestDifferentCurrentLocale) {
  DictionaryValue manifest;
  manifest.SetString(keys::kDefaultLocale, "en_US");
  manifest.SetString(keys::kCurrentLocale, "sr");

  ExtensionInfo info(&manifest, "", FilePath(), Extension::LOAD);

  EXPECT_TRUE(extension_l10n_util::ShouldRelocalizeManifest(info));
}

}  // namespace
