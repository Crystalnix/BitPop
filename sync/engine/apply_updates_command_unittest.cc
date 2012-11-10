// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/format_macros.h"
#include "base/location.h"
#include "base/memory/scoped_ptr.h"
#include "base/stringprintf.h"
#include "sync/engine/apply_updates_command.h"
#include "sync/engine/syncer.h"
#include "sync/internal_api/public/test/test_entry_factory.h"
#include "sync/protocol/bookmark_specifics.pb.h"
#include "sync/protocol/password_specifics.pb.h"
#include "sync/sessions/sync_session.h"
#include "sync/syncable/mutable_entry.h"
#include "sync/syncable/nigori_util.h"
#include "sync/syncable/read_transaction.h"
#include "sync/syncable/syncable_id.h"
#include "sync/syncable/syncable_util.h"
#include "sync/syncable/write_transaction.h"
#include "sync/test/engine/fake_model_worker.h"
#include "sync/test/engine/syncer_command_test.h"
#include "sync/test/engine/test_id_factory.h"
#include "sync/test/fake_encryptor.h"
#include "sync/util/cryptographer.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace syncer {

using sessions::SyncSession;
using std::string;
using syncable::Id;
using syncable::MutableEntry;
using syncable::UNITTEST;
using syncable::WriteTransaction;

namespace {
sync_pb::EntitySpecifics DefaultBookmarkSpecifics() {
  sync_pb::EntitySpecifics result;
  AddDefaultFieldValue(BOOKMARKS, &result);
  return result;
}
} // namespace

// A test fixture for tests exercising ApplyUpdatesCommand.
class ApplyUpdatesCommandTest : public SyncerCommandTest {
 public:
 protected:
  ApplyUpdatesCommandTest() {}
  virtual ~ApplyUpdatesCommandTest() {}

  virtual void SetUp() {
    workers()->clear();
    mutable_routing_info()->clear();
    workers()->push_back(
        make_scoped_refptr(new FakeModelWorker(GROUP_UI)));
    workers()->push_back(
        make_scoped_refptr(new FakeModelWorker(GROUP_PASSWORD)));
    (*mutable_routing_info())[BOOKMARKS] = GROUP_UI;
    (*mutable_routing_info())[PASSWORDS] = GROUP_PASSWORD;
    (*mutable_routing_info())[NIGORI] = GROUP_PASSIVE;
    SyncerCommandTest::SetUp();
    entry_factory_.reset(new TestEntryFactory(directory()));
    ExpectNoGroupsToChange(apply_updates_command_);
  }

  ApplyUpdatesCommand apply_updates_command_;
  FakeEncryptor encryptor_;
  TestIdFactory id_factory_;
  scoped_ptr<TestEntryFactory> entry_factory_;
 private:
  DISALLOW_COPY_AND_ASSIGN(ApplyUpdatesCommandTest);
};

TEST_F(ApplyUpdatesCommandTest, Simple) {
  string root_server_id = syncable::GetNullId().GetServerId();
  entry_factory_->CreateUnappliedNewItemWithParent("parent",
                                                   DefaultBookmarkSpecifics(),
                                                   root_server_id);
  entry_factory_->CreateUnappliedNewItemWithParent("child",
                                                   DefaultBookmarkSpecifics(),
                                                   "parent");

  ExpectGroupToChange(apply_updates_command_, GROUP_UI);
  apply_updates_command_.ExecuteImpl(session());

  sessions::StatusController* status = session()->mutable_status_controller();

  sessions::ScopedModelSafeGroupRestriction r(status, GROUP_UI);
  ASSERT_TRUE(status->update_progress());
  EXPECT_EQ(2, status->update_progress()->AppliedUpdatesSize())
      << "All updates should have been attempted";
  ASSERT_TRUE(status->conflict_progress());
  EXPECT_EQ(0, status->conflict_progress()->SimpleConflictingItemsSize())
      << "Simple update shouldn't result in conflicts";
  EXPECT_EQ(0, status->conflict_progress()->EncryptionConflictingItemsSize())
      << "Simple update shouldn't result in conflicts";
  EXPECT_EQ(0, status->conflict_progress()->HierarchyConflictingItemsSize())
      << "Simple update shouldn't result in conflicts";
  EXPECT_EQ(2, status->update_progress()->SuccessfullyAppliedUpdateCount())
      << "All items should have been successfully applied";
}

