// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SPEECH_CHROME_SPEECH_INPUT_PREFERENCES_H_
#define CHROME_BROWSER_SPEECH_CHROME_SPEECH_INPUT_PREFERENCES_H_
#pragma once

#include "base/basictypes.h"
#include "chrome/browser/prefs/pref_service.h"
#include "content/browser/speech/speech_input_preferences.h"

class PrefService;

class ChromeSpeechInputPreferences : public SpeechInputPreferences {
 public:
  explicit ChromeSpeechInputPreferences(PrefService* pref_service);

  // SpeechInputPreferences methods.
  virtual bool filter_profanities() const OVERRIDE;
  virtual void set_filter_profanities(bool filter_profanities) OVERRIDE;

 private:
  virtual ~ChromeSpeechInputPreferences();

  // Only to be accessed and modified on the IO thread.
  bool filter_profanities_;

  DISALLOW_COPY_AND_ASSIGN(ChromeSpeechInputPreferences);
};

#endif  // CHROME_BROWSER_SPEECH_CHROME_SPEECH_INPUT_PREFERENCES_H_
