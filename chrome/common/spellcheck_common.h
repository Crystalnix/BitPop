// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_COMMON_SPELLCHECK_COMMON_H_
#define CHROME_COMMON_SPELLCHECK_COMMON_H_

#include <string>
#include <vector>

class FilePath;

namespace chrome {
namespace spellcheck_common {

// Max number of dictionary suggestions.
static const int kMaxSuggestions = 5;

static const int kMaxAutoCorrectWordSize = 8;

FilePath GetVersionedFileName(const std::string& input_language,
                              const FilePath& dict_dir);

std::string GetCorrespondingSpellCheckLanguage(const std::string& language);

// Get SpellChecker supported languages.
void SpellCheckLanguages(std::vector<std::string>* languages);

}  // namespace spellcheck_common
}  // namespace chrome

#endif  // CHROME_COMMON_SPELLCHECK_COMMON_H_
