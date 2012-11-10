// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cloud_print/service/win/chrome_launcher.h"

#include "base/command_line.h"
#include "base/process.h"
#include "base/process_util.h"
#include "base/win/scoped_handle.h"
#include "base/win/scoped_process_information.h"
#include "chrome/installer/launcher_support/chrome_launcher_support.h"
#include "cloud_print/service/service_switches.h"

namespace {

const int kShutdownTimeoutMs = 30 * 1000;

void ShutdownChrome(HANDLE process, DWORD thread_id) {
  if (::PostThreadMessage(thread_id, WM_QUIT, 0, 0) &&
      WAIT_OBJECT_0 == ::WaitForSingleObject(process, kShutdownTimeoutMs)) {
    return;
  }
  LOG(ERROR) << "Failed to shutdown process.";
  base::KillProcess(process, 0, true);
}

bool LaunchProcess(const CommandLine& cmdline,
                   base::ProcessHandle* process_handle,
                   DWORD* thread_id) {
  STARTUPINFO startup_info = {};
  startup_info.cb = sizeof(startup_info);
  startup_info.dwFlags = STARTF_USESHOWWINDOW;
  startup_info.wShowWindow = SW_SHOW;

  base::win::ScopedProcessInformation process_info;
  if (!CreateProcess(NULL,
      const_cast<wchar_t*>(cmdline.GetCommandLineString().c_str()), NULL, NULL,
      FALSE, 0, NULL, NULL, &startup_info, process_info.Receive())) {
    return false;
  }

  if (process_handle)
    *process_handle = process_info.TakeProcessHandle();

  if (thread_id)
    *thread_id = process_info.thread_id();

  return true;
}

}  // namespace

ChromeLauncher::ChromeLauncher(const FilePath& user_data)
    : stop_event_(true, true),
      user_data_(user_data) {
}

ChromeLauncher::~ChromeLauncher() {
}

bool ChromeLauncher::Start() {
  stop_event_.Reset();
  thread_.reset(new base::DelegateSimpleThread(this, "chrome_launcher"));
  thread_->Start();
  return true;
}

void ChromeLauncher::Stop() {
  stop_event_.Signal();
  thread_->Join();
  thread_.reset();
}

void ChromeLauncher::Run() {
  const base::TimeDelta default_time_out = base::TimeDelta::FromSeconds(1);
  const base::TimeDelta max_time_out = base::TimeDelta::FromHours(1);

  for (base::TimeDelta time_out = default_time_out;;
       time_out = std::min(time_out * 2, max_time_out)) {
    FilePath chrome_path = chrome_launcher_support::GetAnyChromePath();

    if (!chrome_path.empty()) {
      CommandLine cmd(chrome_path);
      cmd.AppendSwitchASCII(kChromeTypeSwitch, "service");
      cmd.AppendSwitchPath(kUserDataDirSwitch, user_data_);
      base::win::ScopedHandle chrome_handle;
      base::Time started = base::Time::Now();
      DWORD thread_id = 0;
      LaunchProcess(cmd, chrome_handle.Receive(), &thread_id);
      int exit_code = 0;
      HANDLE handles[] = {stop_event_.handle(), chrome_handle};
      DWORD wait_result = ::WaitForMultipleObjects(arraysize(handles), handles,
                                                   FALSE, INFINITE);
      if (wait_result == WAIT_OBJECT_0) {
        ShutdownChrome(chrome_handle, thread_id);
        break;
      } else if (wait_result == WAIT_OBJECT_0 + 1) {
        LOG(ERROR) << "Chrome process exited.";
      } else {
        LOG(ERROR) << "Error waiting Chrome (" << ::GetLastError() << ").";
      }
      if (base::Time::Now() - started > base::TimeDelta::FromHours(1)) {
        // Reset timeout because process worked long enough.
        time_out = default_time_out;
      }
    }
    if (stop_event_.TimedWait(time_out))
      break;
  }
}


