// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_COMMON_BINDINGS_POLICY_H_
#define CONTENT_COMMON_BINDINGS_POLICY_H_
#pragma once

// This is a utility class that specifies flag values for the types of
// JavaScript bindings exposed to renderers.
class BindingsPolicy {
 public:
  enum {
    // HTML-based UI bindings that allows he js content to send JSON-encoded
    // data back to the browser process.
    WEB_UI = 1 << 0,
    // DOM automation bindings that allows the js content to send JSON-encoded
    // data back to automation in the parent process.  (By default this isn't
    // allowed unless the app has been started up with the --dom-automation
    // switch.)
    DOM_AUTOMATION = 1 << 1,
    // Bindings that allow access to the external host (through automation).
    EXTERNAL_HOST = 1 << 2,
    // Special bindings with privileged APIs for code running in the extension
    // process.
    EXTENSION = 1 << 3,
  };

  static bool is_web_ui_enabled(int flags) {
    return (flags & WEB_UI) != 0;
  }
  static bool is_dom_automation_enabled(int flags) {
    return (flags & DOM_AUTOMATION) != 0;
  }
  static bool is_external_host_enabled(int flags) {
    return (flags & EXTERNAL_HOST) != 0;
  }
  static bool is_extension_enabled(int flags) {
    return (flags & EXTENSION) != 0;
  }
};

#endif  // CONTENT_COMMON_BINDINGS_POLICY_H_
