// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SPELLCHECKER_SPELLCHECK_DICTIONARY_H_
#define CHROME_BROWSER_SPELLCHECKER_SPELLCHECK_DICTIONARY_H_

#include "base/basictypes.h"

class Profile;

// Defines a dictionary for use in the spellchecker system and provides access
// to words within the dictionary.
class SpellcheckDictionary {
 public:
  explicit SpellcheckDictionary(Profile* profile) : profile_(profile) {}
  virtual ~SpellcheckDictionary() {}

  virtual void Load() = 0;

 protected:
  // Weak pointer to the profile owning this dictionary
  // TODO(rlp): Entire profile may be overkill. Might be able to use just
  // profile_dir.
  Profile* profile_;
  DISALLOW_COPY_AND_ASSIGN(SpellcheckDictionary);
};

#endif  // CHROME_BROWSER_SPELLCHECKER_SPELLCHECK_DICTIONARY_H_
