// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/basictypes.h"
#include "base/bind.h"
#include "base/file_util.h"
#include "base/location.h"
#include "base/message_loop_proxy.h"
#include "base/run_loop.h"
#include "base/threading/thread.h"
#include "chrome/browser/sync_file_system/local_file_sync_service.h"
#include "chrome/browser/sync_file_system/mock_local_change_processor.h"
#include "chrome/browser/sync_file_system/sync_file_system_test_util.h"
#include "chrome/test/base/testing_profile.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "webkit/fileapi/file_system_context.h"
#include "webkit/fileapi/syncable/canned_syncable_file_system.h"
#include "webkit/fileapi/syncable/file_change.h"
#include "webkit/fileapi/syncable/local_file_change_tracker.h"
#include "webkit/fileapi/syncable/local_file_sync_context.h"
#include "webkit/fileapi/syncable/local_file_sync_status.h"
#include "webkit/fileapi/syncable/mock_sync_status_observer.h"
#include "webkit/fileapi/syncable/sync_file_metadata.h"
#include "webkit/fileapi/syncable/sync_status_code.h"
#include "webkit/fileapi/syncable/syncable_file_system_util.h"

using fileapi::FileChange;
using fileapi::FileChangeList;
using fileapi::FileSystemURL;
using fileapi::LocalFileSyncStatus;
using fileapi::MockSyncStatusObserver;
using fileapi::SyncFileMetadata;
using fileapi::SyncFileType;
using fileapi::SyncStatusCallback;
using fileapi::SyncStatusCode;

using ::testing::_;
using ::testing::AtLeast;
using ::testing::InvokeWithoutArgs;
using ::testing::StrictMock;

namespace sync_file_system {

namespace {

const char kOrigin[] = "http://example.com";
const char kServiceName[] = "test";

void DidPrepareForProcessRemoteChange(const tracked_objects::Location& where,
                                      const base::Closure& oncompleted,
                                      SyncStatusCode expected_status,
                                      const SyncFileMetadata& expected_metadata,
                                      SyncStatusCode status,
                                      const SyncFileMetadata& metadata,
                                      const FileChangeList& changes) {
  SCOPED_TRACE(testing::Message() << where.ToString());
  ASSERT_EQ(expected_status, status);
  ASSERT_EQ(expected_metadata.file_type, metadata.file_type);
  ASSERT_EQ(expected_metadata.size, metadata.size);
  ASSERT_TRUE(changes.empty());
  oncompleted.Run();
}

void OnSyncCompleted(const tracked_objects::Location& where,
                     const base::Closure& oncompleted,
                     SyncStatusCode expected_status,
                     const FileSystemURL& expected_url,
                     SyncStatusCode status,
                     const FileSystemURL& url) {
  SCOPED_TRACE(testing::Message() << where.ToString());
  ASSERT_EQ(expected_status, status);
  ASSERT_EQ(expected_url, url);
  oncompleted.Run();
}

void OnGetFileMetadata(const tracked_objects::Location& where,
                       const base::Closure& oncompleted,
                       SyncStatusCode* status_out,
                       SyncFileMetadata* metadata_out,
                       SyncStatusCode status,
                       const SyncFileMetadata& metadata) {
  SCOPED_TRACE(testing::Message() << where.ToString());
  *status_out = status;
  *metadata_out = metadata;
  oncompleted.Run();
}

ACTION_P(MockStatusCallback, status) {
  base::MessageLoopProxy::current()->PostTask(
      FROM_HERE, base::Bind(arg3, status));
}

ACTION_P2(MockStatusCallbackAndRecordChange, status, changes) {
  base::MessageLoopProxy::current()->PostTask(
      FROM_HERE, base::Bind(arg3, status));
  changes->push_back(arg0);
}

}  // namespace

class LocalFileSyncServiceTest
    : public testing::Test,
      public LocalFileSyncService::Observer {
 protected:
  LocalFileSyncServiceTest() : num_changes_(0) {}

  ~LocalFileSyncServiceTest() {}

  virtual void SetUp() OVERRIDE {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());

    thread_helper_.SetUp();

    file_system_.reset(new fileapi::CannedSyncableFileSystem(
        GURL(kOrigin), kServiceName,
        thread_helper_.io_task_runner(),
        thread_helper_.file_task_runner()));

    local_service_.reset(new LocalFileSyncService(&profile_));

    file_system_->SetUp();

    base::RunLoop run_loop;
    SyncStatusCode status = fileapi::SYNC_STATUS_UNKNOWN;
    local_service_->MaybeInitializeFileSystemContext(
        GURL(kOrigin), kServiceName, file_system_->file_system_context(),
        AssignAndQuitCallback(&run_loop, &status));
    run_loop.Run();

    local_service_->AddChangeObserver(this);

    EXPECT_EQ(base::PLATFORM_FILE_OK, file_system_->OpenFileSystem());

    file_system_->file_system_context()->sync_context()->
        set_mock_notify_changes_duration_in_sec(0);
  }

