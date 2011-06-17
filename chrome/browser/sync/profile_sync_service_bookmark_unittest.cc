// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(akalin): This file is basically just a unit test for
// BookmarkChangeProcessor.  Write unit tests for
// BookmarkModelAssociator separately.

#include <stack>
#include <vector>

#include "base/file_path.h"
#include "base/file_util.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "base/string16.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#include "chrome/browser/sync/abstract_profile_sync_service_test.h"
#include "chrome/browser/sync/engine/syncapi.h"
#include "chrome/browser/sync/glue/bookmark_change_processor.h"
#include "chrome/browser/sync/glue/bookmark_model_associator.h"
#include "chrome/browser/sync/syncable/directory_manager.h"
#include "chrome/test/sync/engine/test_id_factory.h"
#include "chrome/test/sync/engine/test_user_share.h"
#include "chrome/test/testing_profile.h"
#include "content/browser/browser_thread.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace browser_sync {

namespace {

using testing::_;
using testing::InvokeWithoutArgs;
using testing::Mock;
using testing::StrictMock;

class TestBookmarkModelAssociator : public BookmarkModelAssociator {
 public:
  TestBookmarkModelAssociator(
      BookmarkModel* bookmark_model,
      sync_api::UserShare* user_share,
      UnrecoverableErrorHandler* unrecoverable_error_handler)
      : BookmarkModelAssociator(bookmark_model, user_share,
                                unrecoverable_error_handler),
        user_share_(user_share) {}

  // TODO(akalin): This logic lazily creates any tagged node that is
  // requested.  A better way would be to have utility functions to
  // create sync nodes from some bookmark structure and to use that.
  virtual bool GetSyncIdForTaggedNode(const std::string& tag, int64* sync_id) {
    std::wstring tag_wide;
    if (!UTF8ToWide(tag.c_str(), tag.length(), &tag_wide)) {
      NOTREACHED() << "Unable to convert UTF8 to wide for string: " << tag;
      return false;
    }

    bool root_exists = false;
    syncable::ModelType type = model_type();
    {
      sync_api::WriteTransaction trans(user_share_);
      sync_api::ReadNode uber_root(&trans);
      uber_root.InitByRootLookup();

      sync_api::ReadNode root(&trans);
      root_exists = root.InitByTagLookup(
          ProfileSyncServiceTestHelper::GetTagForType(type));
    }

    if (!root_exists) {
      bool created = ProfileSyncServiceTestHelper::CreateRoot(
          type,
          user_share_,
          &id_factory_);
      if (!created)
        return false;
    }

    sync_api::WriteTransaction trans(user_share_);
    sync_api::ReadNode root(&trans);
    EXPECT_TRUE(root.InitByTagLookup(
        ProfileSyncServiceTestHelper::GetTagForType(type)));

    // First, try to find a node with the title among the root's children.
    // This will be the case if we are testing model persistence, and
    // are reloading a sync repository created earlier in the test.
    int64 last_child_id = sync_api::kInvalidId;
    for (int64 id = root.GetFirstChildId(); id != sync_api::kInvalidId; /***/) {
      sync_api::ReadNode child(&trans);
      child.InitByIdLookup(id);
      last_child_id = id;
      if (tag_wide == child.GetTitle()) {
        *sync_id = id;
        return true;
      }
      id = child.GetSuccessorId();
    }

    sync_api::ReadNode predecessor_node(&trans);
    sync_api::ReadNode* predecessor = NULL;
    if (last_child_id != sync_api::kInvalidId) {
      predecessor_node.InitByIdLookup(last_child_id);
      predecessor = &predecessor_node;
    }
    sync_api::WriteNode node(&trans);
    // Create new fake tagged nodes at the end of the ordering.
    node.InitByCreation(type, root, predecessor);
    node.SetIsFolder(true);
    node.SetTitle(tag_wide);
    node.SetExternalId(0);
    *sync_id = node.GetId();
    return true;
  }

 private:
  sync_api::UserShare* user_share_;
  browser_sync::TestIdFactory id_factory_;
};

// FakeServerChange constructs a list of sync_api::ChangeRecords while modifying
// the sync model, and can pass the ChangeRecord list to a
// sync_api::SyncObserver (i.e., the ProfileSyncService) to test the client
// change-application behavior.
// Tests using FakeServerChange should be careful to avoid back-references,
// since FakeServerChange will send the edits in the order specified.
class FakeServerChange {
 public:
  explicit FakeServerChange(sync_api::WriteTransaction* trans) : trans_(trans) {
  }

  // Pretend that the server told the syncer to add a bookmark object.
  int64 Add(const std::wstring& title,
            const std::string& url,
            bool is_folder,
            int64 parent_id,
            int64 predecessor_id) {
    sync_api::ReadNode parent(trans_);
    EXPECT_TRUE(parent.InitByIdLookup(parent_id));
    sync_api::WriteNode node(trans_);
    if (predecessor_id == 0) {
      EXPECT_TRUE(node.InitByCreation(syncable::BOOKMARKS, parent, NULL));
    } else {
      sync_api::ReadNode predecessor(trans_);
      EXPECT_TRUE(predecessor.InitByIdLookup(predecessor_id));
      EXPECT_EQ(predecessor.GetParentId(), parent.GetId());
      EXPECT_TRUE(node.InitByCreation(syncable::BOOKMARKS, parent,
                                      &predecessor));
    }
    EXPECT_EQ(node.GetPredecessorId(), predecessor_id);
    EXPECT_EQ(node.GetParentId(), parent_id);
    node.SetIsFolder(is_folder);
    node.SetTitle(title);
    if (!is_folder)
      node.SetURL(GURL(url));
    sync_api::SyncManager::ChangeRecord record;
    record.action = sync_api::SyncManager::ChangeRecord::ACTION_ADD;
    record.id = node.GetId();
    changes_.push_back(record);
    return node.GetId();
  }

  // Add a bookmark folder.
  int64 AddFolder(const std::wstring& title,
                  int64 parent_id,
                  int64 predecessor_id) {
    return Add(title, std::string(), true, parent_id, predecessor_id);
  }

  // Add a bookmark.
  int64 AddURL(const std::wstring& title,
               const std::string& url,
               int64 parent_id,
               int64 predecessor_id) {
    return Add(title, url, false, parent_id, predecessor_id);
  }

  // Pretend that the server told the syncer to delete an object.
  void Delete(int64 id) {
    {
      // Delete the sync node.
      sync_api::WriteNode node(trans_);
      EXPECT_TRUE(node.InitByIdLookup(id));
      EXPECT_FALSE(node.GetFirstChildId());
      node.Remove();
    }
    {
      // Verify the deletion.
      sync_api::ReadNode node(trans_);
      EXPECT_FALSE(node.InitByIdLookup(id));
    }

    sync_api::SyncManager::ChangeRecord record;
    record.action = sync_api::SyncManager::ChangeRecord::ACTION_DELETE;
    record.id = id;
    // Deletions are always first in the changelist, but we can't actually do
    // WriteNode::Remove() on the node until its children are moved. So, as
    // a practical matter, users of FakeServerChange must move or delete
    // children before parents.  Thus, we must insert the deletion record
    // at the front of the vector.
    changes_.insert(changes_.begin(), record);
  }

