// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/file_path.h"
#include "base/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/weak_ptr.h"
#include "base/message_loop.h"
#include "base/observer_list.h"
#include "base/test/test_file_util.h"
#include "chrome/browser/download/download_path_reservation_tracker.h"
#include "chrome/browser/download/download_util.h"
#include "content/public/test/mock_download_item.h"
#include "content/public/test/test_browser_thread.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using content::BrowserThread;
using content::DownloadId;
using content::DownloadItem;
using content::MockDownloadItem;
using testing::AnyNumber;
using testing::Return;
using testing::ReturnRef;
using testing::ReturnRefOfCopy;

namespace {

// MockDownloadItem with real observers and state.
class FakeDownloadItem : public MockDownloadItem {
 public:
  explicit FakeDownloadItem()
      : state_(IN_PROGRESS) {
  }
  virtual ~FakeDownloadItem() {
    FOR_EACH_OBSERVER(Observer, observers_, OnDownloadDestroyed(this));
    EXPECT_EQ(0u, observers_.size());
  }
  virtual void AddObserver(Observer* observer) OVERRIDE {
    observers_.AddObserver(observer);
  }
  virtual void RemoveObserver(Observer* observer) OVERRIDE {
    observers_.RemoveObserver(observer);
  }
  virtual void UpdateObservers() OVERRIDE {
    FOR_EACH_OBSERVER(Observer, observers_, OnDownloadUpdated(this));
  }

  virtual DownloadState GetState() const OVERRIDE {
    return state_;
  }

  void SetState(DownloadState state) {
    state_ = state;
    UpdateObservers();
  }

 private:
  DownloadState state_;
  ObserverList<Observer> observers_;
};

class DownloadPathReservationTrackerTest : public testing::Test {
 public:
  DownloadPathReservationTrackerTest();

  // testing::Test
  virtual void SetUp() OVERRIDE;
  virtual void TearDown() OVERRIDE;

  FakeDownloadItem* CreateDownloadItem(int32 id);
  FilePath GetPathInDownloadsDirectory(const FilePath::CharType* suffix);
  bool IsPathInUse(const FilePath& path);
  void CallGetReservedPath(DownloadItem& download_item,
                           const FilePath& target_path, bool uniquify_path,
                           FilePath* return_path, bool* return_verified);

  const FilePath& default_download_path() const {
    return default_download_path_;
  }
  void set_default_download_path(const FilePath& path) {
    default_download_path_ = path;
  }

 protected:
  base::ScopedTempDir test_download_dir_;
  FilePath default_download_path_;
  MessageLoopForUI message_loop_;
  content::TestBrowserThread ui_thread_;
  content::TestBrowserThread file_thread_;

 private:
  void TestReservedPathCallback(FilePath* return_path, bool* return_verified,
                                bool* did_run_callback,
                                const FilePath& path, bool verified);
};

DownloadPathReservationTrackerTest::DownloadPathReservationTrackerTest()
    : ui_thread_(BrowserThread::UI, &message_loop_),
      file_thread_(BrowserThread::FILE, &message_loop_) {
}

void DownloadPathReservationTrackerTest::SetUp() {
  ASSERT_TRUE(test_download_dir_.CreateUniqueTempDir());
  set_default_download_path(test_download_dir_.path());
}

void DownloadPathReservationTrackerTest::TearDown() {
  message_loop_.RunUntilIdle();
}

FakeDownloadItem* DownloadPathReservationTrackerTest::CreateDownloadItem(
    int32 id) {
  FakeDownloadItem* item = new ::testing::StrictMock<FakeDownloadItem>;
  DownloadId download_id(reinterpret_cast<void*>(this), id);
  EXPECT_CALL(*item, GetGlobalId())
      .WillRepeatedly(Return(download_id));
  EXPECT_CALL(*item, GetTargetFilePath())
      .WillRepeatedly(ReturnRefOfCopy(FilePath()));
  return item;
}

FilePath DownloadPathReservationTrackerTest::GetPathInDownloadsDirectory(
    const FilePath::CharType* suffix) {
  return default_download_path().Append(suffix).NormalizePathSeparators();
}

bool DownloadPathReservationTrackerTest::IsPathInUse(const FilePath& path) {
  return DownloadPathReservationTracker::IsPathInUseForTesting(path);
}

void DownloadPathReservationTrackerTest::CallGetReservedPath(
    DownloadItem& download_item,
    const FilePath& target_path,
    bool uniquify_path,
    FilePath* return_path,
    bool* return_verified) {
  // Weak pointer factory to prevent the callback from running after this
  // function has returned.
  base::WeakPtrFactory<DownloadPathReservationTrackerTest> weak_ptr_factory(
      this);
  bool did_run_callback = false;
  DownloadPathReservationTracker::GetReservedPath(
      download_item, target_path, default_download_path(), uniquify_path,
      base::Bind(&DownloadPathReservationTrackerTest::TestReservedPathCallback,
                 weak_ptr_factory.GetWeakPtr(), return_path, return_verified,
                 &did_run_callback));
  message_loop_.RunUntilIdle();
  EXPECT_TRUE(did_run_callback);
}

void DownloadPathReservationTrackerTest::TestReservedPathCallback(
    FilePath* return_path, bool* return_verified, bool* did_run_callback,
    const FilePath& path, bool verified) {
  *did_run_callback = true;
  *return_path = path;
  *return_verified = verified;
}

}  // namespace

