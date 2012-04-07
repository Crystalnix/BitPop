// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/message_loop.h"
#include "base/stl_util.h"
#include "base/threading/thread.h"
#include "content/browser/download/download_create_info.h"
#include "content/browser/download/download_item_impl.h"
#include "content/browser/download/download_request_handle.h"
#include "content/browser/download/download_status_updater.h"
#include "content/browser/download/interrupt_reasons.h"
#include "content/browser/download/mock_download_item.h"
#include "content/public/browser/download_id.h"
#include "content/test/test_browser_thread.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using content::BrowserThread;
using content::DownloadId;
using content::DownloadItem;
using content::DownloadManager;

DownloadId::Domain kValidDownloadItemIdDomain = "valid DownloadId::Domain";

namespace {
class MockDelegate : public DownloadItemImpl::Delegate {
 public:
  MOCK_METHOD1(ShouldOpenFileBasedOnExtension, bool(const FilePath& path));
  MOCK_METHOD1(ShouldOpenDownload, bool(DownloadItem* download));
  MOCK_METHOD1(CheckForFileRemoval, void(DownloadItem* download));
  MOCK_METHOD1(MaybeCompleteDownload, void(DownloadItem* download));
  MOCK_CONST_METHOD0(GetBrowserContext, content::BrowserContext*());
  MOCK_METHOD1(DownloadCancelled, void(DownloadItem* download));
  MOCK_METHOD1(DownloadCompleted, void(DownloadItem* download));
  MOCK_METHOD1(DownloadOpened, void(DownloadItem* download));
  MOCK_METHOD1(DownloadRemoved, void(DownloadItem* download));
  MOCK_CONST_METHOD1(AssertStateConsistent, void(DownloadItem* download));
};

class MockRequestHandle : public DownloadRequestHandleInterface {
 public:
  MOCK_CONST_METHOD0(GetTabContents, TabContents*());
  MOCK_CONST_METHOD0(GetDownloadManager, DownloadManager*());
  MOCK_CONST_METHOD0(PauseRequest, void());
  MOCK_CONST_METHOD0(ResumeRequest, void());
  MOCK_CONST_METHOD0(CancelRequest, void());
  MOCK_CONST_METHOD0(DebugString, std::string());
};

}

class DownloadItemTest : public testing::Test {
 public:
  class MockObserver : public content::DownloadItem::Observer {
   public:
    explicit MockObserver(DownloadItem* item) : item_(item), updated_(false) {
      item_->AddObserver(this);
    }
    ~MockObserver() { item_->RemoveObserver(this); }

    virtual void OnDownloadUpdated(content::DownloadItem* download) {
      updated_ = true;
    }

