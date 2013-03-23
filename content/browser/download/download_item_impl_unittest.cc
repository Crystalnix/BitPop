// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/message_loop.h"
#include "base/stl_util.h"
#include "base/threading/thread.h"
#include "content/browser/download/byte_stream.h"
#include "content/browser/download/download_create_info.h"
#include "content/browser/download/download_file_factory.h"
#include "content/browser/download/download_item_impl.h"
#include "content/browser/download/download_item_impl_delegate.h"
#include "content/browser/download/download_request_handle.h"
#include "content/browser/download/mock_download_file.h"
#include "content/public/browser/download_id.h"
#include "content/public/browser/download_destination_observer.h"
#include "content/public/browser/download_interrupt_reasons.h"
#include "content/public/test/mock_download_item.h"
#include "content/public/test/test_browser_thread.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::_;
using ::testing::AllOf;
using ::testing::Property;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::StrictMock;

namespace content {
DownloadId::Domain kValidDownloadItemIdDomain = "valid DownloadId::Domain";

namespace {
class MockDelegate : public DownloadItemImplDelegate {
 public:
  MOCK_METHOD2(DetermineDownloadTarget, void(
      DownloadItemImpl*, const DownloadTargetCallback&));
  MOCK_METHOD2(ShouldOpenDownload,
               bool(DownloadItemImpl*, const ShouldOpenDownloadCallback&));
  MOCK_METHOD1(ShouldOpenFileBasedOnExtension, bool(const FilePath&));
  MOCK_METHOD1(CheckForFileRemoval, void(DownloadItemImpl*));
  MOCK_CONST_METHOD0(GetBrowserContext, BrowserContext*());
  MOCK_METHOD1(UpdatePersistence, void(DownloadItemImpl*));
  MOCK_METHOD1(DownloadOpened, void(DownloadItemImpl*));
  MOCK_METHOD1(DownloadRemoved, void(DownloadItemImpl*));
  MOCK_METHOD1(ShowDownloadInBrowser, void(DownloadItemImpl*));
  MOCK_CONST_METHOD1(AssertStateConsistent, void(DownloadItemImpl*));
};

class MockRequestHandle : public DownloadRequestHandleInterface {
 public:
  MOCK_CONST_METHOD0(GetWebContents, WebContents*());
  MOCK_CONST_METHOD0(GetDownloadManager, DownloadManager*());
  MOCK_CONST_METHOD0(PauseRequest, void());
  MOCK_CONST_METHOD0(ResumeRequest, void());
  MOCK_CONST_METHOD0(CancelRequest, void());
  MOCK_CONST_METHOD0(DebugString, std::string());
};

// Schedules a task to invoke the RenameCompletionCallback with |new_path| on
// the UI thread. Should only be used as the action for
// MockDownloadFile::Rename as follows:
//   EXPECT_CALL(download_file, Rename*(_,_))
//       .WillOnce(ScheduleRenameCallback(DOWNLOAD_INTERRUPT_REASON_NONE,
//                                        new_path));
ACTION_P2(ScheduleRenameCallback, interrupt_reason, new_path) {
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(arg1, interrupt_reason, new_path));
}

}  // namespace

class DownloadItemTest : public testing::Test {
 public:
  class MockObserver : public DownloadItem::Observer {
   public:
    explicit MockObserver(DownloadItem* item)
      : item_(item),
        removed_(false),
        destroyed_(false),
        updated_(false) {
      item_->AddObserver(this);
    }

    virtual ~MockObserver() {
      if (item_) item_->RemoveObserver(this);
    }

    virtual void OnDownloadRemoved(DownloadItem* download) {
      removed_ = true;
    }

    virtual void OnDownloadUpdated(DownloadItem* download) {
      updated_ = true;
    }

    virtual void OnDownloadOpened(DownloadItem* download) {
    }