  virtual void TearDown() OVERRIDE {
    local_service_->Shutdown();
    file_system_->TearDown();
    fileapi::RevokeSyncableFileSystem(kServiceName);

    thread_helper_.TearDown();
  }

  // LocalChangeObserver overrides.
  virtual void OnLocalChangeAvailable(int64 num_changes) {
    num_changes_ = num_changes;
  }

  void PrepareForProcessRemoteChange(
      const FileSystemURL& url,
      const tracked_objects::Location& where,
      SyncStatusCode expected_status,
      const SyncFileMetadata& expected_metadata) {
    base::RunLoop run_loop;
    local_service_->PrepareForProcessRemoteChange(
        url,
        kServiceName,
        base::Bind(&DidPrepareForProcessRemoteChange,
                   where,
                   run_loop.QuitClosure(),
                   expected_status,
                   expected_metadata));
    run_loop.Run();
  }

  SyncStatusCode ApplyRemoteChange(const FileChange& change,
                                   const FilePath& local_path,
                                   const FileSystemURL& url) {
    base::RunLoop run_loop;
    SyncStatusCode sync_status = fileapi::SYNC_STATUS_UNKNOWN;
    local_service_->ApplyRemoteChange(
        change, local_path, url,
        AssignAndQuitCallback(&run_loop, &sync_status));
    run_loop.Run();
    return sync_status;
  }

  int64 GetNumChangesInTracker() const {
    return file_system_->file_system_context()->change_tracker()->num_changes();
  }

  TestingProfile profile_;

  MultiThreadTestHelper thread_helper_;

  base::ScopedTempDir temp_dir_;

  scoped_ptr<fileapi::CannedSyncableFileSystem> file_system_;
  scoped_ptr<LocalFileSyncService> local_service_;

  int64 num_changes_;
};

