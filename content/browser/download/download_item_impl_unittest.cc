// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/message_loop.h"
#include "base/stl_util.h"
#include "base/threading/thread.h"
#include "content/browser/download/byte_stream.h"
#include "content/browser/download/download_create_info.h"
#include "content/browser/download/download_file_manager.h"
#include "content/browser/download/download_item_impl.h"
#include "content/browser/download/download_item_impl_delegate.h"
#include "content/browser/download/download_request_handle.h"
#include "content/public/browser/download_id.h"
#include "content/public/browser/download_interrupt_reasons.h"
#include "content/public/test/mock_download_item.h"
#include "content/public/test/test_browser_thread.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using content::BrowserThread;
using content::DownloadId;
using content::DownloadItem;
using content::DownloadManager;
using content::MockDownloadItem;
using content::WebContents;
using ::testing::_;
using ::testing::AllOf;
using ::testing::Property;
using ::testing::Return;

DownloadId::Domain kValidDownloadItemIdDomain = "valid DownloadId::Domain";

namespace {
class MockDelegate : public DownloadItemImplDelegate {
 public:
  MockDelegate(DownloadFileManager* file_manager)
      : file_manager_(file_manager) {
  }
  MOCK_METHOD1(ShouldOpenFileBasedOnExtension, bool(const FilePath& path));
  MOCK_METHOD1(ShouldOpenDownload, bool(DownloadItemImpl* download));
  MOCK_METHOD1(CheckForFileRemoval, void(DownloadItemImpl* download));
  MOCK_METHOD1(MaybeCompleteDownload, void(DownloadItemImpl* download));
  MOCK_CONST_METHOD0(GetBrowserContext, content::BrowserContext*());
  MOCK_METHOD1(DownloadStopped, void(DownloadItemImpl* download));
  MOCK_METHOD1(DownloadCompleted, void(DownloadItemImpl* download));
  MOCK_METHOD1(DownloadOpened, void(DownloadItemImpl* download));
  MOCK_METHOD1(DownloadRemoved, void(DownloadItemImpl* download));
  MOCK_METHOD1(DownloadRenamedToIntermediateName,
               void(DownloadItemImpl* download));
  MOCK_METHOD1(DownloadRenamedToFinalName, void(DownloadItemImpl* download));
  MOCK_CONST_METHOD1(AssertStateConsistent, void(DownloadItemImpl* download));
  virtual DownloadFileManager* GetDownloadFileManager() OVERRIDE {
    return file_manager_;
  }
 private:
  DownloadFileManager* file_manager_;
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

class MockDownloadFileFactory
    : public DownloadFileManager::DownloadFileFactory {
 public:
  content::DownloadFile* CreateFile(
      DownloadCreateInfo* info,
      scoped_ptr<content::ByteStreamReader> stream_reader,
      DownloadManager* mgr,
      bool calculate_hash,
      const net::BoundNetLog& bound_net_log) {
    return MockCreateFile(
        info, stream_reader.get(), info->request_handle, mgr, calculate_hash,
        bound_net_log);
  }

  MOCK_METHOD6(MockCreateFile,
               content::DownloadFile*(DownloadCreateInfo*,
                                      content::ByteStreamReader*,
                                      const DownloadRequestHandle&,
                                      DownloadManager*,
                                      bool,
                                      const net::BoundNetLog&));
};

class MockDownloadFileManager : public DownloadFileManager {
 public:
  MockDownloadFileManager();
  MOCK_METHOD0(Shutdown, void());
  MOCK_METHOD1(CancelDownload, void(DownloadId));
  MOCK_METHOD2(CompleteDownload, void(DownloadId, const base::Closure&));
  MOCK_METHOD1(OnDownloadManagerShutdown, void(DownloadManager*));
  MOCK_METHOD4(RenameDownloadFile, void(DownloadId, const FilePath&, bool,
                                        const RenameCompletionCallback&));
  MOCK_CONST_METHOD0(NumberOfActiveDownloads, int());
 private:
  ~MockDownloadFileManager() {}
};

// Schedules a task to invoke the RenameCompletionCallback with |new_path| on
// the UI thread. Should only be used as the action for
// MockDownloadFileManager::Rename*DownloadFile as follows:
//   EXPECT_CALL(mock_download_file_manager,
//               RenameDownloadFile(_,_,_,_))
//       .WillOnce(ScheduleRenameCallback(new_path));
ACTION_P(ScheduleRenameCallback, new_path) {
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(arg3, content::DOWNLOAD_INTERRUPT_REASON_NONE, new_path));
}

// Similarly for scheduling a completion callback.
ACTION(ScheduleCompleteCallback) {
  BrowserThread::PostTask(BrowserThread::UI, FROM_HERE, base::Bind(arg1));
}

MockDownloadFileManager::MockDownloadFileManager()
    : DownloadFileManager(new MockDownloadFileFactory) {
}

}  // namespace

