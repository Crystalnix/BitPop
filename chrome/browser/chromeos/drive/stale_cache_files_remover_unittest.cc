// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <vector>

#include "base/bind.h"
#include "base/file_path.h"
#include "base/file_util.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "base/stringprintf.h"
#include "base/threading/sequenced_worker_pool.h"
#include "base/values.h"
#include "chrome/browser/chromeos/drive/drive.pb.h"
#include "chrome/browser/chromeos/drive/drive_file_system.h"
#include "chrome/browser/chromeos/drive/drive_test_util.h"
#include "chrome/browser/chromeos/drive/drive_webapps_registry.h"
#include "chrome/browser/chromeos/drive/fake_free_disk_space_getter.h"
#include "chrome/browser/chromeos/drive/mock_directory_change_observer.h"
#include "chrome/browser/chromeos/drive/mock_drive_cache_observer.h"
#include "chrome/browser/chromeos/drive/stale_cache_files_remover.h"
#include "chrome/browser/google_apis/drive_api_parser.h"
#include "chrome/browser/google_apis/mock_drive_service.h"
#include "chrome/browser/google_apis/time_util.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/test/test_browser_thread.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::AtLeast;
using ::testing::Eq;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::StrictMock;
using ::testing::_;

namespace drive {
namespace {

const int64 kLotsOfSpace = kMinFreeSpace * 10;

}  // namespace

class StaleCacheFilesRemoverTest : public testing::Test {
 protected:
  StaleCacheFilesRemoverTest()
      : ui_thread_(content::BrowserThread::UI, &message_loop_),
        io_thread_(content::BrowserThread::IO),
        cache_(NULL),
        file_system_(NULL),
        mock_drive_service_(NULL),
        drive_webapps_registry_(NULL),
        root_feed_changestamp_(0) {
  }

  virtual void SetUp() OVERRIDE {
    io_thread_.StartIOThread();

    profile_.reset(new TestingProfile);

    // Allocate and keep a pointer to the mock, and inject it into the
    // DriveFileSystem object, which will own the mock object.
    mock_drive_service_ = new StrictMock<google_apis::MockDriveService>;

    fake_free_disk_space_getter_.reset(new FakeFreeDiskSpaceGetter);

    scoped_refptr<base::SequencedWorkerPool> pool =
        content::BrowserThread::GetBlockingPool();
    blocking_task_runner_ =
        pool->GetSequencedTaskRunner(pool->GetSequenceToken());

    // Likewise, this will be owned by DriveFileSystem.
    cache_ = new DriveCache(
        DriveCache::GetCacheRootPath(profile_.get()),
        blocking_task_runner_,
        fake_free_disk_space_getter_.get());

    drive_webapps_registry_.reset(new DriveWebAppsRegistry);

    ASSERT_FALSE(file_system_);
    file_system_ = new DriveFileSystem(profile_.get(),
                                       cache_,
                                       mock_drive_service_,
                                       NULL,  // drive_uploader
                                       drive_webapps_registry_.get(),
                                       blocking_task_runner_);

    mock_cache_observer_.reset(new StrictMock<MockDriveCacheObserver>);
    cache_->AddObserver(mock_cache_observer_.get());

    mock_directory_observer_.reset(new StrictMock<MockDirectoryChangeObserver>);
    file_system_->AddObserver(mock_directory_observer_.get());

    file_system_->Initialize();
    cache_->RequestInitializeForTesting();

    stale_cache_files_remover_.reset(new StaleCacheFilesRemover(file_system_,
                                                                cache_));

    google_apis::test_util::RunBlockingPoolTask();
  }

  virtual void TearDown() OVERRIDE {
    ASSERT_TRUE(file_system_);
    stale_cache_files_remover_.reset();
    EXPECT_CALL(*mock_drive_service_, CancelAll()).Times(1);
    delete file_system_;
    file_system_ = NULL;
    delete mock_drive_service_;
    mock_drive_service_ = NULL;
    cache_->Destroy();
    // The cache destruction requires to post a task to the blocking pool.
    google_apis::test_util::RunBlockingPoolTask();

    profile_.reset(NULL);
  }

