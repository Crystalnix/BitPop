// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/web_applications/web_app.h"

#include <shlobj.h>

#include "base/command_line.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "base/md5.h"
#include "base/path_service.h"
#include "base/stringprintf.h"
#include "base/utf_string_conversions.h"
#include "base/win/windows_version.h"
#include "chrome/common/chrome_paths.h"
#include "content/public/browser/browser_thread.h"
#include "ui/gfx/icon_util.h"

namespace {

const FilePath::CharType kIconChecksumFileExt[] = FILE_PATH_LITERAL(".ico.md5");

// Calculates image checksum using MD5.
void GetImageCheckSum(const SkBitmap& image, base::MD5Digest* digest) {
  DCHECK(digest);

  SkAutoLockPixels image_lock(image);
  MD5Sum(image.getPixels(), image.getSize(), digest);
}

// Saves |image| as an |icon_file| with the checksum.
bool SaveIconWithCheckSum(const FilePath& icon_file, const SkBitmap& image) {
  if (!IconUtil::CreateIconFileFromSkBitmap(image, icon_file))
    return false;

  base::MD5Digest digest;
  GetImageCheckSum(image, &digest);

  FilePath cheksum_file(icon_file.ReplaceExtension(kIconChecksumFileExt));
  return file_util::WriteFile(cheksum_file,
                              reinterpret_cast<const char*>(&digest),
                              sizeof(digest)) == sizeof(digest);
}

// Returns true if |icon_file| is missing or different from |image|.
bool ShouldUpdateIcon(const FilePath& icon_file, const SkBitmap& image) {
  FilePath checksum_file(icon_file.ReplaceExtension(kIconChecksumFileExt));

  // Returns true if icon_file or checksum file is missing.
  if (!file_util::PathExists(icon_file) ||
      !file_util::PathExists(checksum_file))
    return true;

  base::MD5Digest persisted_image_checksum;
  if (sizeof(persisted_image_checksum) != file_util::ReadFile(checksum_file,
                      reinterpret_cast<char*>(&persisted_image_checksum),
                      sizeof(persisted_image_checksum)))
    return true;

  base::MD5Digest downloaded_image_checksum;
  GetImageCheckSum(image, &downloaded_image_checksum);

  // Update icon if checksums are not equal.
  return memcmp(&persisted_image_checksum, &downloaded_image_checksum,
                sizeof(base::MD5Digest)) != 0;
}

}  // namespace

namespace web_app {

namespace internals {

// Saves |image| to |icon_file| if the file is outdated and refresh shell's
// icon cache to ensure correct icon is displayed. Returns true if icon_file
// is up to date or successfully updated.
bool CheckAndSaveIcon(const FilePath& icon_file, const SkBitmap& image) {
  if (ShouldUpdateIcon(icon_file, image)) {
    if (SaveIconWithCheckSum(icon_file, image)) {
      // Refresh shell's icon cache. This call is quite disruptive as user would
      // see explorer rebuilding the icon cache. It would be great that we find
      // a better way to achieve this.
      SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST | SHCNF_FLUSHNOWAIT,
                     NULL, NULL);
    } else {
      return false;
    }
  }