class DownloadItemTest : public testing::Test {
 public:
  class MockObserver : public DownloadItem::Observer {
   public:
    explicit MockObserver(DownloadItem* item) : item_(item), updated_(false) {
      item_->AddObserver(this);
    }
    ~MockObserver() { item_->RemoveObserver(this); }

    virtual void OnDownloadUpdated(DownloadItem* download) {
      updated_ = true;
    }

    virtual void OnDownloadOpened(DownloadItem* download) { }

    bool CheckUpdated() {
      bool was_updated = updated_;
      updated_ = false;
      return was_updated;
    }

   private:
    DownloadItem* item_;
    bool updated_;
  };

  DownloadItemTest()
      : ui_thread_(BrowserThread::UI, &loop_),
        file_thread_(BrowserThread::FILE, &loop_),
        file_manager_(new MockDownloadFileManager),
        delegate_(file_manager_.get()) {
  }

  ~DownloadItemTest() {
  }

  virtual void SetUp() {
  }

  virtual void TearDown() {
    ui_thread_.DeprecatedGetThreadObject()->message_loop()->RunAllPending();
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
    info_->download_id =
        content::DownloadId(kValidDownloadItemIdDomain, ++next_id);
    info_->prompt_user_for_save_location = false;
    info_->url_chain.push_back(GURL());
    info_->state = state;

    scoped_ptr<DownloadRequestHandleInterface> request_handle(
        new testing::NiceMock<MockRequestHandle>);
    DownloadItemImpl* download =
        new DownloadItemImpl(&delegate_, *(info_.get()),
                             request_handle.Pass(), false, net::BoundNetLog());
    allocated_downloads_.insert(download);
    return download;
  }

  // Destroy a previously created download item.
  void DestroyDownloadItem(DownloadItem* item) {
    allocated_downloads_.erase(item);
    delete item;
  }

  void RunAllPendingInMessageLoops() {
    loop_.RunAllPending();
  }

  MockDelegate* mock_delegate() {
    return &delegate_;
  }

  MockDownloadFileManager* mock_file_manager() {
    return file_manager_.get();
  }

 private:
  MessageLoopForUI loop_;
  content::TestBrowserThread ui_thread_;    // UI thread
  content::TestBrowserThread file_thread_;  // FILE thread
  scoped_refptr<MockDownloadFileManager> file_manager_;
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
//  void OnDownloadCompleting();
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
  MockObserver observer1(user_cancel);

  user_cancel->Cancel(true);
  ASSERT_TRUE(observer1.CheckUpdated());

  DownloadItemImpl* system_cancel =
      CreateDownloadItem(DownloadItem::IN_PROGRESS);
  MockObserver observer2(system_cancel);

  system_cancel->Cancel(false);
  ASSERT_TRUE(observer2.CheckUpdated());
}

TEST_F(DownloadItemTest, NotificationAfterComplete) {
  DownloadItemImpl* item = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  MockObserver observer(item);

  item->OnAllDataSaved(kDownloadChunkSize, DownloadItem::kEmptyFileHash);
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
  MockObserver observer(item);

  item->Interrupt(content::DOWNLOAD_INTERRUPT_REASON_NONE);
  ASSERT_TRUE(observer.CheckUpdated());
}