TEST_F(ApplyUpdatesCommandTest, UpdateWithChildrenBeforeParents) {
  // Set a bunch of updates which are difficult to apply in the order
  // they're received due to dependencies on other unseen items.
  string root_server_id = syncable::GetNullId().GetServerId();
  entry_factory_->CreateUnappliedNewItemWithParent(
      "a_child_created_first", DefaultBookmarkSpecifics(), "parent");
  entry_factory_->CreateUnappliedNewItemWithParent(
      "x_child_created_first", DefaultBookmarkSpecifics(), "parent");
  entry_factory_->CreateUnappliedNewItemWithParent(
      "parent", DefaultBookmarkSpecifics(), root_server_id);
  entry_factory_->CreateUnappliedNewItemWithParent(
      "a_child_created_second", DefaultBookmarkSpecifics(), "parent");
  entry_factory_->CreateUnappliedNewItemWithParent(
      "x_child_created_second", DefaultBookmarkSpecifics(), "parent");

  ExpectGroupToChange(apply_updates_command_, GROUP_UI);
  apply_updates_command_.ExecuteImpl(session());

  sessions::StatusController* status = session()->mutable_status_controller();
  sessions::ScopedModelSafeGroupRestriction r(status, GROUP_UI);
  ASSERT_TRUE(status->update_progress());
  EXPECT_EQ(5, status->update_progress()->AppliedUpdatesSize())
      << "All updates should have been attempted";
  ASSERT_TRUE(status->conflict_progress());
  EXPECT_EQ(0, status->conflict_progress()->SimpleConflictingItemsSize())
      << "Simple update shouldn't result in conflicts, even if out-of-order";
  EXPECT_EQ(5, status->update_progress()->SuccessfullyAppliedUpdateCount())
      << "All updates should have been successfully applied";
}

// Runs the ApplyUpdatesCommand on an item that has both local and remote
// modifications (IS_UNSYNCED and IS_UNAPPLIED_UPDATE).  We expect the command
// to detect that this update can't be applied because it is in a CONFLICT
// state.
TEST_F(ApplyUpdatesCommandTest, SimpleConflict) {
  entry_factory_->CreateUnappliedAndUnsyncedItem("item", BOOKMARKS);

  ExpectGroupToChange(apply_updates_command_, GROUP_UI);
  apply_updates_command_.ExecuteImpl(session());

  sessions::StatusController* status = session()->mutable_status_controller();
  sessions::ScopedModelSafeGroupRestriction r(status, GROUP_UI);
  ASSERT_TRUE(status->conflict_progress());
  EXPECT_EQ(1, status->conflict_progress()->SimpleConflictingItemsSize())
      << "Unsynced and unapplied item should be a simple conflict";
}

// Runs the ApplyUpdatesCommand on an item that has both local and remote
// modifications *and* the remote modification cannot be applied without
// violating the tree constraints.  We expect the command to detect that this
// update can't be applied and that this situation can't be resolved with the
// simple conflict processing logic; it is in a CONFLICT_HIERARCHY state.
TEST_F(ApplyUpdatesCommandTest, HierarchyAndSimpleConflict) {
  // Create a simply-conflicting item.  It will start with valid parent ids.
  int64 handle = entry_factory_->CreateUnappliedAndUnsyncedItem(
      "orphaned_by_server", BOOKMARKS);
  {
    // Manually set the SERVER_PARENT_ID to bad value.
    // A bad parent indicates a hierarchy conflict.
    WriteTransaction trans(FROM_HERE, UNITTEST, directory());
    MutableEntry entry(&trans, syncable::GET_BY_HANDLE, handle);
    ASSERT_TRUE(entry.good());

    entry.Put(syncable::SERVER_PARENT_ID,
              id_factory_.MakeServer("bogus_parent"));
  }

  ExpectGroupToChange(apply_updates_command_, GROUP_UI);
  apply_updates_command_.ExecuteImpl(session());

  sessions::StatusController* status = session()->mutable_status_controller();
  sessions::ScopedModelSafeGroupRestriction r(status, GROUP_UI);

  EXPECT_EQ(1, status->update_progress()->AppliedUpdatesSize());

  // An update that is both a simple conflict and a hierarchy conflict should be
  // treated as a hierarchy conflict.
  ASSERT_TRUE(status->conflict_progress());
  EXPECT_EQ(1, status->conflict_progress()->HierarchyConflictingItemsSize());
  EXPECT_EQ(0, status->conflict_progress()->SimpleConflictingItemsSize());
}


// Runs the ApplyUpdatesCommand on an item with remote modifications that would
// create a directory loop if the update were applied.  We expect the command to
// detect that this update can't be applied because it is in a
// CONFLICT_HIERARCHY state.
TEST_F(ApplyUpdatesCommandTest, HierarchyConflictDirectoryLoop) {
  // Item 'X' locally has parent of 'root'.  Server is updating it to have
  // parent of 'Y'.
  {
    // Create it as a child of root node.
    int64 handle = entry_factory_->CreateSyncedItem("X", BOOKMARKS, true);

    WriteTransaction trans(FROM_HERE, UNITTEST, directory());
    MutableEntry entry(&trans, syncable::GET_BY_HANDLE, handle);
    ASSERT_TRUE(entry.good());

    // Re-parent from root to "Y"
    entry.Put(syncable::SERVER_VERSION, entry_factory_->GetNextRevision());
    entry.Put(syncable::IS_UNAPPLIED_UPDATE, true);
    entry.Put(syncable::SERVER_PARENT_ID, id_factory_.MakeServer("Y"));
  }

  // Item 'Y' is child of 'X'.
  entry_factory_->CreateUnsyncedItem(
      id_factory_.MakeServer("Y"), id_factory_.MakeServer("X"), "Y", true,
      BOOKMARKS, NULL);

  // If the server's update were applied, we would have X be a child of Y, and Y
  // as a child of X.  That's a directory loop.  The UpdateApplicator should
  // prevent the update from being applied and note that this is a hierarchy
  // conflict.

  ExpectGroupToChange(apply_updates_command_, GROUP_UI);
  apply_updates_command_.ExecuteImpl(session());

  sessions::StatusController* status = session()->mutable_status_controller();
  sessions::ScopedModelSafeGroupRestriction r(status, GROUP_UI);

  EXPECT_EQ(1, status->update_progress()->AppliedUpdatesSize());

  // This should count as a hierarchy conflict.
  ASSERT_TRUE(status->conflict_progress());
  EXPECT_EQ(1, status->conflict_progress()->HierarchyConflictingItemsSize());
  EXPECT_EQ(0, status->conflict_progress()->SimpleConflictingItemsSize());
}

