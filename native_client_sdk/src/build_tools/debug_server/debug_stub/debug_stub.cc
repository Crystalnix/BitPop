/* Copyright (c) 2012 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "native_client/src/debug_server/debug_stub/debug_stub.h"

void NaClDebugStubInit() {
  NaClDebugStubPlatformInit();
}

void NaClDebugStubFini() {
  NaClDebugStubPlatformFini();
}

