// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/chromeos_switches.h"

namespace chromeos {
namespace switches {

// Forces the stub implementation of dbus clients.
const char kDbusStub[] = "dbus-stub";

// Sends test messages on first call to RequestUpdate (stub only).
const char kSmsTestMessages[] = "sms-test-messages";

}  // namespace switches
}  // namespace chromeos
