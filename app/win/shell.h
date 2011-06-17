// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APP_WIN_SHELL_H_
#define APP_WIN_SHELL_H_

#include <windows.h>

#include "base/string16.h"

class FilePath;

namespace app {
namespace win {

// Open or run a file via the Windows shell. In the event that there is no
// default application registered for the file specified by 'full_path',
// ask the user, via the Windows "Open With" dialog.
// Returns 'true' on successful open, 'false' otherwise.
bool OpenItemViaShell(const FilePath& full_path);

// The download manager now writes the alternate data stream with the
// zone on all downloads. This function is equivalent to OpenItemViaShell
// without showing the zone warning dialog.
bool OpenItemViaShellNoZoneCheck(const FilePath& full_path);

// Ask the user, via the Windows "Open With" dialog, for an application to use
// to open the file specified by 'full_path'.
// Returns 'true' on successful open, 'false' otherwise.
bool OpenItemWithExternalApp(const string16& full_path);

// Sets the application id given as the Application Model ID for the window
// specified.  This method is used to insure that different web applications
// do not group together on the Win7 task bar.
void SetAppIdForWindow(const string16& app_id, HWND hwnd);

}  // namespace win
}  // namespace app

#endif  // APP_WIN_SHELL_H_
