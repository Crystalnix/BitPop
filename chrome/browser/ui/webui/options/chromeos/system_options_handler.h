// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_OPTIONS_CHROMEOS_SYSTEM_OPTIONS_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_OPTIONS_CHROMEOS_SYSTEM_OPTIONS_HANDLER_H_
#pragma once

#include "base/memory/weak_ptr.h"
#include "base/compiler_specific.h"
#include "chrome/browser/chromeos/device_hierarchy_observer.h"
#include "chrome/browser/ui/webui/options/options_ui.h"

namespace base {
class DictionaryValue;
}

// ChromeOS system options page UI handler.
class SystemOptionsHandler
  : public OptionsPageUIHandler,
    public chromeos::DeviceHierarchyObserver,
    public base::SupportsWeakPtr<SystemOptionsHandler> {
 public:
  SystemOptionsHandler();
  virtual ~SystemOptionsHandler();

  // OptionsPageUIHandler implementation.
  virtual void GetLocalizedValues(
      base::DictionaryValue* localized_strings) OVERRIDE;
  virtual void Initialize() OVERRIDE;

  virtual void RegisterMessages() OVERRIDE;

  // DeviceHierarchyObserver implementation.
  virtual void DeviceHierarchyChanged() OVERRIDE;

  // Called when the accessibility checkbox values are changed.
  // |args| will contain the checkbox checked state as a string
  // ("true" or "false").
  void SpokenFeedbackChangeCallback(const base::ListValue* args);
  void HighContrastChangeCallback(const base::ListValue* args);
  void ScreenMagnifierChangeCallback(const base::ListValue* args);
  void VirtualKeyboardChangeCallback(const base::ListValue* args);

  // Called when the System configuration screen is used to adjust
  // the screen brightness.
  // |args| will be an empty list.
  void DecreaseScreenBrightnessCallback(const base::ListValue* args);
  void IncreaseScreenBrightnessCallback(const base::ListValue* args);

 private:
  // Check for input devices.
  void CheckTouchpadExists();
  void CheckMouseExists();

  // Callback for input device checks.
  void TouchpadExists(bool* exists);
  void MouseExists(bool* exists);

  DISALLOW_COPY_AND_ASSIGN(SystemOptionsHandler);
};

#endif  // CHROME_BROWSER_UI_WEBUI_OPTIONS_CHROMEOS_SYSTEM_OPTIONS_HANDLER_H_
