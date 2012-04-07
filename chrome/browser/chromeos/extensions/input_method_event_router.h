// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_EXTENSIONS_INPUT_METHOD_EVENT_ROUTER_H_
#define CHROME_BROWSER_CHROMEOS_EXTENSIONS_INPUT_METHOD_EVENT_ROUTER_H_

#include "base/basictypes.h"
#include "chrome/browser/chromeos/input_method/input_method_manager.h"

namespace chromeos {

// Event router class for the input method events.
class ExtensionInputMethodEventRouter
    : public input_method::InputMethodManager::Observer {
 public:
  ExtensionInputMethodEventRouter();
  virtual ~ExtensionInputMethodEventRouter();

  // Implements input_method::InputMethodManager::Observer:
  virtual void InputMethodChanged(
      input_method::InputMethodManager* manager,
      const input_method::InputMethodDescriptor& current_input_method,
      size_t num_active_input_methods) OVERRIDE;
  virtual void ActiveInputMethodsChanged(
      input_method::InputMethodManager* manager,
      const input_method::InputMethodDescriptor& current_input_method,
      size_t num_active_input_methods) OVERRIDE;
  virtual void PropertyListChanged(
      input_method::InputMethodManager* manager,
      const input_method::ImePropertyList& current_ime_properties) OVERRIDE;

  // Returns input method name for the given XKB (X keyboard extensions in X
  // Window System) id.
  std::string GetInputMethodForXkb(const std::string& xkb_id);

 private:
  DISALLOW_COPY_AND_ASSIGN(ExtensionInputMethodEventRouter);
};

}  // namespace chromeos
#endif  // CHROME_BROWSER_CHROMEOS_EXTENSIONS_INPUT_METHOD_EVENT_ROUTER_H_