// Runs the ApplyUpdatesCommand on a directory where the server sent us an
// update to add a child to a locally deleted (and unsynced) parent.  We expect
// the command to not apply the update and to indicate the update is in a
// CONFLICT_HIERARCHY state.
TEST_F(ApplyUpdatesCommandTest, HierarchyConflictDeletedParent) {
  // Create a locally deleted parent item.
  int64 parent_handle;
  entry_factory_->CreateUnsyncedItem(
      Id::CreateFromServerId("parent"), id_factory_.root(),
      "parent", true, BOOKMARKS, &parent_handle);
  {
    WriteTransaction trans(FROM_HERE, UNITTEST, directory());
    MutableEntry entry(&trans, syncable::GET_BY_HANDLE, parent_handle);
    entry.Put(syncable::IS_DEL, true);
  }

  // Create an incoming child from the server.
  entry_factory_->CreateUnappliedNewItemWithParent(
      "child", DefaultBookmarkSpecifics(), "parent");

  // The server's update may seem valid to some other client, but on this client
  // that new item's parent no longer exists.  The update should not be applied
  // and the update applicator should indicate this is a hierarchy conflict.

  ExpectGroupToChange(apply_updates_command_, GROUP_UI);
  apply_updates_command_.ExecuteImpl(session());

  sessions::StatusController* status = session()->mutable_status_controller();
  sessions::ScopedModelSafeGroupRestriction r(status, GROUP_UI);

  // This should count as a hierarchy conflict.
  ASSERT_TRUE(status->conflict_progress());
  EXPECT_EQ(1, status->conflict_progress()->HierarchyConflictingItemsSize());
  EXPECT_EQ(0, status->conflict_progress()->SimpleConflictingItemsSize());
}

// Runs the ApplyUpdatesCommand on a directory where the server is trying to
// delete a folder that has a recently added (and unsynced) child.  We expect
// the command to not apply the update because it is in a CONFLICT_HIERARCHY
// state.
TEST_F(ApplyUpdatesCommandTest, HierarchyConflictDeleteNonEmptyDirectory) {
  // Create a server-deleted directory.
  {
    // Create it as a child of root node.
    int64 handle =
        entry_factory_->CreateSyncedItem("parent", BOOKMARKS, true);

    WriteTransaction trans(FROM_HERE, UNITTEST, directory());
    MutableEntry entry(&trans, syncable::GET_BY_HANDLE, handle);
    ASSERT_TRUE(entry.good());

    // Delete it on the server.
    entry.Put(syncable::SERVER_VERSION, entry_factory_->GetNextRevision());
    entry.Put(syncable::IS_UNAPPLIED_UPDATE, true);
    entry.Put(syncable::SERVER_PARENT_ID, id_factory_.root());
    entry.Put(syncable::SERVER_IS_DEL, true);
  }

  // Create a local child of the server-deleted directory.
  entry_factory_->CreateUnsyncedItem(
      id_factory_.MakeServer("child"), id_factory_.MakeServer("parent"),
      "child", false, BOOKMARKS, NULL);

  // The server's request to delete the directory must be ignored, otherwise our
  // unsynced new child would be orphaned.  This is a hierarchy conflict.

  ExpectGroupToChange(apply_updates_command_, GROUP_UI);
  apply_updates_command_.ExecuteImpl(session());

  sessions::StatusController* status = session()->mutable_status_controller();
  sessions::ScopedModelSafeGroupRestriction r(status, GROUP_UI);

  // This should count as a hierarchy conflict.
  ASSERT_TRUE(status->conflict_progress());
  EXPECT_EQ(1, status->conflict_progress()->HierarchyConflictingItemsSize());
  EXPECT_EQ(0, status->conflict_progress()->SimpleConflictingItemsSize());
}

// Runs the ApplyUpdatesCommand on a server-created item that has a locally
// unknown parent.  We expect the command to not apply the update because the
// item is in a CONFLICT_HIERARCHY state.
TEST_F(ApplyUpdatesCommandTest, HierarchyConflictUnknownParent) {
  // We shouldn't be able to do anything with either of these items.
  entry_factory_->CreateUnappliedNewItemWithParent(
      "some_item", DefaultBookmarkSpecifics(), "unknown_parent");
  entry_factory_->CreateUnappliedNewItemWithParent(
      "some_other_item", DefaultBookmarkSpecifics(), "some_item");

  ExpectGroupToChange(apply_updates_command_, GROUP_UI);
  apply_updates_command_.ExecuteImpl(session());

  sessions::StatusController* status = session()->mutable_status_controller();
  sessions::ScopedModelSafeGroupRestriction r(status, GROUP_UI);
  ASSERT_TRUE(status->update_progress());
  EXPECT_EQ(2, status->update_progress()->AppliedUpdatesSize())
      << "All updates should have been attempted";
  ASSERT_TRUE(status->conflict_progress());
  EXPECT_EQ(0, status->conflict_progress()->SimpleConflictingItemsSize())
      << "Updates with unknown parent should not be treated as 'simple'"
      << " conflicts";
  EXPECT_EQ(2, status->conflict_progress()->HierarchyConflictingItemsSize())
      << "All updates with an unknown ancestors should be in conflict";
  EXPECT_EQ(0, status->update_progress()->SuccessfullyAppliedUpdateCount())
      << "No item with an unknown ancestor should be applied";
}

