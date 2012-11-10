// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/shell_integration.h"

#include <windows.h>
#include <shobjidl.h>
#include <propkey.h>
#include <propvarutil.h>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/file_util.h"
#include "base/message_loop.h"
#include "base/path_service.h"
#include "base/scoped_native_library.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "base/stringprintf.h"
#include "base/utf_string_conversions.h"
#include "base/win/metro.h"
#include "base/win/registry.h"
#include "base/win/scoped_co_mem.h"
#include "base/win/scoped_comptr.h"
#include "base/win/windows_version.h"
#include "chrome/browser/web_applications/web_app.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_paths_internal.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/installer/setup/setup_util.h"
#include "chrome/installer/util/browser_distribution.h"
#include "chrome/installer/util/create_reg_key_work_item.h"
#include "chrome/installer/util/install_util.h"
#include "chrome/installer/util/set_reg_value_work_item.h"
#include "chrome/installer/util/shell_util.h"
#include "chrome/installer/util/util_constants.h"
#include "chrome/installer/util/work_item.h"
#include "chrome/installer/util/work_item_list.h"
#include "content/public/browser/browser_thread.h"

using content::BrowserThread;

namespace {

// Gets the short (8.3) form of |path|, putting the result in |short_path| and
// returning true on success.  |short_path| is not modified on failure.
bool ShortNameFromPath(const FilePath& path, string16* short_path) {
  DCHECK(short_path);
  string16 result(MAX_PATH, L'\0');
  DWORD short_length = GetShortPathName(path.value().c_str(), &result[0],
                                        result.size());
  if (short_length == 0 || short_length > result.size()) {
    PLOG(ERROR) << "Error getting short (8.3) path";
    return false;
  }

  result.resize(short_length);
  short_path->swap(result);
  return true;
}

// Probe using IApplicationAssociationRegistration::QueryCurrentDefault
// (Windows 8); see ProbeProtocolHandlers.  This mechanism is not suitable for
// use on previous versions of Windows despite the presence of
// QueryCurrentDefault on them since versions of Windows prior to Windows 8
// did not perform validation on the ProgID registered as the current default.
// As a result, stale ProgIDs could be returned, leading to false positives.
ShellIntegration::DefaultWebClientState ProbeCurrentDefaultHandlers(
    const wchar_t* const* protocols,
    size_t num_protocols) {
  base::win::ScopedComPtr<IApplicationAssociationRegistration> registration;
  HRESULT hr = registration.CreateInstance(
      CLSID_ApplicationAssociationRegistration, NULL, CLSCTX_INPROC);
  if (FAILED(hr))
    return ShellIntegration::UNKNOWN_DEFAULT_WEB_CLIENT;

  BrowserDistribution* dist = BrowserDistribution::GetDistribution();
  FilePath chrome_exe;
  if (!PathService::Get(base::FILE_EXE, &chrome_exe)) {
    NOTREACHED();
    return ShellIntegration::UNKNOWN_DEFAULT_WEB_CLIENT;
  }
  string16 prog_id(ShellUtil::kChromeHTMLProgId);
  prog_id += ShellUtil::GetCurrentInstallationSuffix(dist, chrome_exe.value());

  for (size_t i = 0; i < num_protocols; ++i) {
    base::win::ScopedCoMem<wchar_t> current_app;
    hr = registration->QueryCurrentDefault(protocols[i], AT_URLPROTOCOL,
                                           AL_EFFECTIVE, &current_app);
    if (FAILED(hr) || prog_id.compare(current_app) != 0)
      return ShellIntegration::NOT_DEFAULT_WEB_CLIENT;
  }

  return ShellIntegration::IS_DEFAULT_WEB_CLIENT;
}

// Probe using IApplicationAssociationRegistration::QueryAppIsDefault (Vista and
// Windows 7); see ProbeProtocolHandlers.
ShellIntegration::DefaultWebClientState ProbeAppIsDefaultHandlers(
    const wchar_t* const* protocols,
    size_t num_protocols) {
  base::win::ScopedComPtr<IApplicationAssociationRegistration> registration;
  HRESULT hr = registration.CreateInstance(
      CLSID_ApplicationAssociationRegistration, NULL, CLSCTX_INPROC);
  if (FAILED(hr))
    return ShellIntegration::UNKNOWN_DEFAULT_WEB_CLIENT;

  BrowserDistribution* dist = BrowserDistribution::GetDistribution();
  FilePath chrome_exe;
  if (!PathService::Get(base::FILE_EXE, &chrome_exe)) {
    NOTREACHED();
    return ShellIntegration::UNKNOWN_DEFAULT_WEB_CLIENT;
  }
  string16 app_name(ShellUtil::GetApplicationName(dist, chrome_exe.value()));

  BOOL result;
  for (size_t i = 0; i < num_protocols; ++i) {
    result = TRUE;
    hr = registration->QueryAppIsDefault(protocols[i], AT_URLPROTOCOL,
        AL_EFFECTIVE, app_name.c_str(), &result);
    if (FAILED(hr) || result == FALSE)
      return ShellIntegration::NOT_DEFAULT_WEB_CLIENT;
  }

  return ShellIntegration::IS_DEFAULT_WEB_CLIENT;
}

// Probe the current commands registered to handle the shell "open" verb for
// |protocols| (Windows XP); see ProbeProtocolHandlers.
ShellIntegration::DefaultWebClientState ProbeOpenCommandHandlers(
    const wchar_t* const* protocols,
    size_t num_protocols) {
  // Get the path to the current exe (Chrome).
  FilePath app_path;
  if (!PathService::Get(base::FILE_EXE, &app_path)) {
    LOG(ERROR) << "Error getting app exe path";
    return ShellIntegration::UNKNOWN_DEFAULT_WEB_CLIENT;
  }

  // Get its short (8.3) form.
  string16 short_app_path;
  if (!ShortNameFromPath(app_path, &short_app_path))
    return ShellIntegration::UNKNOWN_DEFAULT_WEB_CLIENT;

  const HKEY root_key = HKEY_CLASSES_ROOT;
  string16 key_path;
  base::win::RegKey key;
  string16 value;
  CommandLine command_line(CommandLine::NO_PROGRAM);
  string16 short_path;

  for (size_t i = 0; i < num_protocols; ++i) {
    // Get the command line from HKCU\<protocol>\shell\open\command.
    key_path.assign(protocols[i]).append(ShellUtil::kRegShellOpen);
    if ((key.Open(root_key, key_path.c_str(),
                  KEY_QUERY_VALUE) != ERROR_SUCCESS) ||
        (key.ReadValue(L"", &value) != ERROR_SUCCESS)) {
      return ShellIntegration::NOT_DEFAULT_WEB_CLIENT;
    }

    // Need to normalize path in case it's been munged.
    command_line = CommandLine::FromString(value);
    if (!ShortNameFromPath(command_line.GetProgram(), &short_path))
      return ShellIntegration::UNKNOWN_DEFAULT_WEB_CLIENT;

    if (!FilePath::CompareEqualIgnoreCase(short_path, short_app_path))
      return ShellIntegration::NOT_DEFAULT_WEB_CLIENT;
  }

  return ShellIntegration::IS_DEFAULT_WEB_CLIENT;
}

// A helper function that probes default protocol handler registration (in a
// manner appropriate for the current version of Windows) to determine if
// Chrome is the default handler for |protocols|.  Returns IS_DEFAULT_WEB_CLIENT
// only if Chrome is the default for all specified protocols.
ShellIntegration::DefaultWebClientState ProbeProtocolHandlers(
    const wchar_t* const* protocols,
    size_t num_protocols) {
  DCHECK(!num_protocols || protocols);
  if (DCHECK_IS_ON()) {
    for (size_t i = 0; i < num_protocols; ++i)
      DCHECK(protocols[i] && *protocols[i]);
  }

  const base::win::Version windows_version = base::win::GetVersion();

  if (windows_version >= base::win::VERSION_WIN8)
    return ProbeCurrentDefaultHandlers(protocols, num_protocols);
  else if (windows_version >= base::win::VERSION_VISTA)
    return ProbeAppIsDefaultHandlers(protocols, num_protocols);

  return ProbeOpenCommandHandlers(protocols, num_protocols);
}

// Helper function for ShellIntegration::GetAppId to generates profile id
// from profile path. "profile_id" is composed of sanitized basenames of
// user data dir and profile dir joined by a ".".
string16 GetProfileIdFromPath(const FilePath& profile_path) {
  // Return empty string if profile_path is empty
  if (profile_path.empty())
    return string16();

  FilePath default_user_data_dir;
  // Return empty string if profile_path is in default user data
  // dir and is the default profile.
  if (chrome::GetDefaultUserDataDirectory(&default_user_data_dir) &&
      profile_path.DirName() == default_user_data_dir &&
      profile_path.BaseName().value() ==
          ASCIIToUTF16(chrome::kInitialProfile)) {
    return string16();
  }

  // Get joined basenames of user data dir and profile.
  string16 basenames = profile_path.DirName().BaseName().value() +
      L"." + profile_path.BaseName().value();

  string16 profile_id;
  profile_id.reserve(basenames.size());

  // Generate profile_id from sanitized basenames.
  for (size_t i = 0; i < basenames.length(); ++i) {
    if (IsAsciiAlpha(basenames[i]) ||
        IsAsciiDigit(basenames[i]) ||
        basenames[i] == L'.')
      profile_id += basenames[i];
  }

  return profile_id;
}

bool GetShortcutAppId(IShellLink* shell_link, string16* app_id) {
  DCHECK(shell_link);
  DCHECK(app_id);

  app_id->clear();

  base::win::ScopedComPtr<IPropertyStore> property_store;
  if (FAILED(property_store.QueryFrom(shell_link)))
    return false;

  PROPVARIANT appid_value;
  PropVariantInit(&appid_value);
  if (FAILED(property_store->GetValue(PKEY_AppUserModel_ID, &appid_value)))
    return false;

  if (appid_value.vt == VT_LPWSTR || appid_value.vt == VT_BSTR)
    app_id->assign(appid_value.pwszVal);

  PropVariantClear(&appid_value);
  return true;
}

// Gets expected app id for given chrome shortcut. Returns true if the shortcut
// points to chrome and expected app id is successfully derived.
bool GetExpectedAppId(const FilePath& chrome_exe,
                      IShellLink* shell_link,
                      string16* expected_app_id) {
  DCHECK(shell_link);
  DCHECK(expected_app_id);

  expected_app_id->clear();

  // Check if the shortcut points to chrome_exe.
  string16 source;
  if (FAILED(shell_link->GetPath(WriteInto(&source, MAX_PATH), MAX_PATH, NULL,
                                 SLGP_RAWPATH)) ||
      lstrcmpi(chrome_exe.value().c_str(), source.c_str()))
    return false;

  string16 arguments;
  if (FAILED(shell_link->GetArguments(WriteInto(&arguments, MAX_PATH),
                                      MAX_PATH)))
    return false;

  // Get expected app id from shortcut command line.
  CommandLine command_line = CommandLine::FromString(base::StringPrintf(
      L"\"%ls\" %ls", source.c_str(), arguments.c_str()));

  FilePath profile_path;
  if (command_line.HasSwitch(switches::kUserDataDir)) {
    profile_path =
        command_line.GetSwitchValuePath(switches::kUserDataDir).AppendASCII(
            chrome::kInitialProfile);
  }

  string16 app_name;
  if (command_line.HasSwitch(switches::kApp)) {
    app_name = UTF8ToUTF16(web_app::GenerateApplicationNameFromURL(
        GURL(command_line.GetSwitchValueASCII(switches::kApp))));
  } else if (command_line.HasSwitch(switches::kAppId)) {
    app_name = UTF8ToUTF16(web_app::GenerateApplicationNameFromExtensionId(
        command_line.GetSwitchValueASCII(switches::kAppId)));
  } else {
    BrowserDistribution* dist = BrowserDistribution::GetDistribution();
    app_name = ShellUtil::GetBrowserModelId(dist, chrome_exe.value());
  }

  expected_app_id->assign(
      ShellIntegration::GetAppModelIdForProfile(app_name, profile_path));
  return true;
}

void MigrateWin7ShortcutsInPath(
    const FilePath& chrome_exe, const FilePath& path) {
  // Enumerate all pinned shortcuts in the given path directly.
  file_util::FileEnumerator shortcuts_enum(
      path, false,  // not recursive
      file_util::FileEnumerator::FILES, FILE_PATH_LITERAL("*.lnk"));

  for (FilePath shortcut = shortcuts_enum.Next(); !shortcut.empty();
       shortcut = shortcuts_enum.Next()) {
    // Load the shortcut.
    base::win::ScopedComPtr<IShellLink> shell_link;
    if (FAILED(shell_link.CreateInstance(CLSID_ShellLink, NULL,
                                         CLSCTX_INPROC_SERVER))) {
      NOTREACHED();
      return;
    }

    base::win::ScopedComPtr<IPersistFile> persist_file;
    if (FAILED(persist_file.QueryFrom(shell_link)) ||
        FAILED(persist_file->Load(shortcut.value().c_str(), STGM_READ))) {
      NOTREACHED();
      return;
    }

    // Get expected app id from shortcut.
    string16 expected_app_id;
    if (!GetExpectedAppId(chrome_exe, shell_link, &expected_app_id) ||
        expected_app_id.empty())
      continue;

    // Get existing app id from shortcut if any.
    string16 existing_app_id;
    GetShortcutAppId(shell_link, &existing_app_id);

    if (expected_app_id != existing_app_id) {
      file_util::CreateOrUpdateShortcutLink(NULL, shortcut.value().c_str(),
                                            NULL, NULL, NULL, NULL, 0,
                                            expected_app_id.c_str(),
                                            file_util::SHORTCUT_NO_OPTIONS);
    }
  }
}

void MigrateChromiumShortcutsCallback() {
  // This should run on the file thread.
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));

  // Get full path of chrome.
  FilePath chrome_exe;
  if (!PathService::Get(base::FILE_EXE, &chrome_exe))
    return;

  // Locations to check for shortcuts migration.
  static const struct {
    int location_id;
    const wchar_t* sub_dir;
  } kLocations[] = {
    {
      base::DIR_APP_DATA,
      L"Microsoft\\Internet Explorer\\Quick Launch\\User Pinned\\TaskBar"
    }, {
      chrome::DIR_USER_DESKTOP,
      NULL
    }, {
      base::DIR_START_MENU,
      NULL
    }, {
      base::DIR_APP_DATA,
      L"Microsoft\\Internet Explorer\\Quick Launch\\User Pinned\\StartMenu"
    }
  };

  for (int i = 0; i < arraysize(kLocations); ++i) {
    FilePath path;
    if (!PathService::Get(kLocations[i].location_id, &path)) {
      NOTREACHED();
      continue;
    }

    if (kLocations[i].sub_dir)
      path = path.Append(kLocations[i].sub_dir);

    MigrateWin7ShortcutsInPath(chrome_exe, path);
  }
}