// A basic reservation is acquired and committed.
TEST_F(DownloadPathReservationTrackerTest, BasicReservation) {
  scoped_ptr<FakeDownloadItem> item(CreateDownloadItem(1));
  FilePath path(GetPathInDownloadsDirectory(FILE_PATH_LITERAL("foo.txt")));
  ASSERT_FALSE(IsPathInUse(path));

  FilePath reserved_path;
  bool verified = false;
  CallGetReservedPath(*item, path, false, &reserved_path, &verified);
  EXPECT_TRUE(IsPathInUse(path));
  EXPECT_TRUE(verified);
  EXPECT_EQ(path.value(), reserved_path.value());

  // Destroying the item should release the reservation.
  item.reset();
  message_loop_.RunUntilIdle();
  EXPECT_FALSE(IsPathInUse(path));
}

// A download that is interrupted should lose its reservation.
TEST_F(DownloadPathReservationTrackerTest, InterruptedDownload) {
  scoped_ptr<FakeDownloadItem> item(CreateDownloadItem(1));
  FilePath path(GetPathInDownloadsDirectory(FILE_PATH_LITERAL("foo.txt")));
  ASSERT_FALSE(IsPathInUse(path));

  FilePath reserved_path;
  bool verified = false;
  CallGetReservedPath(*item, path, false, &reserved_path, &verified);
  EXPECT_TRUE(IsPathInUse(path));
  EXPECT_TRUE(verified);
  EXPECT_EQ(path.value(), reserved_path.value());

  // Once the download is interrupted, the path should become available again.
  item->SetState(DownloadItem::INTERRUPTED);
  message_loop_.RunUntilIdle();
  EXPECT_FALSE(IsPathInUse(path));
}

// A completed download should also lose its reservation.
TEST_F(DownloadPathReservationTrackerTest, CompleteDownload) {
  scoped_ptr<FakeDownloadItem> item(CreateDownloadItem(1));
  FilePath path(GetPathInDownloadsDirectory(FILE_PATH_LITERAL("foo.txt")));
  ASSERT_FALSE(IsPathInUse(path));

  FilePath reserved_path;
  bool verified = false;
  CallGetReservedPath(*item, path, false, &reserved_path, &verified);
  EXPECT_TRUE(IsPathInUse(path));
  EXPECT_TRUE(verified);
  EXPECT_EQ(path.value(), reserved_path.value());

  // Once the download completes, the path should become available again. For a
  // real download, at this point only the path reservation will be released.
  // The path wouldn't be available since it is occupied on disk by the
  // completed download.
  item->SetState(DownloadItem::COMPLETE);
  message_loop_.RunUntilIdle();
  EXPECT_FALSE(IsPathInUse(path));
}

// If there are files on the file system, a unique reservation should uniquify
// around it.
TEST_F(DownloadPathReservationTrackerTest, ConflictingFiles) {
  scoped_ptr<FakeDownloadItem> item(CreateDownloadItem(1));
  FilePath path(GetPathInDownloadsDirectory(FILE_PATH_LITERAL("foo.txt")));
  FilePath path1(GetPathInDownloadsDirectory(FILE_PATH_LITERAL("foo (1).txt")));
  // Create a file at |path|, and a .crdownload file at |path1|.
  ASSERT_EQ(0, file_util::WriteFile(path, "", 0));
  ASSERT_EQ(0, file_util::WriteFile(download_util::GetCrDownloadPath(path1),
                                    "", 0));
  ASSERT_TRUE(IsPathInUse(path));

  FilePath reserved_path;
  bool verified = false;
  CallGetReservedPath(*item, path, true, &reserved_path, &verified);
  EXPECT_TRUE(IsPathInUse(path));
  EXPECT_TRUE(IsPathInUse(reserved_path));
  EXPECT_TRUE(verified);
  // The path should be uniquified, skipping over foo.txt but not over
  // "foo (1).txt.crdownload"
  EXPECT_EQ(
      GetPathInDownloadsDirectory(FILE_PATH_LITERAL("foo (1).txt")).value(),
      reserved_path.value());

  item.reset();
  message_loop_.RunUntilIdle();
  EXPECT_TRUE(IsPathInUse(path));
  EXPECT_FALSE(IsPathInUse(reserved_path));
}

