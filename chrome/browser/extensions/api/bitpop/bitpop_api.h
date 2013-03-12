// Copyright (c) 2013 House of Life Property ltd. All rights reserved.
// Copyright (c) 2013 Crystalnix <vgachkaylo@crystalnix.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_BITPOP_BITPOP_API_H_
#define CHROME_BROWSER_EXTENSIONS_API_BITPOP_BITPOP_API_H_

#include <string>
#include "chrome/browser/extensions/extension_function.h"

class BitpopGetSyncStatusFunction : public SyncExtensionFunction {
 public:
  virtual bool RunImpl() OVERRIDE;
  DECLARE_EXTENSION_FUNCTION_NAME("bitpop.getSyncStatus");
 protected:
  virtual ~BitpopGetSyncStatusFunction() {}
};

class BitpopLaunchFacebookSyncFunction : public SyncExtensionFunction {
 public:
  virtual bool RunImpl() OVERRIDE;
  DECLARE_EXTENSION_FUNCTION_NAME("bitpop.launchFacebookSync");
 protected:
  virtual ~BitpopLaunchFacebookSyncFunction() {}
};

#endif	// CHROME_BROWSER_EXTENSIONS_API_BITPOP_BITPOP_API_H_