    virtual void OnDownloadDestroyed(DownloadItem* download) {
      destroyed_ = true;
      item_->RemoveObserver(this);
      item_ = NULL;
    }

    bool CheckRemoved() {
      return removed_;
    }

    bool CheckDestroyed() {
      return destroyed_;
    }

    bool CheckUpdated() {
      bool was_updated = updated_;
      updated_ = false;
      return was_updated;
    }

   private:
    DownloadItem* item_;
    bool removed_;
    bool destroyed_;
    bool updated_;
  };

  DownloadItemTest()
      : ui_thread_(BrowserThread::UI, &loop_),
        file_thread_(BrowserThread::FILE, &loop_),
        delegate_() {
  }

  ~DownloadItemTest() {
  }

  virtual void SetUp() {
  }

  virtual void TearDown() {
    ui_thread_.DeprecatedGetThreadObject()->message_loop()->RunUntilIdle();
    STLDeleteElements(&allocated_downloads_);
    allocated_downloads_.clear();
  }

  // This class keeps ownership of the created download item; it will
  // be torn down at the end of the test unless DestroyDownloadItem is
  // called.
  DownloadItemImpl* CreateDownloadItem(DownloadItem::DownloadState state) {
    // Normally, the download system takes ownership of info, and is
    // responsible for deleting it.  In these unit tests, however, we
    // don't call the function that deletes it, so we do so ourselves.
    scoped_ptr<DownloadCreateInfo> info_;

    info_.reset(new DownloadCreateInfo());
    static int next_id;
    info_->download_id = DownloadId(kValidDownloadItemIdDomain, ++next_id);
    info_->save_info = scoped_ptr<DownloadSaveInfo>(new DownloadSaveInfo());
    info_->save_info->prompt_for_save_location = false;
    info_->url_chain.push_back(GURL());

    scoped_ptr<DownloadRequestHandleInterface> request_handle(
        new testing::NiceMock<MockRequestHandle>);
    DownloadItemImpl* download =
        new DownloadItemImpl(&delegate_, *(info_.get()),
                             request_handle.Pass(), net::BoundNetLog());
    allocated_downloads_.insert(download);
    return download;
  }

  // Add DownloadFile to DownloadItem
  MockDownloadFile* AddDownloadFileToDownloadItem(
      DownloadItemImpl* item,
      DownloadItemImplDelegate::DownloadTargetCallback *callback) {
    MockDownloadFile* mock_download_file(new StrictMock<MockDownloadFile>);
    scoped_ptr<DownloadFile> download_file(mock_download_file);
    EXPECT_CALL(*mock_download_file, Initialize(_));
    if (callback) {
      // Save the callback.
      EXPECT_CALL(*mock_delegate(), DetermineDownloadTarget(item, _))
          .WillOnce(SaveArg<1>(callback));
    } else {
      // Drop it on the floor.
      EXPECT_CALL(*mock_delegate(), DetermineDownloadTarget(item, _));
    }

    item->Start(download_file.Pass());
    loop_.RunUntilIdle();

    // So that we don't have a function writing to a stack variable
    // lying around if the above failed.
    ::testing::Mock::VerifyAndClearExpectations(mock_delegate());

    return mock_download_file;
  }

  // Cleanup a download item (specifically get rid of the DownloadFile on it).
  // The item must be in the IN_PROGRESS state.
  void CleanupItem(DownloadItemImpl* item, MockDownloadFile* download_file) {
    EXPECT_EQ(DownloadItem::IN_PROGRESS, item->GetState());

    EXPECT_CALL(*download_file, Cancel());
    item->Cancel(true);
    loop_.RunUntilIdle();
  }

  // Destroy a previously created download item.
  void DestroyDownloadItem(DownloadItem* item) {
    allocated_downloads_.erase(item);
    delete item;
  }

  void RunAllPendingInMessageLoops() {
    loop_.RunUntilIdle();
  }

  MockDelegate* mock_delegate() {
    return &delegate_;
  }

