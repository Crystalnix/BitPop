// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "base/message_loop.h"
#include "base/system_monitor/system_monitor.h"
#include "base/threading/platform_thread.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/extensions/extension_l10n_util.h"
#include "chrome/utility/utility_thread.h"
#include "content/common/child_process.h"
#include "content/common/hi_res_timer_manager.h"
#include "content/common/main_function_params.h"
#include "ui/base/ui_base_switches.h"

#if defined(OS_WIN)
#include "base/file_util.h"
#include "base/path_service.h"
#include "chrome/common/chrome_switches.h"
#include "content/common/sandbox_init_wrapper.h"
#include "sandbox/src/sandbox.h"
#endif  // defined(OS_WIN)

// Mainline routine for running as the utility process.
int UtilityMain(const MainFunctionParams& parameters) {
  // The main message loop of the utility process.
  MessageLoop main_message_loop;
  base::PlatformThread::SetName("CrUtilityMain");

  base::SystemMonitor system_monitor;
  HighResolutionTimerManager hi_res_timer_manager;

  ChildProcess utility_process;
  utility_process.set_main_thread(new UtilityThread());
#if defined(OS_WIN)
  // Load the pdf plugin before the sandbox is turned on. This is for Windows
  // only because we need this DLL only on Windows.
  FilePath pdf;
  if (PathService::Get(chrome::FILE_PDF_PLUGIN, &pdf) &&
      file_util::PathExists(pdf)) {
    bool rv = !!LoadLibrary(pdf.value().c_str());
    DCHECK(rv) << "Couldn't load PDF plugin";
  }

  bool no_sandbox = parameters.command_line_.HasSwitch(switches::kNoSandbox);
  if (!no_sandbox) {
    sandbox::TargetServices* target_services =
        parameters.sandbox_info_.TargetServices();
    if (!target_services)
      return false;
    target_services->LowerToken();
  }
#endif

  CommandLine* command_line = CommandLine::ForCurrentProcess();
  std::string lang = command_line->GetSwitchValueASCII(switches::kLang);
  if (!lang.empty())
    extension_l10n_util::SetProcessLocale(lang);

  MessageLoop::current()->Run();

  return 0;
}
