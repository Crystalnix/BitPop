// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/at_exit.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "sandbox/tests/common/controller.h"

int wmain(int argc, wchar_t **argv) {
  // The exit manager is in charge of calling the dtors of singleton objects.
  base::AtExitManager exit_manager;

  if (argc >= 2) {
    if (0 == _wcsicmp(argv[1], L"-child"))
      // This instance is a child, not the test.
      return sandbox::DispatchCall(argc, argv);
  }

  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