 private:
  MessageLoopForUI loop_;
  TestBrowserThread ui_thread_;    // UI thread
  TestBrowserThread file_thread_;  // FILE thread
  testing::NiceMock<MockDelegate> delegate_;
  std::set<DownloadItem*> allocated_downloads_;
};

namespace {

const int kDownloadChunkSize = 1000;
const int kDownloadSpeed = 1000;
const int kDummyDBHandle = 10;
const FilePath::CharType kDummyPath[] = FILE_PATH_LITERAL("/testpath");

} // namespace

// Tests to ensure calls that change a DownloadItem generate an update to
// observers.
// State changing functions not tested:
//  void OpenDownload();
//  void ShowDownloadInShell();
//  void CompleteDelayedDownload();
//  set_* mutators

TEST_F(DownloadItemTest, NotificationAfterUpdate) {
  DownloadItemImpl* item = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  MockObserver observer(item);

  item->UpdateProgress(kDownloadChunkSize, kDownloadSpeed, "");
  ASSERT_TRUE(observer.CheckUpdated());
  EXPECT_EQ(kDownloadSpeed, item->CurrentSpeed());
}

TEST_F(DownloadItemTest, NotificationAfterCancel) {
  DownloadItemImpl* user_cancel = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  MockDownloadFile* download_file =
      AddDownloadFileToDownloadItem(user_cancel, NULL);
  EXPECT_CALL(*download_file, Cancel());
  MockObserver observer1(user_cancel);

  user_cancel->Cancel(true);
  ASSERT_TRUE(observer1.CheckUpdated());

  DownloadItemImpl* system_cancel =
      CreateDownloadItem(DownloadItem::IN_PROGRESS);
  download_file = AddDownloadFileToDownloadItem(system_cancel, NULL);
  EXPECT_CALL(*download_file, Cancel());
  MockObserver observer2(system_cancel);

  system_cancel->Cancel(false);
  ASSERT_TRUE(observer2.CheckUpdated());
}

TEST_F(DownloadItemTest, NotificationAfterComplete) {
  DownloadItemImpl* item = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  MockObserver observer(item);

  item->OnAllDataSaved(DownloadItem::kEmptyFileHash);
  ASSERT_TRUE(observer.CheckUpdated());

  item->MarkAsComplete();
  ASSERT_TRUE(observer.CheckUpdated());
}

TEST_F(DownloadItemTest, NotificationAfterDownloadedFileRemoved) {
  DownloadItemImpl* item = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  MockObserver observer(item);

  item->OnDownloadedFileRemoved();
  ASSERT_TRUE(observer.CheckUpdated());
}

TEST_F(DownloadItemTest, NotificationAfterInterrupted) {
  DownloadItemImpl* item = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  MockDownloadFile* download_file = AddDownloadFileToDownloadItem(item, NULL);
  EXPECT_CALL(*download_file, Cancel());
  MockObserver observer(item);

  item->DestinationObserverAsWeakPtr()->DestinationError(
      DOWNLOAD_INTERRUPT_REASON_NONE);
  ASSERT_TRUE(observer.CheckUpdated());
}

TEST_F(DownloadItemTest, NotificationAfterDelete) {
  DownloadItemImpl* item = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  MockDownloadFile* download_file = AddDownloadFileToDownloadItem(item, NULL);
  EXPECT_CALL(*download_file, Cancel());
  MockObserver observer(item);

  item->Delete(DownloadItem::DELETE_DUE_TO_BROWSER_SHUTDOWN);
  ASSERT_TRUE(observer.CheckUpdated());
}

TEST_F(DownloadItemTest, NotificationAfterDestroyed) {
  DownloadItemImpl* item = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  MockObserver observer(item);

  DestroyDownloadItem(item);
  ASSERT_TRUE(observer.CheckDestroyed());
}

