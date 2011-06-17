// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/file_path.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/task.h"
#include "base/utf_string_conversions.h"
#include "base/win/registry.h"
#include "chrome/browser/background_mode_manager.h"
#include "chrome/common/chrome_switches.h"
#include "content/browser/browser_thread.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"

namespace {

class DisableLaunchOnStartupTask : public Task {
 public:
  virtual void Run();
};

class EnableLaunchOnStartupTask : public Task {
 public:
  virtual void Run();
};

const HKEY kBackgroundModeRegistryRootKey = HKEY_CURRENT_USER;
const wchar_t* kBackgroundModeRegistrySubkey =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* kBackgroundModeRegistryKeyName = L"chromium";

void DisableLaunchOnStartupTask::Run() {
  const wchar_t* key_name = kBackgroundModeRegistryKeyName;
  base::win::RegKey read_key(kBackgroundModeRegistryRootKey,
                             kBackgroundModeRegistrySubkey, KEY_READ);
  base::win::RegKey write_key(kBackgroundModeRegistryRootKey,
                              kBackgroundModeRegistrySubkey, KEY_WRITE);
  if (read_key.ValueExists(key_name)) {
    LONG result = write_key.DeleteValue(key_name);
    DCHECK_EQ(ERROR_SUCCESS, result) <<
        "Failed to deregister launch on login. error: " << result;
  }
}

void EnableLaunchOnStartupTask::Run() {
  // TODO(rickcam): Bug 53597: Make RegKey mockable.
  // TODO(rickcam): Bug 53600: Use distinct registry keys per flavor+profile.
  const wchar_t* key_name = kBackgroundModeRegistryKeyName;
  base::win::RegKey read_key(kBackgroundModeRegistryRootKey,
                             kBackgroundModeRegistrySubkey, KEY_READ);
  base::win::RegKey write_key(kBackgroundModeRegistryRootKey,
                              kBackgroundModeRegistrySubkey, KEY_WRITE);
  FilePath executable;
  if (!PathService::Get(base::FILE_EXE, &executable))
    return;
  std::wstring new_value = executable.value() +
      L" --" + ASCIIToUTF16(switches::kNoStartupWindow);
  if (read_key.ValueExists(key_name)) {
    std::wstring current_value;
    if ((read_key.ReadValue(key_name, &current_value) == ERROR_SUCCESS) &&
        (current_value == new_value)) {
      return;
    }
  }
  LONG result = write_key.WriteValue(key_name, new_value.c_str());
  DCHECK_EQ(ERROR_SUCCESS, result) <<
      "Failed to register launch on login. error: " << result;
}

}  // namespace

void BackgroundModeManager::EnableLaunchOnStartup(bool should_launch) {
  // This functionality is only defined for default profile, currently.
  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kUserDataDir))
    return;
  if (should_launch) {
    BrowserThread::PostTask(BrowserThread::FILE, FROM_HERE,
                            new EnableLaunchOnStartupTask());
  } else {
    BrowserThread::PostTask(BrowserThread::FILE, FROM_HERE,
                            new DisableLaunchOnStartupTask());
  }
}

void BackgroundModeManager::DisplayAppInstalledNotification(
    const Extension* extension) {
  // Create a status tray notification balloon explaining to the user that
  // a background app has been installed.
  CreateStatusTrayIcon();
  status_icon_->DisplayBalloon(
      l10n_util::GetStringUTF16(IDS_BACKGROUND_APP_INSTALLED_BALLOON_TITLE),
      l10n_util::GetStringFUTF16(
          IDS_BACKGROUND_APP_INSTALLED_BALLOON_BODY,
          UTF8ToUTF16(extension->name()),
          l10n_util::GetStringUTF16(IDS_PRODUCT_NAME)));
}

string16 BackgroundModeManager::GetPreferencesMenuLabel() {
  return l10n_util::GetStringUTF16(IDS_OPTIONS);
}
