// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/file_util.h"
#include "base/scoped_temp_dir.h"
#include "content/browser/browser_thread_impl.h"
#include "content/browser/in_process_webkit/indexed_db_context_impl.h"
#include "content/public/common/url_constants.h"
#include "content/public/test/test_browser_context.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "webkit/database/database_util.h"
#include "webkit/quota/mock_special_storage_policy.h"
#include "webkit/quota/special_storage_policy.h"

using content::BrowserContext;
using content::BrowserThread;
using content::BrowserThreadImpl;
using webkit_database::DatabaseUtil;

class IndexedDBTest : public testing::Test {
 public:
  IndexedDBTest()
      : message_loop_(MessageLoop::TYPE_IO),
        webkit_thread_(BrowserThread::WEBKIT_DEPRECATED, &message_loop_),
        file_thread_(BrowserThread::FILE_USER_BLOCKING, &message_loop_),
        io_thread_(BrowserThread::IO, &message_loop_) {
  }

 protected:
  MessageLoop message_loop_;

 private:
  BrowserThreadImpl webkit_thread_;
  BrowserThreadImpl file_thread_;
  BrowserThreadImpl io_thread_;
};

TEST_F(IndexedDBTest, ClearSessionOnlyDatabases) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  FilePath normal_path;
  FilePath session_only_path;

  // Create the scope which will ensure we run the destructor of the webkit
  // context which should trigger the clean up.
  {
    content::TestBrowserContext browser_context;

    const GURL kNormalOrigin("http://normal/");
    const GURL kSessionOnlyOrigin("http://session-only/");
    scoped_refptr<quota::MockSpecialStoragePolicy> special_storage_policy =
        new quota::MockSpecialStoragePolicy;
    special_storage_policy->AddSessionOnly(kSessionOnlyOrigin);

    // Create some indexedDB paths.
    // With the levelDB backend, these are directories.
    IndexedDBContextImpl* idb_context =
        static_cast<IndexedDBContextImpl*>(
            BrowserContext::GetIndexedDBContext(&browser_context));

    // Override the storage policy with our own.
    idb_context->special_storage_policy_ = special_storage_policy;
    idb_context->set_data_path_for_testing(temp_dir.path());

    normal_path = idb_context->GetFilePathForTesting(
        DatabaseUtil::GetOriginIdentifier(kNormalOrigin));
    session_only_path = idb_context->GetFilePathForTesting(
        DatabaseUtil::GetOriginIdentifier(kSessionOnlyOrigin));
    ASSERT_TRUE(file_util::CreateDirectory(normal_path));
    ASSERT_TRUE(file_util::CreateDirectory(session_only_path));
    message_loop_.RunAllPending();
  }

  message_loop_.RunAllPending();

  EXPECT_TRUE(file_util::DirectoryExists(normal_path));
  EXPECT_FALSE(file_util::DirectoryExists(session_only_path));
}

TEST_F(IndexedDBTest, SetForceKeepSessionState) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  FilePath normal_path;
  FilePath session_only_path;

  // Create the scope which will ensure we run the destructor of the webkit
  // context.
  {
    content::TestBrowserContext browser_context;

    const GURL kNormalOrigin("http://normal/");
    const GURL kSessionOnlyOrigin("http://session-only/");
    scoped_refptr<quota::MockSpecialStoragePolicy> special_storage_policy =
        new quota::MockSpecialStoragePolicy;
    special_storage_policy->AddSessionOnly(kSessionOnlyOrigin);

    // Create some indexedDB paths.
    // With the levelDB backend, these are directories.
    IndexedDBContextImpl* idb_context =
        static_cast<IndexedDBContextImpl*>(
            BrowserContext::GetIndexedDBContext(&browser_context));

    // Override the storage policy with our own.
    idb_context->special_storage_policy_ = special_storage_policy;
    idb_context->set_data_path_for_testing(temp_dir.path());

    // Save session state. This should bypass the destruction-time deletion.
    idb_context->SetForceKeepSessionState();

    normal_path = idb_context->GetFilePathForTesting(
        DatabaseUtil::GetOriginIdentifier(kNormalOrigin));
    session_only_path = idb_context->GetFilePathForTesting(
        DatabaseUtil::GetOriginIdentifier(kSessionOnlyOrigin));
    ASSERT_TRUE(file_util::CreateDirectory(normal_path));
    ASSERT_TRUE(file_util::CreateDirectory(session_only_path));
    message_loop_.RunAllPending();
  }

  // Make sure we wait until the destructor has run.
  message_loop_.RunAllPending();

  // No data was cleared because of SetForceKeepSessionState.
  EXPECT_TRUE(file_util::DirectoryExists(normal_path));
  EXPECT_TRUE(file_util::DirectoryExists(session_only_path));
}
