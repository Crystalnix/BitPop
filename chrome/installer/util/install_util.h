// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This file declares utility functions for the installer. The original reason
// for putting these functions in installer\util library is so that we can
// separate out the critical logic and write unit tests for it.

#ifndef CHROME_INSTALLER_UTIL_INSTALL_UTIL_H_
#define CHROME_INSTALLER_UTIL_INSTALL_UTIL_H_
#pragma once

#include <tchar.h>
#include <windows.h>
#include <string>

#include "base/basictypes.h"
#include "base/command_line.h"
#include "chrome/installer/util/browser_distribution.h"
#include "chrome/installer/util/util_constants.h"

class Version;
class WorkItemList;

// This is a utility class that provides common installation related
// utility methods that can be used by installer and also unit tested
// independently.
class InstallUtil {
 public:
  // Launches given exe as admin on Vista.
  static bool ExecuteExeAsAdmin(const CommandLine& cmd, DWORD* exit_code);

  // Reads the uninstall command for Chromium from registry and returns it.
  // If system_install is true the command is read from HKLM, otherwise
  // from HKCU.
  static CommandLine GetChromeUninstallCmd(
      bool system_install,
      BrowserDistribution::Type distribution_type);

  // Find the version of Chrome installed on the system by checking the
  // Google Update registry key. Returns the version or NULL if no version is
  // found.
  // system_install: if true, looks for version number under the HKLM root,
  //                 otherwise looks under the HKCU.
  static Version* GetChromeVersion(BrowserDistribution* dist,
                                   bool system_install);

  // Find the last critical update (version) of Chrome. Returns the version or
  // NULL if no such version is found. A critical update is a specially flagged
  // version (by Google Update) that contains an important security fix.
  // system_install: if true, looks for version number under the HKLM root,
  //                 otherwise looks under the HKCU.
  static Version* GetCriticalUpdateVersion(BrowserDistribution* dist,
                                           bool system_install);

  // This function checks if the current OS is supported for Chromium.
  static bool IsOSSupported();

  // Adds work items to |install_list|, which should be a
  // NoRollbackWorkItemList, to set installer error information in the registry
  // for consumption by Google Update.  |state_key| must be the full path to an
  // app's ClientState key.  See InstallerState::WriteInstallerResult for more
  // details.
  static void AddInstallerResultItems(bool system_install,
                                      const std::wstring& state_key,
                                      installer::InstallStatus status,
                                      int string_resource_id,
                                      const std::wstring* const launch_cmd,
                                      WorkItemList* install_list);

  // Update the installer stage reported by Google Update.  |state_key_path|
  // should be obtained via the state_key method of an InstallerState instance
  // created before the machine state is modified by the installer.
  static void UpdateInstallerStage(bool system_install,
                                   const std::wstring& state_key_path,
                                   installer::InstallerStage stage);

  // Returns true if this installation path is per user, otherwise returns
  // false (per machine install, meaning: the exe_path contains path to
  // Program Files).
  static bool IsPerUserInstall(const wchar_t* const exe_path);

  // Returns true if the installation represented by the pair of |dist| and
  // |system_level| is a multi install.
  static bool IsMultiInstall(BrowserDistribution* dist, bool system_install);

  // Returns true if this is running setup process for Chrome SxS (as
  // indicated by the presence of --chrome-sxs on the command line) or if this
  // is running Chrome process from the Chrome SxS installation (as indicated
  // by either --chrome-sxs or the executable path).
  static bool IsChromeSxSProcess();

  // Adds all DLLs in install_path whose names are given by dll_names to a
  // work item list containing registration or unregistration actions.
  //
  // install_path: Install path containing the registrable DLLs.
  // dll_names: the array of strings containing dll_names
  // dll_names_count: the number of DLL names in dll_names
  // do_register: whether to register or unregister the DLLs
  // user_level_registration: whether to use alternate DLL entry point names to
  //     perform non-admin registration.
  // registration_list: the WorkItemList that this method populates
  //
  // Returns true if at least one DLL was successfully added to
  // registration_list.
  static bool BuildDLLRegistrationList(const std::wstring& install_path,
                                       const wchar_t** const dll_names,
                                       int dll_names_count,
                                       bool do_register,
                                       bool user_level_registration,
                                       WorkItemList* registration_list);

  // Deletes the registry key at path key_path under the key given by root_key.
  static bool DeleteRegistryKey(HKEY root_key, const std::wstring& key_path);

  // Deletes the registry value named value_name at path key_path under the key
  // given by reg_root.
  static bool DeleteRegistryValue(HKEY reg_root, const std::wstring& key_path,
                                  const std::wstring& value_name);

  // An interface to a predicate function for use by DeleteRegistryKeyIf and
  // DeleteRegistryValueIf.
  class RegistryValuePredicate {
   public:
    virtual ~RegistryValuePredicate() { }
    virtual bool Evaluate(const std::wstring& value) const = 0;
  };

  // The result of a conditional delete operation (i.e., DeleteFOOIf).
  enum ConditionalDeleteResult {
    NOT_FOUND,      // The condition was not satisfied.
    DELETED,        // The condition was satisfied and the delete succeeded.
    DELETE_FAILED   // The condition was satisfied but the delete failed.
  };

  // Deletes the key |key_to_delete_path| under |root_key| iff the value
  // |value_name| in the key |key_to_test_path| under |root_key| satisfies
  // |predicate|.  |value_name| must be an empty string to test the key's
  // default value.
  static ConditionalDeleteResult DeleteRegistryKeyIf(
      HKEY root_key,
      const std::wstring& key_to_delete_path,
      const std::wstring& key_to_test_path,
      const wchar_t* value_name,
      const RegistryValuePredicate& predicate);

  // Deletes the value |value_name| in the key |key_path| under |root_key| iff
  // its current value satisfies |predicate|.  |value_name| must be an empty
  // string to test the key's default value.
  static ConditionalDeleteResult DeleteRegistryValueIf(
      HKEY root_key,
      const wchar_t* key_path,
      const wchar_t* value_name,
      const RegistryValuePredicate& predicate);

  // A predicate that performs a case-sensitive string comparison.
  class ValueEquals : public RegistryValuePredicate {
   public:
    explicit ValueEquals(const std::wstring& value_to_match)
        : value_to_match_(value_to_match) { }
    virtual bool Evaluate(const std::wstring& value) const OVERRIDE;
   protected:
    std::wstring value_to_match_;
   private:
    DISALLOW_COPY_AND_ASSIGN(ValueEquals);
  };

  // Returns zero on install success, or an InstallStatus value otherwise.
  static int GetInstallReturnCode(installer::InstallStatus install_status);

  // Composes |program| and |arguments| into |command_line|.
  static void MakeUninstallCommand(const std::wstring& program,
                                   const std::wstring& arguments,
                                   CommandLine* command_line);

  // Returns a string in the form YYYYMMDD of the current date.
  static std::wstring GetCurrentDate();

 private:
  DISALLOW_COPY_AND_ASSIGN(InstallUtil);
};


#endif  // CHROME_INSTALLER_UTIL_INSTALL_UTIL_H_
