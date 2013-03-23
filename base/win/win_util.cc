// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/win/win_util.h"

#include <aclapi.h>
#include <shobjidl.h>  // Must be before propkey.
#include <initguid.h>
#include <propkey.h>
#include <propvarutil.h>
#include <sddl.h>
#include <shlobj.h>
#include <signal.h>
#include <stdlib.h>

#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/win/registry.h"
#include "base/string_util.h"
#include "base/stringprintf.h"
#include "base/threading/thread_restrictions.h"
#include "base/win/scoped_handle.h"
#include "base/win/windows_version.h"

namespace {

// Sets the value of |property_key| to |property_value| in |property_store|.
// Clears the PropVariant contained in |property_value|.
bool SetPropVariantValueForPropertyStore(
    IPropertyStore* property_store,
    const PROPERTYKEY& property_key,
    PROPVARIANT* property_value) {
  DCHECK(property_store);

  HRESULT result = property_store->SetValue(property_key, *property_value);
  if (result == S_OK)
    result = property_store->Commit();

  PropVariantClear(property_value);
  return SUCCEEDED(result);
}

void __cdecl ForceCrashOnSigAbort(int) {
  *((int*)0) = 0x1337;
}

}  // namespace

namespace base {
namespace win {

static bool g_crash_on_process_detach = false;

#define NONCLIENTMETRICS_SIZE_PRE_VISTA \
    SIZEOF_STRUCT_WITH_SPECIFIED_LAST_MEMBER(NONCLIENTMETRICS, lfMessageFont)

void GetNonClientMetrics(NONCLIENTMETRICS* metrics) {
  DCHECK(metrics);

  static const UINT SIZEOF_NONCLIENTMETRICS =
      (base::win::GetVersion() >= base::win::VERSION_VISTA) ?
      sizeof(NONCLIENTMETRICS) : NONCLIENTMETRICS_SIZE_PRE_VISTA;
  metrics->cbSize = SIZEOF_NONCLIENTMETRICS;
  const bool success = !!SystemParametersInfo(SPI_GETNONCLIENTMETRICS,
                                              SIZEOF_NONCLIENTMETRICS, metrics,
                                              0);
  DCHECK(success);
}

bool GetUserSidString(std::wstring* user_sid) {
  // Get the current token.
  HANDLE token = NULL;
  if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token))
    return false;
  base::win::ScopedHandle token_scoped(token);

  DWORD size = sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE;
  scoped_array<BYTE> user_bytes(new BYTE[size]);
  TOKEN_USER* user = reinterpret_cast<TOKEN_USER*>(user_bytes.get());

  if (!::GetTokenInformation(token, TokenUser, user, size, &size))
    return false;

  if (!user->User.Sid)
    return false;

  // Convert the data to a string.
  wchar_t* sid_string;
  if (!::ConvertSidToStringSid(user->User.Sid, &sid_string))
    return false;

  *user_sid = sid_string;

  ::LocalFree(sid_string);

  return true;
}

bool IsShiftPressed() {
  return (::GetKeyState(VK_SHIFT) & 0x8000) == 0x8000;
}

bool IsCtrlPressed() {
  return (::GetKeyState(VK_CONTROL) & 0x8000) == 0x8000;
}

bool IsAltPressed() {
  return (::GetKeyState(VK_MENU) & 0x8000) == 0x8000;
}

bool UserAccountControlIsEnabled() {
  // This can be slow if Windows ends up going to disk.  Should watch this key
  // for changes and only read it once, preferably on the file thread.
  //   http://code.google.com/p/chromium/issues/detail?id=61644
  base::ThreadRestrictions::ScopedAllowIO allow_io;

  base::win::RegKey key(HKEY_LOCAL_MACHINE,
      L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
      KEY_READ);
  DWORD uac_enabled;
  if (key.ReadValueDW(L"EnableLUA", &uac_enabled) != ERROR_SUCCESS)
    return true;
  // Users can set the EnableLUA value to something arbitrary, like 2, which
  // Vista will treat as UAC enabled, so we make sure it is not set to 0.
  return (uac_enabled != 0);
}

bool SetBooleanValueForPropertyStore(IPropertyStore* property_store,
                                     const PROPERTYKEY& property_key,
                                     bool property_bool_value) {
  PROPVARIANT property_value;
  if (FAILED(InitPropVariantFromBoolean(property_bool_value, &property_value)))
    return false;

  return SetPropVariantValueForPropertyStore(property_store,
                                             property_key,
                                             &property_value);
}

