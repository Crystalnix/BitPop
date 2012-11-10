// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/base/win/shell.h"

#include <shellapi.h>
#include <shlobj.h>  // Must be before propkey.
#include <propkey.h>

#include "base/file_path.h"
#include "base/native_library.h"
#include "base/string_util.h"
#include "base/win/metro.h"
#include "base/win/scoped_comptr.h"
#include "base/win/win_util.h"
#include "base/win/windows_version.h"

namespace ui {
namespace win {

namespace {

void SetAppIdAndIconForWindow(const string16& app_id,
                              const string16& app_icon,
                              HWND hwnd) {
  // This functionality is only available on Win7+. It also doesn't make sense
  // to do this for Chrome Metro.
  if (base::win::GetVersion() < base::win::VERSION_WIN7 ||
      base::win::IsMetroProcess())
    return;
  base::win::ScopedComPtr<IPropertyStore> pps;
  HRESULT result = SHGetPropertyStoreForWindow(
      hwnd, __uuidof(*pps), reinterpret_cast<void**>(pps.Receive()));
  if (S_OK == result) {
    if (!app_id.empty())
      base::win::SetAppIdForPropertyStore(pps, app_id.c_str());
    if (!app_icon.empty()) {
      base::win::SetStringValueForPropertyStore(
          pps, PKEY_AppUserModel_RelaunchIconResource, app_icon.c_str());
    }
  }
}

}  // namespace

// Show the Windows "Open With" dialog box to ask the user to pick an app to
// open the file with.
bool OpenItemWithExternalApp(const string16& full_path) {
  SHELLEXECUTEINFO sei = { sizeof(sei) };
  sei.fMask = SEE_MASK_FLAG_DDEWAIT;
  sei.nShow = SW_SHOWNORMAL;
  sei.lpVerb = L"openas";
  sei.lpFile = full_path.c_str();
  return (TRUE == ::ShellExecuteExW(&sei));
}

bool OpenAnyViaShell(const string16& full_path,
                     const string16& directory,
                     DWORD mask) {
  SHELLEXECUTEINFO sei = { sizeof(sei) };
  sei.fMask = mask;
  sei.nShow = SW_SHOWNORMAL;
  sei.lpFile = full_path.c_str();
  sei.lpDirectory = directory.c_str();

  if (::ShellExecuteExW(&sei))
    return true;
  if (::GetLastError() == ERROR_NO_ASSOCIATION)
    return OpenItemWithExternalApp(full_path);
  return false;
}

bool OpenItemViaShell(const FilePath& full_path) {
  return OpenAnyViaShell(full_path.value(), full_path.DirName().value(), 0);
}

bool OpenItemViaShellNoZoneCheck(const FilePath& full_path) {
  return OpenAnyViaShell(full_path.value(), string16(),
      SEE_MASK_NOZONECHECKS | SEE_MASK_FLAG_DDEWAIT);
}

void SetAppIdForWindow(const string16& app_id, HWND hwnd) {
  SetAppIdAndIconForWindow(app_id, string16(), hwnd);
}

void SetAppIconForWindow(const string16& app_icon, HWND hwnd) {
  SetAppIdAndIconForWindow(string16(), app_icon, hwnd);
}

}  // namespace win
}  // namespace ui