// Multiple reservations for the same path should uniquify around each other.
TEST_F(DownloadPathReservationTrackerTest, ConflictingReservations) {
  scoped_ptr<FakeDownloadItem> item1(CreateDownloadItem(1));
  FilePath path(GetPathInDownloadsDirectory(FILE_PATH_LITERAL("foo.txt")));
  FilePath uniquified_path(
      GetPathInDownloadsDirectory(FILE_PATH_LITERAL("foo (1).txt")));
  ASSERT_FALSE(IsPathInUse(path));
  ASSERT_FALSE(IsPathInUse(uniquified_path));

  FilePath reserved_path1;
  bool verified = false;

  CallGetReservedPath(*item1, path, true, &reserved_path1, &verified);
  EXPECT_TRUE(IsPathInUse(path));
  EXPECT_TRUE(verified);

  {
    // Requesting a reservation for the same path with uniquification results in
    // a uniquified path.
    scoped_ptr<FakeDownloadItem> item2(CreateDownloadItem(2));
    FilePath reserved_path2;
    CallGetReservedPath(*item2, path, true, &reserved_path2, &verified);
    EXPECT_TRUE(IsPathInUse(path));
    EXPECT_TRUE(IsPathInUse(uniquified_path));
    EXPECT_EQ(uniquified_path.value(), reserved_path2.value());
  }
  message_loop_.RunUntilIdle();
  EXPECT_TRUE(IsPathInUse(path));
  EXPECT_FALSE(IsPathInUse(uniquified_path));

  {
    // Since the previous download item was removed, requesting a reservation
    // for the same path should result in the same uniquified path.
    scoped_ptr<FakeDownloadItem> item2(CreateDownloadItem(2));
    FilePath reserved_path2;
    CallGetReservedPath(*item2, path, true, &reserved_path2, &verified);
    EXPECT_TRUE(IsPathInUse(path));
    EXPECT_TRUE(IsPathInUse(uniquified_path));
    EXPECT_EQ(uniquified_path.value(), reserved_path2.value());
  }
  message_loop_.RunUntilIdle();

  // Now acquire an overwriting reservation. We should end up with the same
  // non-uniquified path for both reservations.
  scoped_ptr<FakeDownloadItem> item3(CreateDownloadItem(2));
  FilePath reserved_path3;
  CallGetReservedPath(*item3, path, false, &reserved_path3, &verified);
  EXPECT_TRUE(IsPathInUse(path));
  EXPECT_FALSE(IsPathInUse(uniquified_path));

  EXPECT_EQ(path.value(), reserved_path1.value());
  EXPECT_EQ(path.value(), reserved_path3.value());
}

// If a unique path cannot be determined after trying kMaxUniqueFiles
// uniquifiers, then the callback should notified that verification failed, and
// the returned path should be set to the original requested path.
TEST_F(DownloadPathReservationTrackerTest, UnresolvedConflicts) {
  FilePath path(GetPathInDownloadsDirectory(FILE_PATH_LITERAL("foo.txt")));
  scoped_ptr<FakeDownloadItem> items[
      DownloadPathReservationTracker::kMaxUniqueFiles + 1];
  // Create |kMaxUniqueFiles + 1| reservations for |path|. The first reservation
  // will have no uniquifier. The |kMaxUniqueFiles| remaining reservations do.
  for (int i = 0; i <= DownloadPathReservationTracker::kMaxUniqueFiles; i++) {
    FilePath reserved_path;
    FilePath expected_path;
    bool verified = false;
    if (i > 0)
      expected_path = path.InsertBeforeExtensionASCII(StringPrintf(" (%d)", i));
    else
      expected_path = path;
    items[i].reset(CreateDownloadItem(i));
    EXPECT_FALSE(IsPathInUse(expected_path));
    CallGetReservedPath(*items[i], path, true, &reserved_path, &verified);
    EXPECT_TRUE(IsPathInUse(expected_path));
    EXPECT_EQ(expected_path.value(), reserved_path.value());
    EXPECT_TRUE(verified);
  }
  // The next reservation for |path| will fail to be unique.
  scoped_ptr<FakeDownloadItem> item(
      CreateDownloadItem(DownloadPathReservationTracker::kMaxUniqueFiles + 1));
  FilePath reserved_path;
  bool verified = true;
  CallGetReservedPath(*item, path, true, &reserved_path, &verified);
  EXPECT_FALSE(verified);
  EXPECT_EQ(path.value(), reserved_path.value());
}