bool SetStringValueForPropertyStore(IPropertyStore* property_store,
                                    const PROPERTYKEY& property_key,
                                    const wchar_t* property_string_value) {
  PROPVARIANT property_value;
  if (FAILED(InitPropVariantFromString(property_string_value, &property_value)))
    return false;

  return SetPropVariantValueForPropertyStore(property_store,
                                             property_key,
                                             &property_value);
}

bool SetAppIdForPropertyStore(IPropertyStore* property_store,
                              const wchar_t* app_id) {
  // App id should be less than 64 chars and contain no space. And recommended
  // format is CompanyName.ProductName[.SubProduct.ProductNumber].
  // See http://msdn.microsoft.com/en-us/library/dd378459%28VS.85%29.aspx
  DCHECK(lstrlen(app_id) < 64 && wcschr(app_id, L' ') == NULL);

  return SetStringValueForPropertyStore(property_store,
                                        PKEY_AppUserModel_ID,
                                        app_id);
}

static const char16 kAutoRunKeyPath[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

bool AddCommandToAutoRun(HKEY root_key, const string16& name,
                         const string16& command) {
  base::win::RegKey autorun_key(root_key, kAutoRunKeyPath, KEY_SET_VALUE);
  return (autorun_key.WriteValue(name.c_str(), command.c_str()) ==
      ERROR_SUCCESS);
}

bool RemoveCommandFromAutoRun(HKEY root_key, const string16& name) {
  base::win::RegKey autorun_key(root_key, kAutoRunKeyPath, KEY_SET_VALUE);
  return (autorun_key.DeleteValue(name.c_str()) == ERROR_SUCCESS);
}

bool ReadCommandFromAutoRun(HKEY root_key,
                            const string16& name,
                            string16* command) {
  base::win::RegKey autorun_key(root_key, kAutoRunKeyPath, KEY_QUERY_VALUE);
  return (autorun_key.ReadValue(name.c_str(), command) == ERROR_SUCCESS);
}

void SetShouldCrashOnProcessDetach(bool crash) {
  g_crash_on_process_detach = crash;
}

bool ShouldCrashOnProcessDetach() {
  return g_crash_on_process_detach;
}

void SetAbortBehaviorForCrashReporting() {
  // Prevent CRT's abort code from prompting a dialog or trying to "report" it.
  // Disabling the _CALL_REPORTFAULT behavior is important since otherwise it
  // has the sideffect of clearing our exception filter, which means we
  // don't get any crash.
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

  // Set a SIGABRT handler for good measure. We will crash even if the default
  // is left in place, however this allows us to crash earlier. And it also
  // lets us crash in response to code which might directly call raise(SIGABRT)
  signal(SIGABRT, ForceCrashOnSigAbort);
}

bool IsMachineATablet() {
  if (base::win::GetVersion() < base::win::VERSION_WIN7)
    return false;
  const int kMultiTouch = NID_INTEGRATED_TOUCH | NID_MULTI_INPUT | NID_READY;
  const int kMaxTabletScreenWidth = 1366;
  const int kMaxTabletScreenHeight = 768;
  int sm = GetSystemMetrics(SM_DIGITIZER);
  if ((sm & kMultiTouch) == kMultiTouch) {
    int cx = GetSystemMetrics(SM_CXSCREEN);
    int cy = GetSystemMetrics(SM_CYSCREEN);
    // Handle landscape and portrait modes.
    return cx > cy ?
        (cx <= kMaxTabletScreenWidth && cy <= kMaxTabletScreenHeight) :
        (cy <= kMaxTabletScreenWidth && cx <= kMaxTabletScreenHeight);
  }
  return false;
}

}  // namespace win
}  // namespace base

#ifdef _MSC_VER
//
// If the ASSERT below fails, please install Visual Studio 2005 Service Pack 1.
//
extern char VisualStudio2005ServicePack1Detection[10];
COMPILE_ASSERT(sizeof(&VisualStudio2005ServicePack1Detection) == sizeof(void*),
               VS2005SP1Detect);
//
// Chrome requires at least Service Pack 1 for Visual Studio 2005.
//
#endif  // _MSC_VER

#ifndef COPY_FILE_COPY_SYMLINK
#error You must install the Windows 2008 or Vista Software Development Kit and \
set it as your default include path to build this library. You can grab it by \
searching for "download windows sdk 2008" in your favorite web search engine.  \
Also make sure you register the SDK with Visual Studio, by selecting \
"Integrate Windows SDK with Visual Studio 2005" from the Windows SDK \
menu (see Start - All Programs - Microsoft Windows SDK - \
Visual Studio Registration).
#endif