TEST_F(DownloadItemTest, NotificationAfterRemove) {
  DownloadItemImpl* item = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  MockDownloadFile* download_file = AddDownloadFileToDownloadItem(item, NULL);
  EXPECT_CALL(*download_file, Cancel());
  MockObserver observer(item);

  item->Remove();
  ASSERT_TRUE(observer.CheckUpdated());
  ASSERT_TRUE(observer.CheckRemoved());
}

TEST_F(DownloadItemTest, NotificationAfterOnContentCheckCompleted) {
  // Setting to NOT_DANGEROUS does not trigger a notification.
  DownloadItemImpl* safe_item = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  MockObserver safe_observer(safe_item);

  safe_item->OnAllDataSaved("");
  EXPECT_TRUE(safe_observer.CheckUpdated());
  safe_item->OnContentCheckCompleted(DOWNLOAD_DANGER_TYPE_NOT_DANGEROUS);
  EXPECT_TRUE(safe_observer.CheckUpdated());

  // Setting to unsafe url or unsafe file should trigger a notification.
  DownloadItemImpl* unsafeurl_item =
      CreateDownloadItem(DownloadItem::IN_PROGRESS);
  MockObserver unsafeurl_observer(unsafeurl_item);

  unsafeurl_item->OnAllDataSaved("");
  EXPECT_TRUE(unsafeurl_observer.CheckUpdated());
  unsafeurl_item->OnContentCheckCompleted(DOWNLOAD_DANGER_TYPE_DANGEROUS_URL);
  EXPECT_TRUE(unsafeurl_observer.CheckUpdated());

  unsafeurl_item->DangerousDownloadValidated();
  EXPECT_TRUE(unsafeurl_observer.CheckUpdated());

  DownloadItemImpl* unsafefile_item =
      CreateDownloadItem(DownloadItem::IN_PROGRESS);
  MockObserver unsafefile_observer(unsafefile_item);

  unsafefile_item->OnAllDataSaved("");
  EXPECT_TRUE(unsafefile_observer.CheckUpdated());
  unsafefile_item->OnContentCheckCompleted(DOWNLOAD_DANGER_TYPE_DANGEROUS_FILE);
  EXPECT_TRUE(unsafefile_observer.CheckUpdated());

  unsafefile_item->DangerousDownloadValidated();
  EXPECT_TRUE(unsafefile_observer.CheckUpdated());
}

// DownloadItemImpl::OnDownloadTargetDetermined will schedule a task to run
// DownloadFile::Rename(). Once the rename
// completes, DownloadItemImpl receives a notification with the new file
// name. Check that observers are updated when the new filename is available and
// not before.
TEST_F(DownloadItemTest, NotificationAfterOnDownloadTargetDetermined) {
  DownloadItemImpl* item = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  DownloadItemImplDelegate::DownloadTargetCallback callback;
  MockDownloadFile* download_file =
      AddDownloadFileToDownloadItem(item, &callback);
  MockObserver observer(item);
  FilePath target_path(kDummyPath);
  FilePath intermediate_path(target_path.InsertBeforeExtensionASCII("x"));
  FilePath new_intermediate_path(target_path.InsertBeforeExtensionASCII("y"));
  EXPECT_CALL(*download_file, RenameAndUniquify(intermediate_path, _))
      .WillOnce(ScheduleRenameCallback(DOWNLOAD_INTERRUPT_REASON_NONE,
                                       new_intermediate_path));

  // Currently, a notification would be generated if the danger type is anything
  // other than NOT_DANGEROUS.
  callback.Run(target_path, DownloadItem::TARGET_DISPOSITION_OVERWRITE,
               DOWNLOAD_DANGER_TYPE_NOT_DANGEROUS, intermediate_path);
  EXPECT_FALSE(observer.CheckUpdated());
  RunAllPendingInMessageLoops();
  EXPECT_TRUE(observer.CheckUpdated());
  EXPECT_EQ(new_intermediate_path, item->GetFullPath());

  CleanupItem(item, download_file);
}

