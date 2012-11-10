// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This file contains the specification of setup main functions.

#ifndef CHROME_INSTALLER_SETUP_INSTALL_H_
#define CHROME_INSTALLER_SETUP_INSTALL_H_

#include <vector>

#include "base/string16.h"
#include "base/version.h"
#include "chrome/installer/util/installation_state.h"
#include "chrome/installer/util/installer_state.h"
#include "chrome/installer/util/master_preferences.h"
#include "chrome/installer/util/product.h"
#include "chrome/installer/util/util_constants.h"

class FilePath;

namespace installer {

class InstallationState;
class InstallerState;
class MasterPreferences;

// Escape |att_value| as per the XML AttValue production
// (http://www.w3.org/TR/2008/REC-xml-20081126/#NT-AttValue) for a value in
// single quotes.
void EscapeXmlAttributeValueInSingleQuotes(string16* att_value);

// Creates VisualElementsManifest.xml beside chrome.exe in |src_path| if
// |src_path|\VisualElements exists.
// Returns true unless the manifest is supposed to be created, but fails to be.
bool CreateVisualElementsManifest(const FilePath& src_path,
                                  const Version& version);

// This method, if SHORTCUT_CREATE_ALWAYS is specified in |options|, creates
// Start Menu shortcuts for all users or only for the current user depending on
// whether it is a system wide install or a user-level install. It also pins
// the browser shortcut to the current user's taskbar.
// If SHORTCUT_CREATE_ALWAYS is not specified in |options|: this method only
// updates existing Start Menu shortcuts.
// |setup_exe|: The path to the setup.exe stored in <version_dir>\Installer
// post-install.
// |options|: bitfield for which the options come from
// ShellUtil::ChromeShortcutOptions.
void CreateOrUpdateStartMenuAndTaskbarShortcuts(
    const InstallerState& installer_state,
    const FilePath& setup_exe,
    const Product& product,
    uint32 options);

// This method, if SHORTCUT_CREATE_ALWAYS is specified in |options|, creates
// Desktop and Quick Launch shortcuts for all users or only for the current user
// depending on whether it is a system wide install or a user-level install.
// If SHORTCUT_CREATE_ALWAYS is not specified in |options|: this method only
// updates existing shortcuts.
// |options|: bitfield for which the options come from
// ShellUtil::ChromeShortcutOptions.
// If SHORTCUT_ALTERNATE is specified in |options|, an alternate shortcut name
// is used for the Desktop shortcut.
void CreateOrUpdateDesktopAndQuickLaunchShortcuts(
    const InstallerState& installer_state,
    const Product& product,
    uint32 options);

// Registers Chrome on this machine.
// If |make_chrome_default|, also attempts to make Chrome default (potentially
// popping a UAC if the user is not an admin and HKLM registrations are required
// to register Chrome's capabilities on this version of Windows (i.e.
// pre-Win8)).
void RegisterChromeOnMachine(const InstallerState& installer_state,
                             const Product& product,
                             bool make_chrome_default);

// This function installs or updates a new version of Chrome. It returns
// install status (failed, new_install, updated etc).
//
// setup_path: Path to the executable (setup.exe) as it will be copied
//           to Chrome install folder after install is complete
// archive_path: Path to the archive (chrome.7z) as it will be copied
//               to Chrome install folder after install is complete
// install_temp_path: working directory used during install/update. It should
//                    also has a sub dir source that contains a complete
//                    and unpacked Chrome package.
// prefs: master preferences. See chrome/installer/util/master_preferences.h.
// new_version: new Chrome version that needs to be installed
// package: Represents the target installation folder and all distributions
//          to be installed in that folder.
//
// Note: since caller unpacks Chrome to install_temp_path\source, the caller
// is responsible for cleaning up install_temp_path.
InstallStatus InstallOrUpdateProduct(
    const InstallationState& original_state,
    const InstallerState& installer_state,
    const FilePath& setup_path,
    const FilePath& archive_path,
    const FilePath& install_temp_path,
    const FilePath& prefs_path,
    const installer::MasterPreferences& prefs,
    const Version& new_version);

}  // namespace installer

#endif  // CHROME_INSTALLER_SETUP_INSTALL_H_
