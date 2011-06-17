// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_APP_BREAKPAD_MAC_H_
#define CHROME_APP_BREAKPAD_MAC_H_
#pragma once

// This header defines the Chrome entry points for Breakpad integration.

// Initializes Breakpad.
void InitCrashReporter();

// Give Breakpad a chance to store information about the current process.
// Extra information requires a parsed command line, so call this after
// CommandLine::Init has been called.
void InitCrashProcessInfo();

// Is Breakpad enabled?
bool IsCrashReporterEnabled();

// Call on clean process shutdown.
void DestructCrashReporter();

#ifdef __OBJC__

#include "base/memory/scoped_nsobject.h"

@class NSString;

// Set and clear meta information for Minidump.
// IMPORTANT: On OS X, the key/value pairs are sent to the crash server
// out of bounds and not recorded on disk in the minidump, this means
// that if you look at the minidump file locally you won't see them!
void SetCrashKeyValue(NSString* key, NSString* value);
void ClearCrashKeyValue(NSString* key);

class ScopedCrashKey {
 public:
  ScopedCrashKey(NSString* key, NSString* value);
  ~ScopedCrashKey();

 private:
  scoped_nsobject<NSString> crash_key_;
};

#endif  // __OBJC__

#endif  // CHROME_APP_BREAKPAD_MAC_H_
