/* Copyright (c) 2012 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdio.h>
#include <stdlib.h>

#include <map>

#include "native_client/src/shared/platform/nacl_log.h"
#include "native_client/src/debug_server/gdb_rsp/abi.h"
#include "native_client/src/debug_server/port/platform.h"

namespace port {

//  Log a message
void IPlatform::LogInfo(const char *fmt, ...) {
  va_list argptr;
  va_start(argptr, fmt);

  NaClLogV(LOG_INFO, fmt, argptr);
}

void IPlatform::LogWarning(const char *fmt, ...) {
  va_list argptr;
  va_start(argptr, fmt);

  NaClLogV(LOG_WARNING, fmt, argptr);
}

void IPlatform::LogError(const char *fmt, ...) {
  va_list argptr;
  va_start(argptr, fmt);

  NaClLogV(LOG_ERROR, fmt, argptr);
}

}  // End of port namespace
