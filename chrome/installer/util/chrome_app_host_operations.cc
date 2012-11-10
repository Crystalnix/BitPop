// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/installer/util/chrome_app_host_operations.h"

#include "base/command_line.h"
#include "base/file_path.h"
#include "base/logging.h"
#include "chrome/installer/util/browser_distribution.h"
#include "chrome/installer/util/channel_info.h"
#include "chrome/installer/util/helper.h"
#include "chrome/installer/util/master_preferences.h"
#include "chrome/installer/util/master_preferences_constants.h"
#include "chrome/installer/util/util_constants.h"

namespace installer {

void ChromeAppHostOperations::ReadOptions(
    const MasterPreferences& prefs,
    std::set<std::wstring>* options) const {
  DCHECK(options);

  bool pref_value;
  if (prefs.GetBool(master_preferences::kMultiInstall, &pref_value) &&
      pref_value) {
    options->insert(kOptionMultiInstall);
  }
}

void ChromeAppHostOperations::ReadOptions(
    const CommandLine& uninstall_command,
    std::set<std::wstring>* options) const {
  DCHECK(options);

  if (uninstall_command.HasSwitch(switches::kMultiInstall))
    options->insert(kOptionMultiInstall);
}

void ChromeAppHostOperations::AddKeyFiles(
    const std::set<std::wstring>& options,
    std::vector<FilePath>* key_files) const {
}

void ChromeAppHostOperations::AddComDllList(
    const std::set<std::wstring>& options,
    std::vector<FilePath>* com_dll_list) const {
}

void ChromeAppHostOperations::AppendProductFlags(
    const std::set<std::wstring>& options,
    CommandLine* cmd_line) const {
  DCHECK(cmd_line);
  bool is_multi_install = options.find(kOptionMultiInstall) != options.end();

  // Non-multi-install not supported for the app host.
  DCHECK(is_multi_install);

  // Add --multi-install if it isn't already there.
  if (is_multi_install && !cmd_line->HasSwitch(switches::kMultiInstall))
    cmd_line->AppendSwitch(switches::kMultiInstall);

  // --app-host is always needed.
  cmd_line->AppendSwitch(switches::kChromeAppHost);
}

void ChromeAppHostOperations::AppendRenameFlags(
    const std::set<std::wstring>& options,
    CommandLine* cmd_line) const {
  DCHECK(cmd_line);
  bool is_multi_install = options.find(kOptionMultiInstall) != options.end();

  // Non-multi-install not supported for the app host.
  DCHECK(is_multi_install);

  // Add --multi-install if it isn't already there.
  if (is_multi_install && !cmd_line->HasSwitch(switches::kMultiInstall))
    cmd_line->AppendSwitch(switches::kMultiInstall);
}

bool ChromeAppHostOperations::SetChannelFlags(
    const std::set<std::wstring>& options,
    bool set,
    ChannelInfo* channel_info) const {
#if defined(GOOGLE_CHROME_BUILD)
  DCHECK(channel_info);
  bool modified = channel_info->SetAppHost(set);

  return modified;
#else
  return false;
#endif
}

bool ChromeAppHostOperations::ShouldCreateUninstallEntry(
    const std::set<std::wstring>& options) const {
  return false;
}

}  // namespace installer