  MessageLoopForUI message_loop_;
  // The order of the test threads is important, do not change the order.
  // See also content/browser/browser_thread_impl.cc.
  content::TestBrowserThread ui_thread_;
  content::TestBrowserThread io_thread_;
  scoped_refptr<base::SequencedTaskRunner> blocking_task_runner_;
  scoped_ptr<TestingProfile> profile_;
  DriveCache* cache_;
  DriveFileSystem* file_system_;
  StrictMock<google_apis::MockDriveService>* mock_drive_service_;
  scoped_ptr<DriveWebAppsRegistry> drive_webapps_registry_;
  scoped_ptr<FakeFreeDiskSpaceGetter> fake_free_disk_space_getter_;
  scoped_ptr<StrictMock<MockDriveCacheObserver> > mock_cache_observer_;
  scoped_ptr<StrictMock<MockDirectoryChangeObserver> > mock_directory_observer_;
  scoped_ptr<StaleCacheFilesRemover> stale_cache_files_remover_;

  int root_feed_changestamp_;
};

TEST_F(StaleCacheFilesRemoverTest, RemoveStaleCacheFiles) {
  FilePath dummy_file =
      google_apis::test_util::GetTestFilePath("gdata/root_feed.json");
  std::string resource_id("pdf:1a2b3c");
  std::string md5("abcdef0123456789");

  fake_free_disk_space_getter_->set_fake_free_disk_space(kLotsOfSpace);

  // Create a stale cache file.
  DriveFileError error = DRIVE_FILE_OK;
  cache_->Store(resource_id, md5, dummy_file, DriveCache::FILE_OPERATION_COPY,
                base::Bind(&test_util::CopyErrorCodeFromFileOperationCallback,
                           &error));
  google_apis::test_util::RunBlockingPoolTask();
  EXPECT_EQ(DRIVE_FILE_OK, error);

  // Verify that the cache file exists.
  FilePath path = cache_->GetCacheFilePath(resource_id,
                                           md5,
                                           DriveCache::CACHE_TYPE_TMP,
                                           DriveCache::CACHED_FILE_FROM_SERVER);
  EXPECT_TRUE(file_util::PathExists(path));

  // Verify that the corresponding file entry doesn't exist.
  EXPECT_CALL(*mock_drive_service_, GetAccountMetadata(_)).Times(2);
  EXPECT_CALL(*mock_drive_service_, GetResourceList(Eq(GURL()), _, "", _, _, _))
      .Times(2);

  FilePath unused;
  scoped_ptr<DriveEntryProto> entry_proto;
  file_system_->GetEntryInfoByResourceId(
      resource_id,
      base::Bind(&test_util::CopyResultsFromGetEntryInfoWithFilePathCallback,
                 &error,
                 &unused,
                 &entry_proto));
  google_apis::test_util::RunBlockingPoolTask();
  EXPECT_EQ(DRIVE_FILE_ERROR_NOT_FOUND, error);

  file_system_->GetEntryInfoByPath(
      path,
      base::Bind(&test_util::CopyResultsFromGetEntryInfoCallback,
                 &error,
                 &entry_proto));
  google_apis::test_util::RunBlockingPoolTask();
  EXPECT_EQ(DRIVE_FILE_ERROR_NOT_FOUND, error);
  EXPECT_FALSE(entry_proto.get());

  // Load a root feed again to kick the StaleCacheFilesRemover.
  file_system_->Reload();

  // Wait for StaleCacheFilesRemover to finish cleaning up the stale file.
  google_apis::test_util::RunBlockingPoolTask();

  // Verify that the cache file is deleted.
  path = cache_->GetCacheFilePath(resource_id,
                                  md5,
                                  DriveCache::CACHE_TYPE_TMP,
                                  DriveCache::CACHED_FILE_FROM_SERVER);
  EXPECT_FALSE(file_util::PathExists(path));
}

}   // namespace drive