// Activates the application with the given AppUserModelId.
bool ActivateApplication(const string16& app_id) {
  // Not supported when running in metro mode.
  // TODO(grt) This should perhaps check that this Chrome isn't in metro mode
  // or, if it is, that |app_id| doesn't identify this Chrome.
  if (base::win::IsMetroProcess())
    return false;

  // Delegate to metro_driver, which has the brains to invoke the activation
  // wizardry.
  bool success = false;
  const FilePath metro_driver_path(chrome::kMetroDriverDll);
  base::ScopedNativeLibrary metro_driver(metro_driver_path);
  if (!metro_driver.is_valid()) {
    PLOG(ERROR) << "Failed to load metro_driver.";
  } else {
    base::win::ActivateApplicationFn activate_application =
        reinterpret_cast<base::win::ActivateApplicationFn>(
            metro_driver.GetFunctionPointer(base::win::kActivateApplication));
    if (!activate_application) {
      PLOG(ERROR) << "Failed to find activation method in metro_driver.";
    } else {
      HRESULT hr = activate_application(app_id.c_str());
      if (FAILED(hr)) {
        LOG(ERROR) << "Failed to activate " << app_id
                   << "; hr=0x" << std::hex << hr;
      } else {
        success = true;
      }
    }
  }
  return success;
}

}  // namespace