TEST_F(ApplyUpdatesCommandTest, ItemsBothKnownAndUnknown) {
  // See what happens when there's a mixture of good and bad updates.
  string root_server_id = syncable::GetNullId().GetServerId();
  entry_factory_->CreateUnappliedNewItemWithParent(
      "first_unknown_item", DefaultBookmarkSpecifics(), "unknown_parent");
  entry_factory_->CreateUnappliedNewItemWithParent(
      "first_known_item", DefaultBookmarkSpecifics(), root_server_id);
  entry_factory_->CreateUnappliedNewItemWithParent(
      "second_unknown_item", DefaultBookmarkSpecifics(), "unknown_parent");
  entry_factory_->CreateUnappliedNewItemWithParent(
      "second_known_item", DefaultBookmarkSpecifics(), "first_known_item");
  entry_factory_->CreateUnappliedNewItemWithParent(
      "third_known_item", DefaultBookmarkSpecifics(), "fourth_known_item");
  entry_factory_->CreateUnappliedNewItemWithParent(
      "fourth_known_item", DefaultBookmarkSpecifics(), root_server_id);

  ExpectGroupToChange(apply_updates_command_, GROUP_UI);
  apply_updates_command_.ExecuteImpl(session());

  sessions::StatusController* status = session()->mutable_status_controller();
  sessions::ScopedModelSafeGroupRestriction r(status, GROUP_UI);
  ASSERT_TRUE(status->update_progress());
  EXPECT_EQ(6, status->update_progress()->AppliedUpdatesSize())
      << "All updates should have been attempted";
  ASSERT_TRUE(status->conflict_progress());
  EXPECT_EQ(2, status->conflict_progress()->HierarchyConflictingItemsSize())
      << "The updates with unknown ancestors should be in conflict";
  EXPECT_EQ(4, status->update_progress()->SuccessfullyAppliedUpdateCount())
      << "The updates with known ancestors should be successfully applied";
}

TEST_F(ApplyUpdatesCommandTest, DecryptablePassword) {
  // Decryptable password updates should be applied.
  Cryptographer* cryptographer;
  {
    // Storing the cryptographer separately is bad, but for this test we
    // know it's safe.
    syncable::ReadTransaction trans(FROM_HERE, directory());
    cryptographer = directory()->GetCryptographer(&trans);
  }

  KeyParams params = {"localhost", "dummy", "foobar"};
  cryptographer->AddKey(params);

  sync_pb::EntitySpecifics specifics;
  sync_pb::PasswordSpecificsData data;
  data.set_origin("http://example.com");

  cryptographer->Encrypt(data,
                         specifics.mutable_password()->mutable_encrypted());
  entry_factory_->CreateUnappliedNewItem("item", specifics, false);

  ExpectGroupToChange(apply_updates_command_, GROUP_PASSWORD);
  apply_updates_command_.ExecuteImpl(session());

  sessions::StatusController* status = session()->mutable_status_controller();
  sessions::ScopedModelSafeGroupRestriction r(status, GROUP_PASSWORD);
  ASSERT_TRUE(status->update_progress());
  EXPECT_EQ(1, status->update_progress()->AppliedUpdatesSize())
      << "All updates should have been attempted";
  ASSERT_TRUE(status->conflict_progress());
  EXPECT_EQ(0, status->conflict_progress()->SimpleConflictingItemsSize())
      << "No update should be in conflict because they're all decryptable";
  EXPECT_EQ(1, status->update_progress()->SuccessfullyAppliedUpdateCount())
      << "The updates that can be decrypted should be applied";
}

