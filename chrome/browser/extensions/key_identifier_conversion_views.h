// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_KEY_IDENTIFIER_CONVERSION_VIEWS_H_
#define CHROME_BROWSER_EXTENSIONS_KEY_IDENTIFIER_CONVERSION_VIEWS_H_

#include "ui/base/keycodes/keyboard_codes.h"

#include <string>

namespace ui {
class KeyEvent;
}

// Convert a KeyIdentifer (see Section 6.3.3 here:
// http://www.w3.org/TR/DOM-Level-3-Events/#keyset-keyidentifiers)
// to a ui::KeyEvent.
const ui::KeyEvent& KeyEventFromKeyIdentifier(
    const std::string& key_identifier);

#endif  // CHROME_BROWSER_EXTENSIONS_KEY_IDENTIFIER_CONVERSION_VIEWS_H_