ShellIntegration::DefaultWebClientSetPermission
    ShellIntegration::CanSetAsDefaultBrowser() {
  if (!BrowserDistribution::GetDistribution()->CanSetAsDefault())
    return SET_DEFAULT_NOT_ALLOWED;

  if (ShellUtil::CanMakeChromeDefaultUnattended())
    return SET_DEFAULT_UNATTENDED;
  else
    return SET_DEFAULT_INTERACTIVE;
}

bool ShellIntegration::SetAsDefaultBrowser() {
  FilePath chrome_exe;
  if (!PathService::Get(base::FILE_EXE, &chrome_exe)) {
    LOG(ERROR) << "Error getting app exe path";
    return false;
  }

  // From UI currently we only allow setting default browser for current user.
  BrowserDistribution* dist = BrowserDistribution::GetDistribution();
  if (!ShellUtil::MakeChromeDefault(dist, ShellUtil::CURRENT_USER,
                                    chrome_exe.value(), true)) {
    LOG(ERROR) << "Chrome could not be set as default browser.";
    return false;
  }

  VLOG(1) << "Chrome registered as default browser.";
  return true;
}

bool ShellIntegration::SetAsDefaultProtocolClient(const std::string& protocol) {
  if (protocol.empty())
    return false;

  FilePath chrome_exe;
  if (!PathService::Get(base::FILE_EXE, &chrome_exe)) {
    LOG(ERROR) << "Error getting app exe path";
    return false;
  }

  string16 wprotocol = UTF8ToUTF16(protocol);
  BrowserDistribution* dist = BrowserDistribution::GetDistribution();
  if (!ShellUtil::MakeChromeDefaultProtocolClient(dist, chrome_exe.value(),
        wprotocol)) {
    LOG(ERROR) << "Chrome could not be set as default handler for "
               << protocol << ".";
    return false;
  }

  VLOG(1) << "Chrome registered as default handler for " << protocol << ".";
  return true;
}

