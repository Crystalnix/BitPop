// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/message_loop.h"
#include "base/stl_util.h"
#include "chrome/browser/download/download_status_updater.h"
#include "content/public/test/mock_download_item.h"
#include "content/public/test/mock_download_manager.h"
#include "content/public/test/test_browser_thread.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"


using ::testing::AtLeast;
using ::testing::Mock;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrictMock;
using ::testing::_;

class TestDownloadStatusUpdater : public DownloadStatusUpdater {
 protected:
  virtual void UpdateAppIconDownloadProgress() OVERRIDE {
    return;
  }
};

class DownloadStatusUpdaterTest : public testing::Test {
 public:
  DownloadStatusUpdaterTest()
      : updater_(new TestDownloadStatusUpdater()),
        ui_thread_(content::BrowserThread::UI, &loop_) {}

  virtual ~DownloadStatusUpdaterTest() {
    for (size_t mgr_idx = 0; mgr_idx < managers_.size(); ++mgr_idx) {
      EXPECT_CALL(*Manager(mgr_idx), RemoveObserver(updater_));
      for (size_t item_idx = 0; item_idx < manager_items_[mgr_idx].size();
           ++item_idx) {
        if (Item(mgr_idx, item_idx)->GetState() ==
            content::DownloadItem::IN_PROGRESS)
          EXPECT_CALL(*Item(mgr_idx, item_idx), RemoveObserver(updater_));
      }
    }

    delete updater_;
    updater_ = NULL;
    VerifyAndClearExpectations();

    managers_.clear();
    for (std::vector<Items>::iterator it = manager_items_.begin();
         it != manager_items_.end(); ++it)
      STLDeleteContainerPointers(it->begin(), it->end());

    loop_.RunAllPending();  // Allow DownloadManager destruction.
  }

 protected:
  // Attach some number of DownloadManagers to the updater.
  void SetupManagers(int manager_count) {
    DCHECK_EQ(0U, managers_.size());
    for (int i = 0; i < manager_count; ++i) {
      content::MockDownloadManager* mgr =
          new StrictMock<content::MockDownloadManager>;
      managers_.push_back(make_scoped_refptr(mgr));
    }
  }

  // Hook the specified manager into the updater.
  void LinkManager(int i) {
    content::MockDownloadManager* mgr = managers_[i].get();
    EXPECT_CALL(*mgr, AddObserver(updater_));
    updater_->AddManager(mgr);
    updater_->ModelChanged(mgr);
  }

  // Add some number of Download items to a particular manager.
  void AddItems(int manager_index, int item_count, int in_progress_count) {
    DCHECK_GT(managers_.size(), static_cast<size_t>(manager_index));
    content::MockDownloadManager* manager = managers_[manager_index].get();

    if (manager_items_.size() <= static_cast<size_t>(manager_index))
      manager_items_.resize(manager_index+1);

    std::vector<content::DownloadItem*> item_list;
    for (int i = 0; i < item_count; ++i) {
      content::MockDownloadItem* item =
          new StrictMock<content::MockDownloadItem>;
      if (i < in_progress_count) {
        EXPECT_CALL(*item, GetState())
            .WillRepeatedly(Return(content::DownloadItem::IN_PROGRESS));
        EXPECT_CALL(*item, AddObserver(updater_))
            .WillOnce(Return());
      } else {
        EXPECT_CALL(*item, GetState())
            .WillRepeatedly(Return(content::DownloadItem::COMPLETE));
      }
      manager_items_[manager_index].push_back(item);
    }
    EXPECT_CALL(*manager, SearchDownloads(string16(), _))
        .WillOnce(SetArgPointee<1>(manager_items_[manager_index]));
  }

  // Return the specified manager.
  content::MockDownloadManager* Manager(int manager_index) {
    DCHECK_GT(managers_.size(), static_cast<size_t>(manager_index));
    return managers_[manager_index].get();
  }

  // Return the specified item.
  content::MockDownloadItem* Item(int manager_index, int item_index) {
    DCHECK_GT(manager_items_.size(), static_cast<size_t>(manager_index));
    DCHECK_GT(manager_items_[manager_index].size(),
              static_cast<size_t>(item_index));
    // All DownloadItems in manager_items_ are MockDownloadItems.
    return static_cast<content::MockDownloadItem*>(
        manager_items_[manager_index][item_index]);
  }

  // Set return values relevant to |DownloadStatusUpdater::GetProgress()|
  // for the specified item
  void SetItemValues(int manager_index, int item_index,
                     int received_bytes, int total_bytes) {
    EXPECT_CALL(*Item(manager_index, item_index), GetReceivedBytes())
        .WillRepeatedly(Return(received_bytes));
    EXPECT_CALL(*Item(manager_index, item_index), GetTotalBytes())
        .WillRepeatedly(Return(total_bytes));
  }

  // Transition specified item to completed.
  void CompleteItem(int manager_index, int item_index) {
    content::MockDownloadItem* item(Item(manager_index, item_index));
    EXPECT_CALL(*item, GetState())
        .WillRepeatedly(Return(content::DownloadItem::COMPLETE));
    EXPECT_CALL(*item, RemoveObserver(updater_))
        .WillOnce(Return());
    updater_->OnDownloadUpdated(item);
  }

