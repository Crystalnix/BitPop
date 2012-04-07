// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This file defines the methods useful for uninstalling Chrome.

#include "chrome/installer/setup/uninstall.h"

#include "base/file_util.h"
#include "base/path_service.h"
#include "base/process_util.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "base/win/registry.h"
#include "base/win/scoped_handle.h"
#include "base/win/windows_version.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_paths_internal.h"
#include "chrome/common/chrome_result_codes.h"
#include "chrome/installer/setup/install.h"
#include "chrome/installer/setup/install_worker.h"
#include "chrome/installer/setup/setup_constants.h"
#include "chrome/installer/setup/setup_util.h"
#include "chrome/installer/util/auto_launch_util.h"
#include "chrome/installer/util/browser_distribution.h"
#include "chrome/installer/util/channel_info.h"
#include "chrome/installer/util/delete_after_reboot_helper.h"
#include "chrome/installer/util/google_update_constants.h"
#include "chrome/installer/util/google_update_settings.h"
#include "chrome/installer/util/helper.h"
#include "chrome/installer/util/install_util.h"
#include "chrome/installer/util/installation_state.h"
#include "chrome/installer/util/installer_state.h"
#include "chrome/installer/util/logging_installer.h"
#include "chrome/installer/util/self_cleaning_temp_dir.h"
#include "chrome/installer/util/shell_util.h"
#include "chrome/installer/util/util_constants.h"
#include "rlz/win/lib/rlz_lib.h"

// Build-time generated include file.
#include "registered_dlls.h"  // NOLINT

using base::win::RegKey;
using installer::InstallStatus;
using installer::MasterPreferences;

namespace {

// Makes appropriate changes to the Google Update "ap" value in the registry.
// Specifically, removes the flags associated with this product ("-chrome" or
// "-chromeframe[-readymode]") from the "ap" values for all other
// installed products and for the multi-installer package.
void ProcessGoogleUpdateItems(
    const installer::InstallationState& original_state,
    const installer::InstallerState& installer_state,
    const installer::Product& product) {
  DCHECK(installer_state.is_multi_install());
  const bool system_level = installer_state.system_install();
  BrowserDistribution* distribution = product.distribution();
  const HKEY reg_root = installer_state.root_key();
  const installer::ProductState* product_state =
      original_state.GetProductState(system_level, distribution->GetType());
  DCHECK(product_state != NULL);
  installer::ChannelInfo channel_info;

  // Remove product's flags from the channel value.
  channel_info.set_value(product_state->channel().value());
  const bool modified = product.SetChannelFlags(false, &channel_info);

  // Apply the new channel value to all other products and to the multi package.
  if (modified) {
    BrowserDistribution::Type other_dist_types[] = {
        (distribution->GetType() == BrowserDistribution::CHROME_BROWSER) ?
            BrowserDistribution::CHROME_FRAME :
            BrowserDistribution::CHROME_BROWSER,
        BrowserDistribution::CHROME_BINARIES
    };
    scoped_ptr<WorkItemList>
        update_list(WorkItem::CreateNoRollbackWorkItemList());

    for (int i = 0; i < arraysize(other_dist_types); ++i) {
      BrowserDistribution::Type other_dist_type = other_dist_types[i];
      product_state =
          original_state.GetProductState(system_level, other_dist_type);
      // Only modify other products if they're installed and multi.
      if (product_state != NULL &&
          product_state->is_multi_install() &&
          !product_state->channel().Equals(channel_info)) {
        BrowserDistribution* other_dist =
            BrowserDistribution::GetSpecificDistribution(other_dist_type);
        update_list->AddSetRegValueWorkItem(reg_root, other_dist->GetStateKey(),
            google_update::kRegApField, channel_info.value(), true);
      } else {
        LOG_IF(ERROR,
               product_state != NULL && product_state->is_multi_install())
            << "Channel value for "
            << BrowserDistribution::GetSpecificDistribution(
                   other_dist_type)->GetAppShortCutName()
            << " is somehow already set to the desired new value of "
            << channel_info.value();
      }
    }

    bool success = update_list->Do();
    LOG_IF(ERROR, !success) << "Failed updating channel values.";
  }
}

// Adds or removes the quick-enable-cf command to the binaries' version key in
// the registry as needed.
void ProcessQuickEnableWorkItems(
    const installer::InstallerState& installer_state,
    const installer::InstallationState& machine_state) {
  scoped_ptr<WorkItemList> work_item_list(
      WorkItem::CreateNoRollbackWorkItemList());

  AddQuickEnableWorkItems(installer_state, machine_state, NULL, NULL,
                          work_item_list.get());
  if (!work_item_list->Do())
    LOG(ERROR) << "Failed to update quick-enable-cf command.";
}

void ProcessIELowRightsPolicyWorkItems(
    const installer::InstallerState& installer_state) {
  scoped_ptr<WorkItemList> work_items(WorkItem::CreateNoRollbackWorkItemList());
  AddDeleteOldIELowRightsPolicyWorkItems(installer_state, work_items.get());
  work_items->Do();
  installer::RefreshElevationPolicy();
}

void ClearRlzProductState() {
  const rlz_lib::AccessPoint points[] = {rlz_lib::CHROME_OMNIBOX,
                                         rlz_lib::CHROME_HOME_PAGE,
                                         rlz_lib::NO_ACCESS_POINT};

  rlz_lib::ClearProductState(rlz_lib::CHROME, points);

  // If chrome has been reactivated, clear all events for this brand as well.
  std::wstring reactivation_brand;
  if (GoogleUpdateSettings::GetReactivationBrand(&reactivation_brand)) {
    rlz_lib::SupplementaryBranding branding(reactivation_brand.c_str());
    rlz_lib::ClearProductState(rlz_lib::CHROME, points);
  }
}

}  // namespace

