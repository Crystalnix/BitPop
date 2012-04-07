// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/file_path.h"
#include "base/file_util.h"
#include "base/scoped_temp_dir.h"
#include "base/test/thread_test_helper.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/testing_profile.h"
#include "content/browser/in_process_webkit/dom_storage_context.h"
#include "content/browser/in_process_webkit/webkit_context.h"

using content::BrowserThread;

typedef InProcessBrowserTest DOMStorageBrowserTest;

// In proc browser test is needed here because ClearLocalState indirectly calls
// WebKit's isMainThread through WebSecurityOrigin->SecurityOrigin.
IN_PROC_BROWSER_TEST_F(DOMStorageBrowserTest, ClearLocalState) {
  // Create test files.
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  FilePath domstorage_dir = temp_dir.path().Append(
      DOMStorageContext::kLocalStorageDirectory);
  ASSERT_TRUE(file_util::CreateDirectory(domstorage_dir));

  FilePath::StringType file_name_1(FILE_PATH_LITERAL("http_foo_0"));
  file_name_1.append(DOMStorageContext::kLocalStorageExtension);
  FilePath::StringType file_name_2(FILE_PATH_LITERAL("chrome-extension_foo_0"));
  file_name_2.append(DOMStorageContext::kLocalStorageExtension);
  FilePath temp_file_path_1 = domstorage_dir.Append(file_name_1);
  FilePath temp_file_path_2 = domstorage_dir.Append(file_name_2);

  ASSERT_EQ(1, file_util::WriteFile(temp_file_path_1, ".", 1));
  ASSERT_EQ(1, file_util::WriteFile(temp_file_path_2, "o", 1));

  // Create the scope which will ensure we run the destructor of the webkit
  // context which should trigger the clean up.
  {
    TestingProfile profile;
    WebKitContext *webkit_context = profile.GetWebKitContext();
    webkit_context->dom_storage_context()->
        set_data_path_for_testing(temp_dir.path());
    webkit_context->set_clear_local_state_on_exit(true);
  }
  // Make sure we wait until the destructor has run.
  scoped_refptr<base::ThreadTestHelper> helper(
      new base::ThreadTestHelper(
          BrowserThread::GetMessageLoopProxyForThread(
              BrowserThread::WEBKIT_DEPRECATED)));
  ASSERT_TRUE(helper->Run());

  // Because we specified https for scheme to be skipped the second file
  // should survive and the first go into vanity.
  ASSERT_FALSE(file_util::PathExists(temp_file_path_1));
  ASSERT_TRUE(file_util::PathExists(temp_file_path_2));
}