// More complete tests for PrepareForProcessRemoteChange and ApplyRemoteChange
// are also in content_unittest:LocalFileSyncContextTest.
TEST_F(LocalFileSyncServiceTest, RemoteSyncStepsSimple) {
  const FileSystemURL kFile(file_system_->URL("file"));
  const FileSystemURL kDir(file_system_->URL("dir"));
  const char kTestFileData[] = "0123456789";
  const int kTestFileDataSize = static_cast<int>(arraysize(kTestFileData) - 1);

  FilePath local_path;
  ASSERT_TRUE(file_util::CreateTemporaryFileInDir(temp_dir_.path(),
                                                  &local_path));
  ASSERT_EQ(kTestFileDataSize,
            file_util::WriteFile(local_path, kTestFileData, kTestFileDataSize));

  // Run PrepareForProcessRemoteChange for kFile.
  SyncFileMetadata expected_metadata;
  expected_metadata.file_type = fileapi::SYNC_FILE_TYPE_UNKNOWN;
  expected_metadata.size = 0;
  PrepareForProcessRemoteChange(kFile, FROM_HERE,
                                fileapi::SYNC_STATUS_OK,
                                expected_metadata);

  // Run ApplyRemoteChange for kFile.
  FileChange change(FileChange::FILE_CHANGE_ADD_OR_UPDATE,
                    fileapi::SYNC_FILE_TYPE_FILE);
  EXPECT_EQ(fileapi::SYNC_STATUS_OK,
            ApplyRemoteChange(change, local_path, kFile));

  // Verify the file is synced.
  EXPECT_EQ(base::PLATFORM_FILE_OK,
            file_system_->VerifyFile(kFile, kTestFileData));

  // Run PrepareForProcessRemoteChange for kDir.
  PrepareForProcessRemoteChange(kDir, FROM_HERE,
                                fileapi::SYNC_STATUS_OK,
                                expected_metadata);

  // Run ApplyRemoteChange for kDir.
  change = FileChange(FileChange::FILE_CHANGE_ADD_OR_UPDATE,
                      fileapi::SYNC_FILE_TYPE_DIRECTORY);
  EXPECT_EQ(fileapi::SYNC_STATUS_OK,
            ApplyRemoteChange(change, FilePath(), kDir));

  // Verify the directory.
  EXPECT_EQ(base::PLATFORM_FILE_OK,
            file_system_->DirectoryExists(kDir));

  // Run PrepareForProcessRemoteChange and ApplyRemoteChange for
  // kDir once again for deletion.
  expected_metadata.file_type = fileapi::SYNC_FILE_TYPE_DIRECTORY;
  expected_metadata.size = 0;
  PrepareForProcessRemoteChange(kDir, FROM_HERE,
                                fileapi::SYNC_STATUS_OK,
                                expected_metadata);

  change = FileChange(FileChange::FILE_CHANGE_DELETE,
                      fileapi::SYNC_FILE_TYPE_UNKNOWN);
  EXPECT_EQ(fileapi::SYNC_STATUS_OK,
            ApplyRemoteChange(change, FilePath(), kDir));

  // Now the directory must have deleted.
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_FOUND,
            file_system_->DirectoryExists(kDir));
}

TEST_F(LocalFileSyncServiceTest, LocalChangeObserver) {
  const FileSystemURL kFile(file_system_->URL("file"));
  const FileSystemURL kDir(file_system_->URL("dir"));
  const char kTestFileData[] = "0123456789";
  const int kTestFileDataSize = static_cast<int>(arraysize(kTestFileData) - 1);

  EXPECT_EQ(base::PLATFORM_FILE_OK, file_system_->CreateFile(kFile));

  EXPECT_EQ(1, num_changes_);

  EXPECT_EQ(base::PLATFORM_FILE_OK, file_system_->CreateDirectory(kDir));
  EXPECT_EQ(kTestFileDataSize,
            file_system_->WriteString(kFile, kTestFileData));

  EXPECT_EQ(2, num_changes_);
}

TEST_F(LocalFileSyncServiceTest, LocalChangeObserverMultipleContexts) {
  const char kOrigin2[] = "http://foo";
  fileapi::CannedSyncableFileSystem file_system2(
      GURL(kOrigin2), kServiceName,
      thread_helper_.io_task_runner(),
      thread_helper_.file_task_runner());
  file_system2.SetUp();

  base::RunLoop run_loop;
  SyncStatusCode status = fileapi::SYNC_STATUS_UNKNOWN;
  local_service_->MaybeInitializeFileSystemContext(
      GURL(kOrigin2), kServiceName, file_system2.file_system_context(),
      AssignAndQuitCallback(&run_loop, &status));
  run_loop.Run();

  EXPECT_EQ(base::PLATFORM_FILE_OK, file_system2.OpenFileSystem());
  file_system2.file_system_context()->sync_context()->
      set_mock_notify_changes_duration_in_sec(0);

  const FileSystemURL kFile1(file_system_->URL("file1"));
  const FileSystemURL kFile2(file_system_->URL("file2"));
  const FileSystemURL kFile3(file_system2.URL("file3"));
  const FileSystemURL kFile4(file_system2.URL("file4"));

  EXPECT_EQ(base::PLATFORM_FILE_OK, file_system_->CreateFile(kFile1));
  EXPECT_EQ(base::PLATFORM_FILE_OK, file_system_->CreateFile(kFile2));
  EXPECT_EQ(base::PLATFORM_FILE_OK, file_system2.CreateFile(kFile3));
  EXPECT_EQ(base::PLATFORM_FILE_OK, file_system2.CreateFile(kFile4));

  EXPECT_EQ(4, num_changes_);

  file_system2.TearDown();
}

