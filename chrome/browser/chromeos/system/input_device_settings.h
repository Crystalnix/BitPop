// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_SYSTEM_INPUT_DEVICE_SETTINGS_H_
#define CHROME_BROWSER_CHROMEOS_SYSTEM_INPUT_DEVICE_SETTINGS_H_
#pragma once

namespace chromeos {
namespace system {

namespace pointer_settings {

// Sets the pointer sensitivity in the range [1, 5].
void SetSensitivity(int value);

}  // namespace pointer_settings

namespace touchpad_settings {

bool TouchpadExists();

// Turns tap to click on / off.
void SetTapToClick(bool enabled);

}  // namespace touchpad_settings

namespace mouse_settings {

bool MouseExists();

void SetPrimaryButtonRight(bool right);

}  // namespace mouse_settings

}  // namespace system
}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_SYSTEM_INPUT_DEVICE_SETTINGS_H_