bool ShellIntegration::SetAsDefaultBrowserInteractive() {
  FilePath chrome_exe;
  if (!PathService::Get(base::FILE_EXE, &chrome_exe)) {
    NOTREACHED() << "Error getting app exe path";
    return false;
  }

  BrowserDistribution* dist = BrowserDistribution::GetDistribution();
  if (!ShellUtil::ShowMakeChromeDefaultSystemUI(dist, chrome_exe.value())) {
    LOG(ERROR) << "Failed to launch the set-default-browser Windows UI.";
    return false;
  }

  VLOG(1) << "Set-as-default Windows UI triggered.";
  return true;
}

ShellIntegration::DefaultWebClientState ShellIntegration::IsDefaultBrowser() {
  // When we check for default browser we don't necessarily want to count file
  // type handlers and icons as having changed the default browser status,
  // since the user may have changed their shell settings to cause HTML files
  // to open with a text editor for example. We also don't want to aggressively
  // claim FTP, since the user may have a separate FTP client. It is an open
  // question as to how to "heal" these settings. Perhaps the user should just
  // re-run the installer or run with the --set-default-browser command line
  // flag. There is doubtless some other key we can hook into to cause "Repair"
  // to show up in Add/Remove programs for us.
  static const wchar_t* const kChromeProtocols[] = { L"http", L"https" };

  return ProbeProtocolHandlers(kChromeProtocols, arraysize(kChromeProtocols));
}

