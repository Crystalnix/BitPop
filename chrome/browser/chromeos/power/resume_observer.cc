// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/power/resume_observer.h"

#include "chrome/browser/extensions/system/system_api.h"
#include "chromeos/dbus/dbus_thread_manager.h"

namespace chromeos {

ResumeObserver::ResumeObserver() {
  DBusThreadManager::Get()->GetPowerManagerClient()->AddObserver(this);
}

ResumeObserver::~ResumeObserver() {
  DBusThreadManager::Get()->GetPowerManagerClient()->RemoveObserver(this);
}

void ResumeObserver::SystemResumed() {
  extensions::DispatchWokeUpEvent();
}

}  // namespace chromeos