TEST_F(DownloadItemTest, NotificationAfterDelete) {
  DownloadItemImpl* item = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  MockObserver observer(item);

  item->Delete(DownloadItem::DELETE_DUE_TO_BROWSER_SHUTDOWN);
  ASSERT_TRUE(observer.CheckUpdated());
}

TEST_F(DownloadItemTest, NotificationAfterRemove) {
  DownloadItemImpl* item = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  MockObserver observer(item);

  item->Remove();
  ASSERT_TRUE(observer.CheckUpdated());
}

TEST_F(DownloadItemTest, NotificationAfterOnContentCheckCompleted) {
  // Setting to NOT_DANGEROUS does not trigger a notification.
  DownloadItemImpl* safe_item = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  MockObserver safe_observer(safe_item);

  safe_item->OnAllDataSaved(1, "");
  EXPECT_TRUE(safe_observer.CheckUpdated());
  safe_item->OnContentCheckCompleted(
      content::DOWNLOAD_DANGER_TYPE_NOT_DANGEROUS);
  EXPECT_TRUE(safe_observer.CheckUpdated());

  // Setting to unsafe url or unsafe file should trigger a notification.
  DownloadItemImpl* unsafeurl_item =
      CreateDownloadItem(DownloadItem::IN_PROGRESS);
  MockObserver unsafeurl_observer(unsafeurl_item);

  unsafeurl_item->OnAllDataSaved(1, "");
  EXPECT_TRUE(unsafeurl_observer.CheckUpdated());
  unsafeurl_item->OnContentCheckCompleted(
      content::DOWNLOAD_DANGER_TYPE_DANGEROUS_URL);
  EXPECT_TRUE(unsafeurl_observer.CheckUpdated());

  unsafeurl_item->DangerousDownloadValidated();
  EXPECT_TRUE(unsafeurl_observer.CheckUpdated());

  DownloadItemImpl* unsafefile_item =
      CreateDownloadItem(DownloadItem::IN_PROGRESS);
  MockObserver unsafefile_observer(unsafefile_item);

  unsafefile_item->OnAllDataSaved(1, "");
  EXPECT_TRUE(unsafefile_observer.CheckUpdated());
  unsafefile_item->OnContentCheckCompleted(
      content::DOWNLOAD_DANGER_TYPE_DANGEROUS_FILE);
  EXPECT_TRUE(unsafefile_observer.CheckUpdated());

  unsafefile_item->DangerousDownloadValidated();
  EXPECT_TRUE(unsafefile_observer.CheckUpdated());
}

// DownloadItemImpl::OnDownloadTargetDetermined will schedule a task to run
// DownloadFileManager::RenameDownloadFile(). Once the rename
// completes, DownloadItemImpl receives a notification with the new file
// name. Check that observers are updated when the new filename is available and
// not before.
TEST_F(DownloadItemTest, NotificationAfterOnDownloadTargetDetermined) {
  DownloadItemImpl* item = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  MockObserver observer(item);
  FilePath target_path(kDummyPath);
  FilePath intermediate_path(target_path.InsertBeforeExtensionASCII("x"));
  FilePath new_intermediate_path(target_path.InsertBeforeExtensionASCII("y"));
  EXPECT_CALL(*mock_file_manager(),
              RenameDownloadFile(_,intermediate_path,false,_))
      .WillOnce(ScheduleRenameCallback(new_intermediate_path));

  // Currently, a notification would be generated if the danger type is anything
  // other than NOT_DANGEROUS.
  item->OnDownloadTargetDetermined(target_path,
                                   DownloadItem::TARGET_DISPOSITION_OVERWRITE,
                                   content::DOWNLOAD_DANGER_TYPE_NOT_DANGEROUS,
                                   intermediate_path);
  EXPECT_FALSE(observer.CheckUpdated());
  RunAllPendingInMessageLoops();
  EXPECT_TRUE(observer.CheckUpdated());
  EXPECT_EQ(new_intermediate_path, item->GetFullPath());
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
  FilePath target_path(FilePath(kDummyPath).AppendASCII("foo.bar"));
  FilePath intermediate_path(target_path.InsertBeforeExtensionASCII("x"));
  EXPECT_EQ(FILE_PATH_LITERAL(""),
            item->GetFileNameToReportUser().value());
  EXPECT_CALL(*mock_file_manager(),
              RenameDownloadFile(_,_,false,_))
      .WillOnce(ScheduleRenameCallback(intermediate_path));
  item->OnDownloadTargetDetermined(target_path,
                                   DownloadItem::TARGET_DISPOSITION_OVERWRITE,
                                   content::DOWNLOAD_DANGER_TYPE_NOT_DANGEROUS,
                                   intermediate_path);
  RunAllPendingInMessageLoops();
  EXPECT_EQ(FILE_PATH_LITERAL("foo.bar"),
            item->GetFileNameToReportUser().value());
  item->SetDisplayName(FilePath(FILE_PATH_LITERAL("new.name")));
  EXPECT_EQ(FILE_PATH_LITERAL("new.name"),
            item->GetFileNameToReportUser().value());
}