TEST_F(ApplyUpdatesCommandTest, UndecryptableData) {
  // Undecryptable updates should not be applied.
  sync_pb::EntitySpecifics encrypted_bookmark;
  encrypted_bookmark.mutable_encrypted();
  AddDefaultFieldValue(BOOKMARKS, &encrypted_bookmark);
  string root_server_id = syncable::GetNullId().GetServerId();
  entry_factory_->CreateUnappliedNewItemWithParent(
      "folder", encrypted_bookmark, root_server_id);
  entry_factory_->CreateUnappliedNewItem("item2", encrypted_bookmark, false);
  sync_pb::EntitySpecifics encrypted_password;
  encrypted_password.mutable_password();
  entry_factory_->CreateUnappliedNewItem("item3", encrypted_password, false);

  ExpectGroupsToChange(apply_updates_command_, GROUP_UI, GROUP_PASSWORD);
  apply_updates_command_.ExecuteImpl(session());

  sessions::StatusController* status = session()->mutable_status_controller();
  EXPECT_TRUE(status->HasConflictingUpdates())
    << "Updates that can't be decrypted should trigger the syncer to have "
    << "conflicting updates.";
  {
    sessions::ScopedModelSafeGroupRestriction r(status, GROUP_UI);
    ASSERT_TRUE(status->update_progress());
    EXPECT_EQ(2, status->update_progress()->AppliedUpdatesSize())
        << "All updates should have been attempted";
    ASSERT_TRUE(status->conflict_progress());
    EXPECT_EQ(0, status->conflict_progress()->SimpleConflictingItemsSize())
        << "The updates that can't be decrypted should not be in regular "
        << "conflict";
    EXPECT_EQ(2, status->conflict_progress()->EncryptionConflictingItemsSize())
        << "The updates that can't be decrypted should be in encryption "
        << "conflict";
    EXPECT_EQ(0, status->update_progress()->SuccessfullyAppliedUpdateCount())
        << "No update that can't be decrypted should be applied";
  }
  {
    sessions::ScopedModelSafeGroupRestriction r(status, GROUP_PASSWORD);
    ASSERT_TRUE(status->update_progress());
    EXPECT_EQ(1, status->update_progress()->AppliedUpdatesSize())
        << "All updates should have been attempted";
    ASSERT_TRUE(status->conflict_progress());
    EXPECT_EQ(0, status->conflict_progress()->SimpleConflictingItemsSize())
        << "The updates that can't be decrypted should not be in regular "
        << "conflict";
    EXPECT_EQ(1, status->conflict_progress()->EncryptionConflictingItemsSize())
        << "The updates that can't be decrypted should be in encryption "
        << "conflict";
    EXPECT_EQ(0, status->update_progress()->SuccessfullyAppliedUpdateCount())
        << "No update that can't be decrypted should be applied";
  }
}

TEST_F(ApplyUpdatesCommandTest, SomeUndecryptablePassword) {
  // Only decryptable password updates should be applied.
  {
    sync_pb::EntitySpecifics specifics;
    sync_pb::PasswordSpecificsData data;
    data.set_origin("http://example.com/1");
    {
      syncable::ReadTransaction trans(FROM_HERE, directory());
      Cryptographer* cryptographer = directory()->GetCryptographer(&trans);

      KeyParams params = {"localhost", "dummy", "foobar"};
      cryptographer->AddKey(params);

      cryptographer->Encrypt(data,
          specifics.mutable_password()->mutable_encrypted());
    }
    entry_factory_->CreateUnappliedNewItem("item1", specifics, false);
  }
  {
    // Create a new cryptographer, independent of the one in the session.
    Cryptographer cryptographer(&encryptor_);
    KeyParams params = {"localhost", "dummy", "bazqux"};
    cryptographer.AddKey(params);

    sync_pb::EntitySpecifics specifics;
    sync_pb::PasswordSpecificsData data;
    data.set_origin("http://example.com/2");

    cryptographer.Encrypt(data,
        specifics.mutable_password()->mutable_encrypted());
    entry_factory_->CreateUnappliedNewItem("item2", specifics, false);
  }

  ExpectGroupToChange(apply_updates_command_, GROUP_PASSWORD);
  apply_updates_command_.ExecuteImpl(session());

  sessions::StatusController* status = session()->mutable_status_controller();
  EXPECT_TRUE(status->HasConflictingUpdates())
    << "Updates that can't be decrypted should trigger the syncer to have "
    << "conflicting updates.";
  {
    sessions::ScopedModelSafeGroupRestriction r(status, GROUP_PASSWORD);
    ASSERT_TRUE(status->update_progress());
    EXPECT_EQ(2, status->update_progress()->AppliedUpdatesSize())
        << "All updates should have been attempted";
    ASSERT_TRUE(status->conflict_progress());
    EXPECT_EQ(0, status->conflict_progress()->SimpleConflictingItemsSize())
        << "The updates that can't be decrypted should not be in regular "
        << "conflict";
    EXPECT_EQ(1, status->conflict_progress()->EncryptionConflictingItemsSize())
        << "The updates that can't be decrypted should be in encryption "
        << "conflict";
    EXPECT_EQ(1, status->update_progress()->SuccessfullyAppliedUpdateCount())
        << "The undecryptable password update shouldn't be applied";
  }
}