  // Set a new title value, and return the old value.
  std::wstring ModifyTitle(int64 id, const std::wstring& new_title) {
    sync_api::WriteNode node(trans_);
    EXPECT_TRUE(node.InitByIdLookup(id));
    std::wstring old_title = node.GetTitle();
    node.SetTitle(new_title);
    SetModified(id);
    return old_title;
  }

  // Set a new parent and predecessor value.  Return the old parent id.
  // We could return the old predecessor id, but it turns out not to be
  // very useful for assertions.
  int64 ModifyPosition(int64 id, int64 parent_id, int64 predecessor_id) {
    sync_api::ReadNode parent(trans_);
    EXPECT_TRUE(parent.InitByIdLookup(parent_id));
    sync_api::WriteNode node(trans_);
    EXPECT_TRUE(node.InitByIdLookup(id));
    int64 old_parent_id = node.GetParentId();
    if (predecessor_id == 0) {
      EXPECT_TRUE(node.SetPosition(parent, NULL));
    } else {
      sync_api::ReadNode predecessor(trans_);
      EXPECT_TRUE(predecessor.InitByIdLookup(predecessor_id));
      EXPECT_EQ(predecessor.GetParentId(), parent.GetId());
      EXPECT_TRUE(node.SetPosition(parent, &predecessor));
    }
    SetModified(id);
    return old_parent_id;
  }

  // Pass the fake change list to |service|.
  void ApplyPendingChanges(ChangeProcessor* processor) {
    processor->ApplyChangesFromSyncModel(trans_,
        changes_.size() ? &changes_[0] : NULL, changes_.size());
  }

  const std::vector<sync_api::SyncManager::ChangeRecord>& changes() {
    return changes_;
  }

 private:
  // Helper function to push an ACTION_UPDATE record onto the back
  // of the changelist.
  void SetModified(int64 id) {
    // Coalesce multi-property edits.
    if (!changes_.empty() && changes_.back().id == id &&
        changes_.back().action ==
        sync_api::SyncManager::ChangeRecord::ACTION_UPDATE)
      return;
    sync_api::SyncManager::ChangeRecord record;
    record.action = sync_api::SyncManager::ChangeRecord::ACTION_UPDATE;
    record.id = id;
    changes_.push_back(record);
  }

  // The transaction on which everything happens.
  sync_api::WriteTransaction *trans_;

  // The change list we construct.
  std::vector<sync_api::SyncManager::ChangeRecord> changes_;
};

class MockUnrecoverableErrorHandler : public UnrecoverableErrorHandler {
 public:
  MOCK_METHOD2(OnUnrecoverableError,
               void(const tracked_objects::Location&, const std::string&));
};

class ProfileSyncServiceBookmarkTest : public testing::Test {
 protected:
  enum LoadOption { LOAD_FROM_STORAGE, DELETE_EXISTING_STORAGE };
  enum SaveOption { SAVE_TO_STORAGE, DONT_SAVE_TO_STORAGE };

  ProfileSyncServiceBookmarkTest()
      : ui_thread_(BrowserThread::UI, &message_loop_),
        file_thread_(BrowserThread::FILE, &message_loop_),
        model_(NULL) {
  }

  virtual ~ProfileSyncServiceBookmarkTest() {
    StopSync();
    UnloadBookmarkModel();
  }

  virtual void SetUp() {
    test_user_share_.SetUp();
  }

  virtual void TearDown() {
    test_user_share_.TearDown();
  }

  // Load (or re-load) the bookmark model.  |load| controls use of the
  // bookmarks file on disk.  |save| controls whether the newly loaded
  // bookmark model will write out a bookmark file as it goes.
  void LoadBookmarkModel(LoadOption load, SaveOption save) {
    bool delete_bookmarks = load == DELETE_EXISTING_STORAGE;
    profile_.CreateBookmarkModel(delete_bookmarks);
    model_ = profile_.GetBookmarkModel();
    // Wait for the bookmarks model to load.
    profile_.BlockUntilBookmarkModelLoaded();
    // This noticeably speeds up the unit tests that request it.
    if (save == DONT_SAVE_TO_STORAGE)
      model_->ClearStore();
    message_loop_.RunAllPending();
  }

  void StartSync() {
    // Set up model associator.
    model_associator_.reset(new TestBookmarkModelAssociator(
        profile_.GetBookmarkModel(),
        test_user_share_.user_share(),
        &mock_unrecoverable_error_handler_));
    EXPECT_TRUE(model_associator_->AssociateModels());
    MessageLoop::current()->RunAllPending();

    // Set up change processor.
    change_processor_.reset(
        new BookmarkChangeProcessor(model_associator_.get(),
                                    &mock_unrecoverable_error_handler_));
    change_processor_->Start(&profile_, test_user_share_.user_share());
  }

  void StopSync() {
    change_processor_->Stop();
    change_processor_.reset();

    EXPECT_TRUE(model_associator_->DisassociateModels());
    model_associator_.reset();

    message_loop_.RunAllPending();

    // TODO(akalin): Actually close the database and flush it to disk
    // (and make StartSync reload from disk).  This would require
    // refactoring TestUserShare.
  }

  void UnloadBookmarkModel() {
    profile_.CreateBookmarkModel(false /* delete_bookmarks */);
    model_ = NULL;
    message_loop_.RunAllPending();
  }

  bool InitSyncNodeFromChromeNode(const BookmarkNode* bnode,
                                  sync_api::BaseNode* sync_node) {
    return model_associator_->InitSyncNodeFromChromeId(bnode->id(),
                                                       sync_node);
  }

  void ExpectSyncerNodeMatching(sync_api::BaseTransaction* trans,
                                const BookmarkNode* bnode) {
    sync_api::ReadNode gnode(trans);
    ASSERT_TRUE(InitSyncNodeFromChromeNode(bnode, &gnode));
    // Non-root node titles and parents must match.
    if (bnode != model_->GetBookmarkBarNode() &&
        bnode != model_->other_node()) {
      EXPECT_EQ(bnode->GetTitle(), WideToUTF16Hack(gnode.GetTitle()));
      EXPECT_EQ(
          model_associator_->GetChromeNodeFromSyncId(gnode.GetParentId()),
          bnode->parent());
    }
    EXPECT_EQ(bnode->is_folder(), gnode.GetIsFolder());
    if (bnode->is_url())
      EXPECT_EQ(bnode->GetURL(), gnode.GetURL());

    // Check for position matches.
    int browser_index = bnode->parent()->GetIndexOf(bnode);
    if (browser_index == 0) {
      EXPECT_EQ(gnode.GetPredecessorId(), 0);
    } else {
      const BookmarkNode* bprev =
          bnode->parent()->GetChild(browser_index - 1);
      sync_api::ReadNode gprev(trans);
      ASSERT_TRUE(InitSyncNodeFromChromeNode(bprev, &gprev));
      EXPECT_EQ(gnode.GetPredecessorId(), gprev.GetId());
      EXPECT_EQ(gnode.GetParentId(), gprev.GetParentId());
    }
    if (browser_index == bnode->parent()->child_count() - 1) {
      EXPECT_EQ(gnode.GetSuccessorId(), 0);
    } else {
      const BookmarkNode* bnext =
          bnode->parent()->GetChild(browser_index + 1);
      sync_api::ReadNode gnext(trans);
      ASSERT_TRUE(InitSyncNodeFromChromeNode(bnext, &gnext));
      EXPECT_EQ(gnode.GetSuccessorId(), gnext.GetId());
      EXPECT_EQ(gnode.GetParentId(), gnext.GetParentId());
    }
    if (bnode->child_count()) {
      EXPECT_TRUE(gnode.GetFirstChildId());
    }
  }

