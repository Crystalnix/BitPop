// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SANDBOX_SRC_SANDBOX_UTILS_H__
#define SANDBOX_SRC_SANDBOX_UTILS_H__

#include <windows.h>
#include <string>

#include "base/basictypes.h"
#include "sandbox/win/src/nt_internals.h"

namespace sandbox {

typedef BOOL (WINAPI* GetModuleHandleExFunction)(DWORD flags,
                                                 LPCWSTR module_name,
                                                 HMODULE* module);

// Windows XP provides a nice function in kernel32.dll called GetModuleHandleEx
// This function allows us to verify if a function exported by the module
// lies in the module itself.
// As we need compatibility with windows 2000, we cannot use this function
// by calling it by name. This helper function checks if the GetModuleHandleEx
// function is exported by kernel32 and uses it, otherwise, implemets part of
// the functionality exposed by GetModuleHandleEx.
bool GetModuleHandleHelper(DWORD flags, const wchar_t* module_name,
                           HMODULE* module);

// Returns true if the current OS is Windows XP SP2 or later.
bool IsXPSP2OrLater();

void InitObjectAttribs(const std::wstring& name, ULONG attributes, HANDLE root,
                       OBJECT_ATTRIBUTES* obj_attr, UNICODE_STRING* uni_name);

};  // namespace sandbox

#endif  // SANDBOX_SRC_SANDBOX_UTILS_H__
