// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sync/util/get_session_name.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/callback.h"
#include "base/location.h"
#include "base/sys_info.h"
#include "base/task_runner.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/system/statistics_provider.h"
#elif defined(OS_LINUX)
#include "base/linux_util.h"
#elif defined(OS_MACOSX)
#include "sync/util/get_session_name_mac.h"
#elif defined(OS_WIN)
#include "sync/util/get_session_name_win.h"
#elif defined(OS_ANDROID)
#include "sync/util/session_utils_android.h"
#endif

namespace syncer {

namespace {

std::string GetSessionNameSynchronously() {
  std::string session_name;
#if defined(OS_CHROMEOS)
  // TODO(kochi): This is very ad hoc and fragile. http://crbug.com/126732.
  std::string board;
  const char kMachineInfoBoard[] = "CHROMEOS_RELEASE_BOARD";
  chromeos::system::StatisticsProvider* provider =
      chromeos::system::StatisticsProvider::GetInstance();
  if (!provider->GetMachineStatistic(kMachineInfoBoard, &board))
    LOG(ERROR) << "Failed to get board information";
  // Currently, only "stumpy" type of board is considered Chromebox, and
  // anything else is Chromebook.  On these devices, session_name should look
  // like "stumpy-signed-mp-v2keys" etc. The information can be checked on
  // "CHROMEOS_RELEASE_BOARD" line in chrome://system.
  session_name = board.substr(0, 6) == "stumpy" ? "Chromebox" : "Chromebook";
#elif defined(OS_LINUX)
  session_name = base::GetLinuxDistro();
#elif defined(OS_MACOSX)
  session_name = internal::GetHardwareModelName();
#elif defined(OS_WIN)
  session_name = internal::GetComputerName();
#elif defined(OS_ANDROID)
  session_name = internal::GetModel();
#endif

  if (session_name == "Unknown" || session_name.empty())
    session_name = base::SysInfo::OperatingSystemName();

  return session_name;
}

void FillSessionName(std::string* session_name) {
  *session_name = GetSessionNameSynchronously();
}

void OnSessionNameFilled(
    const base::Callback<void(const std::string&)>& done_callback,
    std::string* session_name) {
  done_callback.Run(*session_name);
}

}  // namespace

void GetSessionName(
    const scoped_refptr<base::TaskRunner>& task_runner,
    const base::Callback<void(const std::string&)>& done_callback) {
  std::string* session_name = new std::string();
  task_runner->PostTaskAndReply(
      FROM_HERE,
      base::Bind(&FillSessionName,
                 base::Unretained(session_name)),
      base::Bind(&OnSessionNameFilled,
                 done_callback,
                 base::Owned(session_name)));
}

std::string GetSessionNameSynchronouslyForTesting() {
  return GetSessionNameSynchronously();
}

}  // namespace syncer
