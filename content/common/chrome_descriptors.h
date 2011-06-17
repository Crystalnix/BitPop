// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_COMMON_CHROME_DESCRIPTORS_H_
#define CONTENT_COMMON_CHROME_DESCRIPTORS_H_
#pragma once

#include "ipc/ipc_descriptors.h"

// This is a list of global descriptor keys to be used with the
// base::GlobalDescriptors object (see base/global_descriptors_posix.h)
enum {
  kCrashDumpSignal = kPrimaryIPCChannel + 1,
  kSandboxIPCChannel = kPrimaryIPCChannel + 2,  // http://code.google.com/p/chromium/LinuxSandboxIPC
};

#endif  // CONTENT_COMMON_CHROME_DESCRIPTORS_H_
