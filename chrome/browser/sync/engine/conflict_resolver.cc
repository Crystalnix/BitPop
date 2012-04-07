// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/engine/conflict_resolver.h"

#include <algorithm>
#include <list>
#include <map>
#include <set>

#include "base/location.h"
#include "base/metrics/histogram.h"
#include "chrome/browser/sync/engine/syncer.h"
#include "chrome/browser/sync/engine/syncer_util.h"
#include "chrome/browser/sync/protocol/nigori_specifics.pb.h"
#include "chrome/browser/sync/protocol/service_constants.h"
#include "chrome/browser/sync/sessions/status_controller.h"
#include "chrome/browser/sync/syncable/directory_manager.h"
#include "chrome/browser/sync/syncable/syncable.h"
#include "chrome/browser/sync/util/cryptographer.h"

using std::list;
using std::map;
using std::set;
using syncable::BaseTransaction;
using syncable::Directory;
using syncable::Entry;
using syncable::GetModelTypeFromSpecifics;
using syncable::Id;
using syncable::IsRealDataType;
using syncable::MutableEntry;
using syncable::ScopedDirLookup;
using syncable::WriteTransaction;

namespace browser_sync {

using sessions::ConflictProgress;
using sessions::StatusController;

namespace {

const int SYNC_CYCLES_BEFORE_ADMITTING_DEFEAT = 8;

}  // namespace

ConflictResolver::ConflictResolver() {
}

ConflictResolver::~ConflictResolver() {
}

void ConflictResolver::IgnoreLocalChanges(MutableEntry* entry) {
  // An update matches local actions, merge the changes.
  // This is a little fishy because we don't actually merge them.
  // In the future we should do a 3-way merge.
  // With IS_UNSYNCED false, changes should be merged.
  entry->Put(syncable::IS_UNSYNCED, false);
}

void ConflictResolver::OverwriteServerChanges(WriteTransaction* trans,
                                              MutableEntry * entry) {
  // This is similar to an overwrite from the old client.
  // This is equivalent to a scenario where we got the update before we'd
  // made our local client changes.
  // TODO(chron): This is really a general property clobber. We clobber
  // the server side property. Perhaps we should actually do property merging.
  entry->Put(syncable::BASE_VERSION, entry->Get(syncable::SERVER_VERSION));
  entry->Put(syncable::IS_UNAPPLIED_UPDATE, false);
}

ConflictResolver::ProcessSimpleConflictResult
ConflictResolver::ProcessSimpleConflict(WriteTransaction* trans,
                                        const Id& id,
                                        const Cryptographer* cryptographer,
                                        StatusController* status) {
  MutableEntry entry(trans, syncable::GET_BY_ID, id);
  // Must be good as the entry won't have been cleaned up.
  CHECK(entry.good());
  // If an update fails, locally we have to be in a set or unsynced. We're not
  // in a set here, so we must be unsynced.
  if (!entry.Get(syncable::IS_UNSYNCED)) {
    return NO_SYNC_PROGRESS;
  }

  if (!entry.Get(syncable::IS_UNAPPLIED_UPDATE)) {
    if (!entry.Get(syncable::PARENT_ID).ServerKnows()) {
      DVLOG(1) << "Item conflicting because its parent not yet committed. Id: "
               << id;
    } else {
      DVLOG(1) << "No set for conflicting entry id " << id << ". There should "
               << "be an update/commit that will fix this soon. This message "
               << "should not repeat.";
    }
    return NO_SYNC_PROGRESS;
  }

  if (entry.Get(syncable::IS_DEL) && entry.Get(syncable::SERVER_IS_DEL)) {
    // we've both deleted it, so lets just drop the need to commit/update this
    // entry.
    entry.Put(syncable::IS_UNSYNCED, false);
    entry.Put(syncable::IS_UNAPPLIED_UPDATE, false);
    // we've made changes, but they won't help syncing progress.
    // METRIC simple conflict resolved by merge.
    return NO_SYNC_PROGRESS;
  }

  // This logic determines "client wins" vs. "server wins" strategy picking.
  // By the time we get to this point, we rely on the following to be true:
  // a) We can decrypt both the local and server data (else we'd be in
  //    conflict encryption and not attempting to resolve).
  // b) All unsynced changes have been re-encrypted with the default key (
  //    occurs either in AttemptToUpdateEntry, SetPassphrase, or
  //    RefreshEncryption).
  // c) Base_server_specifics having a valid datatype means that we received
  //    an undecryptable update that only changed specifics, and since then have
  //    not received any further non-specifics-only or decryptable updates.
  // d) If the server_specifics match specifics, server_specifics are
  //    encrypted with the default key, and all other visible properties match,
  //    then we can safely ignore the local changes as redundant.
  // e) Otherwise if the base_server_specifics match the server_specifics, no
  //    functional change must have been made server-side (else
  //    base_server_specifics would have been cleared), and we can therefore
  //    safely ignore the server changes as redundant.
  // f) Otherwise, it's in general safer to ignore local changes, with the
  //    exception of deletion conflicts (choose to undelete) and conflicts
  //    where the non_unique_name or parent don't match.
  if (!entry.Get(syncable::SERVER_IS_DEL)) {
    // TODO(nick): The current logic is arbitrary; instead, it ought to be made
    // consistent with the ModelAssociator behavior for a datatype.  It would
    // be nice if we could route this back to ModelAssociator code to pick one
    // of three options: CLIENT, SERVER, or MERGE.  Some datatypes (autofill)
    // are easily mergeable.
    // See http://crbug.com/77339.
    bool name_matches = entry.Get(syncable::NON_UNIQUE_NAME) ==
                        entry.Get(syncable::SERVER_NON_UNIQUE_NAME);
    bool parent_matches = entry.Get(syncable::PARENT_ID) ==
                          entry.Get(syncable::SERVER_PARENT_ID);
    bool entry_deleted = entry.Get(syncable::IS_DEL);

    // This positional check is meant to be necessary but not sufficient. As a
    // result, it may be false even when the position hasn't changed, possibly
    // resulting in unnecessary commits, but if it's true the position has
    // definitely not changed. The check works by verifying that the prev id
    // as calculated from the server position (which will ignore any
    // unsynced/unapplied predecessors and be root for non-bookmark datatypes)
    // matches the client prev id. Because we traverse chains of conflicting
    // items in predecessor -> successor order, we don't need to also verify the
    // successor matches (If it's in conflict, we'll verify it next. If it's
    // not, then it should be taken into account already in the
    // ComputePrevIdFromServerPosition calculation). This works even when there
    // are chains of conflicting items.
    //
    // Example: Original sequence was abcde. Server changes to aCDbe, while
    // client changes to aDCbe (C and D are in conflict). Locally, D's prev id
    // is a, while C's prev id is D. On the other hand, the server prev id will
    // ignore unsynced/unapplied items, so D's server prev id will also be a,
    // just like C's. Because we traverse in client predecessor->successor
    // order, we evaluate D first. Since prev id and server id match, we
    // consider the position to have remained the same for D, and will unset
    // it's UNSYNCED/UNAPPLIED bits. When we evaluate C though, we'll see that
    // the prev id is D locally while the server's prev id is a. C will
    // therefore count as a positional conflict (and the local data will be
    // overwritten by the server data typically). The final result will be
    // aCDbe (the same as the server's view). Even though both C and D were
    // modified, only one counted as being in actual conflict and was resolved
    // with local/server wins.
    //
    // In general, when there are chains of positional conflicts, only the first
    // item in chain (based on the clients point of view) will have both it's
    // server prev id and local prev id match. For all the rest the server prev
    // id will be the predecessor of the first item in the chain, and therefore
    // not match the local prev id.
    //
    // Similarly, chains of conflicts where the server and client info are the
    // same are supported due to the predecessor->successor ordering. In this
    // case, from the first item onward, we unset the UNSYNCED/UNAPPLIED bits as
    // we decide that nothing changed. The subsequent item's server prev id will
    // accurately match the local prev id because the predecessor is no longer
    // UNSYNCED/UNAPPLIED.
    // TODO(zea): simplify all this once we can directly compare server position
    // to client position.
    syncable::Id server_prev_id = entry.ComputePrevIdFromServerPosition(
        entry.Get(syncable::SERVER_PARENT_ID));
    bool needs_reinsertion = !parent_matches ||
         server_prev_id != entry.Get(syncable::PREV_ID);
    DVLOG_IF(1, needs_reinsertion) << "Insertion needed, server prev id "
        << " is " << server_prev_id << ", local prev id is "
        << entry.Get(syncable::PREV_ID);
    const sync_pb::EntitySpecifics& specifics =
        entry.Get(syncable::SPECIFICS);
    const sync_pb::EntitySpecifics& server_specifics =
        entry.Get(syncable::SERVER_SPECIFICS);
    const sync_pb::EntitySpecifics& base_server_specifics =
        entry.Get(syncable::BASE_SERVER_SPECIFICS);
    std::string decrypted_specifics, decrypted_server_specifics;
    bool specifics_match = false;
    bool server_encrypted_with_default_key = false;
    if (specifics.has_encrypted()) {
      DCHECK(cryptographer->CanDecryptUsingDefaultKey(specifics.encrypted()));
      decrypted_specifics = cryptographer->DecryptToString(
          specifics.encrypted());
    } else {
      decrypted_specifics = specifics.SerializeAsString();
    }
    if (server_specifics.has_encrypted()) {
      server_encrypted_with_default_key =
          cryptographer->CanDecryptUsingDefaultKey(
              server_specifics.encrypted());
      decrypted_server_specifics = cryptographer->DecryptToString(
          server_specifics.encrypted());
    } else {
      decrypted_server_specifics = server_specifics.SerializeAsString();
    }
    if (decrypted_server_specifics == decrypted_specifics &&
        server_encrypted_with_default_key == specifics.has_encrypted()) {
      specifics_match = true;
    }
    bool base_server_specifics_match = false;
    if (server_specifics.has_encrypted() &&
        IsRealDataType(GetModelTypeFromSpecifics(base_server_specifics))) {
      std::string decrypted_base_server_specifics;
      if (!base_server_specifics.has_encrypted()) {
        decrypted_base_server_specifics =
            base_server_specifics.SerializeAsString();
      } else {
        decrypted_base_server_specifics = cryptographer->DecryptToString(
            base_server_specifics.encrypted());
      }
      if (decrypted_server_specifics == decrypted_base_server_specifics)
          base_server_specifics_match = true;
    }

    // We manually merge nigori data.
    if (entry.GetModelType() == syncable::NIGORI) {
      // Create a new set of specifics based on the server specifics (which
      // preserves their encryption keys).
      sync_pb::EntitySpecifics specifics =
          entry.Get(syncable::SERVER_SPECIFICS);
      sync_pb::NigoriSpecifics* nigori =
          specifics.MutableExtension(sync_pb::nigori);
      // Store the merged set of encrypted types (cryptographer->Update(..) will
      // have merged the local types already).
      cryptographer->UpdateNigoriFromEncryptedTypes(nigori);
      // The local set of keys is already merged with the server's set within
      // the cryptographer. If we don't have pending keys we can store the
      // merged set back immediately. Else we preserve the server keys and will
      // update the nigori when the user provides the pending passphrase via
      // SetPassphrase(..).
      if (cryptographer->is_ready()) {
        cryptographer->GetKeys(nigori->mutable_encrypted());
      }
      // TODO(zea): Find a better way of doing this. As it stands, we have to
      // update this code whenever we add a new non-cryptographer related field
      // to the nigori node.
      if (entry.Get(syncable::SPECIFICS).GetExtension(sync_pb::nigori)
              .sync_tabs()) {
        nigori->set_sync_tabs(true);
      }
      // We deliberately leave the server's device information. This client will
      // add it's own device information on restart.
      entry.Put(syncable::SPECIFICS, specifics);
      DVLOG(1) << "Resovling simple conflict, merging nigori nodes: " << entry;
      status->increment_num_server_overwrites();
      OverwriteServerChanges(trans, &entry);
      UMA_HISTOGRAM_ENUMERATION("Sync.ResolveSimpleConflict",
                                NIGORI_MERGE,
                                CONFLICT_RESOLUTION_SIZE);
    } else if (!entry_deleted && name_matches && parent_matches &&
               specifics_match && !needs_reinsertion) {
      DVLOG(1) << "Resolving simple conflict, everything matches, ignoring "
               << "changes for: " << entry;
      // This unsets both IS_UNSYNCED and IS_UNAPPLIED_UPDATE, and sets the
      // BASE_VERSION to match the SERVER_VERSION. If we didn't also unset
      // IS_UNAPPLIED_UPDATE, then we would lose unsynced positional data from
      // adjacent entries when the server update gets applied and the item is
      // re-inserted into the PREV_ID/NEXT_ID linked list. This is primarily
      // an issue because we commit after applying updates, and is most
      // commonly seen when positional changes are made while a passphrase
      // is required (and hence there will be many encryption conflicts).
      OverwriteServerChanges(trans, &entry);
      IgnoreLocalChanges(&entry);
      UMA_HISTOGRAM_ENUMERATION("Sync.ResolveSimpleConflict",
                                CHANGES_MATCH,
                                CONFLICT_RESOLUTION_SIZE);
    } else if (base_server_specifics_match) {
      DVLOG(1) << "Resolving simple conflict, ignoring server encryption "
               << " changes for: " << entry;
      status->increment_num_server_overwrites();
      OverwriteServerChanges(trans, &entry);
      UMA_HISTOGRAM_ENUMERATION("Sync.ResolveSimpleConflict",
                                IGNORE_ENCRYPTION,
                                CONFLICT_RESOLUTION_SIZE);
    } else if (entry_deleted || !name_matches || !parent_matches) {
      OverwriteServerChanges(trans, &entry);
      status->increment_num_server_overwrites();
      DVLOG(1) << "Resolving simple conflict, overwriting server changes "
               << "for: " << entry;
      UMA_HISTOGRAM_ENUMERATION("Sync.ResolveSimpleConflict",
                                OVERWRITE_SERVER,
                                CONFLICT_RESOLUTION_SIZE);
    } else {
      DVLOG(1) << "Resolving simple conflict, ignoring local changes for: "
               << entry;
      IgnoreLocalChanges(&entry);
      status->increment_num_local_overwrites();
      UMA_HISTOGRAM_ENUMERATION("Sync.ResolveSimpleConflict",
                                OVERWRITE_LOCAL,
                                CONFLICT_RESOLUTION_SIZE);
    }
    // Now that we've resolved the conflict, clear the prev server
    // specifics.
    entry.Put(syncable::BASE_SERVER_SPECIFICS, sync_pb::EntitySpecifics());
    return SYNC_PROGRESS;
  } else {  // SERVER_IS_DEL is true
    // If a server deleted folder has local contents we should be in a set.
    if (entry.Get(syncable::IS_DIR)) {
      Directory::ChildHandles children;
      trans->directory()->GetChildHandlesById(trans,
                                              entry.Get(syncable::ID),
                                              &children);
      if (0 != children.size()) {
        DVLOG(1) << "Entry is a server deleted directory with local contents, "
                 << "should be in a set. (race condition).";
        return NO_SYNC_PROGRESS;
      }
    }

    // The entry is deleted on the server but still exists locally.
    if (!entry.Get(syncable::UNIQUE_CLIENT_TAG).empty()) {
      // If we've got a client-unique tag, we can undelete while retaining
      // our present ID.
      DCHECK_EQ(entry.Get(syncable::SERVER_VERSION), 0) << "For the server to "
          "know to re-create, client-tagged items should revert to version 0 "
          "when server-deleted.";
      OverwriteServerChanges(trans, &entry);
      status->increment_num_server_overwrites();
      DVLOG(1) << "Resolving simple conflict, undeleting server entry: "
               << entry;
      UMA_HISTOGRAM_ENUMERATION("Sync.ResolveSimpleConflict",
                                OVERWRITE_SERVER,
                                CONFLICT_RESOLUTION_SIZE);
      // Clobber the versions, just in case the above DCHECK is violated.
      entry.Put(syncable::SERVER_VERSION, 0);
      entry.Put(syncable::BASE_VERSION, 0);
    } else {
      // Otherwise, we've got to undelete by creating a new locally
      // uncommitted entry.
      SyncerUtil::SplitServerInformationIntoNewEntry(trans, &entry);

      MutableEntry server_update(trans, syncable::GET_BY_ID, id);
      CHECK(server_update.good());
      CHECK(server_update.Get(syncable::META_HANDLE) !=
            entry.Get(syncable::META_HANDLE))
          << server_update << entry;
      UMA_HISTOGRAM_ENUMERATION("Sync.ResolveSimpleConflict",
                                UNDELETE,
                                CONFLICT_RESOLUTION_SIZE);
    }
    return SYNC_PROGRESS;
  }
}

bool ConflictResolver::ResolveSimpleConflicts(
    syncable::WriteTransaction* trans,
    const Cryptographer* cryptographer,
    const ConflictProgress& progress,
    sessions::StatusController* status) {
  bool forward_progress = false;
  // First iterate over simple conflict items (those that belong to no set).
  set<Id>::const_iterator conflicting_item_it;
  set<Id> processed_items;
  for (conflicting_item_it = progress.ConflictingItemsBegin();
       conflicting_item_it != progress.ConflictingItemsEnd();
       ++conflicting_item_it) {
    Id id = *conflicting_item_it;
    if (processed_items.count(id) > 0)
      continue;
    map<Id, ConflictSet*>::const_iterator item_set_it =
        progress.IdToConflictSetFind(id);
    if (item_set_it == progress.IdToConflictSetEnd() ||
        0 == item_set_it->second) {
      // We have a simple conflict. In order check if positions have changed,
      // we need to process conflicting predecessors before successors. Traverse
      // backwards through all continuous conflicting predecessors, building a
      // stack of items to resolve in predecessor->successor order, then process
      // each item individually.
      list<Id> predecessors;
      Id prev_id = id;
      do {
        predecessors.push_back(prev_id);
        Entry entry(trans, syncable::GET_BY_ID, prev_id);
        // Any entry in conflict must be valid.
        CHECK(entry.good());
        Id new_prev_id = entry.Get(syncable::PREV_ID);
        if (new_prev_id == prev_id)
          break;
        prev_id = new_prev_id;
      } while (processed_items.count(prev_id) == 0 &&
               progress.HasSimpleConflictItem(prev_id));  // Excludes root.
      while (!predecessors.empty()) {
        id = predecessors.back();
        predecessors.pop_back();
        switch (ProcessSimpleConflict(trans, id, cryptographer, status)) {
          case NO_SYNC_PROGRESS:
            break;
          case SYNC_PROGRESS:
            forward_progress = true;
            break;
        }
        processed_items.insert(id);
      }
    }
  }
  return forward_progress;
}

bool ConflictResolver::ResolveConflicts(syncable::WriteTransaction* trans,
                                        const Cryptographer* cryptographer,
                                        const ConflictProgress& progress,
                                        sessions::StatusController* status) {
  // TODO(rlarocque): A good amount of code related to the resolution of
  // conflict sets has been deleted here.  This was done because the code had
  // not been used in years.  An unrelated bug fix accidentally re-enabled the
  // code, forcing us to make a decision about what we should do with the code.
  // We decided to do the safe thing and delete it for now.  This restores the
  // behaviour we've relied on for quite some time.  We should think about what
  // that code was trying to do and consider re-enabling parts of it.

  if (progress.ConflictSetsSize() > 0) {
    DVLOG(1) << "Detected " << progress.IdToConflictSetSize()
        << " non-simple conflicting entries in " << progress.ConflictSetsSize()
        << " unprocessed conflict sets.";
  }

  return ResolveSimpleConflicts(trans, cryptographer, progress, status);
}

}  // namespace browser_sync
