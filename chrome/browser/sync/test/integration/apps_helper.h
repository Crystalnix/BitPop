// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_TEST_INTEGRATION_APPS_HELPER_H_
#define CHROME_BROWSER_SYNC_TEST_INTEGRATION_APPS_HELPER_H_

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "chrome/browser/sync/test/integration/sync_test.h"
#include "chrome/common/string_ordinal.h"

class Profile;

namespace apps_helper {

// Returns true iff the profile with index |index| has the same apps as the
// verifier.
bool HasSameAppsAsVerifier(int index) WARN_UNUSED_RESULT;

// Returns true iff all existing profiles have the same apps as the verifier.
bool AllProfilesHaveSameAppsAsVerifier() WARN_UNUSED_RESULT;

// Installs the app for the given index to |profile|, and returns the extension
// ID of the new app.
std::string InstallApp(Profile* profile, int index);

// Installs the app for the given index to all profiles (including the
// verifier), and returns the extension ID of the new app.
std::string InstallAppForAllProfiles(int index);

// Uninstalls the app for the given index from |profile|. Assumes that it was
// previously installed.
void UninstallApp(Profile* profile, int index);

// Installs all pending synced apps for |profile|.
void InstallAppsPendingForSync(Profile* profile);

// Enables the app for the given index on |profile|.
void EnableApp(Profile* profile, int index);

// Disables the appfor the given index on |profile|.
void DisableApp(Profile* profile, int index);

// Enables the app for the given index in incognito mode on |profile|.
void IncognitoEnableApp(Profile* profile, int index);

// Disables the app for the given index in incognito mode on |profile|.
void IncognitoDisableApp(Profile* profile, int index);

// Gets the page ordinal value for the application at the given index on
// |profile|.
StringOrdinal GetPageOrdinalForApp(Profile* profile, int app_index);

// Sets a new |page_ordinal| value for the application at the given index
// on |profile|.
void SetPageOrdinalForApp(
    Profile* profile, int app_index, const StringOrdinal& page_ordinal);

// Gets the app launch ordinal value for the application at the given index on
// |profile|.
StringOrdinal GetAppLaunchOrdinalForApp(Profile* profile, int app_index);

// Sets a new |page_ordinal| value for the application at the given index
// on |profile|.
void SetAppLaunchOrdinalForApp(
    Profile* profile, int app_index, const StringOrdinal& app_launch_ordinal);

// Copy the page and app launch ordinal value for the application at the given
// index on |profile_source| to |profile_destination|.
// The main intention of this is to properly setup the values on the verifier
// profile in situations where the other profiles have conflicting values.
void CopyNTPOrdinals(Profile* source, Profile* destination, int index);

// Fix any NTP icon collisions that are currently in |profile|.
void FixNTPOrdinalCollisions(Profile* profile);

}  // namespace apps_helper

#endif  // CHROME_BROWSER_SYNC_TEST_INTEGRATION_APPS_HELPER_H_