namespace installer {

// This functions checks for any Chrome instances that are
// running and first asks them to close politely by sending a Windows message.
// If there is an error while sending message or if there are still Chrome
// procesess active after the message has been sent, this function will try
// to kill them.
void CloseAllChromeProcesses() {
  for (int j = 0; j < 4; ++j) {
    std::wstring wnd_class(L"Chrome_WidgetWin_");
    wnd_class.append(base::IntToString16(j));
    HWND window = FindWindowEx(NULL, NULL, wnd_class.c_str(), NULL);
    while (window) {
      HWND tmpWnd = window;
      window = FindWindowEx(NULL, window, wnd_class.c_str(), NULL);
      if (!SendMessageTimeout(tmpWnd, WM_CLOSE, 0, 0, SMTO_BLOCK, 3000, NULL) &&
          (GetLastError() == ERROR_TIMEOUT)) {
        base::CleanupProcesses(installer::kChromeExe, 0,
                               content::RESULT_CODE_HUNG, NULL);
        base::CleanupProcesses(installer::kNaClExe, 0,
                               content::RESULT_CODE_HUNG, NULL);
        return;
      }
    }
  }

  // If asking politely didn't work, wait for 15 seconds and then kill all
  // chrome.exe. This check is just in case Chrome is ignoring WM_CLOSE
  // messages.
  base::CleanupProcesses(installer::kChromeExe, 15000,
                         content::RESULT_CODE_HUNG, NULL);
  base::CleanupProcesses(installer::kNaClExe, 15000,
                         content::RESULT_CODE_HUNG, NULL);
}

// Attempts to close the Chrome Frame helper process by sending WM_CLOSE
// messages to its window, or just killing it if that doesn't work.
void CloseChromeFrameHelperProcess() {
  HWND window = FindWindow(installer::kChromeFrameHelperWndClass, NULL);
  if (!::IsWindow(window))
    return;

  const DWORD kWaitMs = 3000;

  DWORD pid = 0;
  ::GetWindowThreadProcessId(window, &pid);
  DCHECK_NE(pid, 0U);
  base::win::ScopedHandle process(::OpenProcess(SYNCHRONIZE, FALSE, pid));
  PLOG_IF(INFO, !process) << "Failed to open process: " << pid;

  bool kill = true;
  if (SendMessageTimeout(window, WM_CLOSE, 0, 0, SMTO_BLOCK, kWaitMs, NULL) &&
      process) {
    VLOG(1) << "Waiting for " << installer::kChromeFrameHelperExe;
    DWORD wait = ::WaitForSingleObject(process, kWaitMs);
    if (wait != WAIT_OBJECT_0) {
      LOG(WARNING) << "Wait for " << installer::kChromeFrameHelperExe
                   << " to exit failed or timed out.";
    } else {
      kill = false;
      VLOG(1) << installer::kChromeFrameHelperExe << " exited normally.";
    }
  }

  if (kill) {
    VLOG(1) << installer::kChromeFrameHelperExe << " hung.  Killing.";
    base::CleanupProcesses(installer::kChromeFrameHelperExe, 0,
                           content::RESULT_CODE_HUNG, NULL);
  }
}

// This method tries to figure out if current user has registered Chrome.
// It returns true iff there is a registered browser that will launch the
// same chrome.exe as the current installation.
bool CurrentUserHasDefaultBrowser(const InstallerState& installer_state) {
  using base::win::RegistryKeyIterator;
  const HKEY root = HKEY_LOCAL_MACHINE;
  ProgramCompare open_command_pred(
      installer_state.target_path().Append(kChromeExe));
  std::wstring client_open_path;
  RegKey client_open_key;
  std::wstring reg_exe;
  for (RegistryKeyIterator iter(root, ShellUtil::kRegStartMenuInternet);
       iter.Valid(); ++iter) {
    client_open_path.assign(ShellUtil::kRegStartMenuInternet)
        .append(1, L'\\')
        .append(iter.Name())
        .append(ShellUtil::kRegShellOpen);
    if (client_open_key.Open(root, client_open_path.c_str(),
                             KEY_QUERY_VALUE) == ERROR_SUCCESS &&
        client_open_key.ReadValue(L"", &reg_exe) == ERROR_SUCCESS &&
        open_command_pred.Evaluate(reg_exe)) {
      return true;
    }
  }
  return false;
}

// This method deletes Chrome shortcut folder from Windows Start menu. It
// checks system_uninstall to see if the shortcut is in all users start menu
// or current user start menu.
// We try to remove the standard desktop shortcut but if that fails we try
// to remove the alternate desktop shortcut. Only one of them should be
// present in a given install but at this point we don't know which one.
void DeleteChromeShortcuts(const InstallerState& installer_state,
                           const Product& product) {
  if (!product.is_chrome()) {
    VLOG(1) << __FUNCTION__ " called for non-CHROME distribution";
    return;
  }

  FilePath shortcut_path;
  if (installer_state.system_install()) {
    PathService::Get(base::DIR_COMMON_START_MENU, &shortcut_path);
    if (!ShellUtil::RemoveChromeDesktopShortcut(product.distribution(),
        ShellUtil::CURRENT_USER | ShellUtil::SYSTEM_LEVEL, false)) {
      ShellUtil::RemoveChromeDesktopShortcut(product.distribution(),
          ShellUtil::CURRENT_USER | ShellUtil::SYSTEM_LEVEL, true);
    }

    ShellUtil::RemoveChromeQuickLaunchShortcut(product.distribution(),
        ShellUtil::CURRENT_USER | ShellUtil::SYSTEM_LEVEL);
  } else {
    PathService::Get(base::DIR_START_MENU, &shortcut_path);
    if (!ShellUtil::RemoveChromeDesktopShortcut(product.distribution(),
        ShellUtil::CURRENT_USER, false)) {
      ShellUtil::RemoveChromeDesktopShortcut(product.distribution(),
          ShellUtil::CURRENT_USER, true);
    }

    ShellUtil::RemoveChromeQuickLaunchShortcut(product.distribution(),
        ShellUtil::CURRENT_USER);
  }
  if (shortcut_path.empty()) {
    LOG(ERROR) << "Failed to get location for shortcut.";
  } else {
    shortcut_path = shortcut_path.Append(
        product.distribution()->GetAppShortCutName());
    VLOG(1) << "Deleting shortcut " << shortcut_path.value();
    if (!file_util::Delete(shortcut_path, true))
      LOG(ERROR) << "Failed to delete folder: " << shortcut_path.value();
  }
}

bool ScheduleParentAndGrandparentForDeletion(const FilePath& path) {
  FilePath parent_dir = path.DirName();
  bool ret = ScheduleFileSystemEntityForDeletion(parent_dir.value().c_str());
  if (!ret) {
    LOG(ERROR) << "Failed to schedule parent dir for deletion: "
               << parent_dir.value();
  } else {
    FilePath grandparent_dir(parent_dir.DirName());
    ret = ScheduleFileSystemEntityForDeletion(grandparent_dir.value().c_str());
    if (!ret) {
      LOG(ERROR) << "Failed to schedule grandparent dir for deletion: "
                 << grandparent_dir.value();
    }
  }
  return ret;
}

// Deletes empty parent & empty grandparent dir of given path.
bool DeleteEmptyParentDir(const FilePath& path) {
  bool ret = true;
  FilePath parent_dir = path.DirName();
  if (!parent_dir.empty() && file_util::IsDirectoryEmpty(parent_dir)) {
    if (!file_util::Delete(parent_dir, true)) {
      ret = false;
      LOG(ERROR) << "Failed to delete folder: " << parent_dir.value();
    }

    parent_dir = parent_dir.DirName();
    if (!parent_dir.empty() && file_util::IsDirectoryEmpty(parent_dir)) {
      if (!file_util::Delete(parent_dir, true)) {
        ret = false;
        LOG(ERROR) << "Failed to delete folder: " << parent_dir.value();
      }
    }
  }
  return ret;
}

FilePath GetLocalStateFolder(const Product& product) {
  // Obtain the location of the user profile data.
  FilePath local_state_folder = product.GetUserDataPath();
  LOG_IF(ERROR, local_state_folder.empty())
      << "Could not retrieve user's profile directory.";

  return local_state_folder;
}

// Creates a copy of the local state file and returns a path to the copy.
FilePath BackupLocalStateFile(const FilePath& local_state_folder) {
  FilePath backup;
  FilePath state_file(local_state_folder.Append(chrome::kLocalStateFilename));
  if (!file_util::CreateTemporaryFile(&backup)) {
    LOG(ERROR) << "Failed to create temporary file for Local State.";
  } else {
    file_util::CopyFile(state_file, backup);
  }
  return backup;
}

enum DeleteResult {
  DELETE_SUCCEEDED,
  DELETE_FAILED,
  DELETE_REQUIRES_REBOOT,
};

// Copies the local state to the temp folder and then deletes it.
// The path to the copy is returned via the local_state_copy parameter.
DeleteResult DeleteLocalState(const Product& product) {
  FilePath user_local_state(GetLocalStateFolder(product));
  if (user_local_state.empty())
    return DELETE_SUCCEEDED;

  DeleteResult result = DELETE_SUCCEEDED;
  VLOG(1) << "Deleting user profile " << user_local_state.value();
  if (!file_util::Delete(user_local_state, true)) {
    LOG(ERROR) << "Failed to delete user profile dir: "
               << user_local_state.value();
    if (product.is_chrome_frame()) {
      ScheduleDirectoryForDeletion(user_local_state.value().c_str());
      result = DELETE_REQUIRES_REBOOT;
    } else {
      result = DELETE_FAILED;
    }
  }

  if (result == DELETE_REQUIRES_REBOOT) {
    ScheduleParentAndGrandparentForDeletion(user_local_state);
  } else {
    DeleteEmptyParentDir(user_local_state);
  }

  return result;
}

bool MoveSetupOutOfInstallFolder(const InstallerState& installer_state,
                                 const FilePath& setup_path,
                                 const Version& installed_version) {
  bool ret = false;
  FilePath setup_exe(installer_state.GetInstallerDirectory(installed_version)
      .Append(setup_path.BaseName()));
  FilePath temp_file;
  if (!file_util::CreateTemporaryFile(&temp_file)) {
    LOG(ERROR) << "Failed to create temporary file for setup.exe.";
  } else {
    VLOG(1) << "Attempting to move setup to: " << temp_file.value();
    ret = file_util::Move(setup_exe, temp_file);
    PLOG_IF(ERROR, !ret) << "Failed to move setup to " << temp_file.value();

    // We cannot delete the file right away, but try to delete it some other
    // way. Either with the help of a different process or the system.
    if (ret && !file_util::DeleteAfterReboot(temp_file)) {
      static const uint32 kDeleteAfterMs = 10 * 1000;
      installer::DeleteFileFromTempProcess(temp_file, kDeleteAfterMs);
    }
  }
  return ret;
}

DeleteResult DeleteFilesAndFolders(const InstallerState& installer_state,
                                   const Version& installed_version) {
  const FilePath& target_path = installer_state.target_path();
  if (target_path.empty()) {
    LOG(ERROR) << "DeleteFilesAndFolders: no installation destination path.";
    return DELETE_FAILED;  // Nothing else we can do to uninstall, so we return.
  }

  DeleteResult result = DELETE_SUCCEEDED;

  // Avoid leaving behind a Temp dir.  If one exists, ask SelfCleaningTempDir to
  // clean it up for us.  This may involve scheduling it for deletion after
  // reboot.  Don't report that a reboot is required in this case, however.
  FilePath temp_path(target_path.DirName().Append(kInstallTempDir));
  if (file_util::DirectoryExists(temp_path)) {
    installer::SelfCleaningTempDir temp_dir;
    if (!temp_dir.Initialize(target_path.DirName(), kInstallTempDir) ||
        !temp_dir.Delete()) {
      LOG(ERROR) << "Failed to delete temp dir " << temp_path.value();
    }
  }

  VLOG(1) << "Deleting install path " << target_path.value();
  if (!file_util::Delete(target_path, true)) {
    LOG(ERROR) << "Failed to delete folder (1st try): " << target_path.value();
    if (installer_state.FindProduct(BrowserDistribution::CHROME_FRAME)) {
      // We don't try killing Chrome processes for Chrome Frame builds since
      // that is unlikely to help. Instead, schedule files for deletion and
      // return a value that will trigger a reboot prompt.
      ScheduleDirectoryForDeletion(target_path.value().c_str());
      result = DELETE_REQUIRES_REBOOT;
    } else {
      // Try closing any running chrome processes and deleting files once again.
      CloseAllChromeProcesses();
      if (!file_util::Delete(target_path, true)) {
        LOG(ERROR) << "Failed to delete folder (2nd try): "
                   << target_path.value();
        result = DELETE_FAILED;
      }
    }
  }

  if (result == DELETE_REQUIRES_REBOOT) {
    // If we need a reboot to continue, schedule the parent directories for
    // deletion unconditionally. If they are not empty, the session manager
    // will not delete them on reboot.
    ScheduleParentAndGrandparentForDeletion(target_path);
  } else {
    // Now check and delete if the parent directories are empty
    // For example Google\Chrome or Chromium
    DeleteEmptyParentDir(target_path);
  }
  return result;
}

// This method checks if Chrome is currently running or if the user has
// cancelled the uninstall operation by clicking Cancel on the confirmation
// box that Chrome pops up.
InstallStatus IsChromeActiveOrUserCancelled(
    const InstallerState& installer_state,
    const Product& product) {
  int32 exit_code = content::RESULT_CODE_NORMAL_EXIT;
  CommandLine options(CommandLine::NO_PROGRAM);
  options.AppendSwitch(installer::switches::kUninstall);

  // Here we want to save user from frustration (in case of Chrome crashes)
  // and continue with the uninstallation as long as chrome.exe process exit
  // code is NOT one of the following:
  // - UNINSTALL_CHROME_ALIVE - chrome.exe is currently running
  // - UNINSTALL_USER_CANCEL - User cancelled uninstallation
  // - HUNG - chrome.exe was killed by HuntForZombieProcesses() (until we can
  //          give this method some brains and not kill chrome.exe launched
  //          by us, we will not uninstall if we get this return code).
  VLOG(1) << "Launching Chrome to do uninstall tasks.";
  if (product.LaunchChromeAndWait(installer_state.target_path(), options,
                                  &exit_code)) {
    VLOG(1) << "chrome.exe launched for uninstall confirmation returned: "
            << exit_code;
    if ((exit_code == chrome::RESULT_CODE_UNINSTALL_CHROME_ALIVE) ||
        (exit_code == chrome::RESULT_CODE_UNINSTALL_USER_CANCEL) ||
        (exit_code == content::RESULT_CODE_HUNG))
      return installer::UNINSTALL_CANCELLED;

    if (exit_code == chrome::RESULT_CODE_UNINSTALL_DELETE_PROFILE)
      return installer::UNINSTALL_DELETE_PROFILE;
  } else {
    PLOG(ERROR) << "Failed to launch chrome.exe for uninstall confirmation.";
  }

  return installer::UNINSTALL_CONFIRMED;
}

bool ShouldDeleteProfile(const InstallerState& installer_state,
                         const CommandLine& cmd_line, InstallStatus status,
                         const Product& product) {
  bool should_delete = false;

  // Chrome Frame uninstallations always want to delete the profile (we have no
  // UI to prompt otherwise and the profile stores no useful data anyway)
  // unless they are managed by MSI. MSI uninstalls will explicitly include
  // the --delete-profile flag to distinguish them from MSI upgrades.
  if (!product.is_chrome() && !installer_state.is_msi()) {
    should_delete = true;
  } else {
    should_delete =
        status == installer::UNINSTALL_DELETE_PROFILE ||
        cmd_line.HasSwitch(installer::switches::kDeleteProfile);
  }

  return should_delete;
}

bool DeleteChromeRegistrationKeys(BrowserDistribution* dist, HKEY root,
                                  const std::wstring& browser_entry_suffix,
                                  const FilePath& target_path,
                                  InstallStatus* exit_code) {
  DCHECK(exit_code);
  if (!dist->CanSetAsDefault()) {
    // We should have never set those keys.
    return true;
  }

  FilePath chrome_exe(target_path.Append(kChromeExe));

  // Delete Software\Classes\ChromeHTML,
  std::wstring html_prog_id(ShellUtil::kRegClasses);
  file_util::AppendToPath(&html_prog_id, ShellUtil::kChromeHTMLProgId);
  html_prog_id.append(browser_entry_suffix);
  InstallUtil::DeleteRegistryKey(root, html_prog_id);

  // Delete all Start Menu Internet registrations that refer to this Chrome.
  {
    using base::win::RegistryKeyIterator;
    ProgramCompare open_command_pred(chrome_exe);
    std::wstring client_name;
    std::wstring client_key;
    std::wstring open_key;
    for (RegistryKeyIterator iter(root, ShellUtil::kRegStartMenuInternet);
         iter.Valid(); ++iter) {
      client_name.assign(iter.Name());
      client_key.assign(ShellUtil::kRegStartMenuInternet)
          .append(1, L'\\')
          .append(client_name);
      open_key.assign(client_key).append(ShellUtil::kRegShellOpen);
      if (InstallUtil::DeleteRegistryKeyIf(root, client_key, open_key, L"",
              open_command_pred) != InstallUtil::NOT_FOUND) {
        // Delete the default value of SOFTWARE\Clients\StartMenuInternet if it
        // references this Chrome (i.e., if it was made the default browser).
        InstallUtil::DeleteRegistryValueIf(
            root, ShellUtil::kRegStartMenuInternet, L"",
            InstallUtil::ValueEquals(client_name));
        // Also delete the value for the default user if we're operating in
        // HKLM.
        if (root == HKEY_LOCAL_MACHINE) {
          InstallUtil::DeleteRegistryValueIf(
              HKEY_USERS,
              std::wstring(L".DEFAULT\\").append(
                  ShellUtil::kRegStartMenuInternet).c_str(),
              L"", InstallUtil::ValueEquals(client_name));
        }
      }
    }
  }

  // Delete Software\RegisteredApplications\Chromium
  InstallUtil::DeleteRegistryValue(root, ShellUtil::kRegRegisteredApplications,
      dist->GetApplicationName() + browser_entry_suffix);

  // Delete Software\Classes\Applications\chrome.exe
  std::wstring app_key(ShellUtil::kRegClasses);
  file_util::AppendToPath(&app_key, L"Applications");
  file_util::AppendToPath(&app_key, installer::kChromeExe);
  InstallUtil::DeleteRegistryKey(root, app_key);

  // Delete the App Paths key that lets explorer find Chrome.
  std::wstring app_path_key(ShellUtil::kAppPathsRegistryKey);
  file_util::AppendToPath(&app_path_key, installer::kChromeExe);
  InstallUtil::DeleteRegistryKey(root, app_path_key);

  // Cleanup OpenWithList
  std::wstring open_with_key;
  for (int i = 0; ShellUtil::kFileAssociations[i] != NULL; i++) {
    open_with_key.assign(ShellUtil::kRegClasses);
    file_util::AppendToPath(&open_with_key, ShellUtil::kFileAssociations[i]);
    file_util::AppendToPath(&open_with_key, L"OpenWithList");
    file_util::AppendToPath(&open_with_key, installer::kChromeExe);
    InstallUtil::DeleteRegistryKey(root, open_with_key);
  }

  // Cleanup in case Chrome had been made the default browser.

  // Delete the default value of SOFTWARE\Clients\StartMenuInternet if it
  // references this Chrome.  Do this explicitly here for the case where HKCU is
  // being processed; the iteration above will have no hits since registration
  // lives in HKLM.
  InstallUtil::DeleteRegistryValueIf(
      root, ShellUtil::kRegStartMenuInternet, L"",
      InstallUtil::ValueEquals(dist->GetApplicationName() +
                               browser_entry_suffix));

  // Delete each protocol association if it references this Chrome.
  ProgramCompare open_command_pred(chrome_exe);
  std::wstring parent_key(ShellUtil::kRegClasses);
  const std::wstring::size_type base_length = parent_key.size();
  std::wstring child_key;
  for (const wchar_t* const* proto =
           &ShellUtil::kPotentialProtocolAssociations[0];
       *proto != NULL;
       ++proto) {
    parent_key.resize(base_length);
    file_util::AppendToPath(&parent_key, *proto);
    child_key.assign(parent_key).append(ShellUtil::kRegShellOpen);
    InstallUtil::DeleteRegistryKeyIf(root, parent_key, child_key, L"",
                                     open_command_pred);
  }

  // Note that we do not attempt to delete filetype associations since MSDN
  // says "Windows respects the Default value only if the ProgID found there is
  // a registered ProgID. If the ProgID is unregistered, it is ignored."

  *exit_code = installer::UNINSTALL_SUCCESSFUL;
  return true;
}

void RemoveChromeLegacyRegistryKeys(BrowserDistribution* dist) {
  // We used to register Chrome to handle crx files, but this turned out
  // to be not worth the hassle. Remove these old registry entries if
  // they exist. See: http://codereview.chromium.org/210007

#if defined(GOOGLE_CHROME_BUILD)
const wchar_t kChromeExtProgId[] = L"ChromeExt";
#else
const wchar_t kChromeExtProgId[] = L"ChromiumExt";
#endif

  HKEY roots[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };
  for (size_t i = 0; i < arraysize(roots); ++i) {
    std::wstring suffix;
    if (roots[i] == HKEY_LOCAL_MACHINE &&
        !ShellUtil::GetUserSpecificDefaultBrowserSuffix(dist, &suffix))
      suffix = L"";

    // Delete Software\Classes\ChromeExt,
    std::wstring ext_prog_id(ShellUtil::kRegClasses);
    file_util::AppendToPath(&ext_prog_id, kChromeExtProgId);
    ext_prog_id.append(suffix);
    InstallUtil::DeleteRegistryKey(roots[i], ext_prog_id);

    // Delete Software\Classes\.crx,
    std::wstring ext_association(ShellUtil::kRegClasses);
    ext_association.append(L"\\");
    ext_association.append(chrome::kExtensionFileExtension);
    InstallUtil::DeleteRegistryKey(roots[i], ext_association);
  }
}

bool ProcessChromeFrameWorkItems(const InstallationState& original_state,
                                 const InstallerState& installer_state,
                                 const FilePath& setup_path,
                                 const Product& product) {
  if (!product.is_chrome_frame())
    return false;

  scoped_ptr<WorkItemList> item_list(WorkItem::CreateWorkItemList());
  AddChromeFrameWorkItems(original_state, installer_state, setup_path,
                          Version(), product, item_list.get());
  return item_list->Do();
}

InstallStatus UninstallProduct(const InstallationState& original_state,
                               const InstallerState& installer_state,
                               const FilePath& setup_path,
                               const Product& product,
                               bool remove_all,
                               bool force_uninstall,
                               const CommandLine& cmd_line) {
  InstallStatus status = installer::UNINSTALL_CONFIRMED;
  std::wstring suffix;
  if (!ShellUtil::GetUserSpecificDefaultBrowserSuffix(product.distribution(),
                                                      &suffix))
    suffix = L"";

  BrowserDistribution* browser_dist = product.distribution();
  bool is_chrome = product.is_chrome();

  VLOG(1) << "UninstallProduct: " << browser_dist->GetApplicationName();

  if (force_uninstall) {
    // Since --force-uninstall command line option is used, we are going to
    // do silent uninstall. Try to close all running Chrome instances.
    // NOTE: We don't do this for Chrome Frame.
    if (is_chrome)
      CloseAllChromeProcesses();
  } else if (is_chrome) {
    // no --force-uninstall so lets show some UI dialog boxes.
    status = IsChromeActiveOrUserCancelled(installer_state, product);
    if (status != installer::UNINSTALL_CONFIRMED &&
        status != installer::UNINSTALL_DELETE_PROFILE)
      return status;

    // Check if we need admin rights to cleanup HKLM. If we do, try to launch
    // another uninstaller (silent) in elevated mode to do HKLM cleanup.
    // And continue uninstalling in the current process also to do HKCU cleanup.
    if (remove_all &&
        (!suffix.empty() || CurrentUserHasDefaultBrowser(installer_state)) &&
        !::IsUserAnAdmin() &&
        base::win::GetVersion() >= base::win::VERSION_VISTA &&
        !cmd_line.HasSwitch(installer::switches::kRunAsAdmin)) {
      CommandLine new_cmd(CommandLine::NO_PROGRAM);
      new_cmd.AppendArguments(cmd_line, true);
      // Append --run-as-admin flag to let the new instance of setup.exe know
      // that we already tried to launch ourselves as admin.
      new_cmd.AppendSwitch(installer::switches::kRunAsAdmin);
      // Append --remove-chrome-registration to remove registry keys only.
      new_cmd.AppendSwitch(installer::switches::kRemoveChromeRegistration);
      if (!suffix.empty()) {
        new_cmd.AppendSwitchNative(
            installer::switches::kRegisterChromeBrowserSuffix, suffix);
      }
      DWORD exit_code = installer::UNKNOWN_STATUS;
      InstallUtil::ExecuteExeAsAdmin(new_cmd, &exit_code);
    }
  }

  // Chrome is not in use so lets uninstall Chrome by deleting various files
  // and registry entries. Here we will just make best effort and keep going
  // in case of errors.
  if (is_chrome) {
    ClearRlzProductState();

    if (auto_launch_util::WillLaunchAtLogin(installer_state.target_path()))
      auto_launch_util::SetWillLaunchAtLogin(false, FilePath());
  }

  // First delete shortcuts from Start->Programs, Desktop & Quick Launch.
  DeleteChromeShortcuts(installer_state, product);

  // Delete the registry keys (Uninstall key and Version key).
  HKEY reg_root = installer_state.root_key();

  // Note that we must retrieve the distribution-specific data before deleting
  // product.GetVersionKey().
  std::wstring distribution_data(browser_dist->GetDistributionData(reg_root));

  // Remove Control Panel uninstall link and Omaha product key.
  InstallUtil::DeleteRegistryKey(reg_root, browser_dist->GetUninstallRegPath());
  InstallUtil::DeleteRegistryKey(reg_root, browser_dist->GetVersionKey());

  // Also try to delete the MSI value in the ClientState key (it might not be
  // there). This is due to a Google Update behaviour where an uninstall and a
  // rapid reinstall might result in stale values from the old ClientState key
  // being picked up on reinstall.
  product.SetMsiMarker(installer_state.system_install(), false);

  // Remove all Chrome registration keys.
  // Registration data is put in HKCU for both system level and user level
  // installs.
  InstallStatus ret = installer::UNKNOWN_STATUS;
  DeleteChromeRegistrationKeys(product.distribution(), HKEY_CURRENT_USER,
                               suffix, installer_state.target_path(), &ret);

  // Registration data is put in HKLM for system level and possibly user level
  // installs (when Chrome is made the default browser at install-time).
  if (installer_state.system_install() || remove_all &&
      (!suffix.empty() || CurrentUserHasDefaultBrowser(installer_state))) {
    DeleteChromeRegistrationKeys(product.distribution(), HKEY_LOCAL_MACHINE,
                                 suffix, installer_state.target_path(), &ret);
  }

  if (!is_chrome) {
    ProcessChromeFrameWorkItems(original_state, installer_state, setup_path,
                                product);
  }

  if (installer_state.is_multi_install())
    ProcessGoogleUpdateItems(original_state, installer_state, product);

  ProcessQuickEnableWorkItems(installer_state, original_state);

  // Get the state of the installed product (if any)
  const ProductState* product_state =
      original_state.GetProductState(installer_state.system_install(),
                                     browser_dist->GetType());

  // Delete shared registry keys as well (these require admin rights) if
  // remove_all option is specified.
  if (remove_all) {
    if (!InstallUtil::IsChromeSxSProcess() && is_chrome) {
      // Delete media player registry key that exists only in HKLM.
      // We don't delete this key in SxS uninstall or Chrome Frame uninstall
      // as we never set the key for those products.
      std::wstring reg_path(installer::kMediaPlayerRegPath);
      file_util::AppendToPath(&reg_path, installer::kChromeExe);
      InstallUtil::DeleteRegistryKey(HKEY_LOCAL_MACHINE, reg_path);
    }

    // Unregister any dll servers that we may have registered for this
    // product.
    if (product_state != NULL) {
      std::vector<FilePath> com_dll_list;
      product.AddComDllList(&com_dll_list);
      FilePath dll_folder = installer_state.target_path().AppendASCII(
          product_state->version().GetString());

      scoped_ptr<WorkItemList> unreg_work_item_list(
          WorkItem::CreateWorkItemList());

      AddRegisterComDllWorkItems(dll_folder,
                                 com_dll_list,
                                 installer_state.system_install(),
                                 false,  // Unregister
                                 true,   // May fail
                                 unreg_work_item_list.get());
      unreg_work_item_list->Do();
    }

    if (!is_chrome)
      ProcessIELowRightsPolicyWorkItems(installer_state);
  }

  // Close any Chrome Frame helper processes that may be running.
  if (product.is_chrome_frame()) {
    VLOG(1) << "Closing the Chrome Frame helper process";
    CloseChromeFrameHelperProcess();
  }

  if (product_state == NULL)
    return installer::UNINSTALL_SUCCESSFUL;

  // Finally delete all the files from Chrome folder after moving setup.exe
  // and the user's Local State to a temp location.
  bool delete_profile = ShouldDeleteProfile(installer_state, cmd_line, status,
                                            product);
  ret = installer::UNINSTALL_SUCCESSFUL;

  // When deleting files, we must make sure that we're either a "single"
  // (aka non-multi) installation or, in the case of multi, that no other
  // "multi" products share the binaries we are about to delete.

  bool can_delete_files = true;
  if (installer_state.is_multi_install()) {
    ProductState prod_state;
    for (size_t i = 0; i < BrowserDistribution::kNumProductTypes; ++i) {
      if (prod_state.Initialize(installer_state.system_install(),
                                BrowserDistribution::kProductTypes[i]) &&
          prod_state.is_multi_install()) {
        can_delete_files = false;
        break;
      }
    }
    LOG(INFO) << (can_delete_files ? "Shared binaries will be deleted." :
                                     "Shared binaries still in use.");
    if (can_delete_files) {
      BrowserDistribution* multi_dist =
          installer_state.multi_package_binaries_distribution();
      InstallUtil::DeleteRegistryKey(reg_root, multi_dist->GetVersionKey());
    }
  }

  FilePath backup_state_file(BackupLocalStateFile(
      GetLocalStateFolder(product)));

  DeleteResult delete_result = DELETE_SUCCEEDED;
  if (can_delete_files) {
    // In order to be able to remove the folder in which we're running, we
    // need to move setup.exe out of the install folder.
    // TODO(tommi): What if the temp folder is on a different volume?
    MoveSetupOutOfInstallFolder(installer_state, setup_path,
                                product_state->version());
    delete_result = DeleteFilesAndFolders(installer_state,
                                          product_state->version());
  }

  if (delete_profile)
    DeleteLocalState(product);

  if (delete_result == DELETE_FAILED) {
    ret = installer::UNINSTALL_FAILED;
  } else if (delete_result == DELETE_REQUIRES_REBOOT) {
    ret = installer::UNINSTALL_REQUIRES_REBOOT;
  }

  if (!force_uninstall) {
    VLOG(1) << "Uninstallation complete. Launching Uninstall survey.";
    browser_dist->DoPostUninstallOperations(product_state->version(),
        backup_state_file, distribution_data);
  }

  // Try and delete the preserved local state once the post-install
  // operations are complete.
  if (!backup_state_file.empty())
    file_util::Delete(backup_state_file, false);

  return ret;
}

}  // namespace installer
