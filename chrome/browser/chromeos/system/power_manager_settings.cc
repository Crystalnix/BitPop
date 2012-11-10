// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/system/power_manager_settings.h"

#include "base/bind.h"
#include "base/chromeos/chromeos_version.h"
#include "base/file_path.h"
#include "base/file_util.h"
#include "base/stringprintf.h"
#include "content/public/browser/browser_thread.h"

using content::BrowserThread;

namespace {

const char kLockOnIdleSuspendPath[] =
    "/var/lib/power_manager/lock_on_idle_suspend";

void EnableScreenLockOnFileThread(bool enable) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));

  if (base::chromeos::IsRunningOnChromeOS()) {
    std::string config = base::StringPrintf("%d", enable);
    file_util::WriteFile(FilePath(kLockOnIdleSuspendPath),
                         config.c_str(),
                         config.size());
  }
}

}  // namespace

namespace chromeos {
namespace system {
namespace power_manager_settings {

void EnableScreenLock(bool enable) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  // Run this on the FILE thread.
  BrowserThread::PostTask(
      BrowserThread::FILE, FROM_HERE,
      base::Bind(&EnableScreenLockOnFileThread, enable));
}

}  // namespace power_manager_settings
}  // namespace system
}  // namespace chromeos
