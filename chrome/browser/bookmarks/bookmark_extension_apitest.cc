// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_apitest.h"

// Flaky test, http://crbug.com/89762.
IN_PROC_BROWSER_TEST_F(ExtensionApiTest, FLAKY_Bookmarks) {
  ASSERT_TRUE(RunExtensionTest("bookmarks")) << message_;
}
