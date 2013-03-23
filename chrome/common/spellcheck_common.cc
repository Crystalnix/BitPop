// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/spellcheck_common.h"

#include "base/file_path.h"

namespace chrome {
namespace spellcheck_common {

struct LanguageRegion {
  const char* language;  // The language.
  const char* language_region;  // language & region, used by dictionaries.
};

struct LanguageVersion {
  const char* language;  // The language input.
  const char* version;   // The corresponding version.
};

static const LanguageRegion g_supported_spellchecker_languages[] = {
  // Several languages are not to be included in the spellchecker list:
  // th-TH
  {"af", "af-ZA"},
  {"bg", "bg-BG"},
  {"ca", "ca-ES"},
  {"cs", "cs-CZ"},
  {"da", "da-DK"},
  {"de", "de-DE"},
  {"el", "el-GR"},
  {"en-AU", "en-AU"},
  {"en-CA", "en-CA"},
  {"en-GB", "en-GB"},
  {"en-US", "en-US"},
  {"es", "es-ES"},
  {"et", "et-EE"},
  {"fo", "fo-FO"},
  {"fr", "fr-FR"},
  {"he", "he-IL"},
  {"hi", "hi-IN"},
  {"hr", "hr-HR"},
  {"hu", "hu-HU"},
  {"id", "id-ID"},
  {"it", "it-IT"},
  {"lt", "lt-LT"},
  {"lv", "lv-LV"},
  {"nb", "nb-NO"},
  {"nl", "nl-NL"},
  {"pl", "pl-PL"},
  {"pt-BR", "pt-BR"},
  {"pt-PT", "pt-PT"},
  {"ro", "ro-RO"},
  {"ru", "ru-RU"},
  {"sk", "sk-SK"},
  {"sl", "sl-SI"},
  {"sh", "sh"},
  {"sr", "sr"},
  {"sv", "sv-SE"},
  {"tr", "tr-TR"},
  {"uk", "uk-UA"},
  {"vi", "vi-VN"},
};

bool IsValidRegion(const std::string& region) {
  for (size_t i = 0; i < arraysize(g_supported_spellchecker_languages);
       ++i) {
    if (g_supported_spellchecker_languages[i].language_region == region)
      return true;
  }
  return false;
}

// This function returns the language-region version of language name.
// e.g. returns hi-IN for hi.
std::string GetSpellCheckLanguageRegion(const std::string& input_language) {
  for (size_t i = 0; i < arraysize(g_supported_spellchecker_languages);
       ++i) {
    if (g_supported_spellchecker_languages[i].language == input_language) {
      return std::string(
          g_supported_spellchecker_languages[i].language_region);
    }
  }

  return input_language;
}

FilePath GetVersionedFileName(const std::string& input_language,
                              const FilePath& dict_dir) {
  // The default dictionary version is 1-2. These versions have been augmented
  // with additional words found by the translation team.
  static const char kDefaultVersionString[] = "-1-2";

  static LanguageVersion special_version_string[] = {
    {"es-ES", "-1-1"},  // 1-1: Have not been augmented with addtional words.
    {"nl-NL", "-1-1"},
    {"sv-SE", "-1-1"},
    {"he-IL", "-1-1"},
    {"el-GR", "-1-1"},
    {"hi-IN", "-1-1"},
    {"tr-TR", "-1-1"},
    {"et-EE", "-1-1"},
    {"lt-LT", "-1-3"},  // 1-3 (Feb 2009): new words, as well as an upgraded
                        // dictionary.
    {"pl-PL", "-1-3"},
    {"fr-FR", "-2-0"},  // 2-0 (2010): upgraded dictionaries.
    {"hu-HU", "-2-0"},
    {"ro-RO", "-2-0"},
    {"ru-RU", "-2-0"},
    {"bg-BG", "-2-0"},
    {"sr",    "-2-0"},
    {"uk-UA", "-2-0"},
    {"pt-BR", "-2-2"},  // 2-2 (Mar 2011): upgraded a dictionary.
    {"sh",    "-2-2"},  // 2-2 (Mar 2011): added a dictionary.
    {"ca-ES", "-2-3"},  // 2-3 (May 2012): upgraded a dictionary.
    {"sv-SE", "-2-3"},  // 2-3 (May 2012): upgraded a dictionary.
    {"af-ZA", "-2-3"},  // 2-3 (May 2012): added a dictionary.
    {"fo-FO", "-2-3"},  // 2-3 (May 2012): added a dictionary.
    {"en-US", "-2-4"},  // 2-4 (October 2012): add more words.
    {"en-CA", "-2-4"},
    {"en-GB", "-2-5"},  // 2-5 (Nov 2012): Added NOSUGGEST flag = !.
    {"en-AU", "-2-5"},  // Marked 1 word in each.

  };

  // Generate the bdict file name using default version string or special
  // version string, depending on the language.
  std::string language = GetSpellCheckLanguageRegion(input_language);
  std::string versioned_bdict_file_name(language + kDefaultVersionString +
                                        ".bdic");
  for (size_t i = 0; i < arraysize(special_version_string); ++i) {
    if (language == special_version_string[i].language) {
      versioned_bdict_file_name =
          language + special_version_string[i].version + ".bdic";
      break;
    }
  }

  return dict_dir.AppendASCII(versioned_bdict_file_name);
}

std::string GetCorrespondingSpellCheckLanguage(const std::string& language) {
  // Look for exact match in the Spell Check language list.
  for (size_t i = 0; i < arraysize(g_supported_spellchecker_languages);
       ++i) {
    // First look for exact match in the language region of the list.
    std::string spellcheck_language(
        g_supported_spellchecker_languages[i].language);
    if (spellcheck_language == language)
      return language;

    // Next, look for exact match in the language_region part of the list.
    std::string spellcheck_language_region(
        g_supported_spellchecker_languages[i].language_region);
    if (spellcheck_language_region == language)
      return g_supported_spellchecker_languages[i].language;
  }

  // No match found - return blank.
  return std::string();
}

void SpellCheckLanguages(std::vector<std::string>* languages) {
  for (size_t i = 0; i < arraysize(g_supported_spellchecker_languages);
       ++i) {
    languages->push_back(g_supported_spellchecker_languages[i].language);
  }
}

}  // namespace spellcheck_common
}  // namespace chrome