TEST_F(DownloadItemTest, NotificationAfterTogglePause) {
  DownloadItemImpl* item = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  MockObserver observer(item);

  item->TogglePause();
  ASSERT_TRUE(observer.CheckUpdated());

  item->TogglePause();
  ASSERT_TRUE(observer.CheckUpdated());
}

TEST_F(DownloadItemTest, DisplayName) {
  DownloadItemImpl* item = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  DownloadItemImplDelegate::DownloadTargetCallback callback;
  MockDownloadFile* download_file =
      AddDownloadFileToDownloadItem(item, &callback);
  FilePath target_path(FilePath(kDummyPath).AppendASCII("foo.bar"));
  FilePath intermediate_path(target_path.InsertBeforeExtensionASCII("x"));
  EXPECT_EQ(FILE_PATH_LITERAL(""),
            item->GetFileNameToReportUser().value());
  EXPECT_CALL(*download_file, RenameAndUniquify(_, _))
      .WillOnce(ScheduleRenameCallback(DOWNLOAD_INTERRUPT_REASON_NONE,
                                       intermediate_path));
  callback.Run(target_path, DownloadItem::TARGET_DISPOSITION_OVERWRITE,
               DOWNLOAD_DANGER_TYPE_NOT_DANGEROUS, intermediate_path);
  RunAllPendingInMessageLoops();
  EXPECT_EQ(FILE_PATH_LITERAL("foo.bar"),
            item->GetFileNameToReportUser().value());
  item->SetDisplayName(FilePath(FILE_PATH_LITERAL("new.name")));
  EXPECT_EQ(FILE_PATH_LITERAL("new.name"),
            item->GetFileNameToReportUser().value());
  CleanupItem(item, download_file);
}

// Test to make sure that Start method calls DF initialize properly.
TEST_F(DownloadItemTest, Start) {
  MockDownloadFile* mock_download_file(new MockDownloadFile);
  scoped_ptr<DownloadFile> download_file(mock_download_file);
  DownloadItemImpl* item = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  EXPECT_CALL(*mock_download_file, Initialize(_));
  item->Start(download_file.Pass());

  CleanupItem(item, mock_download_file);
}

