// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_SYNC_PROMO_SYNC_PROMO_UI_H_
#define CHROME_BROWSER_UI_WEBUI_SYNC_PROMO_SYNC_PROMO_UI_H_
#pragma once

#include "content/public/browser/web_ui_controller.h"

class Profile;
class PrefService;

// The Web UI handler for chrome://syncpromo.
class SyncPromoUI : public content::WebUIController {
 public:
  enum Version {
    VERSION_DEFAULT = 0,
    VERSION_DEVICES,
    VERSION_VERBOSE,
    VERSION_SIMPLE,
    VERSION_DIALOG,
    VERSION_COUNT,
  };

  // Constructs a SyncPromoUI.
  explicit SyncPromoUI(content::WebUI* web_ui);

  // Returns true if the sync promo should be visible.
  // |profile| is the profile of the tab the promo would be shown on.
  static bool ShouldShowSyncPromo(Profile* profile);

  // Returns true if we should show the sync promo at startup.
  // On return |promo_suppressed| is true if a sync promo would normally
  // have been shown but was suppressed due to a experiment.
  static bool ShouldShowSyncPromoAtStartup(Profile* profile,
                                           bool is_new_profile,
                                           bool* promo_suppressed);

  // Called when the sync promo has been shown so that we can keep track
  // of the number of times we've displayed it.
  static void DidShowSyncPromoAtStartup(Profile* profile);

  // Returns true if a user has seen the sync promo at startup previously.
  static bool HasShownPromoAtStartup(Profile* profile);

  // Returns true if the user has previously skipped the sync promo.
  static bool HasUserSkippedSyncPromo(Profile* profile);

  // Registers the fact that the user has skipped the sync promo.
  static void SetUserSkippedSyncPromo(Profile* profile);

  // Registers the preferences the Sync Promo UI needs.
  static void RegisterUserPrefs(PrefService* prefs);

  // Returns the sync promo URL wth the given arguments in the query.
  // |next_page| is the URL to navigate to when the user completes or skips the
  // promo. If an empty URL is given then the promo will navigate to the NTP.
  // If |show_title| is true then the promo title is made visible.
  // |source| is a string that identifies from where the sync promo is being
  // called, and is used to record sync promo UMA stats in the context of the
  // source.
  static GURL GetSyncPromoURL(const GURL& next_page,
                              bool show_title,
                              const std::string& source);

  // Gets the is launch page value from the query portion of the sync promo URL.
  static bool GetIsLaunchPageForSyncPromoURL(const GURL& url);

  // Gets the next page URL from the query portion of the sync promo URL.
  static GURL GetNextPageURLForSyncPromoURL(const GURL& url);

  // Gets the source from the query portion of the sync promo URL.
  static std::string GetSourceForSyncPromoURL(const GURL& url);

  // Returns true if the sync promo page was ever shown at startup.
  static bool UserHasSeenSyncPromoAtStartup(Profile* profile);

  // Returns the version of the sync promo UI that we should display.
  // Each version changes the UI slightly (for example, replacing text with
  // an infographic).
  static Version GetSyncPromoVersion();

 private:
  DISALLOW_COPY_AND_ASSIGN(SyncPromoUI);
};

#endif  // CHROME_BROWSER_UI_WEBUI_SYNC_PROMO_SYNC_PROMO_UI_H_
