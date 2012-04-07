// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Test cases for PPAPI Dev interfaces.
//

#include <string.h>

#include "native_client/src/include/nacl_macros.h"
#include "native_client/src/shared/platform/nacl_check.h"
#include "native_client/src/shared/ppapi_proxy/plugin_nacl_file.h"
#include "native_client/tests/ppapi_test_lib/get_browser_interface.h"
#include "native_client/tests/ppapi_test_lib/test_interface.h"
#include "ppapi/c/pp_bool.h"
#include "ppapi/c/pp_errors.h"
#include "ppapi/c/ppb_core.h"

#include "ppapi/c/dev/ppb_cursor_control_dev.h"

namespace {

////////////////////////////////////////////////////////////////////////////////
// Test Cases
////////////////////////////////////////////////////////////////////////////////

void TestGetDevInterfaces() {
  // This test is run as a simple embedded .nexe with --enable-nacl. It should
  // have access to all dev interfaces. Only test one to make the test more
  // robust as interfaces change.
  EXPECT(GetBrowserInterface(PPB_CURSOR_CONTROL_DEV_INTERFACE) != NULL);

  TEST_PASSED;
}

}  // namespace

void SetupTests() {
  RegisterTest("TestGetDevInterfaces", TestGetDevInterfaces);
}

void SetupPluginInterfaces() {
  // none
}
