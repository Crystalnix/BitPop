// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_ENGINE_GET_COMMIT_IDS_COMMAND_H_
#define CHROME_BROWSER_SYNC_ENGINE_GET_COMMIT_IDS_COMMAND_H_
#pragma once

#include <utility>
#include <vector>

#include "base/compiler_specific.h"
#include "chrome/browser/sync/engine/syncer_command.h"
#include "chrome/browser/sync/engine/syncer_util.h"
#include "chrome/browser/sync/sessions/ordered_commit_set.h"
#include "chrome/browser/sync/sessions/sync_session.h"

using std::pair;
using std::vector;

namespace browser_sync {

class GetCommitIdsCommand : public SyncerCommand {
  friend class SyncerTest;

 public:
  explicit GetCommitIdsCommand(int commit_batch_size);
  virtual ~GetCommitIdsCommand();

  // SyncerCommand implementation.
  virtual SyncerError ExecuteImpl(sessions::SyncSession* session) OVERRIDE;

  // Builds a vector of IDs that should be committed.
  void BuildCommitIds(syncable::WriteTransaction* write_transaction,
                      const ModelSafeRoutingInfo& routes,
                      const std::set<int64>& ready_unsynced_set);

  // Fill |ready_unsynced_set| with all entries from |unsynced_handles| that
  // are ready to commit.
  // An entry is not considered ready for commit if any are true:
  // 1. It's in conflict.
  // 2. It requires encryption (either the type is encrypted but a passphrase
  //    is missing from the cryptographer, or the entry itself wasn't properly
  //    encrypted).
  // 3. It's type is currently throttled.
  // 4. It's a delete but has not been committed.
  void FilterUnreadyEntries(
      syncable::BaseTransaction* trans,
      syncable::ModelTypeSet throttled_types,
      syncable::ModelTypeSet encrypted_types,
      bool passphrase_missing,
      const syncable::Directory::UnsyncedMetaHandles& unsynced_handles,
      std::set<int64>* ready_unsynced_set);

 private:
  // Add all the uncommitted parents (and their predecessors) of |item| to
  // |result| if they are ready to commit. Entries are added in root->child
  // order and predecessor->successor order.
  // Returns values:
  //    False: if a dependent item was in conflict, and hence no child cannot be
  //           committed.
  //    True: if all parents and their predecessors were checked for commit
  //          readiness and were added to |result| as necessary.
  bool AddUncommittedParentsAndTheirPredecessors(
      syncable::BaseTransaction* trans,
      const ModelSafeRoutingInfo& routes,
      const std::set<int64>& ready_unsynced_set,
      const syncable::Entry& item,
      sessions::OrderedCommitSet* result) const;

  // OrderedCommitSet helpers for adding predecessors in order.

  // Adds |item| to |result| if it's ready for committing and was not already
  // present.
  // Prereq: |item| is unsynced.
  // Returns values:
  //    False: if |item| was in conflict.
  //    True: if |item| was checked for commit readiness and added to |result|
  //          as necessary.
  bool AddItem(const std::set<int64>& ready_unsynced_set,
               const syncable::Entry& item,
               sessions::OrderedCommitSet* result) const;

  // Adds item and all it's unsynced predecessors to |result| as necessary, as
  // long as no item was in conflict.
  // Return values:
  //   False: if there was an entry in conflict.
  //   True: if all entries were checked for commit readiness and added to
  //         |result| as necessary.
  bool AddItemThenPredecessors(syncable::BaseTransaction* trans,
                               const std::set<int64>& ready_unsynced_set,
                               const syncable::Entry& item,
                               sessions::OrderedCommitSet* result) const;

  // Appends all commit ready predecessors of |item|, followed by |item| itself,
  // to |ordered_commit_set_|, iff item and all its predecessors not in
  // conflict.
  // Return values:
  //   False: if there was an entry in conflict.
  //   True: if all entries were checked for commit readiness and added to
  //         |result| as necessary.
  bool AddPredecessorsThenItem(syncable::BaseTransaction* trans,
                               const ModelSafeRoutingInfo& routes,
                               const std::set<int64>& ready_unsynced_set,
                               const syncable::Entry& item,
                               sessions::OrderedCommitSet* result) const;

  bool IsCommitBatchFull() const;

  void AddCreatesAndMoves(syncable::WriteTransaction* write_transaction,
                          const ModelSafeRoutingInfo& routes,
                          const std::set<int64>& ready_unsynced_set);

  void AddDeletes(syncable::WriteTransaction* write_transaction,
                  const std::set<int64>& ready_unsynced_set);

  scoped_ptr<sessions::OrderedCommitSet> ordered_commit_set_;

  int requested_commit_batch_size_;

  DISALLOW_COPY_AND_ASSIGN(GetCommitIdsCommand);
};

}  // namespace browser_sync

#endif  // CHROME_BROWSER_SYNC_ENGINE_GET_COMMIT_IDS_COMMAND_H_
