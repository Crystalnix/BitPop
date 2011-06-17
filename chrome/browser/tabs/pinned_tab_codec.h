// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_TABS_PINNED_TAB_CODEC_H_
#define CHROME_BROWSER_TABS_PINNED_TAB_CODEC_H_
#pragma once

#include <vector>

#include "chrome/browser/ui/browser_init.h"
#include "googleurl/src/gurl.h"

class PrefService;
class Profile;

// PinnedTabCodec is used to read and write the set of pinned tabs to
// preferences. When Chrome exits the sets of pinned tabs are written to prefs.
// On startup if the user has not chosen to restore the last session the set of
// pinned tabs is opened.
//
// The entries are stored in preferences as a list of dictionaries, with each
// dictionary describing the entry.
class PinnedTabCodec {
 public:
  // Registers the preference used by this class.
  static void RegisterUserPrefs(PrefService* prefs);

  // Resets the preferences state.
  static void WritePinnedTabs(Profile* profile);

  // Reads and returns the set of pinned tabs to restore from preferences.
  static std::vector<BrowserInit::LaunchWithProfile::Tab> ReadPinnedTabs(
      Profile* profile);

 private:
  PinnedTabCodec();
  ~PinnedTabCodec();

  DISALLOW_COPY_AND_ASSIGN(PinnedTabCodec);
};

#endif  // CHROME_BROWSER_TABS_PINNED_TAB_CODEC_H_