ShellIntegration::DefaultWebClientState
    ShellIntegration::IsDefaultProtocolClient(const std::string& protocol) {
  if (protocol.empty())
    return UNKNOWN_DEFAULT_WEB_CLIENT;

  string16 wide_protocol(UTF8ToUTF16(protocol));
  const wchar_t* const protocols[] = { wide_protocol.c_str() };

  return ProbeProtocolHandlers(protocols, arraysize(protocols));
}

// There is no reliable way to say which browser is default on a machine (each
// browser can have some of the protocols/shortcuts). So we look for only HTTP
// protocol handler. Even this handler is located at different places in
// registry on XP and Vista:
// - HKCR\http\shell\open\command (XP)
// - HKCU\Software\Microsoft\Windows\Shell\Associations\UrlAssociations\
//   http\UserChoice (Vista)
// This method checks if Firefox is defualt browser by checking these
// locations and returns true if Firefox traces are found there. In case of
// error (or if Firefox is not found)it returns the default value which
// is false.
bool ShellIntegration::IsFirefoxDefaultBrowser() {
  bool ff_default = false;
  if (base::win::GetVersion() >= base::win::VERSION_VISTA) {
    string16 app_cmd;
    base::win::RegKey key(HKEY_CURRENT_USER,
                          ShellUtil::kRegVistaUrlPrefs, KEY_READ);
    if (key.Valid() && (key.ReadValue(L"Progid", &app_cmd) == ERROR_SUCCESS) &&
        app_cmd == L"FirefoxURL")
      ff_default = true;
  } else {
    string16 key_path(L"http");
    key_path.append(ShellUtil::kRegShellOpen);
    base::win::RegKey key(HKEY_CLASSES_ROOT, key_path.c_str(), KEY_READ);
    string16 app_cmd;
    if (key.Valid() && (key.ReadValue(L"", &app_cmd) == ERROR_SUCCESS) &&
        string16::npos != StringToLowerASCII(app_cmd).find(L"firefox"))
      ff_default = true;
  }
  return ff_default;
}