TEST_F(ApplyUpdatesCommandTest, NigoriUpdate) {
  // Storing the cryptographer separately is bad, but for this test we
  // know it's safe.
  Cryptographer* cryptographer;
  ModelTypeSet encrypted_types;
  encrypted_types.Put(PASSWORDS);
  encrypted_types.Put(NIGORI);
  {
    syncable::ReadTransaction trans(FROM_HERE, directory());
    cryptographer = directory()->GetCryptographer(&trans);
    EXPECT_TRUE(cryptographer->GetEncryptedTypes().Equals(encrypted_types));
  }

  // Nigori node updates should update the Cryptographer.
  Cryptographer other_cryptographer(&encryptor_);
  KeyParams params = {"localhost", "dummy", "foobar"};
  other_cryptographer.AddKey(params);

  sync_pb::EntitySpecifics specifics;
  sync_pb::NigoriSpecifics* nigori = specifics.mutable_nigori();
  other_cryptographer.GetKeys(nigori->mutable_encrypted());
  nigori->set_encrypt_bookmarks(true);
  encrypted_types.Put(BOOKMARKS);
  entry_factory_->CreateUnappliedNewItem(
      ModelTypeToRootTag(NIGORI), specifics, true);
  EXPECT_FALSE(cryptographer->has_pending_keys());

  ExpectGroupToChange(apply_updates_command_, GROUP_PASSIVE);
  apply_updates_command_.ExecuteImpl(session());

  sessions::StatusController* status = session()->mutable_status_controller();
  sessions::ScopedModelSafeGroupRestriction r(status, GROUP_PASSIVE);
  ASSERT_TRUE(status->update_progress());
  EXPECT_EQ(1, status->update_progress()->AppliedUpdatesSize())
      << "All updates should have been attempted";
  ASSERT_TRUE(status->conflict_progress());
  EXPECT_EQ(0, status->conflict_progress()->SimpleConflictingItemsSize())
      << "The nigori update shouldn't be in conflict";
  EXPECT_EQ(1, status->update_progress()->SuccessfullyAppliedUpdateCount())
      << "The nigori update should be applied";

  EXPECT_FALSE(cryptographer->is_ready());
  EXPECT_TRUE(cryptographer->has_pending_keys());
  EXPECT_TRUE(
      cryptographer->GetEncryptedTypes().Equals(ModelTypeSet::All()));
}

TEST_F(ApplyUpdatesCommandTest, NigoriUpdateForDisabledTypes) {
  // Storing the cryptographer separately is bad, but for this test we
  // know it's safe.
  Cryptographer* cryptographer;
  ModelTypeSet encrypted_types;
  encrypted_types.Put(PASSWORDS);
  encrypted_types.Put(NIGORI);
  {
    syncable::ReadTransaction trans(FROM_HERE, directory());
    cryptographer = directory()->GetCryptographer(&trans);
    EXPECT_TRUE(cryptographer->GetEncryptedTypes().Equals(encrypted_types));
  }

  // Nigori node updates should update the Cryptographer.
  Cryptographer other_cryptographer(&encryptor_);
  KeyParams params = {"localhost", "dummy", "foobar"};
  other_cryptographer.AddKey(params);

  sync_pb::EntitySpecifics specifics;
  sync_pb::NigoriSpecifics* nigori = specifics.mutable_nigori();
  other_cryptographer.GetKeys(nigori->mutable_encrypted());
  nigori->set_encrypt_sessions(true);
  nigori->set_encrypt_themes(true);
  encrypted_types.Put(SESSIONS);
  encrypted_types.Put(THEMES);
  entry_factory_->CreateUnappliedNewItem(
      ModelTypeToRootTag(NIGORI), specifics, true);
  EXPECT_FALSE(cryptographer->has_pending_keys());

  ExpectGroupToChange(apply_updates_command_, GROUP_PASSIVE);
  apply_updates_command_.ExecuteImpl(session());

  sessions::StatusController* status = session()->mutable_status_controller();
  sessions::ScopedModelSafeGroupRestriction r(status, GROUP_PASSIVE);
  ASSERT_TRUE(status->update_progress());
  EXPECT_EQ(1, status->update_progress()->AppliedUpdatesSize())
      << "All updates should have been attempted";
  ASSERT_TRUE(status->conflict_progress());
  EXPECT_EQ(0, status->conflict_progress()->SimpleConflictingItemsSize())
      << "The nigori update shouldn't be in conflict";
  EXPECT_EQ(1, status->update_progress()->SuccessfullyAppliedUpdateCount())
      << "The nigori update should be applied";

  EXPECT_FALSE(cryptographer->is_ready());
  EXPECT_TRUE(cryptographer->has_pending_keys());
  EXPECT_TRUE(
      cryptographer->GetEncryptedTypes().Equals(ModelTypeSet::All()));
}