static char external_data_test_string[] = "External data test";
static int destructor_called = 0;

class TestExternalData : public DownloadItem::ExternalData {
 public:
  int value;
  virtual ~TestExternalData() {
    destructor_called++;
  }
};

TEST_F(DownloadItemTest, ExternalData) {
  DownloadItemImpl* item = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  const DownloadItemImpl* const_item = item;

  // Shouldn't be anything there before set.
  EXPECT_EQ(NULL, item->GetExternalData(&external_data_test_string));
  EXPECT_EQ(NULL, const_item->GetExternalData(&external_data_test_string));

  TestExternalData* test1(new TestExternalData());
  test1->value = 2;

  // Should be able to get back what you set.
  item->SetExternalData(&external_data_test_string, test1);
  TestExternalData* test_result =
      static_cast<TestExternalData*>(
          item->GetExternalData(&external_data_test_string));
  EXPECT_EQ(test1, test_result);

  // Ditto for const lookup.
  const TestExternalData* test_const_result =
      static_cast<const TestExternalData*>(
          const_item->GetExternalData(&external_data_test_string));
  EXPECT_EQ(static_cast<const TestExternalData*>(test1),
            test_const_result);

  // Destructor should be called if value overwritten.  New value
  // should then be retrievable.
  TestExternalData* test2(new TestExternalData());
  test2->value = 3;
  EXPECT_EQ(0, destructor_called);
  item->SetExternalData(&external_data_test_string, test2);
  EXPECT_EQ(1, destructor_called);
  EXPECT_EQ(static_cast<DownloadItem::ExternalData*>(test2),
            item->GetExternalData(&external_data_test_string));

  // Overwriting with the same value shouldn't do anything.
  EXPECT_EQ(1, destructor_called);
  item->SetExternalData(&external_data_test_string, test2);
  EXPECT_EQ(1, destructor_called);
  EXPECT_EQ(static_cast<DownloadItem::ExternalData*>(test2),
            item->GetExternalData(&external_data_test_string));

  // Overwriting with NULL should result in destruction.
  item->SetExternalData(&external_data_test_string, NULL);
  EXPECT_EQ(2, destructor_called);

  // Destroying the download item should destroy the external data.

  TestExternalData* test3(new TestExternalData());
  item->SetExternalData(&external_data_test_string, test3);
  EXPECT_EQ(static_cast<DownloadItem::ExternalData*>(test3),
            item->GetExternalData(&external_data_test_string));
  DestroyDownloadItem(item);
  EXPECT_EQ(3, destructor_called);
}

