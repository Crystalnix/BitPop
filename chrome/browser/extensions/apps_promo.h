// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_APPS_PROMO_H_
#define CHROME_BROWSER_EXTENSIONS_APPS_PROMO_H_
#pragma once

#include <set>
#include <string>

#include "base/gtest_prod_util.h"
#include "chrome/common/extensions/extension.h"

class PrefService;

// This encapsulates business logic for:
// - Whether to show the apps promo in the launcher
// - Whether to expire existing default apps
class AppsPromo {
 public:
  // Groups users by whether they have seen a web store promo before. This is
  // used for deciding to maximize the promo and apps section on the NTP.
  enum UserGroup {
    // Matches no users.
    USERS_NONE = 0,

    // Users who have not seen a promo (last promo id is default value).
    USERS_NEW = 1,

    // Users who have already seen a promo (last promo id is non-default).
    USERS_EXISTING = 1 << 1,
  };

  // Register our preferences. Parts of the promo content are stored in Local
  // State since they're independent of the user profile.
  static void RegisterPrefs(PrefService* local_state);
  static void RegisterUserPrefs(PrefService* prefs);

  // Removes the current promo data.
  static void ClearPromo();

  // Returns true if a promo is available for the current locale.
  static bool IsPromoSupportedForLocale();

  // Returns true if the web store is active for the existing locale.
  static bool IsWebStoreSupportedForLocale();

  // Gets the ID of the current promo.
  static std::string GetPromoId();

  // Gets the text for the promo button.
  static std::string GetPromoButtonText();

  // Gets the text for the promo header.
  static std::string GetPromoHeaderText();

  // Gets the promo link.
  static GURL GetPromoLink();

  // Gets the URL of the promo logo image.
  static GURL GetPromoLogo();

  // Gets the text for the promo "hide this" link.
  static std::string GetPromoExpireText();

  // Gets the user groups for which we should maximize the promo and apps
  // section. The return value is a bitwise OR of UserGroup enums.
  static int GetPromoUserGroup();

  // Called to set the current promo data. The default web store logo will be
  // used if |logo| is empty or not valid.
  static void SetPromo(const std::string& id,
                       const std::string& header_text,
                       const std::string& button_text,
                       const GURL& link,
                       const std::string& expire_text,
                       const GURL& logo,
                       const int user_group);

  // Sets whether the web store and apps section is supported for the current
  // locale.
  static void SetWebStoreSupportedForLocale(bool supported);

  explicit AppsPromo(PrefService* prefs);
  ~AppsPromo();

  // Gets the set of old default apps that may have been installed by previous
  // versions of Chrome.
  const ExtensionIdSet& old_default_apps() const {
    return old_default_app_ids_;
  }

  // Halts the special treatment of the default apps. The default apps may be
  // removed by the caller after calling this method. If the apps remain
  // installed, AppsPromo will no longer consider the apps "default".
  void ExpireDefaultApps();

  // Called to hide the promo from the apps section.
  void HidePromo();

  // Maximizes the apps section on the NTP if the following conditions are met:
  //  (a) the existing promo has not already been maximized
  //  (b) the current user group is targetted by the promo
  void MaximizeAppsIfNecessary();

  // Returns true if the app launcher should be displayed on the NTP.
  bool ShouldShowAppLauncher(const ExtensionIdSet& installed_ids);

  // Returns true if the apps promo should be displayed in the launcher.
  bool ShouldShowPromo(const ExtensionIdSet& installed_ids,
                       bool* just_expired);

 private:
  FRIEND_TEST_ALL_PREFIXES(ExtensionAppsPromo, HappyPath);
  FRIEND_TEST_ALL_PREFIXES(ExtensionAppsPromo, PromoPrefs);
  FRIEND_TEST_ALL_PREFIXES(ExtensionAppsPromo, UpdatePromoFocus);

  // The maximum number of times to show the apps promo. The promo counter
  // actually goes up to this number + 1 because we need to differentiate
  // between the first time we overflow and subsequent times.
  static const int kDefaultAppsCounterMax;

  bool GetDefaultAppsInstalled() const;

  // Gets the UserGroup classification of the current user.
  UserGroup GetCurrentUserGroup() const;

  // Gets/sets the ID of the last promo shown.
  std::string GetLastPromoId();
  void SetLastPromoId(const std::string& id);

  // Gets/sets the number of times the promo has been viewed. Promo views are
  // only counted when the default apps are installed.
  int GetPromoCounter() const;
  void SetPromoCounter(int val);

  // Our permanent state is stored in this PrefService instance.
  PrefService* prefs_;

  // The set of default extensions. Initialized to a static list in the
  // constructor.
  ExtensionIdSet old_default_app_ids_;

  DISALLOW_COPY_AND_ASSIGN(AppsPromo);
};

#endif  // CHROME_BROWSER_EXTENSIONS_APPS_PROMO_H_
