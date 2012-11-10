// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_CLOUD_PRINT_PRIVATE_CLOUD_PRINT_PRIVATE_API_H_
#define CHROME_BROWSER_EXTENSIONS_API_CLOUD_PRINT_PRIVATE_CLOUD_PRINT_PRIVATE_API_H_

#include <string>
#include "chrome/browser/extensions/extension_function.h"

namespace extensions {

class CloudPrintSetCredentialsFunction : public AsyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("cloudPrintPrivate.setCredentials");

  CloudPrintSetCredentialsFunction();

  // For use only in tests - sets a flag that can cause this function to not
  // actually set the credentials but instead simply reflect the passed in
  // arguments appended together as one string back in results_.
  static void SetTestMode(bool test_mode_enabled);

 protected:
  virtual ~CloudPrintSetCredentialsFunction();

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_CLOUD_PRINT_PRIVATE_CLOUD_PRINT_PRIVATE_API_H_