// Test that the delegate is invoked after the download file is renamed.
TEST_F(DownloadItemTest, CallbackAfterRename) {
  DownloadItemImpl* item = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  DownloadItemImplDelegate::DownloadTargetCallback callback;
  MockDownloadFile* download_file =
      AddDownloadFileToDownloadItem(item, &callback);
  FilePath final_path(FilePath(kDummyPath).AppendASCII("foo.bar"));
  FilePath intermediate_path(final_path.InsertBeforeExtensionASCII("x"));
  FilePath new_intermediate_path(final_path.InsertBeforeExtensionASCII("y"));
  EXPECT_CALL(*download_file, RenameAndUniquify(intermediate_path, _))
      .WillOnce(ScheduleRenameCallback(DOWNLOAD_INTERRUPT_REASON_NONE,
                                       new_intermediate_path));
  EXPECT_CALL(*mock_delegate(), ShowDownloadInBrowser(item))
      .Times(1);

  callback.Run(final_path, DownloadItem::TARGET_DISPOSITION_OVERWRITE,
               DOWNLOAD_DANGER_TYPE_NOT_DANGEROUS, intermediate_path);
  RunAllPendingInMessageLoops();
  // All the callbacks should have happened by now.
  ::testing::Mock::VerifyAndClearExpectations(download_file);
  ::testing::Mock::VerifyAndClearExpectations(mock_delegate());

  EXPECT_CALL(*download_file, RenameAndAnnotate(final_path, _))
      .WillOnce(ScheduleRenameCallback(DOWNLOAD_INTERRUPT_REASON_NONE,
                                       final_path));
  EXPECT_CALL(*mock_delegate(), ShouldOpenDownload(item, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*download_file, Detach());
  item->DestinationObserverAsWeakPtr()->DestinationCompleted("");
  RunAllPendingInMessageLoops();
  ::testing::Mock::VerifyAndClearExpectations(download_file);
  ::testing::Mock::VerifyAndClearExpectations(mock_delegate());
}

// Test that the delegate is invoked after the download file is renamed and the
// download item is in an interrupted state.
TEST_F(DownloadItemTest, CallbackAfterInterruptedRename) {
  DownloadItemImpl* item = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  DownloadItemImplDelegate::DownloadTargetCallback callback;
  MockDownloadFile* download_file =
      AddDownloadFileToDownloadItem(item, &callback);
  FilePath final_path(FilePath(kDummyPath).AppendASCII("foo.bar"));
  FilePath intermediate_path(final_path.InsertBeforeExtensionASCII("x"));
  FilePath new_intermediate_path(final_path.InsertBeforeExtensionASCII("y"));
  EXPECT_CALL(*download_file, RenameAndUniquify(intermediate_path, _))
      .WillOnce(ScheduleRenameCallback(DOWNLOAD_INTERRUPT_REASON_FILE_FAILED,
                                       new_intermediate_path));
  EXPECT_CALL(*download_file, Cancel())
      .Times(1);
  EXPECT_CALL(*mock_delegate(), ShowDownloadInBrowser(item))
      .Times(1);

  callback.Run(final_path, DownloadItem::TARGET_DISPOSITION_OVERWRITE,
               DOWNLOAD_DANGER_TYPE_NOT_DANGEROUS, intermediate_path);
  RunAllPendingInMessageLoops();
  // All the callbacks should have happened by now.
  ::testing::Mock::VerifyAndClearExpectations(download_file);
  ::testing::Mock::VerifyAndClearExpectations(mock_delegate());
}

TEST_F(DownloadItemTest, Interrupted) {
  DownloadItemImpl* item = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  MockDownloadFile* download_file = AddDownloadFileToDownloadItem(item, NULL);

  const DownloadInterruptReason reason(
      DOWNLOAD_INTERRUPT_REASON_FILE_ACCESS_DENIED);

  // Confirm interrupt sets state properly.
  EXPECT_CALL(*download_file, Cancel());
  item->DestinationObserverAsWeakPtr()->DestinationError(reason);
  RunAllPendingInMessageLoops();
  EXPECT_EQ(DownloadItem::INTERRUPTED, item->GetState());
  EXPECT_EQ(reason, item->GetLastReason());

  // Cancel should result in no change.
  item->Cancel(true);
  EXPECT_EQ(DownloadItem::INTERRUPTED, item->GetState());
  EXPECT_EQ(DOWNLOAD_INTERRUPT_REASON_USER_CANCELED, item->GetLastReason());
}

TEST_F(DownloadItemTest, Canceled) {
  DownloadItemImpl* item = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  MockDownloadFile* download_file = AddDownloadFileToDownloadItem(item, NULL);

  // Confirm cancel sets state properly.
  EXPECT_CALL(*download_file, Cancel());
  item->Cancel(true);
  EXPECT_EQ(DownloadItem::CANCELLED, item->GetState());
}

TEST_F(DownloadItemTest, FileRemoved) {
  DownloadItemImpl* item = CreateDownloadItem(DownloadItem::IN_PROGRESS);

  EXPECT_FALSE(item->GetFileExternallyRemoved());
  item->OnDownloadedFileRemoved();
  EXPECT_TRUE(item->GetFileExternallyRemoved());
}

TEST_F(DownloadItemTest, DestinationUpdate) {
  DownloadItemImpl* item = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  base::WeakPtr<DownloadDestinationObserver> as_observer(
      item->DestinationObserverAsWeakPtr());
  MockObserver observer(item);

  EXPECT_EQ(0l, item->CurrentSpeed());
  EXPECT_EQ("", item->GetHashState());
  EXPECT_EQ(0l, item->GetReceivedBytes());
  EXPECT_EQ(0l, item->GetTotalBytes());
  EXPECT_FALSE(observer.CheckUpdated());
  item->SetTotalBytes(100l);
  EXPECT_EQ(100l, item->GetTotalBytes());

  as_observer->DestinationUpdate(10, 20, "deadbeef");
  EXPECT_EQ(20l, item->CurrentSpeed());
  EXPECT_EQ("deadbeef", item->GetHashState());
  EXPECT_EQ(10l, item->GetReceivedBytes());
  EXPECT_EQ(100l, item->GetTotalBytes());
  EXPECT_TRUE(observer.CheckUpdated());

  as_observer->DestinationUpdate(200, 20, "livebeef");
  EXPECT_EQ(20l, item->CurrentSpeed());
  EXPECT_EQ("livebeef", item->GetHashState());
  EXPECT_EQ(200l, item->GetReceivedBytes());
  EXPECT_EQ(0l, item->GetTotalBytes());
  EXPECT_TRUE(observer.CheckUpdated());
}

TEST_F(DownloadItemTest, DestinationError) {
  DownloadItemImpl* item = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  MockDownloadFile* download_file = AddDownloadFileToDownloadItem(item, NULL);
  base::WeakPtr<DownloadDestinationObserver> as_observer(
      item->DestinationObserverAsWeakPtr());
  MockObserver observer(item);

  EXPECT_EQ(DownloadItem::IN_PROGRESS, item->GetState());
  EXPECT_EQ(DOWNLOAD_INTERRUPT_REASON_NONE, item->GetLastReason());
  EXPECT_FALSE(observer.CheckUpdated());

  EXPECT_CALL(*download_file, Cancel());
  as_observer->DestinationError(
      DOWNLOAD_INTERRUPT_REASON_FILE_ACCESS_DENIED);
  ::testing::Mock::VerifyAndClearExpectations(mock_delegate());
  EXPECT_TRUE(observer.CheckUpdated());
  EXPECT_EQ(DownloadItem::INTERRUPTED, item->GetState());
  EXPECT_EQ(DOWNLOAD_INTERRUPT_REASON_FILE_ACCESS_DENIED,
            item->GetLastReason());
}

TEST_F(DownloadItemTest, DestinationCompleted) {
  DownloadItemImpl* item = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  base::WeakPtr<DownloadDestinationObserver> as_observer(
      item->DestinationObserverAsWeakPtr());
  MockObserver observer(item);

  EXPECT_EQ(DownloadItem::IN_PROGRESS, item->GetState());
  EXPECT_EQ("", item->GetHash());
  EXPECT_EQ("", item->GetHashState());
  EXPECT_FALSE(item->AllDataSaved());
  EXPECT_FALSE(observer.CheckUpdated());

  as_observer->DestinationUpdate(10, 20, "deadbeef");
  EXPECT_TRUE(observer.CheckUpdated());
  EXPECT_FALSE(observer.CheckUpdated()); // Confirm reset.
  EXPECT_EQ(DownloadItem::IN_PROGRESS, item->GetState());
  EXPECT_EQ("", item->GetHash());
  EXPECT_EQ("deadbeef", item->GetHashState());
  EXPECT_FALSE(item->AllDataSaved());

  as_observer->DestinationCompleted("livebeef");
  ::testing::Mock::VerifyAndClearExpectations(mock_delegate());
  EXPECT_EQ(DownloadItem::IN_PROGRESS, item->GetState());
  EXPECT_TRUE(observer.CheckUpdated());
  EXPECT_EQ("livebeef", item->GetHash());
  EXPECT_EQ("", item->GetHashState());
  EXPECT_TRUE(item->AllDataSaved());
}

TEST(MockDownloadItem, Compiles) {
  MockDownloadItem mock_item;
}

}  // namespace content