    virtual void OnDownloadOpened(content::DownloadItem* download) { }

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
      : ui_thread_(BrowserThread::UI, &loop_) {
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
  DownloadItem* CreateDownloadItem(DownloadItem::DownloadState state) {
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

    MockRequestHandle* request_handle =
        new testing::NiceMock<MockRequestHandle>;
    DownloadItem* download =
        new DownloadItemImpl(&delegate_, *(info_.get()),
                             request_handle, false);
    allocated_downloads_.insert(download);
    return download;
  }

  // Destroy a previously created download item.
  void DestroyDownloadItem(DownloadItem* item) {
    allocated_downloads_.erase(item);
    delete item;
  }

 protected:
  DownloadStatusUpdater download_status_updater_;

 private:
  MessageLoopForUI loop_;
  // UI thread.
  content::TestBrowserThread ui_thread_;
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
//  void OnDownloadCompleting(DownloadFileManager* file_manager);
//  void OnDownloadRenamedToFinalName(const FilePath& full_path);
//  set_* mutators

TEST_F(DownloadItemTest, NotificationAfterUpdate) {
  DownloadItem* item = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  MockObserver observer(item);

  item->UpdateProgress(kDownloadChunkSize, kDownloadSpeed, "");
  ASSERT_TRUE(observer.CheckUpdated());
  EXPECT_EQ(kDownloadSpeed, item->CurrentSpeed());
}

TEST_F(DownloadItemTest, NotificationAfterCancel) {
  DownloadItem* user_cancel = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  MockObserver observer1(user_cancel);

  user_cancel->Cancel(true);
  ASSERT_TRUE(observer1.CheckUpdated());

  DownloadItem* system_cancel = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  MockObserver observer2(system_cancel);

  system_cancel->Cancel(false);
  ASSERT_TRUE(observer2.CheckUpdated());
}

TEST_F(DownloadItemTest, NotificationAfterComplete) {
  DownloadItem* item = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  MockObserver observer(item);

  // Calling OnAllDataSaved does not trigger notification
  item->OnAllDataSaved(kDownloadChunkSize, DownloadItem::kEmptyFileHash);
  ASSERT_FALSE(observer.CheckUpdated());

  item->MarkAsComplete();
  ASSERT_TRUE(observer.CheckUpdated());
}

TEST_F(DownloadItemTest, NotificationAfterDownloadedFileRemoved) {
  DownloadItem* item = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  MockObserver observer(item);

  item->OnDownloadedFileRemoved();
  ASSERT_TRUE(observer.CheckUpdated());
}

TEST_F(DownloadItemTest, NotificationAfterInterrupted) {
  DownloadItem* item = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  MockObserver observer(item);

  item->Interrupted(kDownloadChunkSize, "", DOWNLOAD_INTERRUPT_REASON_NONE);
  ASSERT_TRUE(observer.CheckUpdated());
}

TEST_F(DownloadItemTest, NotificationAfterDelete) {
  DownloadItem* item = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  MockObserver observer(item);

  item->Delete(DownloadItem::DELETE_DUE_TO_BROWSER_SHUTDOWN);
  ASSERT_TRUE(observer.CheckUpdated());
}

TEST_F(DownloadItemTest, NotificationAfterRemove) {
  DownloadItem* item = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  MockObserver observer(item);

  item->Remove();
  ASSERT_TRUE(observer.CheckUpdated());
}

TEST_F(DownloadItemTest, NotificationAfterSetFileCheckResults) {
  // Setting to safe should not trigger any notifications
  DownloadItem* safe_item = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  MockObserver safe_observer(safe_item);

  DownloadStateInfo state = safe_item->GetStateInfo();;
  state.danger = content::DOWNLOAD_DANGER_TYPE_NOT_DANGEROUS;
  safe_item->SetFileCheckResults(state);
  ASSERT_FALSE(safe_observer.CheckUpdated());

  // Setting to unsafe url or unsafe file should trigger notification
  DownloadItem* unsafeurl_item = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  MockObserver unsafeurl_observer(unsafeurl_item);

  state = unsafeurl_item->GetStateInfo();;
  state.danger = content::DOWNLOAD_DANGER_TYPE_DANGEROUS_URL;
  unsafeurl_item->SetFileCheckResults(state);
  ASSERT_TRUE(unsafeurl_observer.CheckUpdated());

  unsafeurl_item->DangerousDownloadValidated();
  ASSERT_TRUE(unsafeurl_observer.CheckUpdated());

  DownloadItem* unsafefile_item = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  MockObserver unsafefile_observer(unsafefile_item);

  state = unsafefile_item->GetStateInfo();;
  state.danger = content::DOWNLOAD_DANGER_TYPE_DANGEROUS_FILE;
  unsafefile_item->SetFileCheckResults(state);
  ASSERT_TRUE(unsafefile_observer.CheckUpdated());

  unsafefile_item->DangerousDownloadValidated();
  ASSERT_TRUE(unsafefile_observer.CheckUpdated());
}

TEST_F(DownloadItemTest, NotificationAfterOnPathDetermined) {
  DownloadItem* item = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  MockObserver observer(item);

  // Calling OnPathDetermined does not trigger notification
  item->OnPathDetermined(FilePath(kDummyPath));
  ASSERT_FALSE(observer.CheckUpdated());
}

TEST_F(DownloadItemTest, NotificationAfterRename) {
  DownloadItem* item = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  MockObserver observer(item);

  // Calling Rename does not trigger notification
  item->Rename(FilePath(kDummyPath));
  ASSERT_FALSE(observer.CheckUpdated());
}

TEST_F(DownloadItemTest, NotificationAfterTogglePause) {
  DownloadItem* item = CreateDownloadItem(DownloadItem::IN_PROGRESS);
  MockObserver observer(item);

  item->TogglePause();
  ASSERT_TRUE(observer.CheckUpdated());

  item->TogglePause();
  ASSERT_TRUE(observer.CheckUpdated());
}

static char external_data_test_string[] = "External data test";
static int destructor_called = 0;

class TestExternalData : public content::DownloadItem::ExternalData {
 public:
  int value;
  virtual ~TestExternalData() {
    destructor_called++;
  }
};

TEST_F(DownloadItemTest, ExternalData) {
  DownloadItem* item = CreateDownloadItem(DownloadItem::IN_PROGRESS);

  // Shouldn't be anything there before set.
  EXPECT_EQ(NULL, item->GetExternalData(&external_data_test_string));

  TestExternalData* test1(new TestExternalData());
  test1->value = 2;

  // Should be able to get back what you set.
  item->SetExternalData(&external_data_test_string, test1);
  TestExternalData* test_result =
      static_cast<TestExternalData*>(
          item->GetExternalData(&external_data_test_string));
  EXPECT_EQ(test1, test_result);

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

TEST(MockDownloadItem, Compiles) {
  MockDownloadItem mock_item;
}
