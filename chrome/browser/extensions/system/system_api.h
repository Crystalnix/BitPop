// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This extension API contains system-wide preferences and functions that shall
// be only available to component extensions.

#ifndef CHROME_BROWSER_EXTENSIONS_SYSTEM_SYSTEM_API_H_
#define CHROME_BROWSER_EXTENSIONS_SYSTEM_SYSTEM_API_H_

#include "chrome/browser/extensions/extension_function.h"

namespace extensions {

class GetIncognitoModeAvailabilityFunction : public SyncExtensionFunction {
 public:
  virtual ~GetIncognitoModeAvailabilityFunction() {}
  virtual bool RunImpl() OVERRIDE;
  DECLARE_EXTENSION_FUNCTION_NAME("systemPrivate.getIncognitoModeAvailability")
};

// API function which returns the status of system update.
class GetUpdateStatusFunction : public SyncExtensionFunction {
 public:
  virtual ~GetUpdateStatusFunction() {}
  virtual bool RunImpl() OVERRIDE;
  DECLARE_EXTENSION_FUNCTION_NAME("systemPrivate.getUpdateStatus")
};

void DispatchBrightnessChangedEvent(int brightness, bool user_initiated);
void DispatchVolumeChangedEvent(double volume, bool is_volume_muted);
void DispatchScreenUnlockedEvent();
void DispatchWokeUpEvent();

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_SYSTEM_SYSTEM_API_H_
