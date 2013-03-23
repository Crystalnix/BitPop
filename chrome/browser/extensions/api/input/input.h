// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_INPUT_INPUT_H_
#define CHROME_BROWSER_EXTENSIONS_API_INPUT_INPUT_H_

#include "base/compiler_specific.h"
#include "chrome/browser/extensions/extension_function.h"

namespace extensions {

// Note that this experimental API is currently only available for
// TOOLKIT_VIEWS (see chrome/chrome_browser.gypi).
//
// We may eventually support other platforms by adding the necessary
// synthetic event distribution code to this Function.
class SendKeyboardEventInputFunction : public SyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME(
      "experimental.input.virtualKeyboard.sendKeyboardEvent");

 protected:
  virtual ~SendKeyboardEventInputFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_INPUT_INPUT_H_