  // Verify and clear all mocks expectations.
  void VerifyAndClearExpectations() {
    for (std::vector<scoped_refptr<content::MockDownloadManager> >::iterator it
             = managers_.begin(); it != managers_.end(); ++it)
      Mock::VerifyAndClearExpectations(it->get());
    for (std::vector<Items>::iterator it = manager_items_.begin();
         it != manager_items_.end(); ++it)
      for (Items::iterator sit = it->begin(); sit != it->end(); ++sit)
        Mock::VerifyAndClearExpectations(*sit);
  }

  std::vector<scoped_refptr<content::MockDownloadManager> > managers_;
  // DownloadItem so that it can be assigned to the result of SearchDownloads.
  typedef std::vector<content::DownloadItem*> Items;
  std::vector<Items> manager_items_;

  // Pointer so we can verify that destruction triggers appropriate
  // changes.
  DownloadStatusUpdater *updater_;

  // Thread so that the DownloadManager (which is a DeleteOnUIThread
  // object) can be deleted.
  // TODO(rdsmith): This can be removed when the DownloadManager
  // is no longer required to be deleted on the UI thread.
  MessageLoop loop_;
  content::TestBrowserThread ui_thread_;
};

// Test null updater.
TEST_F(DownloadStatusUpdaterTest, Basic) {
  float progress = -1;
  int download_count = -1;
  EXPECT_TRUE(updater_->GetProgress(&progress, &download_count));
  EXPECT_FLOAT_EQ(0.0f, progress);
  EXPECT_EQ(0, download_count);
}

// Test updater with null manager.
TEST_F(DownloadStatusUpdaterTest, OneManagerNoItems) {
  SetupManagers(1);
  AddItems(0, 0, 0);
  LinkManager(0);
  VerifyAndClearExpectations();

  float progress = -1;
  int download_count = -1;
  EXPECT_TRUE(updater_->GetProgress(&progress, &download_count));
  EXPECT_FLOAT_EQ(0.0f, progress);
  EXPECT_EQ(0, download_count);
}

// Test updater with non-null manager, including transition an item to
// |content::DownloadItem::COMPLETE| and adding a new item.
TEST_F(DownloadStatusUpdaterTest, OneManagerManyItems) {
  SetupManagers(1);
  AddItems(0, 3, 2);
  LinkManager(0);

  // Prime items
  SetItemValues(0, 0, 10, 20);
  SetItemValues(0, 1, 50, 60);
  SetItemValues(0, 2, 90, 90);

  float progress = -1;
  int download_count = -1;
  EXPECT_TRUE(updater_->GetProgress(&progress, &download_count));
  EXPECT_FLOAT_EQ((10+50)/(20.0f+60), progress);
  EXPECT_EQ(2, download_count);

  // Transition one item to completed and confirm progress is updated
  // properly.
  CompleteItem(0, 0);
  EXPECT_TRUE(updater_->GetProgress(&progress, &download_count));
  EXPECT_FLOAT_EQ(50/60.0f, progress);
  EXPECT_EQ(1, download_count);

  // Add a new item to manager and confirm progress is updated properly.
  AddItems(0, 1, 1);
  updater_->ModelChanged(Manager(0));
  SetItemValues(0, 3, 150, 200);

  EXPECT_TRUE(updater_->GetProgress(&progress, &download_count));
  EXPECT_FLOAT_EQ((50+150)/(60+200.0f), progress);
  EXPECT_EQ(2, download_count);
}

// Confirm we recognize the situation where we have an unknown size.
TEST_F(DownloadStatusUpdaterTest, UnknownSize) {
  SetupManagers(1);
  AddItems(0, 2, 2);
  LinkManager(0);

  // Prime items
  SetItemValues(0, 0, 10, 20);
  SetItemValues(0, 1, 50, -1);

  float progress = -1;
  int download_count = -1;
  EXPECT_FALSE(updater_->GetProgress(&progress, &download_count));
}

// Test many null managers.
TEST_F(DownloadStatusUpdaterTest, ManyManagersNoItems) {
  SetupManagers(1);
  AddItems(0, 0, 0);
  LinkManager(0);

  float progress = -1;
  int download_count = -1;
  EXPECT_TRUE(updater_->GetProgress(&progress, &download_count));
  EXPECT_FLOAT_EQ(0.0f, progress);
  EXPECT_EQ(0, download_count);
}

// Test many managers with all items complete.
TEST_F(DownloadStatusUpdaterTest, ManyManagersEmptyItems) {
  SetupManagers(2);
  AddItems(0, 3, 0);
  LinkManager(0);
  AddItems(1, 3, 0);
  LinkManager(1);

  float progress = -1;
  int download_count = -1;
  EXPECT_TRUE(updater_->GetProgress(&progress, &download_count));
  EXPECT_FLOAT_EQ(0.0f, progress);
  EXPECT_EQ(0, download_count);
}

// Test many managers with some non-complete items.
TEST_F(DownloadStatusUpdaterTest, ManyManagersMixedItems) {
  SetupManagers(2);
  AddItems(0, 3, 2);
  LinkManager(0);
  AddItems(1, 3, 1);
  LinkManager(1);

  SetItemValues(0, 0, 10, 20);
  SetItemValues(0, 1, 50, 60);
  SetItemValues(1, 0, 80, 90);

  float progress = -1;
  int download_count = -1;
  EXPECT_TRUE(updater_->GetProgress(&progress, &download_count));
  EXPECT_FLOAT_EQ((10+50+80)/(20.0f+60+90), progress);
  EXPECT_EQ(3, download_count);
}
