// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_INSTALL_DIALOG_H_
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_INSTALL_DIALOG_H_

#include <vector>

#include "base/memory/ref_counted.h"
#include "base/string16.h"
#include "chrome/browser/extensions/extension_install_prompt.h"

namespace base {
class DictionaryValue;
}

void ShowExtensionInstallDialog(gfx::NativeWindow parent,
                                content::PageNavigator* navigator,
                                ExtensionInstallPrompt::Delegate* delegate,
                                const ExtensionInstallPrompt::Prompt& prompt);

// The implementations of this function are platform-specific.
void ShowExtensionInstallDialogImpl(
    gfx::NativeWindow parent,
    content::PageNavigator* navigator,
    ExtensionInstallPrompt::Delegate* delegate,
    const ExtensionInstallPrompt::Prompt& prompt);

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_INSTALL_DIALOG_H_