TEST_F(LocalFileSyncServiceTest, ProcessLocalChange_CreateFile) {
  const FileSystemURL kFile(file_system_->URL("foo"));
  const char kTestFileData[] = "0123456789";
  const int kTestFileDataSize = static_cast<int>(arraysize(kTestFileData) - 1);

  base::RunLoop run_loop;

  // We should get called OnSyncEnabled on kFile.
  StrictMock<MockSyncStatusObserver> status_observer;
  EXPECT_CALL(status_observer, OnSyncEnabled(kFile))
      .Times(AtLeast(1));
  file_system_->AddSyncStatusObserver(&status_observer);

  // Creates and writes into a file.
  EXPECT_EQ(base::PLATFORM_FILE_OK, file_system_->CreateFile(kFile));
  EXPECT_EQ(kTestFileDataSize,
            file_system_->WriteString(kFile, std::string(kTestFileData)));

  // Retrieve the expected platform_path.
  base::PlatformFileInfo info;
  FilePath platform_path;
  EXPECT_EQ(base::PLATFORM_FILE_OK,
            file_system_->GetMetadata(kFile, &info, &platform_path));

  // The local_change_processor's ApplyLocalChange should be called once
  // with ADD_OR_UPDATE change for TYPE_FILE.
  StrictMock<MockLocalChangeProcessor> local_change_processor;
  const FileChange change(FileChange::FILE_CHANGE_ADD_OR_UPDATE,
                          fileapi::SYNC_FILE_TYPE_FILE);
  EXPECT_CALL(local_change_processor,
              ApplyLocalChange(change, platform_path, kFile, _))
      .WillOnce(MockStatusCallback(fileapi::SYNC_STATUS_OK));

  local_service_->ProcessLocalChange(
      &local_change_processor,
      base::Bind(&OnSyncCompleted, FROM_HERE, run_loop.QuitClosure(),
                 fileapi::SYNC_STATUS_OK, kFile));

  run_loop.Run();

  file_system_->RemoveSyncStatusObserver(&status_observer);

  EXPECT_EQ(0, GetNumChangesInTracker());
}

TEST_F(LocalFileSyncServiceTest, ProcessLocalChange_CreateAndRemoveFile) {
  const FileSystemURL kFile(file_system_->URL("foo"));

  base::RunLoop run_loop;

  // We should get called OnSyncEnabled and OnWriteEnabled on kFile.
  StrictMock<MockSyncStatusObserver> status_observer;
  EXPECT_CALL(status_observer, OnSyncEnabled(kFile))
      .Times(AtLeast(1));
  file_system_->AddSyncStatusObserver(&status_observer);

  // Creates and then deletes a file.
  EXPECT_EQ(base::PLATFORM_FILE_OK, file_system_->CreateFile(kFile));
  EXPECT_EQ(base::PLATFORM_FILE_OK, file_system_->Remove(kFile, false));

  // The local_change_processor's ApplyLocalChange should be called once
  // with DELETE change for TYPE_FILE.
  // The file will NOT exist in the remote side and the processor might
  // return SYNC_FILE_ERROR_NOT_FOUND (as mocked).
  StrictMock<MockLocalChangeProcessor> local_change_processor;
  const FileChange change(FileChange::FILE_CHANGE_DELETE,
                          fileapi::SYNC_FILE_TYPE_FILE);
  EXPECT_CALL(local_change_processor, ApplyLocalChange(change, _, kFile, _))
      .WillOnce(MockStatusCallback(fileapi::SYNC_FILE_ERROR_NOT_FOUND));

  // The sync should succeed anyway.
  local_service_->ProcessLocalChange(
      &local_change_processor,
      base::Bind(&OnSyncCompleted, FROM_HERE, run_loop.QuitClosure(),
                 fileapi::SYNC_STATUS_OK, kFile));

  run_loop.Run();

  file_system_->RemoveSyncStatusObserver(&status_observer);

  EXPECT_EQ(0, GetNumChangesInTracker());
}

