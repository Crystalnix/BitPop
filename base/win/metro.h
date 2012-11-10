// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_WIN_METRO_H_
#define BASE_WIN_METRO_H_

#include <windows.h>
#include <wpcapi.h>

#include "base/base_export.h"
#include "base/string16.h"

namespace base {
namespace win {

// Identifies the type of the metro launch.
enum MetroLaunchType {
  LAUNCH,
  SEARCH,
  SHARE,
  FILE,
  PROTOCOL,
  LASTLAUNCHTYPE,
};

// In metro mode, this enum identifies the last execution state, i.e. whether
// we crashed, terminated, etc.
enum MetroPreviousExecutionState {
  NOTRUNNING,
  RUNNING,
  SUSPENDED,
  TERMINATED,
  CLOSEDBYUSER,
  LASTEXECUTIONSTATE,
};

// Contains information about the currently displayed tab in metro mode.
struct CurrentTabInfo {
  wchar_t* title;
  wchar_t* url;
};

// The types of exports in metro_driver.dll.
typedef HRESULT (*ActivateApplicationFn)(const wchar_t*);

// The names of the exports in metro_driver.dll.
BASE_EXPORT extern const char kActivateApplication[];

// Returns the handle to the metro dll loaded in the process. A NULL return
// indicates that the metro dll was not loaded in the process.
BASE_EXPORT HMODULE GetMetroModule();

// Returns true if this process is running as an immersive program
// in Windows Metro mode.
BASE_EXPORT bool IsMetroProcess();

// Allocates and returns the destination string via the LocalAlloc API after
// copying the src to it.
BASE_EXPORT wchar_t* LocalAllocAndCopyString(const string16& src);

// Returns true if the screen supports touch.
BASE_EXPORT bool IsTouchEnabled();

// Returns true if Windows Parental control activity logging is enabled. This
// feature is available on Windows Vista and beyond.
// This function should ideally be called on the UI thread.
BASE_EXPORT bool IsParentalControlActivityLoggingOn();

// Handler function for the buttons on a metro dialog box
typedef void (*MetroDialogButtonPressedHandler)();

// Function to display metro style notifications.
typedef void (*MetroNotification)(const char* origin_url,
                                  const char* icon_url,
                                  const wchar_t* title,
                                  const wchar_t* body,
                                  const wchar_t* display_source,
                                  const char* notification_id);


}  // namespace win
}  // namespace base

#endif  // BASE_WIN_METRO_H_
