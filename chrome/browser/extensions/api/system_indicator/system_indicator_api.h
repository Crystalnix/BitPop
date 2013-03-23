// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_SYSTEM_INDICATOR_SYSTEM_INDICATOR_API_H_
#define CHROME_BROWSER_EXTENSIONS_API_SYSTEM_INDICATOR_SYSTEM_INDICATOR_API_H_

#include "chrome/browser/extensions/api/extension_action/extension_actions_api.h"
#include "chrome/browser/extensions/extension_function.h"

namespace extensions {

class SystemIndicatorSetIconFunction : public ExtensionActionSetIconFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("systemIndicator.setIcon")

 protected:
  virtual ~SystemIndicatorSetIconFunction() {}
};

class SystemIndicatorEnableFunction : public ExtensionActionShowFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("systemIndicator.enable")

 protected:
  virtual ~SystemIndicatorEnableFunction() {}
};

class SystemIndicatorDisableFunction : public ExtensionActionHideFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("systemIndicator.disable");

 protected:
  virtual ~SystemIndicatorDisableFunction() {}
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_SYSTEM_INDICATOR_SYSTEM_INDICATOR_API_H_