// If the target directory is unwriteable, then callback should be notified that
// verification failed.
TEST_F(DownloadPathReservationTrackerTest, UnwriteableDirectory) {
  scoped_ptr<FakeDownloadItem> item(CreateDownloadItem(1));
  FilePath path(GetPathInDownloadsDirectory(FILE_PATH_LITERAL("foo.txt")));
  FilePath dir(path.DirName());
  ASSERT_FALSE(IsPathInUse(path));

  {
    // Scope for PermissionRestorer
    file_util::PermissionRestorer restorer(dir);
    EXPECT_TRUE(file_util::MakeFileUnwritable(dir));
    FilePath reserved_path;
    bool verified = true;
    CallGetReservedPath(*item, path, false, &reserved_path, &verified);
    // Verification fails.
    EXPECT_FALSE(verified);
    EXPECT_EQ(path.BaseName().value(), reserved_path.BaseName().value());
  }
}

// If the default download directory doesn't exist, then it should be
// created. But only if we are actually going to create the download path there.
TEST_F(DownloadPathReservationTrackerTest, CreateDefaultDownloadPath) {
  FilePath path(GetPathInDownloadsDirectory(FILE_PATH_LITERAL("foo/foo.txt")));
  FilePath dir(path.DirName());
  ASSERT_FALSE(file_util::DirectoryExists(dir));

  {
    scoped_ptr<FakeDownloadItem> item(CreateDownloadItem(1));
    FilePath reserved_path;
    bool verified = true;
    CallGetReservedPath(*item, path, false, &reserved_path, &verified);
    // Verification fails because the directory doesn't exist.
    EXPECT_FALSE(verified);
  }
  ASSERT_FALSE(IsPathInUse(path));
  {
    scoped_ptr<FakeDownloadItem> item(CreateDownloadItem(1));
    FilePath reserved_path;
    bool verified = true;
    set_default_download_path(dir);
    CallGetReservedPath(*item, path, false, &reserved_path, &verified);
    // Verification succeeds because the directory is created.
    EXPECT_TRUE(verified);
    EXPECT_TRUE(file_util::DirectoryExists(dir));
  }
}

// If the target path of the download item changes, the reservation should be
// updated to match.
TEST_F(DownloadPathReservationTrackerTest, UpdatesToTargetPath) {
  scoped_ptr<FakeDownloadItem> item(CreateDownloadItem(1));
  FilePath path(GetPathInDownloadsDirectory(FILE_PATH_LITERAL("foo.txt")));
  ASSERT_FALSE(IsPathInUse(path));

  FilePath reserved_path;
  bool verified = false;
  CallGetReservedPath(*item, path, false, &reserved_path, &verified);
  EXPECT_TRUE(IsPathInUse(path));
  EXPECT_TRUE(verified);
  EXPECT_EQ(path.value(), reserved_path.value());

  // The target path is initially empty. If an OnDownloadUpdated() is issued in
  // this state, we shouldn't lose the reservation.
  ASSERT_EQ(FilePath::StringType(), item->GetTargetFilePath().value());
  item->UpdateObservers();
  message_loop_.RunUntilIdle();
  EXPECT_TRUE(IsPathInUse(path));

  // If the target path changes, we should update the reservation to match.
  FilePath new_target_path(
      GetPathInDownloadsDirectory(FILE_PATH_LITERAL("bar.txt")));
  ASSERT_FALSE(IsPathInUse(new_target_path));
  EXPECT_CALL(*item, GetTargetFilePath())
      .WillRepeatedly(ReturnRef(new_target_path));
  item->UpdateObservers();
  message_loop_.RunUntilIdle();
  EXPECT_FALSE(IsPathInUse(path));
  EXPECT_TRUE(IsPathInUse(new_target_path));

  // Destroying the item should release the reservation.
  item.reset();
  message_loop_.RunUntilIdle();
  EXPECT_FALSE(IsPathInUse(new_target_path));
}
