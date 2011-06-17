// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#if defined(OS_WIN)
#include <windows.h>
#endif

#include "app/win/scoped_com_initializer.h"
#include "base/environment.h"
#include "base/message_loop.h"
#include "base/stringprintf.h"
#include "base/threading/platform_thread.h"
#include "build/build_config.h"
#include "content/common/content_switches.h"
#include "content/common/gpu/gpu_config.h"
#include "content/common/main_function_params.h"
#include "content/gpu/gpu_child_thread.h"
#include "content/gpu/gpu_process.h"

#if defined(OS_MACOSX)
#include "content/common/chrome_application_mac.h"
#endif

#if defined(USE_X11)
#include "ui/base/x/x11_util.h"
#endif

// Main function for starting the Gpu process.
int GpuMain(const MainFunctionParams& parameters) {
  base::Time start_time = base::Time::Now();

  const CommandLine& command_line = parameters.command_line_;
  if (command_line.HasSwitch(switches::kGpuStartupDialog)) {
    ChildProcess::WaitForDebugger("Gpu");
  }

#if defined(OS_MACOSX)
  chrome_application_mac::RegisterCrApp();
#endif

  MessageLoop main_message_loop(MessageLoop::TYPE_UI);
  base::PlatformThread::SetName("CrGpuMain");

  if (!command_line.HasSwitch(switches::kSingleProcess)) {
#if defined(OS_WIN)
    // Prevent Windows from displaying a modal dialog on failures like not being
    // able to load a DLL.
    SetErrorMode(
        SEM_FAILCRITICALERRORS |
        SEM_NOGPFAULTERRORBOX |
        SEM_NOOPENFILEERRORBOX);
#elif defined(USE_X11)
    ui::SetDefaultX11ErrorHandlers();
#endif
  }

  app::win::ScopedCOMInitializer com_initializer;

  // We can not tolerate early returns from this code, because the
  // detection of early return of a child process is implemented using
  // an IPC channel error. If the IPC channel is not fully set up
  // between the browser and GPU process, and the GPU process crashes
  // or exits early, the browser process will never detect it.  For
  // this reason we defer all work related to the GPU until receiving
  // the GpuMsg_Initialize message from the browser.
  GpuProcess gpu_process;

  GpuChildThread* child_thread =
#if defined(OS_WIN)
      new GpuChildThread(parameters.sandbox_info_.TargetServices());
#else
      new GpuChildThread;
#endif

  child_thread->Init(start_time);

  gpu_process.set_main_thread(child_thread);

  main_message_loop.Run();

  child_thread->StopWatchdog();

  return 0;
}
