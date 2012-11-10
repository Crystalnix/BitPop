// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <atlbase.h>
#include <atlcom.h>
#include <atlctl.h>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/file_path.h"
#include "base/logging.h"
#include "remoting/base/breakpad.h"
#include "remoting/host/branding.h"
#include "remoting/host/usage_stats_consent.h"

// MIDL-generated declarations.
#include "remoting/host/win/elevated_controller.h"

namespace remoting {

class ElevatedControllerModule
    : public ATL::CAtlExeModuleT<ElevatedControllerModule> {
 public:
  DECLARE_LIBID(LIBID_ChromotingElevatedControllerLib)
};

} // namespace remoting


remoting::ElevatedControllerModule _AtlModule;

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int command) {
#ifdef OFFICIAL_BUILD
  if (remoting::IsUsageStatsAllowed()) {
    remoting::InitializeCrashReporting();
  }
#endif  // OFFICIAL_BUILD

  CommandLine::Init(0, NULL);

  // Register and initialize common controls.
  INITCOMMONCONTROLSEX info;
  info.dwSize = sizeof(info);
  info.dwICC = ICC_STANDARD_CLASSES;
  InitCommonControlsEx(&info);

  // This object instance is required by Chrome code (for example,
  // FilePath, LazyInstance, MessageLoop).
  base::AtExitManager exit_manager;

  // Write logs to the application profile directory.
  FilePath debug_log = remoting::GetConfigDir().
      Append(FILE_PATH_LITERAL("debug.log"));
  InitLogging(debug_log.value().c_str(),
              logging::LOG_ONLY_TO_FILE,
              logging::DONT_LOCK_LOG_FILE,
              logging::APPEND_TO_OLD_LOG_FILE,
              logging::DISABLE_DCHECK_FOR_NON_OFFICIAL_RELEASE_BUILDS);

  return _AtlModule.WinMain(command);
}