TEST_F(LocalFileSyncServiceTest, ProcessLocalChange_CreateAndRemoveDirectory) {
  const FileSystemURL kDir(file_system_->URL("foo"));

  base::RunLoop run_loop;

  // OnSyncEnabled is expected to be called at least or more than once.
  StrictMock<MockSyncStatusObserver> status_observer;
  EXPECT_CALL(status_observer, OnSyncEnabled(kDir)).Times(AtLeast(1));
  file_system_->AddSyncStatusObserver(&status_observer);

  // Creates and then deletes a directory.
  EXPECT_EQ(base::PLATFORM_FILE_OK, file_system_->CreateDirectory(kDir));
  EXPECT_EQ(base::PLATFORM_FILE_OK, file_system_->Remove(kDir, false));

  // The local_change_processor's ApplyLocalChange should never be called.
  StrictMock<MockLocalChangeProcessor> local_change_processor;

  local_service_->ProcessLocalChange(
      &local_change_processor,
      base::Bind(&OnSyncCompleted, FROM_HERE, run_loop.QuitClosure(),
                 fileapi::SYNC_STATUS_NO_CHANGE_TO_SYNC, FileSystemURL()));

  run_loop.Run();

  file_system_->RemoveSyncStatusObserver(&status_observer);

  EXPECT_EQ(0, GetNumChangesInTracker());
}

TEST_F(LocalFileSyncServiceTest, ProcessLocalChange_MultipleChanges) {
  const FileSystemURL kPath(file_system_->URL("foo"));
  const FileSystemURL kOther(file_system_->URL("bar"));

  base::RunLoop run_loop;

  // We should get called OnSyncEnabled and OnWriteEnabled on kPath and
  // OnSyncEnabled on kOther.
  StrictMock<MockSyncStatusObserver> status_observer;
  EXPECT_CALL(status_observer, OnSyncEnabled(kPath)).Times(AtLeast(1));
  EXPECT_CALL(status_observer, OnSyncEnabled(kOther)).Times(AtLeast(1));
  file_system_->AddSyncStatusObserver(&status_observer);

  // Creates a file, delete the file and creates a directory with the same
  // name.
  EXPECT_EQ(base::PLATFORM_FILE_OK, file_system_->CreateFile(kPath));
  EXPECT_EQ(base::PLATFORM_FILE_OK, file_system_->Remove(kPath, false));
  EXPECT_EQ(base::PLATFORM_FILE_OK, file_system_->CreateDirectory(kPath));

  // Creates one more file.
  EXPECT_EQ(base::PLATFORM_FILE_OK, file_system_->CreateFile(kOther));

  // The local_change_processor's ApplyLocalChange will be called
  // twice for FILE_TYPE and FILE_DIRECTORY.
  StrictMock<MockLocalChangeProcessor> local_change_processor;
  std::vector<FileChange> changes;
  EXPECT_CALL(local_change_processor, ApplyLocalChange(_, _, kPath, _))
      .Times(2)
      .WillOnce(MockStatusCallbackAndRecordChange(fileapi::SYNC_STATUS_OK,
                                                  &changes))
      .WillOnce(MockStatusCallbackAndRecordChange(fileapi::SYNC_STATUS_OK,
                                                  &changes));

  local_service_->ProcessLocalChange(
      &local_change_processor,
      base::Bind(&OnSyncCompleted, FROM_HERE, run_loop.QuitClosure(),
                 fileapi::SYNC_STATUS_OK, kPath));

  run_loop.Run();

  EXPECT_EQ(2U, changes.size());
  EXPECT_EQ(FileChange(FileChange::FILE_CHANGE_DELETE,
                       fileapi::SYNC_FILE_TYPE_FILE),
            changes[0]);
  EXPECT_EQ(FileChange(FileChange::FILE_CHANGE_ADD_OR_UPDATE,
                       fileapi::SYNC_FILE_TYPE_DIRECTORY),
            changes[1]);

  file_system_->RemoveSyncStatusObserver(&status_observer);

  // We have one more change for kOther.
  EXPECT_EQ(1, GetNumChangesInTracker());
}

