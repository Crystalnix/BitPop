// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_ACCESSIBILITY_BROWSER_ACCESSIBILITY_STATE_H_
#define CONTENT_BROWSER_ACCESSIBILITY_BROWSER_ACCESSIBILITY_STATE_H_
#pragma once

#include "base/basictypes.h"
#include "base/timer.h"
#include "content/common/content_export.h"

template <typename T> struct DefaultSingletonTraits;

// The BrowserAccessibilityState class is used to determine if Chrome should be
// customized for users with assistive technology, such as screen readers. We
// modify the behavior of certain user interfaces to provide a better experience
// for screen reader users. The way we detect a screen reader program is
// different for each platform.
//
// Screen Reader Detection
// (1) On windows many screen reader detection mechinisms will give false
// positives like relying on the SPI_GETSCREENREADER system parameter. In Chrome
// we attempt to dynamically detect a MSAA client screen reader by calling
// NotifiyWinEvent in NativeWidgetWin with a custom ID and wait to see if the ID
// is requested by a subsequent call to WM_GETOBJECT.
// (2) On mac we detect dynamically if VoiceOver is running.  We rely upon the
// undocumented accessibility attribute @"AXEnhancedUserInterface" which is set
// when VoiceOver is launched and unset when VoiceOver is closed.  This is an
// improvement over reading defaults preference values (which has no callback
// mechanism).
class CONTENT_EXPORT BrowserAccessibilityState {
 public:
  // Returns the singleton instance.
  static BrowserAccessibilityState* GetInstance();

  ~BrowserAccessibilityState();

  // Called when accessibility is enabled manually (via command-line flag).
  void OnAccessibilityEnabledManually();

  // Called when screen reader client is detected.
  void OnScreenReaderDetected();

  // Returns true if the browser should be customized for accessibility.
  bool IsAccessibleBrowser();

  // Called a short while after startup to allow time for the accessibility
  // state to be determined. Updates a histogram with the current state.
  void UpdateHistogram();

 private:
  BrowserAccessibilityState();
  friend struct DefaultSingletonTraits<BrowserAccessibilityState>;

  // Set to true when full accessibility features should be enabled.
  bool accessibility_enabled_;

  // Timer to update the histogram a short while after startup.
  base::OneShotTimer<BrowserAccessibilityState> update_histogram_timer_;

  DISALLOW_COPY_AND_ASSIGN(BrowserAccessibilityState);
};

#endif  // CONTENT_BROWSER_ACCESSIBILITY_BROWSER_ACCESSIBILITY_STATE_H_
