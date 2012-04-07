// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_apitest.h"
#include "net/base/mock_host_resolver.h"

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, CrossOriginXHRBackgroundPage) {
  host_resolver()->AddRule("*.com", "127.0.0.1");
  ASSERT_TRUE(StartTestServer());
  ASSERT_TRUE(RunExtensionTest("cross_origin_xhr/background_page")) << message_;
}

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, CrossOriginXHRAllURLs) {
  host_resolver()->AddRule("*.com", "127.0.0.1");
  ASSERT_TRUE(StartTestServer());
  ASSERT_TRUE(RunExtensionTest("cross_origin_xhr/all_urls")) << message_;
}

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, CrossOriginXHRContentScript) {
  host_resolver()->AddRule("*.com", "127.0.0.1");
  ASSERT_TRUE(StartTestServer());
  ASSERT_TRUE(RunExtensionTest("cross_origin_xhr/content_script")) << message_;
}

// Flaky on Mac 10.5, crbug.com/105179.
#if defined(OS_MACOSX)
#define MAYBE_CrossOriginXHRFileAccess FLAKY_CrossOriginXHRFileAccess
#else
#define MAYBE_CrossOriginXHRFileAccess CrossOriginXHRFileAccess
#endif

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, MAYBE_CrossOriginXHRFileAccess) {
  ASSERT_TRUE(RunExtensionTest("cross_origin_xhr/file_access")) << message_;
}

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, CrossOriginXHRNoFileAccess) {
  ASSERT_TRUE(RunExtensionTestNoFileAccess(
      "cross_origin_xhr/no_file_access")) << message_;
}