// Create some local unsynced and unencrypted data. Apply a nigori update that
// turns on encryption for the unsynced data. Ensure we properly encrypt the
// data as part of the nigori update. Apply another nigori update with no
// changes. Ensure we ignore already-encrypted unsynced data and that nothing
// breaks.
TEST_F(ApplyUpdatesCommandTest, EncryptUnsyncedChanges) {
  // Storing the cryptographer separately is bad, but for this test we
  // know it's safe.
  Cryptographer* cryptographer;
  ModelTypeSet encrypted_types;
  encrypted_types.Put(PASSWORDS);
  encrypted_types.Put(NIGORI);
  {
    syncable::ReadTransaction trans(FROM_HERE, directory());
    cryptographer = directory()->GetCryptographer(&trans);
    EXPECT_TRUE(cryptographer->GetEncryptedTypes().Equals(encrypted_types));

    // With default encrypted_types, this should be true.
    EXPECT_TRUE(VerifyUnsyncedChangesAreEncrypted(&trans, encrypted_types));

    Syncer::UnsyncedMetaHandles handles;
    GetUnsyncedEntries(&trans, &handles);
    EXPECT_TRUE(handles.empty());
  }

  // Create unsynced bookmarks without encryption.
  // First item is a folder
  Id folder_id = id_factory_.NewLocalId();
  entry_factory_->CreateUnsyncedItem(folder_id, id_factory_.root(), "folder",
                                     true, BOOKMARKS, NULL);
  // Next five items are children of the folder
  size_t i;
  size_t batch_s = 5;
  for (i = 0; i < batch_s; ++i) {
    entry_factory_->CreateUnsyncedItem(id_factory_.NewLocalId(), folder_id,
                                       base::StringPrintf("Item %"PRIuS"", i),
                                       false, BOOKMARKS, NULL);
  }
  // Next five items are children of the root.
  for (; i < 2*batch_s; ++i) {
    entry_factory_->CreateUnsyncedItem(
        id_factory_.NewLocalId(), id_factory_.root(),
        base::StringPrintf("Item %"PRIuS"", i), false,
        BOOKMARKS, NULL);
  }

  KeyParams params = {"localhost", "dummy", "foobar"};
  cryptographer->AddKey(params);
  sync_pb::EntitySpecifics specifics;
  sync_pb::NigoriSpecifics* nigori = specifics.mutable_nigori();
  cryptographer->GetKeys(nigori->mutable_encrypted());
  nigori->set_encrypt_bookmarks(true);
  encrypted_types.Put(BOOKMARKS);
  entry_factory_->CreateUnappliedNewItem(
      ModelTypeToRootTag(NIGORI), specifics, true);
  EXPECT_FALSE(cryptographer->has_pending_keys());
  EXPECT_TRUE(cryptographer->is_ready());

  {
    // Ensure we have unsynced nodes that aren't properly encrypted.
    syncable::ReadTransaction trans(FROM_HERE, directory());
    EXPECT_FALSE(VerifyUnsyncedChangesAreEncrypted(&trans, encrypted_types));

    Syncer::UnsyncedMetaHandles handles;
    GetUnsyncedEntries(&trans, &handles);
    EXPECT_EQ(2*batch_s+1, handles.size());
  }

  ExpectGroupToChange(apply_updates_command_, GROUP_PASSIVE);
  apply_updates_command_.ExecuteImpl(session());

  {
    sessions::StatusController* status = session()->mutable_status_controller();
    sessions::ScopedModelSafeGroupRestriction r(status, GROUP_PASSIVE);
    ASSERT_TRUE(status->update_progress());
    EXPECT_EQ(1, status->update_progress()->AppliedUpdatesSize())
        << "All updates should have been attempted";
    ASSERT_TRUE(status->conflict_progress());
    EXPECT_EQ(0, status->conflict_progress()->SimpleConflictingItemsSize())
        << "No updates should be in conflict";
    EXPECT_EQ(0, status->conflict_progress()->EncryptionConflictingItemsSize())
        << "No updates should be in conflict";
    EXPECT_EQ(1, status->update_progress()->SuccessfullyAppliedUpdateCount())
        << "The nigori update should be applied";
  }
  EXPECT_FALSE(cryptographer->has_pending_keys());
  EXPECT_TRUE(cryptographer->is_ready());
  {
    syncable::ReadTransaction trans(FROM_HERE, directory());

    // If ProcessUnsyncedChangesForEncryption worked, all our unsynced changes
    // should be encrypted now.
    EXPECT_TRUE(ModelTypeSet::All().Equals(
        cryptographer->GetEncryptedTypes()));
    EXPECT_TRUE(VerifyUnsyncedChangesAreEncrypted(&trans, encrypted_types));

    Syncer::UnsyncedMetaHandles handles;
    GetUnsyncedEntries(&trans, &handles);
    EXPECT_EQ(2*batch_s+1, handles.size());
  }

  // Simulate another nigori update that doesn't change anything.
  {
    WriteTransaction trans(FROM_HERE, UNITTEST, directory());
    MutableEntry entry(&trans, syncable::GET_BY_SERVER_TAG,
                       ModelTypeToRootTag(NIGORI));
    ASSERT_TRUE(entry.good());
    entry.Put(syncable::SERVER_VERSION, entry_factory_->GetNextRevision());
    entry.Put(syncable::IS_UNAPPLIED_UPDATE, true);
  }
  ExpectGroupToChange(apply_updates_command_, GROUP_PASSIVE);
  apply_updates_command_.ExecuteImpl(session());
  {
    sessions::StatusController* status = session()->mutable_status_controller();
    sessions::ScopedModelSafeGroupRestriction r(status, GROUP_PASSIVE);
    ASSERT_TRUE(status->update_progress());
    EXPECT_EQ(2, status->update_progress()->AppliedUpdatesSize())
        << "All updates should have been attempted";
    ASSERT_TRUE(status->conflict_progress());
    EXPECT_EQ(0, status->conflict_progress()->SimpleConflictingItemsSize())
        << "No updates should be in conflict";
    EXPECT_EQ(0, status->conflict_progress()->EncryptionConflictingItemsSize())
        << "No updates should be in conflict";
    EXPECT_EQ(2, status->update_progress()->SuccessfullyAppliedUpdateCount())
        << "The nigori update should be applied";
  }
  EXPECT_FALSE(cryptographer->has_pending_keys());
  EXPECT_TRUE(cryptographer->is_ready());
  {
    syncable::ReadTransaction trans(FROM_HERE, directory());

    // All our changes should still be encrypted.
    EXPECT_TRUE(ModelTypeSet::All().Equals(
        cryptographer->GetEncryptedTypes()));
    EXPECT_TRUE(VerifyUnsyncedChangesAreEncrypted(&trans, encrypted_types));

    Syncer::UnsyncedMetaHandles handles;
    GetUnsyncedEntries(&trans, &handles);
    EXPECT_EQ(2*batch_s+1, handles.size());
  }
}