// Test that the delegate is invoked after the download file is renamed.
// Delegate::DownloadRenamedToIntermediateName() should be invoked when the
// download is renamed to the intermediate name.
// Delegate::DownloadRenamedToFinalName() should be invoked after the final
// rename.
TEST_F(DownloadItemTest, CallbackAfterRename) {
  DownloadItemImpl* item = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  FilePath final_path(FilePath(kDummyPath).AppendASCII("foo.bar"));
  FilePath intermediate_path(final_path.InsertBeforeExtensionASCII("x"));
  FilePath new_intermediate_path(final_path.InsertBeforeExtensionASCII("y"));
  EXPECT_CALL(*mock_file_manager(),
              RenameDownloadFile(item->GetGlobalId(),
                                 intermediate_path, false, _))
      .WillOnce(ScheduleRenameCallback(new_intermediate_path));
  // DownloadItemImpl should invoke this callback on the delegate once the
  // download is renamed to the intermediate name. Also check that GetFullPath()
  // returns the intermediate path at the time of the call.
  EXPECT_CALL(*mock_delegate(),
              DownloadRenamedToIntermediateName(
                  AllOf(item,
                        Property(&DownloadItem::GetFullPath,
                                 new_intermediate_path))));
  item->OnDownloadTargetDetermined(final_path,
                                   DownloadItem::TARGET_DISPOSITION_OVERWRITE,
                                   content::DOWNLOAD_DANGER_TYPE_NOT_DANGEROUS,
                                   intermediate_path);
  RunAllPendingInMessageLoops();
  // All the callbacks should have happened by now.
  ::testing::Mock::VerifyAndClearExpectations(mock_file_manager());
  ::testing::Mock::VerifyAndClearExpectations(mock_delegate());

  item->OnAllDataSaved(10, "");
  EXPECT_CALL(*mock_file_manager(),
              RenameDownloadFile(item->GetGlobalId(),
                                 final_path, true, _))
      .WillOnce(ScheduleRenameCallback(final_path));
  EXPECT_CALL(*mock_file_manager(),
              CompleteDownload(item->GetGlobalId(), _))
      .WillOnce(ScheduleCompleteCallback());
  // DownloadItemImpl should invoke this callback on the delegate after the
  // final rename has completed. Also check that GetFullPath() and
  // GetTargetFilePath() return the final path at the time of the call.
  EXPECT_CALL(*mock_delegate(),
              DownloadRenamedToFinalName(
                  AllOf(item,
                        Property(&DownloadItem::GetFullPath, final_path),
                        Property(&DownloadItem::GetTargetFilePath,
                                 final_path))));
  EXPECT_CALL(*mock_delegate(), DownloadCompleted(item));
  EXPECT_CALL(*mock_delegate(), ShouldOpenDownload(item))
      .WillOnce(Return(true));
  item->OnDownloadCompleting();
  RunAllPendingInMessageLoops();
  ::testing::Mock::VerifyAndClearExpectations(mock_file_manager());
  ::testing::Mock::VerifyAndClearExpectations(mock_delegate());
}

TEST_F(DownloadItemTest, Interrupted) {
  DownloadItemImpl* item = CreateDownloadItem(DownloadItem::IN_PROGRESS);

  const content::DownloadInterruptReason reason(
      content::DOWNLOAD_INTERRUPT_REASON_FILE_ACCESS_DENIED);

  // Confirm interrupt sets state properly.
  item->Interrupt(reason);
  EXPECT_EQ(DownloadItem::INTERRUPTED, item->GetState());
  EXPECT_EQ(reason, item->GetLastReason());

  // Cancel should result in no change.
  item->Cancel(true);
  EXPECT_EQ(DownloadItem::INTERRUPTED, item->GetState());
  EXPECT_EQ(content::DOWNLOAD_INTERRUPT_REASON_USER_CANCELED,
            item->GetLastReason());
}

TEST_F(DownloadItemTest, Canceled) {
  DownloadItemImpl* item = CreateDownloadItem(DownloadItem::IN_PROGRESS);

  // Confirm cancel sets state properly.
  EXPECT_CALL(*mock_delegate(), DownloadStopped(item));
  item->Cancel(true);
  EXPECT_EQ(DownloadItem::CANCELLED, item->GetState());
}

TEST_F(DownloadItemTest, FileRemoved) {
  DownloadItemImpl* item = CreateDownloadItem(DownloadItem::IN_PROGRESS);

  EXPECT_FALSE(item->GetFileExternallyRemoved());
  item->OnDownloadedFileRemoved();
  EXPECT_TRUE(item->GetFileExternallyRemoved());
}

TEST(MockDownloadItem, Compiles) {
  MockDownloadItem mock_item;
}
