// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "build/build_config.h"
#include "chrome/browser/first_run/upgrade_util.h"

// The entry point for all invocations of Chromium, browser and renderer. On
// windows, this does nothing but load chrome.dll and invoke its entry point in
// order to make it easy to update the app from GoogleUpdate. We don't need
// that extra layer with on linux.

extern "C" {
int ChromeMain(int argc, const char** argv);
}

int main(int argc, const char** argv) {
  int return_code = ChromeMain(argc, argv);

#if defined(OS_LINUX)
  // Launch a new instance if we're shutting down because we detected an
  // upgrade in the persistent mode.
  upgrade_util::RelaunchChromeBrowserWithNewCommandLineIfNeeded();
#endif

  return return_code;
}