  return true;
}

bool CreatePlatformShortcut(
    const FilePath& web_app_path,
    const FilePath& profile_path,
    const ShellIntegration::ShortcutInfo& shortcut_info) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::FILE));

  // Shortcut paths under which to create shortcuts.
  std::vector<FilePath> shortcut_paths;

  // Locations to add to shortcut_paths.
  struct {
    const bool& use_this_location;
    int location_id;
    const wchar_t* sub_dir;
  } locations[] = {
    {
      shortcut_info.create_on_desktop,
      chrome::DIR_USER_DESKTOP,
      NULL
    }, {
      shortcut_info.create_in_applications_menu,
      base::DIR_START_MENU,
      NULL
    }, {
      shortcut_info.create_in_quick_launch_bar,
      // For Win7, create_in_quick_launch_bar means pinning to taskbar. Use
      // base::PATH_START as a flag for this case.
      (base::win::GetVersion() >= base::win::VERSION_WIN7) ?
          base::PATH_START : base::DIR_APP_DATA,
      (base::win::GetVersion() >= base::win::VERSION_WIN7) ?
          NULL : L"Microsoft\\Internet Explorer\\Quick Launch"
    }
  };

  // Populate shortcut_paths.
  for (int i = 0; i < arraysize(locations); ++i) {
    if (locations[i].use_this_location) {
      FilePath path;

      // Skip the Win7 case.
      if (locations[i].location_id == base::PATH_START)
        continue;

      if (!PathService::Get(locations[i].location_id, &path)) {
        return false;
      }

      if (locations[i].sub_dir != NULL)
        path = path.Append(locations[i].sub_dir);

      shortcut_paths.push_back(path);
    }
  }

  bool pin_to_taskbar =
      shortcut_info.create_in_quick_launch_bar &&
      (base::win::GetVersion() >= base::win::VERSION_WIN7);

  // For Win7's pinning support, any shortcut could be used. So we only create
  // the shortcut file when there is no shortcut file will be created. That is,
  // user only selects "Pin to taskbar".
  if (pin_to_taskbar && shortcut_paths.empty()) {
    // Creates the shortcut in web_app_path in this case.
    shortcut_paths.push_back(web_app_path);
  }

  if (shortcut_paths.empty()) {
    return false;
  }

  // Ensure web_app_path exists
  if (!file_util::PathExists(web_app_path) &&
      !file_util::CreateDirectory(web_app_path)) {
    return false;
  }

  // Generates file name to use with persisted ico and shortcut file.
  FilePath file_name =
      web_app::internals::GetSanitizedFileName(shortcut_info.title);

  // Creates an ico file to use with shortcut.
  FilePath icon_file = web_app_path.Append(file_name).ReplaceExtension(
      FILE_PATH_LITERAL(".ico"));
  if (!web_app::internals::CheckAndSaveIcon(icon_file,
        *shortcut_info.favicon.ToSkBitmap())) {
    return false;
  }

  FilePath chrome_exe;
  if (!PathService::Get(base::FILE_EXE, &chrome_exe)) {
    return false;
  }

  // Working directory.
  FilePath chrome_folder = chrome_exe.DirName();

  CommandLine cmd_line(CommandLine::NO_PROGRAM);
  cmd_line = ShellIntegration::CommandLineArgsForLauncher(shortcut_info.url,
      shortcut_info.extension_id, shortcut_info.is_platform_app,
      shortcut_info.profile_path);

  // TODO(evan): we rely on the fact that command_line_string() is
  // properly quoted for a Windows command line.  The method on
  // CommandLine should probably be renamed to better reflect that
  // fact.
  string16 wide_switches(cmd_line.GetCommandLineString());

  // Sanitize description
  string16 description = shortcut_info.description;
  if (description.length() >= MAX_PATH)
    description.resize(MAX_PATH - 1);

  // Generates app id from web app url and profile path.
  std::string app_name =
      web_app::GenerateApplicationNameFromInfo(shortcut_info);
  string16 app_id = ShellIntegration::GetAppModelIdForProfile(
      UTF8ToUTF16(app_name), profile_path);

  FilePath shortcut_to_pin;

  bool success = true;
  for (size_t i = 0; i < shortcut_paths.size(); ++i) {
    FilePath shortcut_file = shortcut_paths[i].Append(file_name).
        ReplaceExtension(FILE_PATH_LITERAL(".lnk"));

    int unique_number =
        file_util::GetUniquePathNumber(shortcut_file, FILE_PATH_LITERAL(""));
    if (unique_number == -1) {
      success = false;
      continue;
    } else if (unique_number > 0) {
      shortcut_file = shortcut_file.InsertBeforeExtensionASCII(
          StringPrintf(" (%d)", unique_number));
    }

    success = file_util::CreateOrUpdateShortcutLink(
        chrome_exe.value().c_str(),
        shortcut_file.value().c_str(),
        chrome_folder.value().c_str(),
        wide_switches.c_str(),
        description.c_str(),
        icon_file.value().c_str(),
        0,
        app_id.c_str(),
        file_util::SHORTCUT_CREATE_ALWAYS) && success;

    // Any shortcut would work for the pinning. We use the first one.
    if (success && pin_to_taskbar && shortcut_to_pin.empty())
      shortcut_to_pin = shortcut_file;
  }

  if (success && pin_to_taskbar) {
    if (!shortcut_to_pin.empty()) {
      success &= file_util::TaskbarPinShortcutLink(
          shortcut_to_pin.value().c_str());
    } else {
      success = false;
    }
  }

  return success;
}

void DeletePlatformShortcuts(const FilePath& profile_path,
                             const std::string& extension_id) {
  // TODO(benwells): Implement this.
}

}  // namespace internals

}  // namespace web_app
