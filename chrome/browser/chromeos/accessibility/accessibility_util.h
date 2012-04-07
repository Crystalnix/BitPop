// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_ACCESSIBILITY_ACCESSIBILITY_UTIL_H_
#define CHROME_BROWSER_CHROMEOS_ACCESSIBILITY_ACCESSIBILITY_UTIL_H_
#pragma once

namespace content {
class WebUI;
}

namespace chromeos {
namespace accessibility {

// Enable or disable accessibility. Enabling accessibility installs the
// ChromeVox component extension.  If this is being called in a login/oobe
// login screen, pass the WebUI object in login_web_ui so that ChromeVox
// can be injected directly into that screen, otherwise it should be NULL.
void EnableAccessibility(bool enabled, content::WebUI* login_web_ui);

// Enable or disable the high contrast mode for Chrome.
void EnableHighContrast(bool enabled);

// Enable or disable the screen magnifier.
void EnableScreenMagnifier(bool enabled);

// Enable or disable the virtual keyboard.
void EnableVirtualKeyboard(bool enabled);

// Toggles whether Chrome OS accessibility is on or off. See docs for
// EnableAccessibility, above.
void ToggleAccessibility(content::WebUI* login_web_ui);

// Speaks the specified string.
void Speak(const char* speak_str);

// Returns true if Accessibility is enabled, or false if not.
bool IsAccessibilityEnabled();

}  // namespace accessibility
}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_ACCESSIBILITY_ACCESSIBILITY_UTIL_H_
