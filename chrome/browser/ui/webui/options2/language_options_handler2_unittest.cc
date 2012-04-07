// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/options2/language_options_handler2.h"

#include <string>

#include "base/memory/scoped_ptr.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/input_method/ibus_controller.h"
#include "chrome/browser/chromeos/input_method/input_method_manager.h"
#include "chrome/browser/ui/webui/options2/chromeos/cros_language_options_handler2.h"
#endif  // defined(OS_CHROMEOS)

#if defined(OS_CHROMEOS)

using chromeos::input_method::IBusController;
using chromeos::input_method::InputMethodDescriptor;
using chromeos::input_method::InputMethodDescriptors;

static InputMethodDescriptor GetDesc(IBusController* controller,
                                     const std::string& id,
                                     const std::string& raw_layout,
                                     const std::string& language_code) {
  return controller->CreateInputMethodDescriptor(id, "", raw_layout,
                                                 language_code);
}

static InputMethodDescriptors CreateInputMethodDescriptors() {
  scoped_ptr<IBusController> controller(IBusController::Create());

  InputMethodDescriptors descriptors;
  descriptors.push_back(GetDesc(controller.get(), "xkb:us::eng", "us", "eng"));
  descriptors.push_back(GetDesc(controller.get(), "xkb:fr::fra", "fr", "fra"));
  descriptors.push_back(GetDesc(controller.get(), "xkb:be::fra", "be", "fr"));
  descriptors.push_back(GetDesc(controller.get(), "mozc", "us", "ja"));
  return descriptors;
}

TEST(LanguageOptionsHandlerTest, GetInputMethodList) {
  InputMethodDescriptors descriptors = CreateInputMethodDescriptors();
  scoped_ptr<ListValue> list(
      chromeos::CrosLanguageOptionsHandler::GetInputMethodList(descriptors));
  ASSERT_EQ(4U, list->GetSize());

  DictionaryValue* entry = NULL;
  DictionaryValue *language_code_set = NULL;
  std::string input_method_id;
  std::string display_name;
  std::string language_code;

  // As shown below, the list should be input method ids should appear in
  // the same order of the descriptors.
  ASSERT_TRUE(list->GetDictionary(0, &entry));
  ASSERT_TRUE(entry->GetString("id", &input_method_id));
  ASSERT_TRUE(entry->GetString("displayName", &display_name));
  ASSERT_TRUE(entry->GetDictionary("languageCodeSet", &language_code_set));
  EXPECT_EQ("xkb:us::eng", input_method_id);
  // Commented out as it depends on translation in generated_resources.grd
  // (i.e. makes the test fragile).
  // EXPECT_EQ("English (USA) keyboard layout", display_name);
  ASSERT_TRUE(language_code_set->HasKey("en-US"));
  ASSERT_TRUE(language_code_set->HasKey("id"));  // From kExtraLanguages.
  ASSERT_TRUE(language_code_set->HasKey("fil"));  // From kExtraLanguages.

  ASSERT_TRUE(list->GetDictionary(1, &entry));
  ASSERT_TRUE(entry->GetString("id", &input_method_id));
  ASSERT_TRUE(entry->GetString("displayName", &display_name));
  ASSERT_TRUE(entry->GetDictionary("languageCodeSet", &language_code_set));
  EXPECT_EQ("xkb:fr::fra", input_method_id);
  // Commented out. See above.
  // EXPECT_EQ("French keyboard layout", display_name);
  ASSERT_TRUE(language_code_set->HasKey("fr"));

  ASSERT_TRUE(list->GetDictionary(2, &entry));
  ASSERT_TRUE(entry->GetString("id", &input_method_id));
  ASSERT_TRUE(entry->GetString("displayName", &display_name));
  ASSERT_TRUE(entry->GetDictionary("languageCodeSet", &language_code_set));
  EXPECT_EQ("xkb:be::fra", input_method_id);
  // Commented out. See above.
  // EXPECT_EQ("Belgian keyboard layout", display_name);
  ASSERT_TRUE(language_code_set->HasKey("fr"));

  ASSERT_TRUE(list->GetDictionary(3, &entry));
  ASSERT_TRUE(entry->GetString("id", &input_method_id));
  ASSERT_TRUE(entry->GetString("displayName", &display_name));
  ASSERT_TRUE(entry->GetDictionary("languageCodeSet", &language_code_set));
  EXPECT_EQ("mozc", input_method_id);
  // Commented out. See above.
  // EXPECT_EQ("Japanese input method (for US keyboard)", display_name);
  ASSERT_TRUE(language_code_set->HasKey("ja"));
}