  void ExpectSyncerNodeMatching(const BookmarkNode* bnode) {
    sync_api::ReadTransaction trans(test_user_share_.user_share());
    ExpectSyncerNodeMatching(&trans, bnode);
  }

  void ExpectBrowserNodeMatching(sync_api::BaseTransaction* trans,
                                 int64 sync_id) {
    EXPECT_TRUE(sync_id);
    const BookmarkNode* bnode =
        model_associator_->GetChromeNodeFromSyncId(sync_id);
    ASSERT_TRUE(bnode);
    int64 id = model_associator_->GetSyncIdFromChromeId(bnode->id());
    EXPECT_EQ(id, sync_id);
    ExpectSyncerNodeMatching(trans, bnode);
  }

  void ExpectBrowserNodeUnknown(int64 sync_id) {
    EXPECT_FALSE(model_associator_->GetChromeNodeFromSyncId(sync_id));
  }

  void ExpectBrowserNodeKnown(int64 sync_id) {
    EXPECT_TRUE(model_associator_->GetChromeNodeFromSyncId(sync_id));
  }

  void ExpectSyncerNodeKnown(const BookmarkNode* node) {
    int64 sync_id = model_associator_->GetSyncIdFromChromeId(node->id());
    EXPECT_NE(sync_id, sync_api::kInvalidId);
  }

  void ExpectSyncerNodeUnknown(const BookmarkNode* node) {
    int64 sync_id = model_associator_->GetSyncIdFromChromeId(node->id());
    EXPECT_EQ(sync_id, sync_api::kInvalidId);
  }

  void ExpectBrowserNodeTitle(int64 sync_id, const std::wstring& title) {
    const BookmarkNode* bnode =
        model_associator_->GetChromeNodeFromSyncId(sync_id);
    ASSERT_TRUE(bnode);
    EXPECT_EQ(bnode->GetTitle(), WideToUTF16Hack(title));
  }

  void ExpectBrowserNodeURL(int64 sync_id, const std::string& url) {
    const BookmarkNode* bnode =
        model_associator_->GetChromeNodeFromSyncId(sync_id);
    ASSERT_TRUE(bnode);
    EXPECT_EQ(GURL(url), bnode->GetURL());
  }

  void ExpectBrowserNodeParent(int64 sync_id, int64 parent_sync_id) {
    const BookmarkNode* node =
        model_associator_->GetChromeNodeFromSyncId(sync_id);
    ASSERT_TRUE(node);
    const BookmarkNode* parent =
        model_associator_->GetChromeNodeFromSyncId(parent_sync_id);
    EXPECT_TRUE(parent);
    EXPECT_EQ(node->parent(), parent);
  }

  void ExpectModelMatch(sync_api::BaseTransaction* trans) {
    const BookmarkNode* root = model_->root_node();
    EXPECT_EQ(root->GetIndexOf(model_->GetBookmarkBarNode()), 0);
    EXPECT_EQ(root->GetIndexOf(model_->other_node()), 1);

    std::stack<int64> stack;
    stack.push(bookmark_bar_id());
    while (!stack.empty()) {
      int64 id = stack.top();
      stack.pop();
      if (!id) continue;

      ExpectBrowserNodeMatching(trans, id);

      sync_api::ReadNode gnode(trans);
      ASSERT_TRUE(gnode.InitByIdLookup(id));
      stack.push(gnode.GetFirstChildId());
      stack.push(gnode.GetSuccessorId());
    }
  }

  void ExpectModelMatch() {
    sync_api::ReadTransaction trans(test_user_share_.user_share());
    ExpectModelMatch(&trans);
  }

  int64 other_bookmarks_id() {
    return
        model_associator_->GetSyncIdFromChromeId(model_->other_node()->id());
  }

  int64 bookmark_bar_id() {
    return model_associator_->GetSyncIdFromChromeId(
        model_->GetBookmarkBarNode()->id());
  }

 private:
  // Used by both |ui_thread_| and |file_thread_|.
  MessageLoop message_loop_;
  BrowserThread ui_thread_;
  // Needed by |model_|.
  BrowserThread file_thread_;

  TestingProfile profile_;
  scoped_ptr<TestBookmarkModelAssociator> model_associator_;