string16 ShellIntegration::GetAppModelIdForProfile(
    const string16& app_name,
    const FilePath& profile_path) {
  std::vector<string16> components;
  components.push_back(app_name);
  const string16 profile_id(GetProfileIdFromPath(profile_path));
  if (!profile_id.empty())
    components.push_back(profile_id);
  return ShellUtil::BuildAppModelId(components);
}

string16 ShellIntegration::GetChromiumModelIdForProfile(
    const FilePath& profile_path) {
  BrowserDistribution* dist = BrowserDistribution::GetDistribution();
  FilePath chrome_exe;
  if (!PathService::Get(base::FILE_EXE, &chrome_exe)) {
    NOTREACHED();
    return dist->GetBaseAppId();
  }
  return GetAppModelIdForProfile(
      ShellUtil::GetBrowserModelId(dist, chrome_exe.value()), profile_path);
}

string16 ShellIntegration::GetChromiumIconPath() {
  // Determine the app path. If we can't determine what that is, we have
  // bigger fish to fry...
  FilePath app_path;
  if (!PathService::Get(base::FILE_EXE, &app_path)) {
    NOTREACHED();
    return string16();
  }

  string16 icon_path(app_path.value());
  icon_path.push_back(',');
  icon_path += base::IntToString16(
      BrowserDistribution::GetDistribution()->GetIconIndex());
  return icon_path;
}

void ShellIntegration::MigrateChromiumShortcuts() {
  if (base::win::GetVersion() < base::win::VERSION_WIN7)
    return;

  BrowserThread::PostTask(
      BrowserThread::FILE, FROM_HERE,
      base::Bind(&MigrateChromiumShortcutsCallback));
}

bool ShellIntegration::ActivateMetroChrome() {
  BrowserDistribution* dist = BrowserDistribution::GetDistribution();
  FilePath chrome_exe;
  if (!PathService::Get(base::FILE_EXE, &chrome_exe)) {
    NOTREACHED();
    return false;
  }
  const string16 app_id(
      ShellUtil::GetBrowserModelId(dist, chrome_exe.value()));
  return ActivateApplication(app_id);
}

FilePath ShellIntegration::GetStartMenuShortcut(const FilePath& chrome_exe) {
  static const int kFolderIds[] = {
    base::DIR_COMMON_START_MENU,
    base::DIR_START_MENU,
  };
  BrowserDistribution* dist = BrowserDistribution::GetDistribution();
  string16 shortcut_name(dist->GetAppShortCutName());
  FilePath shortcut;

  // Check both the common and the per-user Start Menu folders for system-level
  // installs.
  size_t folder =
      InstallUtil::IsPerUserInstall(chrome_exe.value().c_str()) ? 1 : 0;
  for (; folder < arraysize(kFolderIds); ++folder) {
    if (!PathService::Get(kFolderIds[folder], &shortcut)) {
      NOTREACHED();
      continue;
    }

    shortcut = shortcut.Append(shortcut_name).Append(shortcut_name + L".lnk");
    if (file_util::PathExists(shortcut))
      return shortcut;
  }

  return FilePath();
}
