// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/input_method/input_method_util.h"

#include <string>

#include "base/utf_string_conversions.h"
#include "chrome/browser/chromeos/cros/cros_library.h"
#include "grit/generated_resources.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/l10n/l10n_util.h"

namespace chromeos {
namespace input_method {

class InputMethodUtilTest : public testing::Test {
 public:
  static void SetUpTestCase() {
    // Reload the internal maps before running tests, with the stub
    // libcros enabled, so that test data is loaded properly.
    ScopedStubCrosEnabler stub_cros_enabler;
    ReloadInternalMaps();
  }

 private:
  // Ensure we always use the stub libcros in each test.
  ScopedStubCrosEnabler stub_cros_enabler_;
};

TEST_F(InputMethodUtilTest, GetStringUTF8) {
  EXPECT_EQ("Pinyin input method",
            GetStringUTF8("Pinyin", ""));
  EXPECT_EQ("Japanese input method (for US Dvorak keyboard)",
            GetStringUTF8("Mozc (US Dvorak keyboard layout)", ""));
  EXPECT_EQ("Google Japanese Input (for US Dvorak keyboard)",
            GetStringUTF8("Google Japanese Input (US Dvorak keyboard layout)",
                          ""));
}

TEST_F(InputMethodUtilTest, StringIsSupported) {
  EXPECT_TRUE(StringIsSupported("Hiragana", "mozc"));
  EXPECT_TRUE(StringIsSupported("Latin", "mozc"));
  EXPECT_TRUE(StringIsSupported("Direct input", "mozc"));
  EXPECT_FALSE(StringIsSupported(
      "####THIS_STRING_IS_NOT_SUPPORTED####", "mozc"));
  EXPECT_TRUE(StringIsSupported("Chinese", "pinyin"));
  EXPECT_TRUE(StringIsSupported("Chinese", "mozc-chewing"));
  // The string "Chinese" is not for "hangul".
  EXPECT_FALSE(StringIsSupported("Chinese", "hangul"));
}

TEST_F(InputMethodUtilTest, NormalizeLanguageCode) {
  // TODO(yusukes): test all language codes that IBus provides.
  EXPECT_EQ("ja", NormalizeLanguageCode("ja"));
  EXPECT_EQ("ja", NormalizeLanguageCode("jpn"));
  // In the past "t" had a meaning of "other language" for some m17n latin
  // input methods for testing purpose, but it is no longer used. We test "t"
  // here as just an "unknown" language.
  EXPECT_EQ("t", NormalizeLanguageCode("t"));
  EXPECT_EQ("zh-CN", NormalizeLanguageCode("zh-CN"));
  EXPECT_EQ("zh-CN", NormalizeLanguageCode("zh_CN"));
  EXPECT_EQ("en-US", NormalizeLanguageCode("EN_us"));
  // See app/l10n_util.cc for es-419.
  EXPECT_EQ("es-419", NormalizeLanguageCode("es_419"));

  // Special three-letter language codes.
  EXPECT_EQ("cs", NormalizeLanguageCode("cze"));
  EXPECT_EQ("de", NormalizeLanguageCode("ger"));
  EXPECT_EQ("el", NormalizeLanguageCode("gre"));
  EXPECT_EQ("hr", NormalizeLanguageCode("scr"));
  EXPECT_EQ("ro", NormalizeLanguageCode("rum"));
  EXPECT_EQ("sk", NormalizeLanguageCode("slo"));
}

TEST_F(InputMethodUtilTest, IsKeyboardLayout) {
  EXPECT_TRUE(IsKeyboardLayout("xkb:us::eng"));
  EXPECT_FALSE(IsKeyboardLayout("mozc"));
}

TEST_F(InputMethodUtilTest, GetLanguageCodeFromDescriptor) {
  EXPECT_EQ("ja", GetLanguageCodeFromDescriptor(
      InputMethodDescriptor("mozc", "Mozc", "us", "ja")));
  EXPECT_EQ("zh-TW", GetLanguageCodeFromDescriptor(
      InputMethodDescriptor("mozc-chewing", "Chewing", "us", "zh")));
  EXPECT_EQ("zh-TW", GetLanguageCodeFromDescriptor(
      InputMethodDescriptor("m17n:zh:cangjie", "Cangjie", "us", "zh")));
  EXPECT_EQ("zh-TW", GetLanguageCodeFromDescriptor(
      InputMethodDescriptor("m17n:zh:quick", "Quick", "us", "zh")));
  EXPECT_EQ("zh-CN", GetLanguageCodeFromDescriptor(
      InputMethodDescriptor("pinyin", "Pinyin", "us", "zh")));
  EXPECT_EQ("en-US", GetLanguageCodeFromDescriptor(
      InputMethodDescriptor("xkb:us::eng", "USA", "us", "eng")));
  EXPECT_EQ("en-UK", GetLanguageCodeFromDescriptor(
      InputMethodDescriptor("xkb:uk::eng", "United Kingdom", "us", "eng")));
}

TEST_F(InputMethodUtilTest, GetKeyboardLayoutName) {
  // Unsupported case.
  EXPECT_EQ("", GetKeyboardLayoutName("UNSUPPORTED_ID"));

  // Supported cases (samples).
  EXPECT_EQ("jp", GetKeyboardLayoutName("mozc-jp"));
  EXPECT_EQ("us", GetKeyboardLayoutName("pinyin"));
  EXPECT_EQ("us", GetKeyboardLayoutName("m17n:ar:kbd"));
  EXPECT_EQ("es", GetKeyboardLayoutName("xkb:es::spa"));
  EXPECT_EQ("es(cat)", GetKeyboardLayoutName("xkb:es:cat:cat"));
  EXPECT_EQ("gb(extd)", GetKeyboardLayoutName("xkb:gb:extd:eng"));
  EXPECT_EQ("us", GetKeyboardLayoutName("xkb:us::eng"));
  EXPECT_EQ("us(dvorak)", GetKeyboardLayoutName("xkb:us:dvorak:eng"));
  EXPECT_EQ("us(colemak)", GetKeyboardLayoutName("xkb:us:colemak:eng"));
  EXPECT_EQ("de(neo)", GetKeyboardLayoutName("xkb:de:neo:ger"));
}

TEST_F(InputMethodUtilTest, GetLanguageCodeFromInputMethodId) {
  // Make sure that the -CN is added properly.
  EXPECT_EQ("zh-CN", GetLanguageCodeFromInputMethodId("pinyin"));
}

TEST_F(InputMethodUtilTest, GetInputMethodDisplayNameFromId) {
  EXPECT_EQ("Pinyin input method", GetInputMethodDisplayNameFromId("pinyin"));
  EXPECT_EQ("US keyboard",
            GetInputMethodDisplayNameFromId("xkb:us::eng"));
  EXPECT_EQ("", GetInputMethodDisplayNameFromId("nonexistent"));
}

TEST_F(InputMethodUtilTest, GetInputMethodDescriptorFromId) {
  EXPECT_EQ(NULL, GetInputMethodDescriptorFromId("non_existent"));

  const InputMethodDescriptor* descriptor =
      GetInputMethodDescriptorFromId("pinyin");
  ASSERT_TRUE(NULL != descriptor);  // ASSERT_NE doesn't compile.
  EXPECT_EQ("pinyin", descriptor->id);
  EXPECT_EQ("Pinyin", descriptor->display_name);
  EXPECT_EQ("us", descriptor->keyboard_layout);
  // This is not zh-CN as the language code in InputMethodDescriptor is
  // not normalized to our format. The normalization is done in
  // GetLanguageCodeFromDescriptor().
  EXPECT_EQ("zh", descriptor->language_code);
}

TEST_F(InputMethodUtilTest, GetLanguageNativeDisplayNameFromCode) {
  EXPECT_EQ(UTF8ToUTF16("suomi"), GetLanguageNativeDisplayNameFromCode("fi"));
}

TEST_F(InputMethodUtilTest, SortLanguageCodesByNames) {
  std::vector<std::string> language_codes;
  // Check if this function can handle an empty list.
  SortLanguageCodesByNames(&language_codes);

  language_codes.push_back("ja");
  language_codes.push_back("fr");
  // For "t", see the comment in NormalizeLanguageCode test.
  language_codes.push_back("t");
  SortLanguageCodesByNames(&language_codes);
  ASSERT_EQ(3U, language_codes.size());
  ASSERT_EQ("fr", language_codes[0]);  // French
  ASSERT_EQ("ja", language_codes[1]);  // Japanese
  ASSERT_EQ("t",  language_codes[2]);  // Others

  // Add a duplicate entry and see if it works.
  language_codes.push_back("ja");
  SortLanguageCodesByNames(&language_codes);
  ASSERT_EQ(4U, language_codes.size());
  ASSERT_EQ("fr", language_codes[0]);  // French
  ASSERT_EQ("ja", language_codes[1]);  // Japanese
  ASSERT_EQ("ja", language_codes[2]);  // Japanese
  ASSERT_EQ("t",  language_codes[3]);  // Others
}

TEST_F(InputMethodUtilTest, GetInputMethodIdsForLanguageCode) {
  std::multimap<std::string, std::string> language_code_to_ids_map;
  language_code_to_ids_map.insert(std::make_pair("ja", "mozc"));
  language_code_to_ids_map.insert(std::make_pair("ja", "mozc-jp"));
  language_code_to_ids_map.insert(std::make_pair("ja", "xkb:jp:jpn"));
  language_code_to_ids_map.insert(std::make_pair("fr", "xkb:fr:fra"));

  std::vector<std::string> result;
  EXPECT_TRUE(GetInputMethodIdsFromLanguageCodeInternal(
      language_code_to_ids_map, "ja", kAllInputMethods, &result));
  EXPECT_EQ(3U, result.size());
  EXPECT_TRUE(GetInputMethodIdsFromLanguageCodeInternal(
      language_code_to_ids_map, "ja", kKeyboardLayoutsOnly, &result));
  ASSERT_EQ(1U, result.size());
  EXPECT_EQ("xkb:jp:jpn", result[0]);

  EXPECT_TRUE(GetInputMethodIdsFromLanguageCodeInternal(
      language_code_to_ids_map, "fr", kAllInputMethods, &result));
  ASSERT_EQ(1U, result.size());
  EXPECT_EQ("xkb:fr:fra", result[0]);
  EXPECT_TRUE(GetInputMethodIdsFromLanguageCodeInternal(
      language_code_to_ids_map, "fr", kKeyboardLayoutsOnly, &result));
  ASSERT_EQ(1U, result.size());
  EXPECT_EQ("xkb:fr:fra", result[0]);

  EXPECT_FALSE(GetInputMethodIdsFromLanguageCodeInternal(
      language_code_to_ids_map, "invalid_lang", kAllInputMethods, &result));
  EXPECT_FALSE(GetInputMethodIdsFromLanguageCodeInternal(
      language_code_to_ids_map, "invalid_lang", kKeyboardLayoutsOnly, &result));
}

// US keyboard + English US UI = US keyboard only.
TEST_F(InputMethodUtilTest, GetFirstLoginInputMethodIds_Us_And_EnUs) {
  const InputMethodDescriptor* descriptor =
      GetInputMethodDescriptorFromId("xkb:us::eng");  // US keyboard.
  ASSERT_TRUE(NULL != descriptor);  // ASSERT_NE doesn't compile.
  std::vector<std::string> input_method_ids;
  GetFirstLoginInputMethodIds("en-US", *descriptor, &input_method_ids);
  ASSERT_EQ(1U, input_method_ids.size());
  EXPECT_EQ("xkb:us::eng", input_method_ids[0]);
}

// US keyboard + Japanese UI = US keyboard + mozc.
TEST_F(InputMethodUtilTest, GetFirstLoginInputMethodIds_Us_And_Ja) {
  const InputMethodDescriptor* descriptor =
      GetInputMethodDescriptorFromId("xkb:us::eng");  // US keyboard.
  ASSERT_TRUE(NULL != descriptor);  // ASSERT_NE doesn't compile.
  std::vector<std::string> input_method_ids;
  GetFirstLoginInputMethodIds("ja", *descriptor, &input_method_ids);
  ASSERT_EQ(2U, input_method_ids.size());
  EXPECT_EQ("xkb:us::eng", input_method_ids[0]);
  EXPECT_EQ("mozc", input_method_ids[1]);  // Mozc for US keybaord.
}

// JP keyboard + Japanese UI = JP keyboard + mozc-jp.
TEST_F(InputMethodUtilTest, GetFirstLoginInputMethodIds_JP_And_Ja) {
  const InputMethodDescriptor* descriptor =
      GetInputMethodDescriptorFromId("xkb:jp::jpn");  // Japanese keyboard.
  ASSERT_TRUE(NULL != descriptor);  // ASSERT_NE doesn't compile.
  std::vector<std::string> input_method_ids;
  GetFirstLoginInputMethodIds("ja", *descriptor, &input_method_ids);
  ASSERT_EQ(2U, input_method_ids.size());
  EXPECT_EQ("xkb:jp::jpn", input_method_ids[0]);
  EXPECT_EQ("mozc-jp", input_method_ids[1]);  // Mozc for JP keybaord.
}

// US dvorak keyboard + Japanese UI = US dvorak keyboard + mozc-dv.
TEST_F(InputMethodUtilTest, GetFirstLoginInputMethodIds_Dvorak_And_Ja) {
  const InputMethodDescriptor* descriptor =
      GetInputMethodDescriptorFromId("xkb:us:dvorak:eng");  // US Drovak keyboard.
  ASSERT_TRUE(NULL != descriptor);  // ASSERT_NE doesn't compile.
  std::vector<std::string> input_method_ids;
  GetFirstLoginInputMethodIds("ja", *descriptor, &input_method_ids);
  ASSERT_EQ(2U, input_method_ids.size());
  EXPECT_EQ("xkb:us:dvorak:eng", input_method_ids[0]);
  EXPECT_EQ("mozc-dv", input_method_ids[1]);  // Mozc for US Dvorak keybaord.
}

// US keyboard + Russian UI = US keyboard + Russsian keyboard
TEST_F(InputMethodUtilTest, GetFirstLoginInputMethodIds_Us_And_Ru) {
  const InputMethodDescriptor* descriptor =
      GetInputMethodDescriptorFromId("xkb:us::eng");  // US keyboard.
  ASSERT_TRUE(NULL != descriptor);  // ASSERT_NE doesn't compile.
  std::vector<std::string> input_method_ids;
  GetFirstLoginInputMethodIds("ru", *descriptor, &input_method_ids);
  ASSERT_EQ(2U, input_method_ids.size());
  EXPECT_EQ("xkb:us::eng", input_method_ids[0]);
  EXPECT_EQ("xkb:ru::rus", input_method_ids[1]);  // Russian keyboard.
}

// US keyboard + Traditional Chinese = US keyboard + chewing.
TEST_F(InputMethodUtilTest, GetFirstLoginInputMethodIds_Us_And_ZhTw) {
  const InputMethodDescriptor* descriptor =
      GetInputMethodDescriptorFromId("xkb:us::eng");  // US keyboard.
  ASSERT_TRUE(NULL != descriptor);  // ASSERT_NE doesn't compile.
  std::vector<std::string> input_method_ids;
  GetFirstLoginInputMethodIds("zh-TW", *descriptor, &input_method_ids);
  ASSERT_EQ(2U, input_method_ids.size());
  EXPECT_EQ("xkb:us::eng", input_method_ids[0]);
  EXPECT_EQ("mozc-chewing", input_method_ids[1]);  // Chewing.
}

// US keyboard + Thai = US keyboard + kesmanee.
TEST_F(InputMethodUtilTest, GetFirstLoginInputMethodIds_Us_And_Th) {
  const InputMethodDescriptor* descriptor =
      GetInputMethodDescriptorFromId("xkb:us::eng");  // US keyboard.
  ASSERT_TRUE(NULL != descriptor);  // ASSERT_NE doesn't compile.
  std::vector<std::string> input_method_ids;
  GetFirstLoginInputMethodIds("th", *descriptor, &input_method_ids);
  ASSERT_EQ(2U, input_method_ids.size());
  EXPECT_EQ("xkb:us::eng", input_method_ids[0]);
  EXPECT_EQ("m17n:th:kesmanee", input_method_ids[1]);  // Kesmanee.
}

// US keyboard + Vietnamese = US keyboard + TCVN6064.
TEST_F(InputMethodUtilTest, GetFirstLoginInputMethodIds_Us_And_Vi) {
  const InputMethodDescriptor* descriptor =
      GetInputMethodDescriptorFromId("xkb:us::eng");  // US keyboard.
  ASSERT_TRUE(NULL != descriptor);  // ASSERT_NE doesn't compile.
  std::vector<std::string> input_method_ids;
  GetFirstLoginInputMethodIds("vi", *descriptor, &input_method_ids);
  ASSERT_EQ(2U, input_method_ids.size());
  EXPECT_EQ("xkb:us::eng", input_method_ids[0]);
  EXPECT_EQ("m17n:vi:tcvn", input_method_ids[1]);  // TCVN6064.
}

TEST_F(InputMethodUtilTest, GetLanguageCodesFromInputMethodIds) {
  std::vector<std::string> input_method_ids;
  input_method_ids.push_back("xkb:us::eng");  // English US.
  input_method_ids.push_back("xkb:us:dvorak:eng");  // English US Dvorak.
  input_method_ids.push_back("mozc-jp");  // Japanese.
  input_method_ids.push_back("xkb:fr::fra");  // French France.
  std::vector<std::string> language_codes;
  GetLanguageCodesFromInputMethodIds(input_method_ids, &language_codes);
  ASSERT_EQ(3U, language_codes.size());
  EXPECT_EQ("en-US", language_codes[0]);
  EXPECT_EQ("ja", language_codes[1]);
  EXPECT_EQ("fr", language_codes[2]);
}

}  // namespace input_method
}  // namespace chromeos