TEST_F(LocalFileSyncServiceTest, ProcessLocalChange_GetLocalMetadata) {
  const FileSystemURL kURL(file_system_->URL("foo"));
  const base::Time kTime = base::Time::FromDoubleT(333);
  const int kSize = 555;

  base::RunLoop run_loop;

  // Creates a file.
  EXPECT_EQ(base::PLATFORM_FILE_OK, file_system_->CreateFile(kURL));
  EXPECT_EQ(base::PLATFORM_FILE_OK, file_system_->TruncateFile(kURL, kSize));
  EXPECT_EQ(base::PLATFORM_FILE_OK,
            file_system_->TouchFile(kURL, base::Time(), kTime));

  SyncStatusCode status = fileapi::SYNC_STATUS_UNKNOWN;
  SyncFileMetadata metadata;
  local_service_->GetLocalFileMetadata(
      kURL,
      base::Bind(&OnGetFileMetadata, FROM_HERE, run_loop.QuitClosure(),
                 &status, &metadata));

  run_loop.Run();

  EXPECT_EQ(fileapi::SYNC_STATUS_OK, status);
  EXPECT_EQ(kTime, metadata.last_modified);
  EXPECT_EQ(kSize, metadata.size);
}

TEST_F(LocalFileSyncServiceTest, RecordFakeChange) {
  const FileSystemURL kURL(file_system_->URL("foo"));

  // Create a file and reset the changes (as preparation).
  EXPECT_EQ(base::PLATFORM_FILE_OK, file_system_->CreateFile(kURL));
  file_system_->ClearChangeForURLInTracker(kURL);

  EXPECT_EQ(0, GetNumChangesInTracker());

  fileapi::FileSystemURLSet urlset;
  file_system_->GetChangedURLsInTracker(&urlset);
  EXPECT_TRUE(urlset.empty());

  const FileChange change(FileChange::FILE_CHANGE_ADD_OR_UPDATE,
                          fileapi::SYNC_FILE_TYPE_FILE);

  // Call RecordFakeLocalChange to add an ADD_OR_UPDATE change.
  {
    base::RunLoop run_loop;
    SyncStatusCode status = fileapi::SYNC_STATUS_UNKNOWN;
    local_service_->RecordFakeLocalChange(
        kURL, change, AssignAndQuitCallback(&run_loop, &status));
    run_loop.Run();
    EXPECT_EQ(fileapi::SYNC_STATUS_OK, status);
  }

  EXPECT_EQ(1, GetNumChangesInTracker());
  file_system_->GetChangedURLsInTracker(&urlset);
  EXPECT_EQ(1U, urlset.size());
  EXPECT_TRUE(urlset.find(kURL) != urlset.end());

  // Next local sync should pick up the recorded change.
  StrictMock<MockLocalChangeProcessor> local_change_processor;
  std::vector<FileChange> changes;
  EXPECT_CALL(local_change_processor, ApplyLocalChange(_, _, kURL, _))
      .WillOnce(MockStatusCallbackAndRecordChange(fileapi::SYNC_STATUS_OK,
                                                  &changes));
  {
    base::RunLoop run_loop;
    local_service_->ProcessLocalChange(
        &local_change_processor,
        base::Bind(&OnSyncCompleted, FROM_HERE, run_loop.QuitClosure(),
                   fileapi::SYNC_STATUS_OK, kURL));
    run_loop.Run();
  }

  EXPECT_EQ(1U, changes.size());
  EXPECT_EQ(change, changes[0]);
}

// TODO(kinuko): Add tests for multiple file changes and multiple
// FileSystemContexts.

}  // namespace sync_file_system