TEST_F(ApplyUpdatesCommandTest, CannotEncryptUnsyncedChanges) {
  // Storing the cryptographer separately is bad, but for this test we
  // know it's safe.
  Cryptographer* cryptographer;
  ModelTypeSet encrypted_types;
  encrypted_types.Put(PASSWORDS);
  encrypted_types.Put(NIGORI);
  {
    syncable::ReadTransaction trans(FROM_HERE, directory());
    cryptographer = directory()->GetCryptographer(&trans);
    EXPECT_TRUE(cryptographer->GetEncryptedTypes().Equals(encrypted_types));

    // With default encrypted_types, this should be true.
    EXPECT_TRUE(VerifyUnsyncedChangesAreEncrypted(&trans, encrypted_types));

    Syncer::UnsyncedMetaHandles handles;
    GetUnsyncedEntries(&trans, &handles);
    EXPECT_TRUE(handles.empty());
  }

  // Create unsynced bookmarks without encryption.
  // First item is a folder
  Id folder_id = id_factory_.NewLocalId();
  entry_factory_->CreateUnsyncedItem(
      folder_id, id_factory_.root(), "folder", true,
      BOOKMARKS, NULL);
  // Next five items are children of the folder
  size_t i;
  size_t batch_s = 5;
  for (i = 0; i < batch_s; ++i) {
    entry_factory_->CreateUnsyncedItem(id_factory_.NewLocalId(), folder_id,
                                       base::StringPrintf("Item %"PRIuS"", i),
                                       false, BOOKMARKS, NULL);
  }
  // Next five items are children of the root.
  for (; i < 2*batch_s; ++i) {
    entry_factory_->CreateUnsyncedItem(
        id_factory_.NewLocalId(), id_factory_.root(),
        base::StringPrintf("Item %"PRIuS"", i), false,
        BOOKMARKS, NULL);
  }

  // We encrypt with new keys, triggering the local cryptographer to be unready
  // and unable to decrypt data (once updated).
  Cryptographer other_cryptographer(&encryptor_);
  KeyParams params = {"localhost", "dummy", "foobar"};
  other_cryptographer.AddKey(params);
  sync_pb::EntitySpecifics specifics;
  sync_pb::NigoriSpecifics* nigori = specifics.mutable_nigori();
  other_cryptographer.GetKeys(nigori->mutable_encrypted());
  nigori->set_encrypt_bookmarks(true);
  encrypted_types.Put(BOOKMARKS);
  entry_factory_->CreateUnappliedNewItem(
      ModelTypeToRootTag(NIGORI), specifics, true);
  EXPECT_FALSE(cryptographer->has_pending_keys());

  {
    // Ensure we have unsynced nodes that aren't properly encrypted.
    syncable::ReadTransaction trans(FROM_HERE, directory());
    EXPECT_FALSE(VerifyUnsyncedChangesAreEncrypted(&trans, encrypted_types));
    Syncer::UnsyncedMetaHandles handles;
    GetUnsyncedEntries(&trans, &handles);
    EXPECT_EQ(2*batch_s+1, handles.size());
  }

  ExpectGroupToChange(apply_updates_command_, GROUP_PASSIVE);
  apply_updates_command_.ExecuteImpl(session());

  sessions::StatusController* status = session()->mutable_status_controller();
  sessions::ScopedModelSafeGroupRestriction r(status, GROUP_PASSIVE);
  ASSERT_TRUE(status->update_progress());
  EXPECT_EQ(1, status->update_progress()->AppliedUpdatesSize())
      << "All updates should have been attempted";
  ASSERT_TRUE(status->conflict_progress());
  EXPECT_EQ(0, status->conflict_progress()->SimpleConflictingItemsSize())
      << "The unsynced changes don't trigger a blocking conflict with the "
      << "nigori update.";
  EXPECT_EQ(0, status->conflict_progress()->EncryptionConflictingItemsSize())
      << "The unsynced changes don't trigger an encryption conflict with the "
      << "nigori update.";
  EXPECT_EQ(1, status->update_progress()->SuccessfullyAppliedUpdateCount())
      << "The nigori update should be applied";
  EXPECT_FALSE(cryptographer->is_ready());
  EXPECT_TRUE(cryptographer->has_pending_keys());
  {
    syncable::ReadTransaction trans(FROM_HERE, directory());

    // Since we have pending keys, we would have failed to encrypt, but the
    // cryptographer should be updated.
    EXPECT_FALSE(VerifyUnsyncedChangesAreEncrypted(&trans, encrypted_types));
    EXPECT_TRUE(cryptographer->GetEncryptedTypes().Equals(
        ModelTypeSet().All()));
    EXPECT_FALSE(cryptographer->is_ready());
    EXPECT_TRUE(cryptographer->has_pending_keys());

    Syncer::UnsyncedMetaHandles handles;
    GetUnsyncedEntries(&trans, &handles);
    EXPECT_EQ(2*batch_s+1, handles.size());
  }
}

}  // namespace syncer