 protected:
  BookmarkModel* model_;
  TestUserShare test_user_share_;
  scoped_ptr<BookmarkChangeProcessor> change_processor_;
  StrictMock<MockUnrecoverableErrorHandler> mock_unrecoverable_error_handler_;
};

TEST_F(ProfileSyncServiceBookmarkTest, InitialState) {
  LoadBookmarkModel(DELETE_EXISTING_STORAGE, DONT_SAVE_TO_STORAGE);
  StartSync();

  EXPECT_TRUE(other_bookmarks_id());
  EXPECT_TRUE(bookmark_bar_id());

  ExpectModelMatch();
}

TEST_F(ProfileSyncServiceBookmarkTest, BookmarkModelOperations) {
  LoadBookmarkModel(DELETE_EXISTING_STORAGE, DONT_SAVE_TO_STORAGE);
  StartSync();

  // Test addition.
  const BookmarkNode* folder =
      model_->AddFolder(model_->other_node(), 0, ASCIIToUTF16("foobar"));
  ExpectSyncerNodeMatching(folder);
  ExpectModelMatch();
  const BookmarkNode* folder2 =
      model_->AddFolder(folder, 0, ASCIIToUTF16("nested"));
  ExpectSyncerNodeMatching(folder2);
  ExpectModelMatch();
  const BookmarkNode* url1 = model_->AddURL(
      folder, 0, ASCIIToUTF16("Internets #1 Pies Site"),
      GURL("http://www.easypie.com/"));
  ExpectSyncerNodeMatching(url1);
  ExpectModelMatch();
  const BookmarkNode* url2 = model_->AddURL(
      folder, 1, ASCIIToUTF16("Airplanes"), GURL("http://www.easyjet.com/"));
  ExpectSyncerNodeMatching(url2);
  ExpectModelMatch();

  // Test modification.
  model_->SetTitle(url2, ASCIIToUTF16("EasyJet"));
  ExpectModelMatch();
  model_->Move(url1, folder2, 0);
  ExpectModelMatch();
  model_->Move(folder2, model_->GetBookmarkBarNode(), 0);
  ExpectModelMatch();
  model_->SetTitle(folder2, ASCIIToUTF16("Not Nested"));
  ExpectModelMatch();
  model_->Move(folder, folder2, 0);
  ExpectModelMatch();
  model_->SetTitle(folder, ASCIIToUTF16("who's nested now?"));
  ExpectModelMatch();
  model_->Copy(url2, model_->GetBookmarkBarNode(), 0);
  ExpectModelMatch();

  // Test deletion.
  // Delete a single item.
  model_->Remove(url2->parent(), url2->parent()->GetIndexOf(url2));
  ExpectModelMatch();
  // Delete an item with several children.
  model_->Remove(folder2->parent(),
                 folder2->parent()->GetIndexOf(folder2));
  ExpectModelMatch();
}

TEST_F(ProfileSyncServiceBookmarkTest, ServerChangeProcessing) {
  LoadBookmarkModel(DELETE_EXISTING_STORAGE, DONT_SAVE_TO_STORAGE);
  StartSync();

  sync_api::WriteTransaction trans(test_user_share_.user_share());

  FakeServerChange adds(&trans);
  int64 f1 = adds.AddFolder(L"Server Folder B", bookmark_bar_id(), 0);
  int64 f2 = adds.AddFolder(L"Server Folder A", bookmark_bar_id(), f1);
  int64 u1 = adds.AddURL(L"Some old site", "ftp://nifty.andrew.cmu.edu/",
                         bookmark_bar_id(), f2);
  int64 u2 = adds.AddURL(L"Nifty", "ftp://nifty.andrew.cmu.edu/", f1, 0);
  // u3 is a duplicate URL
  int64 u3 = adds.AddURL(L"Nifty2", "ftp://nifty.andrew.cmu.edu/", f1, u2);
  // u4 is a duplicate title, different URL.
  adds.AddURL(L"Some old site", "http://slog.thestranger.com/",
              bookmark_bar_id(), u1);
  // u5 tests an empty-string title.
  std::string javascript_url(
      "javascript:(function(){var w=window.open(" \
      "'about:blank','gnotesWin','location=0,menubar=0," \
      "scrollbars=0,status=0,toolbar=0,width=300," \
      "height=300,resizable');});");
  adds.AddURL(L"", javascript_url, other_bookmarks_id(), 0);

  std::vector<sync_api::SyncManager::ChangeRecord>::const_iterator it;
  // The bookmark model shouldn't yet have seen any of the nodes of |adds|.
  for (it = adds.changes().begin(); it != adds.changes().end(); ++it)
    ExpectBrowserNodeUnknown(it->id);

  adds.ApplyPendingChanges(change_processor_.get());

  // Make sure the bookmark model received all of the nodes in |adds|.
  for (it = adds.changes().begin(); it != adds.changes().end(); ++it)
    ExpectBrowserNodeMatching(&trans, it->id);
  ExpectModelMatch(&trans);

  // Part two: test modifications.
  FakeServerChange mods(&trans);
  // Mess with u2, and move it into empty folder f2
  // TODO(ncarter): Determine if we allow ModifyURL ops or not.
  /* std::wstring u2_old_url = mods.ModifyURL(u2, L"http://www.google.com"); */
  std::wstring u2_old_title = mods.ModifyTitle(u2, L"The Google");
  int64 u2_old_parent = mods.ModifyPosition(u2, f2, 0);

  // Now move f1 after u2.
  std::wstring f1_old_title = mods.ModifyTitle(f1, L"Server Folder C");
  int64 f1_old_parent = mods.ModifyPosition(f1, f2, u2);

  // Then add u3 after f1.
  int64 u3_old_parent = mods.ModifyPosition(u3, f2, f1);

  // Test that the property changes have not yet taken effect.
  ExpectBrowserNodeTitle(u2, u2_old_title);
  /* ExpectBrowserNodeURL(u2, u2_old_url); */
  ExpectBrowserNodeParent(u2, u2_old_parent);

  ExpectBrowserNodeTitle(f1, f1_old_title);
  ExpectBrowserNodeParent(f1, f1_old_parent);

  ExpectBrowserNodeParent(u3, u3_old_parent);

  // Apply the changes.
  mods.ApplyPendingChanges(change_processor_.get());

  // Check for successful application.
  for (it = mods.changes().begin(); it != mods.changes().end(); ++it)
    ExpectBrowserNodeMatching(&trans, it->id);
  ExpectModelMatch(&trans);

  // Part 3: Test URL deletion.
  FakeServerChange dels(&trans);
  dels.Delete(u2);
  dels.Delete(u3);

  ExpectBrowserNodeKnown(u2);
  ExpectBrowserNodeKnown(u3);

  dels.ApplyPendingChanges(change_processor_.get());

  ExpectBrowserNodeUnknown(u2);
  ExpectBrowserNodeUnknown(u3);
  ExpectModelMatch(&trans);
}

// Tests a specific case in ApplyModelChanges where we move the
// children out from under a parent, and then delete the parent
// in the same changelist.  The delete shows up first in the changelist,
// requiring the children to be moved to a temporary location.
TEST_F(ProfileSyncServiceBookmarkTest, ServerChangeRequiringFosterParent) {
  LoadBookmarkModel(DELETE_EXISTING_STORAGE, DONT_SAVE_TO_STORAGE);
  StartSync();

  sync_api::WriteTransaction trans(test_user_share_.user_share());

  // Stress the immediate children of other_node because that's where
  // ApplyModelChanges puts a temporary foster parent node.
  std::string url("http://dev.chromium.org/");
  FakeServerChange adds(&trans);
  int64 f0 = other_bookmarks_id();                 // + other_node
  int64 f1 = adds.AddFolder(L"f1",      f0, 0);    //   + f1
  int64 f2 = adds.AddFolder(L"f2",      f1, 0);    //     + f2
  int64 u3 = adds.AddURL(   L"u3", url, f2, 0);    //       + u3    NOLINT
  int64 u4 = adds.AddURL(   L"u4", url, f2, u3);   //       + u4    NOLINT
  int64 u5 = adds.AddURL(   L"u5", url, f1, f2);   //     + u5      NOLINT
  int64 f6 = adds.AddFolder(L"f6",      f1, u5);   //     + f6
  int64 u7 = adds.AddURL(   L"u7", url, f0, f1);   //   + u7        NOLINT

  std::vector<sync_api::SyncManager::ChangeRecord>::const_iterator it;
  // The bookmark model shouldn't yet have seen any of the nodes of |adds|.
  for (it = adds.changes().begin(); it != adds.changes().end(); ++it)
    ExpectBrowserNodeUnknown(it->id);

  adds.ApplyPendingChanges(change_processor_.get());

  // Make sure the bookmark model received all of the nodes in |adds|.
  for (it = adds.changes().begin(); it != adds.changes().end(); ++it)
    ExpectBrowserNodeMatching(&trans, it->id);
  ExpectModelMatch(&trans);

  // We have to do the moves before the deletions, but FakeServerChange will
  // put the deletion at the front of the changelist.
  FakeServerChange ops(&trans);
  ops.ModifyPosition(f6, other_bookmarks_id(), 0);
  ops.ModifyPosition(u3, other_bookmarks_id(), f1);  // Prev == f1 is OK here.
  ops.ModifyPosition(f2, other_bookmarks_id(), u7);
  ops.ModifyPosition(u7, f2, 0);
  ops.ModifyPosition(u4, other_bookmarks_id(), f2);
  ops.ModifyPosition(u5, f6, 0);
  ops.Delete(f1);

  ops.ApplyPendingChanges(change_processor_.get());

  ExpectModelMatch(&trans);
}

// Simulate a server change record containing a valid but non-canonical URL.
TEST_F(ProfileSyncServiceBookmarkTest, ServerChangeWithNonCanonicalURL) {
  LoadBookmarkModel(DELETE_EXISTING_STORAGE, SAVE_TO_STORAGE);
  StartSync();

  {
    sync_api::WriteTransaction trans(test_user_share_.user_share());

    FakeServerChange adds(&trans);
    std::string url("http://dev.chromium.org");
    EXPECT_NE(GURL(url).spec(), url);
    adds.AddURL(L"u1", url, other_bookmarks_id(), 0);

    adds.ApplyPendingChanges(change_processor_.get());

    EXPECT_TRUE(model_->other_node()->child_count() == 1);
    ExpectModelMatch(&trans);
  }

  // Now reboot the sync service, forcing a merge step.
  StopSync();
  LoadBookmarkModel(LOAD_FROM_STORAGE, SAVE_TO_STORAGE);
  StartSync();

  // There should still be just the one bookmark.
  EXPECT_TRUE(model_->other_node()->child_count() == 1);
  ExpectModelMatch();
}

// Simulate a server change record containing an invalid URL (per GURL).
// TODO(ncarter): Disabled due to crashes.  Fix bug 1677563.
TEST_F(ProfileSyncServiceBookmarkTest, DISABLED_ServerChangeWithInvalidURL) {
  LoadBookmarkModel(DELETE_EXISTING_STORAGE, SAVE_TO_STORAGE);
  StartSync();

  int child_count = 0;
  {
    sync_api::WriteTransaction trans(test_user_share_.user_share());

    FakeServerChange adds(&trans);
    std::string url("x");
    EXPECT_FALSE(GURL(url).is_valid());
    adds.AddURL(L"u1", url, other_bookmarks_id(), 0);

    adds.ApplyPendingChanges(change_processor_.get());

    // We're lenient about what should happen -- the model could wind up with
    // the node or without it; but things should be consistent, and we
    // shouldn't crash.
    child_count = model_->other_node()->child_count();
    EXPECT_TRUE(child_count == 0 || child_count == 1);
    ExpectModelMatch(&trans);
  }

  // Now reboot the sync service, forcing a merge step.
  StopSync();
  LoadBookmarkModel(LOAD_FROM_STORAGE, SAVE_TO_STORAGE);
  StartSync();

  // Things ought not to have changed.
  EXPECT_EQ(model_->other_node()->child_count(), child_count);
  ExpectModelMatch();
}


// Test strings that might pose a problem if the titles ever became used as
// file names in the sync backend.
TEST_F(ProfileSyncServiceBookmarkTest, CornerCaseNames) {
  // TODO(ncarter): Bug 1570238 explains the failure of this test.
  LoadBookmarkModel(DELETE_EXISTING_STORAGE, DONT_SAVE_TO_STORAGE);
  StartSync();

  const char* names[] = {
      // The empty string.
      "",
      // Illegal Windows filenames.
      "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4",
      "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3",
      "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
      // Current/parent directory markers.
      ".", "..", "...",
      // Files created automatically by the Windows shell.
      "Thumbs.db", ".DS_Store",
      // Names including Win32-illegal characters, and path separators.
      "foo/bar", "foo\\bar", "foo?bar", "foo:bar", "foo|bar", "foo\"bar",
      "foo'bar", "foo<bar", "foo>bar", "foo%bar", "foo*bar", "foo]bar",
      "foo[bar",
  };
  // Create both folders and bookmarks using each name.
  GURL url("http://www.doublemint.com");
  for (size_t i = 0; i < arraysize(names); ++i) {
    model_->AddFolder(model_->other_node(), 0, ASCIIToUTF16(names[i]));
    model_->AddURL(model_->other_node(), 0, ASCIIToUTF16(names[i]), url);
  }

  // Verify that the browser model matches the sync model.
  EXPECT_TRUE(model_->other_node()->child_count() == 2*arraysize(names));
  ExpectModelMatch();
}

// Stress the internal representation of position by sparse numbers. We want
// to repeatedly bisect the range of available positions, to force the
// syncer code to renumber its ranges.  Pick a number big enough so that it
// would exhaust 32bits of room between items a couple of times.
TEST_F(ProfileSyncServiceBookmarkTest, RepeatedMiddleInsertion) {
  LoadBookmarkModel(DELETE_EXISTING_STORAGE, DONT_SAVE_TO_STORAGE);
  StartSync();

  static const int kTimesToInsert = 256;

  // Create two book-end nodes to insert between.
  model_->AddFolder(model_->other_node(), 0, ASCIIToUTF16("Alpha"));
  model_->AddFolder(model_->other_node(), 1, ASCIIToUTF16("Omega"));
  int count = 2;

  // Test insertion in first half of range by repeatedly inserting in second
  // position.
  for (int i = 0; i < kTimesToInsert; ++i) {
    string16 title = ASCIIToUTF16("Pre-insertion ") + base::IntToString16(i);
    model_->AddFolder(model_->other_node(), 1, title);
    count++;
  }

  // Test insertion in second half of range by repeatedly inserting in
  // second-to-last position.
  for (int i = 0; i < kTimesToInsert; ++i) {
    string16 title = ASCIIToUTF16("Post-insertion ") + base::IntToString16(i);
    model_->AddFolder(model_->other_node(), count - 1, title);
    count++;
  }

  // Verify that the browser model matches the sync model.
  EXPECT_EQ(model_->other_node()->child_count(), count);
  ExpectModelMatch();
}

// Introduce a consistency violation into the model, and see that it
// puts itself into a lame, error state.
TEST_F(ProfileSyncServiceBookmarkTest, UnrecoverableErrorSuspendsService) {
  EXPECT_CALL(mock_unrecoverable_error_handler_,
              OnUnrecoverableError(_, _));

  LoadBookmarkModel(DELETE_EXISTING_STORAGE, DONT_SAVE_TO_STORAGE);
  StartSync();

  // Add a node which will be the target of the consistency violation.
  const BookmarkNode* node =
      model_->AddFolder(model_->other_node(), 0, ASCIIToUTF16("node"));
  ExpectSyncerNodeMatching(node);

  // Now destroy the syncer node as if we were the ProfileSyncService without
  // updating the ProfileSyncService state.  This should introduce
  // inconsistency between the two models.
  {
    sync_api::WriteTransaction trans(test_user_share_.user_share());
    sync_api::WriteNode sync_node(&trans);
    ASSERT_TRUE(InitSyncNodeFromChromeNode(node, &sync_node));
    sync_node.Remove();
  }
  // The models don't match at this point, but the ProfileSyncService
  // doesn't know it yet.
  ExpectSyncerNodeKnown(node);

  // Add a child to the inconsistent node.  This should cause detection of the
  // problem and the syncer should stop processing changes.
  model_->AddFolder(node, 0, ASCIIToUTF16("nested"));
}

// See what happens if we run model association when there are two exact URL
// duplicate bookmarks.  The BookmarkModelAssociator should not fall over when
// this happens.
TEST_F(ProfileSyncServiceBookmarkTest, MergeDuplicates) {
  LoadBookmarkModel(DELETE_EXISTING_STORAGE, SAVE_TO_STORAGE);
  StartSync();

  model_->AddURL(model_->other_node(), 0, ASCIIToUTF16("Dup"),
                 GURL("http://dup.com/"));
  model_->AddURL(model_->other_node(), 0, ASCIIToUTF16("Dup"),
                 GURL("http://dup.com/"));

  EXPECT_EQ(2, model_->other_node()->child_count());

  // Restart the sync service to trigger model association.
  StopSync();
  StartSync();

  EXPECT_EQ(2, model_->other_node()->child_count());
  ExpectModelMatch();
}

struct TestData {
  const wchar_t* title;
  const char* url;
};

// TODO(ncarter): Integrate the existing TestNode/PopulateNodeFromString code
// in the bookmark model unittest, to make it simpler to set up test data
// here (and reduce the amount of duplication among tests), and to reduce the
// duplication.
class ProfileSyncServiceBookmarkTestWithData
    : public ProfileSyncServiceBookmarkTest {
 protected:
  // Populates or compares children of the given bookmark node from/with the
  // given test data array with the given size.
  void PopulateFromTestData(const BookmarkNode* node,
                            const TestData* data,
                            int size);
  void CompareWithTestData(const BookmarkNode* node,
                           const TestData* data,
                           int size);

  void ExpectBookmarkModelMatchesTestData();
  void WriteTestDataToBookmarkModel();
};

namespace {

// Constants for bookmark model that looks like:
// |-- Bookmark bar
// |   |-- u2, http://www.u2.com/
// |   |-- f1
// |   |   |-- f1u4, http://www.f1u4.com/
// |   |   |-- f1u2, http://www.f1u2.com/
// |   |   |-- f1u3, http://www.f1u3.com/
// |   |   +-- f1u1, http://www.f1u1.com/
// |   |-- u1, http://www.u1.com/
// |   +-- f2
// |       |-- f2u2, http://www.f2u2.com/
// |       |-- f2u4, http://www.f2u4.com/
// |       |-- f2u3, http://www.f2u3.com/
// |       +-- f2u1, http://www.f2u1.com/
// +-- Other bookmarks
//     |-- f3
//     |   |-- f3u4, http://www.f3u4.com/
//     |   |-- f3u2, http://www.f3u2.com/
//     |   |-- f3u3, http://www.f3u3.com/
//     |   +-- f3u1, http://www.f3u1.com/
//     |-- u4, http://www.u4.com/
//     |-- u3, http://www.u3.com/
//     --- f4
//     |   |-- f4u1, http://www.f4u1.com/
//     |   |-- f4u2, http://www.f4u2.com/
//     |   |-- f4u3, http://www.f4u3.com/
//     |   +-- f4u4, http://www.f4u4.com/
//     |-- dup
//     |   +-- dupu1, http://www.dupu1.com/
//     +-- dup
//         +-- dupu2, http://www.dupu1.com/
//
static TestData kBookmarkBarChildren[] = {
  { L"u2", "http://www.u2.com/" },
  { L"f1", NULL },
  { L"u1", "http://www.u1.com/" },
  { L"f2", NULL },
};
static TestData kF1Children[] = {
  { L"f1u4", "http://www.f1u4.com/" },
  { L"f1u2", "http://www.f1u2.com/" },
  { L"f1u3", "http://www.f1u3.com/" },
  { L"f1u1", "http://www.f1u1.com/" },
};
static TestData kF2Children[] = {
  { L"f2u2", "http://www.f2u2.com/" },
  { L"f2u4", "http://www.f2u4.com/" },
  { L"f2u3", "http://www.f2u3.com/" },
  { L"f2u1", "http://www.f2u1.com/" },
};

static TestData kOtherBookmarkChildren[] = {
  { L"f3", NULL },
  { L"u4", "http://www.u4.com/" },
  { L"u3", "http://www.u3.com/" },
  { L"f4", NULL },
  { L"dup", NULL },
  { L"dup", NULL },
};
static TestData kF3Children[] = {
  { L"f3u4", "http://www.f3u4.com/" },
  { L"f3u2", "http://www.f3u2.com/" },
  { L"f3u3", "http://www.f3u3.com/" },
  { L"f3u1", "http://www.f3u1.com/" },
};
static TestData kF4Children[] = {
  { L"f4u1", "http://www.f4u1.com/" },
  { L"f4u2", "http://www.f4u2.com/" },
  { L"f4u3", "http://www.f4u3.com/" },
  { L"f4u4", "http://www.f4u4.com/" },
};
static TestData kDup1Children[] = {
  { L"dupu1", "http://www.dupu1.com/" },
};
static TestData kDup2Children[] = {
  { L"dupu2", "http://www.dupu2.com/" },
};

}  // anonymous namespace.

void ProfileSyncServiceBookmarkTestWithData::PopulateFromTestData(
    const BookmarkNode* node, const TestData* data, int size) {
  DCHECK(node);
  DCHECK(data);
  DCHECK(node->is_folder());
  for (int i = 0; i < size; ++i) {
    const TestData& item = data[i];
    if (item.url) {
      model_->AddURL(node, i, WideToUTF16Hack(item.title), GURL(item.url));
    } else {
      model_->AddFolder(node, i, WideToUTF16Hack(item.title));
    }
  }
}

void ProfileSyncServiceBookmarkTestWithData::CompareWithTestData(
    const BookmarkNode* node, const TestData* data, int size) {
  DCHECK(node);
  DCHECK(data);
  DCHECK(node->is_folder());
  ASSERT_EQ(size, node->child_count());
  for (int i = 0; i < size; ++i) {
    const BookmarkNode* child_node = node->GetChild(i);
    const TestData& item = data[i];
    EXPECT_EQ(child_node->GetTitle(), WideToUTF16Hack(item.title));
    if (item.url) {
      EXPECT_FALSE(child_node->is_folder());
      EXPECT_TRUE(child_node->is_url());
      EXPECT_EQ(child_node->GetURL(), GURL(item.url));
    } else {
      EXPECT_TRUE(child_node->is_folder());
      EXPECT_FALSE(child_node->is_url());
    }
  }
}

// TODO(munjal): We should implement some way of generating random data and can
// use the same seed to generate the same sequence.
void ProfileSyncServiceBookmarkTestWithData::WriteTestDataToBookmarkModel() {
  const BookmarkNode* bookmarks_bar_node = model_->GetBookmarkBarNode();
  PopulateFromTestData(bookmarks_bar_node,
                       kBookmarkBarChildren,
                       arraysize(kBookmarkBarChildren));

  ASSERT_GE(bookmarks_bar_node->child_count(), 4);
  const BookmarkNode* f1_node = bookmarks_bar_node->GetChild(1);
  PopulateFromTestData(f1_node, kF1Children, arraysize(kF1Children));
  const BookmarkNode* f2_node = bookmarks_bar_node->GetChild(3);
  PopulateFromTestData(f2_node, kF2Children, arraysize(kF2Children));

  const BookmarkNode* other_bookmarks_node = model_->other_node();
  PopulateFromTestData(other_bookmarks_node,
                       kOtherBookmarkChildren,
                       arraysize(kOtherBookmarkChildren));

  ASSERT_GE(other_bookmarks_node->child_count(), 6);
  const BookmarkNode* f3_node = other_bookmarks_node->GetChild(0);
  PopulateFromTestData(f3_node, kF3Children, arraysize(kF3Children));
  const BookmarkNode* f4_node = other_bookmarks_node->GetChild(3);
  PopulateFromTestData(f4_node, kF4Children, arraysize(kF4Children));
  const BookmarkNode* dup_node = other_bookmarks_node->GetChild(4);
  PopulateFromTestData(dup_node, kDup1Children, arraysize(kDup1Children));
  dup_node = other_bookmarks_node->GetChild(5);
  PopulateFromTestData(dup_node, kDup2Children, arraysize(kDup2Children));

  ExpectBookmarkModelMatchesTestData();
}

void ProfileSyncServiceBookmarkTestWithData::
    ExpectBookmarkModelMatchesTestData() {
  const BookmarkNode* bookmark_bar_node = model_->GetBookmarkBarNode();
  CompareWithTestData(bookmark_bar_node,
                      kBookmarkBarChildren,
                      arraysize(kBookmarkBarChildren));

  ASSERT_GE(bookmark_bar_node->child_count(), 4);
  const BookmarkNode* f1_node = bookmark_bar_node->GetChild(1);
  CompareWithTestData(f1_node, kF1Children, arraysize(kF1Children));
  const BookmarkNode* f2_node = bookmark_bar_node->GetChild(3);
  CompareWithTestData(f2_node, kF2Children, arraysize(kF2Children));

  const BookmarkNode* other_bookmarks_node = model_->other_node();
  CompareWithTestData(other_bookmarks_node,
                      kOtherBookmarkChildren,
                      arraysize(kOtherBookmarkChildren));

  ASSERT_GE(other_bookmarks_node->child_count(), 6);
  const BookmarkNode* f3_node = other_bookmarks_node->GetChild(0);
  CompareWithTestData(f3_node, kF3Children, arraysize(kF3Children));
  const BookmarkNode* f4_node = other_bookmarks_node->GetChild(3);
  CompareWithTestData(f4_node, kF4Children, arraysize(kF4Children));
  const BookmarkNode* dup_node = other_bookmarks_node->GetChild(4);
  CompareWithTestData(dup_node, kDup1Children, arraysize(kDup1Children));
  dup_node = other_bookmarks_node->GetChild(5);
  CompareWithTestData(dup_node, kDup2Children, arraysize(kDup2Children));
}

// Tests persistence of the profile sync service by unloading the
// database and then reloading it from disk.
TEST_F(ProfileSyncServiceBookmarkTestWithData, Persistence) {
  LoadBookmarkModel(DELETE_EXISTING_STORAGE, SAVE_TO_STORAGE);
  StartSync();

  WriteTestDataToBookmarkModel();

  ExpectModelMatch();

  // Force both models to discard their data and reload from disk.  This
  // simulates what would happen if the browser were to shutdown normally,
  // and then relaunch.
  StopSync();
  UnloadBookmarkModel();
  LoadBookmarkModel(LOAD_FROM_STORAGE, SAVE_TO_STORAGE);
  StartSync();

  ExpectBookmarkModelMatchesTestData();

  // With the BookmarkModel contents verified, ExpectModelMatch will
  // verify the contents of the sync model.
  ExpectModelMatch();
}

// Tests the merge case when the BookmarkModel is non-empty but the
// sync model is empty.  This corresponds to uploading browser
// bookmarks to an initially empty, new account.
TEST_F(ProfileSyncServiceBookmarkTestWithData, MergeWithEmptySyncModel) {
  // Don't start the sync service until we've populated the bookmark model.
  LoadBookmarkModel(DELETE_EXISTING_STORAGE, SAVE_TO_STORAGE);

  WriteTestDataToBookmarkModel();

  // Restart sync.  This should trigger a merge step during
  // initialization -- we expect the browser bookmarks to be written
  // to the sync service during this call.
  StartSync();

  // Verify that the bookmark model hasn't changed, and that the sync model
  // matches it exactly.
  ExpectBookmarkModelMatchesTestData();
  ExpectModelMatch();
}

// Tests the merge case when the BookmarkModel is empty but the sync model is
// non-empty.  This corresponds (somewhat) to a clean install of the browser,
// with no bookmarks, connecting to a sync account that has some bookmarks.
TEST_F(ProfileSyncServiceBookmarkTestWithData, MergeWithEmptyBookmarkModel) {
  LoadBookmarkModel(DELETE_EXISTING_STORAGE, DONT_SAVE_TO_STORAGE);
  StartSync();

  WriteTestDataToBookmarkModel();

  ExpectModelMatch();

  // Force the databse to unload and write itself to disk.
  StopSync();

  // Blow away the bookmark model -- it should be empty afterwards.
  UnloadBookmarkModel();
  LoadBookmarkModel(DELETE_EXISTING_STORAGE, DONT_SAVE_TO_STORAGE);
  EXPECT_EQ(model_->GetBookmarkBarNode()->child_count(), 0);
  EXPECT_EQ(model_->other_node()->child_count(), 0);

  // Now restart the sync service.  Starting it should populate the bookmark
  // model -- test for consistency.
  StartSync();
  ExpectBookmarkModelMatchesTestData();
  ExpectModelMatch();
}

// Tests the merge cases when both the models are expected to be identical
// after the merge.
TEST_F(ProfileSyncServiceBookmarkTestWithData, MergeExpectedIdenticalModels) {
  LoadBookmarkModel(DELETE_EXISTING_STORAGE, SAVE_TO_STORAGE);
  StartSync();
  WriteTestDataToBookmarkModel();
  ExpectModelMatch();
  StopSync();
  UnloadBookmarkModel();

  // At this point both the bookmark model and the server should have the
  // exact same data and it should match the test data.
  LoadBookmarkModel(LOAD_FROM_STORAGE, DONT_SAVE_TO_STORAGE);
  StartSync();
  ExpectBookmarkModelMatchesTestData();
  ExpectModelMatch();
  StopSync();
  UnloadBookmarkModel();

  // Now reorder some bookmarks in the bookmark model and then merge. Make
  // sure we get the order of the server after merge.
  LoadBookmarkModel(LOAD_FROM_STORAGE, DONT_SAVE_TO_STORAGE);
  ExpectBookmarkModelMatchesTestData();
  const BookmarkNode* bookmark_bar = model_->GetBookmarkBarNode();
  ASSERT_TRUE(bookmark_bar);
  ASSERT_GT(bookmark_bar->child_count(), 1);
  model_->Move(bookmark_bar->GetChild(0), bookmark_bar, 1);
  StartSync();
  ExpectModelMatch();
  ExpectBookmarkModelMatchesTestData();
}

// Tests the merge cases when both the models are expected to be identical
// after the merge.
TEST_F(ProfileSyncServiceBookmarkTestWithData, MergeModelsWithSomeExtras) {
  LoadBookmarkModel(DELETE_EXISTING_STORAGE, DONT_SAVE_TO_STORAGE);
  WriteTestDataToBookmarkModel();
  ExpectBookmarkModelMatchesTestData();

  // Remove some nodes and reorder some nodes.
  const BookmarkNode* bookmark_bar_node = model_->GetBookmarkBarNode();
  int remove_index = 2;
  ASSERT_GT(bookmark_bar_node->child_count(), remove_index);
  const BookmarkNode* child_node = bookmark_bar_node->GetChild(remove_index);
  ASSERT_TRUE(child_node);
  ASSERT_TRUE(child_node->is_url());
  model_->Remove(bookmark_bar_node, remove_index);
  ASSERT_GT(bookmark_bar_node->child_count(), remove_index);
  child_node = bookmark_bar_node->GetChild(remove_index);
  ASSERT_TRUE(child_node);
  ASSERT_TRUE(child_node->is_folder());
  model_->Remove(bookmark_bar_node, remove_index);

  const BookmarkNode* other_node = model_->other_node();
  ASSERT_GE(other_node->child_count(), 1);
  const BookmarkNode* f3_node = other_node->GetChild(0);
  ASSERT_TRUE(f3_node);
  ASSERT_TRUE(f3_node->is_folder());
  remove_index = 2;
  ASSERT_GT(f3_node->child_count(), remove_index);
  model_->Remove(f3_node, remove_index);
  ASSERT_GT(f3_node->child_count(), remove_index);
  model_->Remove(f3_node, remove_index);

  StartSync();
  ExpectModelMatch();
  StopSync();

  LoadBookmarkModel(DELETE_EXISTING_STORAGE, DONT_SAVE_TO_STORAGE);
  WriteTestDataToBookmarkModel();
  ExpectBookmarkModelMatchesTestData();

  // Remove some nodes and reorder some nodes.
  bookmark_bar_node = model_->GetBookmarkBarNode();
  remove_index = 0;
  ASSERT_GT(bookmark_bar_node->child_count(), remove_index);
  child_node = bookmark_bar_node->GetChild(remove_index);
  ASSERT_TRUE(child_node);
  ASSERT_TRUE(child_node->is_url());
  model_->Remove(bookmark_bar_node, remove_index);
  ASSERT_GT(bookmark_bar_node->child_count(), remove_index);
  child_node = bookmark_bar_node->GetChild(remove_index);
  ASSERT_TRUE(child_node);
  ASSERT_TRUE(child_node->is_folder());
  model_->Remove(bookmark_bar_node, remove_index);

  ASSERT_GE(bookmark_bar_node->child_count(), 2);
  model_->Move(bookmark_bar_node->GetChild(0), bookmark_bar_node, 1);

  other_node = model_->other_node();
  ASSERT_GE(other_node->child_count(), 1);
  f3_node = other_node->GetChild(0);
  ASSERT_TRUE(f3_node);
  ASSERT_TRUE(f3_node->is_folder());
  remove_index = 0;
  ASSERT_GT(f3_node->child_count(), remove_index);
  model_->Remove(f3_node, remove_index);
  ASSERT_GT(f3_node->child_count(), remove_index);
  model_->Remove(f3_node, remove_index);

  ASSERT_GE(other_node->child_count(), 4);
  model_->Move(other_node->GetChild(0), other_node, 1);
  model_->Move(other_node->GetChild(2), other_node, 3);

  StartSync();
  ExpectModelMatch();

  // After the merge, the model should match the test data.
  ExpectBookmarkModelMatchesTestData();
}

// Tests that when persisted model associations are used, things work fine.
TEST_F(ProfileSyncServiceBookmarkTestWithData, ModelAssociationPersistence) {
  LoadBookmarkModel(DELETE_EXISTING_STORAGE, DONT_SAVE_TO_STORAGE);
  WriteTestDataToBookmarkModel();
  StartSync();
  ExpectModelMatch();
  // Force sync to shut down and write itself to disk.
  StopSync();
  // Now restart sync. This time it should use the persistent
  // associations.
  StartSync();
  ExpectModelMatch();
}

// Tests that when persisted model associations are used, things work fine.
TEST_F(ProfileSyncServiceBookmarkTestWithData,
       ModelAssociationInvalidPersistence) {
  LoadBookmarkModel(DELETE_EXISTING_STORAGE, DONT_SAVE_TO_STORAGE);
  WriteTestDataToBookmarkModel();
  StartSync();
  ExpectModelMatch();
  // Force sync to shut down and write itself to disk.
  StopSync();
  // Change the bookmark model before restarting sync service to simulate
  // the situation where bookmark model is different from sync model and
  // make sure model associator correctly rebuilds associations.
  const BookmarkNode* bookmark_bar_node = model_->GetBookmarkBarNode();
  model_->AddURL(bookmark_bar_node, 0, ASCIIToUTF16("xtra"),
                 GURL("http://www.xtra.com"));
  // Now restart sync. This time it will try to use the persistent
  // associations and realize that they are invalid and hence will rebuild
  // associations.
  StartSync();
  ExpectModelMatch();
}

TEST_F(ProfileSyncServiceBookmarkTestWithData, SortChildren) {
  LoadBookmarkModel(DELETE_EXISTING_STORAGE, DONT_SAVE_TO_STORAGE);
  StartSync();

  // Write test data to bookmark model and verify that the models match.
  WriteTestDataToBookmarkModel();
  const BookmarkNode* folder_added = model_->other_node()->GetChild(0);
  ASSERT_TRUE(folder_added);
  ASSERT_TRUE(folder_added->is_folder());

  ExpectModelMatch();

  // Sort the other-bookmarks children and expect that hte models match.
  model_->SortChildren(folder_added);
  ExpectModelMatch();
}

// See what happens if we enable sync but then delete the "Sync Data"
// folder.
TEST_F(ProfileSyncServiceBookmarkTestWithData,
       RecoverAfterDeletingSyncDataDirectory) {
  LoadBookmarkModel(DELETE_EXISTING_STORAGE, SAVE_TO_STORAGE);
  StartSync();

  WriteTestDataToBookmarkModel();

  StopSync();

  // Nuke the sync DB and reload.
  test_user_share_.TearDown();
  test_user_share_.SetUp();

  StartSync();

  // Make sure we're back in sync.  In real life, the user would need
  // to reauthenticate before this happens, but in the test, authentication
  // is sidestepped.
  ExpectBookmarkModelMatchesTestData();
  ExpectModelMatch();
}

}  // namespace

}  // namespace browser_sync