TEST(LanguageOptionsHandlerTest, GetLanguageList) {
  InputMethodDescriptors descriptors = CreateInputMethodDescriptors();
  scoped_ptr<ListValue> list(
      chromeos::CrosLanguageOptionsHandler::GetLanguageList(descriptors));
  ASSERT_EQ(8U, list->GetSize());

  DictionaryValue* entry = NULL;
  std::string language_code;
  std::string display_name;
  std::string native_display_name;

  // As shown below, the list should be sorted by the display names,
  // and these names should not have duplicates.

  // This comes from kExtraLanguages.
  ASSERT_TRUE(list->GetDictionary(0, &entry));
  ASSERT_TRUE(entry->GetString("code", &language_code));
  ASSERT_TRUE(entry->GetString("displayName", &display_name));
  ASSERT_TRUE(entry->GetString("nativeDisplayName", &native_display_name));
  EXPECT_EQ("nl", language_code);
  EXPECT_EQ("Dutch", display_name);
  EXPECT_EQ("Nederlands", native_display_name);

  // This comes from kExtraLanguages.
  ASSERT_TRUE(list->GetDictionary(1, &entry));
  ASSERT_TRUE(entry->GetString("code", &language_code));
  ASSERT_TRUE(entry->GetString("displayName", &display_name));
  ASSERT_TRUE(entry->GetString("nativeDisplayName", &native_display_name));
  EXPECT_EQ("en-AU", language_code);
  EXPECT_EQ("English (Australia)", display_name);
  EXPECT_EQ("English (Australia)", native_display_name);

  ASSERT_TRUE(list->GetDictionary(2, &entry));
  ASSERT_TRUE(entry->GetString("code", &language_code));
  ASSERT_TRUE(entry->GetString("displayName", &display_name));
  ASSERT_TRUE(entry->GetString("nativeDisplayName", &native_display_name));
  EXPECT_EQ("en-US", language_code);
  EXPECT_EQ("English (United States)", display_name);
  EXPECT_EQ("English (United States)", native_display_name);

  // This comes from kExtraLanguages.
  ASSERT_TRUE(list->GetDictionary(3, &entry));
  ASSERT_TRUE(entry->GetString("code", &language_code));
  ASSERT_TRUE(entry->GetString("displayName", &display_name));
  ASSERT_TRUE(entry->GetString("nativeDisplayName", &native_display_name));
  EXPECT_EQ("fil", language_code);
  EXPECT_EQ("Filipino", display_name);
  EXPECT_EQ("Filipino", native_display_name);

  ASSERT_TRUE(list->GetDictionary(4, &entry));
  ASSERT_TRUE(entry->GetString("code", &language_code));
  ASSERT_TRUE(entry->GetString("displayName", &display_name));
  ASSERT_TRUE(entry->GetString("nativeDisplayName", &native_display_name));
  EXPECT_EQ("fr", language_code);
  EXPECT_EQ("French", display_name);
  EXPECT_EQ("fran\u00E7ais", native_display_name);

  // This comes from kExtraLanguages.
  ASSERT_TRUE(list->GetDictionary(5, &entry));
  ASSERT_TRUE(entry->GetString("code", &language_code));
  ASSERT_TRUE(entry->GetString("displayName", &display_name));
  ASSERT_TRUE(entry->GetString("nativeDisplayName", &native_display_name));
  EXPECT_EQ("id", language_code);
  EXPECT_EQ("Indonesian", display_name);
  EXPECT_EQ("Bahasa Indonesia", native_display_name);

  ASSERT_TRUE(list->GetDictionary(6, &entry));
  ASSERT_TRUE(entry->GetString("code", &language_code));
  ASSERT_TRUE(entry->GetString("displayName", &display_name));
  ASSERT_TRUE(entry->GetString("nativeDisplayName", &native_display_name));
  EXPECT_EQ("ja", language_code);
  EXPECT_EQ("Japanese", display_name);
  EXPECT_EQ("\u65E5\u672C\u8A9E", native_display_name);

  // This comes from kExtraLanguages.
  ASSERT_TRUE(list->GetDictionary(7, &entry));
  ASSERT_TRUE(entry->GetString("code", &language_code));
  ASSERT_TRUE(entry->GetString("displayName", &display_name));
  ASSERT_TRUE(entry->GetString("nativeDisplayName", &native_display_name));
  EXPECT_EQ("es-419", language_code);
  EXPECT_EQ("Spanish (Latin America)", display_name);
  EXPECT_EQ("espa\u00F1ol (Latinoam\u00E9rica)", native_display_name);
}
#endif  // defined(OS_CHROMEOS)

#if !defined(OS_MACOSX)
TEST(LanguageOptionsHandlerTest, GetUILanguageCodeSet) {
  scoped_ptr<DictionaryValue> dictionary(
      LanguageOptionsHandler::GetUILanguageCodeSet());
  EXPECT_TRUE(dictionary->HasKey("en-US"));
  // Note that we don't test a false case, as such an expectation will
  // fail when we add support for the language.
  // EXPECT_FALSE(dictionary->HasKey("no"));
}
#endif  // !defined(OS_MACOSX)

TEST(LanguageOptionsHandlerTest, GetSpellCheckLanguageCodeSet) {
  scoped_ptr<DictionaryValue> dictionary(
      LanguageOptionsHandler::GetSpellCheckLanguageCodeSet());
  EXPECT_TRUE(dictionary->HasKey("en-US"));
}
