// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/sessions/session_state.h"

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/values.h"
#include "chrome/browser/sync/protocol/proto_enum_conversions.h"

using std::set;
using std::vector;

namespace browser_sync {
namespace sessions {

SyncSourceInfo::SyncSourceInfo()
    : updates_source(sync_pb::GetUpdatesCallerInfo::UNKNOWN) {}

SyncSourceInfo::SyncSourceInfo(
    const syncable::ModelTypePayloadMap& t)
    : updates_source(sync_pb::GetUpdatesCallerInfo::UNKNOWN), types(t) {}

SyncSourceInfo::SyncSourceInfo(
    const sync_pb::GetUpdatesCallerInfo::GetUpdatesSource& u,
    const syncable::ModelTypePayloadMap& t)
    : updates_source(u), types(t) {}

SyncSourceInfo::~SyncSourceInfo() {}

DictionaryValue* SyncSourceInfo::ToValue() const {
  DictionaryValue* value = new DictionaryValue();
  value->SetString("updatesSource",
                   GetUpdatesSourceString(updates_source));
  value->Set("types", syncable::ModelTypePayloadMapToValue(types));
  return value;
}

SyncerStatus::SyncerStatus()
    : invalid_store(false),
      syncer_stuck(false),
      syncing(false),
      num_successful_commits(0),
      num_successful_bookmark_commits(0),
      num_updates_downloaded_total(0),
      num_tombstone_updates_downloaded_total(0),
      num_local_overwrites(0),
      num_server_overwrites(0) {
}

SyncerStatus::~SyncerStatus() {
}

DictionaryValue* SyncerStatus::ToValue() const {
  DictionaryValue* value = new DictionaryValue();
  value->SetBoolean("invalidStore", invalid_store);
  value->SetBoolean("syncerStuck", syncer_stuck);
  value->SetBoolean("syncing", syncing);
  value->SetInteger("numSuccessfulCommits", num_successful_commits);
  value->SetInteger("numSuccessfulBookmarkCommits",
                num_successful_bookmark_commits);
  value->SetInteger("numUpdatesDownloadedTotal",
                num_updates_downloaded_total);
  value->SetInteger("numTombstoneUpdatesDownloadedTotal",
                num_tombstone_updates_downloaded_total);
  value->SetInteger("numLocalOverwrites", num_local_overwrites);
  value->SetInteger("numServerOverwrites", num_server_overwrites);
  return value;
}

DictionaryValue* DownloadProgressMarkersToValue(
    const std::string
        (&download_progress_markers)[syncable::MODEL_TYPE_COUNT]) {
  DictionaryValue* value = new DictionaryValue();
  for (int i = syncable::FIRST_REAL_MODEL_TYPE;
       i < syncable::MODEL_TYPE_COUNT; ++i) {
    // TODO(akalin): Unpack the value into a protobuf.
    std::string base64_marker;
    bool encoded =
        base::Base64Encode(download_progress_markers[i], &base64_marker);
    DCHECK(encoded);
    value->SetString(
        syncable::ModelTypeToString(syncable::ModelTypeFromInt(i)),
        base64_marker);
  }
  return value;
}

ErrorCounters::ErrorCounters()
    : num_conflicting_commits(0),
      consecutive_transient_error_commits(0),
      consecutive_errors(0) {
}

DictionaryValue* ErrorCounters::ToValue() const {
  DictionaryValue* value = new DictionaryValue();
  value->SetInteger("numConflictingCommits", num_conflicting_commits);
  value->SetInteger("consecutiveTransientErrorCommits",
                consecutive_transient_error_commits);
  value->SetInteger("consecutiveErrors", consecutive_errors);
  return value;
}

SyncSessionSnapshot::SyncSessionSnapshot(
    const SyncerStatus& syncer_status,
    const ErrorCounters& errors,
    int64 num_server_changes_remaining,
    bool is_share_usable,
    const syncable::ModelTypeBitSet& initial_sync_ended,
    const std::string
        (&download_progress_markers)[syncable::MODEL_TYPE_COUNT],
    bool more_to_sync,
    bool is_silenced,
    int64 unsynced_count,
    int num_blocking_conflicting_updates,
    int num_conflicting_updates,
    bool did_commit_items,
    const SyncSourceInfo& source,
    size_t num_entries)
    : syncer_status(syncer_status),
      errors(errors),
      num_server_changes_remaining(num_server_changes_remaining),
      is_share_usable(is_share_usable),
      initial_sync_ended(initial_sync_ended),
      download_progress_markers(),
      has_more_to_sync(more_to_sync),
      is_silenced(is_silenced),
      unsynced_count(unsynced_count),
      num_blocking_conflicting_updates(num_blocking_conflicting_updates),
      num_conflicting_updates(num_conflicting_updates),
      did_commit_items(did_commit_items),
      source(source),
      num_entries(num_entries){
  for (int i = syncable::FIRST_REAL_MODEL_TYPE;
       i < syncable::MODEL_TYPE_COUNT; ++i) {
    const_cast<std::string&>(this->download_progress_markers[i]).assign(
        download_progress_markers[i]);
  }
}

SyncSessionSnapshot::~SyncSessionSnapshot() {}

DictionaryValue* SyncSessionSnapshot::ToValue() const {
  DictionaryValue* value = new DictionaryValue();
  value->Set("syncerStatus", syncer_status.ToValue());
  value->Set("errors", errors.ToValue());
  // We don't care too much if we lose precision here.
  value->SetInteger("numServerChangesRemaining",
                    static_cast<int>(num_server_changes_remaining));
  value->SetBoolean("isShareUsable", is_share_usable);
  value->Set("initialSyncEnded",
             syncable::ModelTypeBitSetToValue(initial_sync_ended));
  value->Set("downloadProgressMarkers",
             DownloadProgressMarkersToValue(download_progress_markers));
  value->SetBoolean("hasMoreToSync", has_more_to_sync);
  value->SetBoolean("isSilenced", is_silenced);
  // We don't care too much if we lose precision here, also.
  value->SetInteger("unsyncedCount",
                    static_cast<int>(unsynced_count));
  value->SetInteger("numBlockingConflictingUpdates",
                    num_blocking_conflicting_updates);
  value->SetInteger("numConflictingUpdates", num_conflicting_updates);
  value->SetBoolean("didCommitItems", did_commit_items);
  value->SetInteger("numEntries", num_entries);
  value->Set("source", source.ToValue());
  return value;
}

ConflictProgress::ConflictProgress(bool* dirty_flag) : dirty_(dirty_flag) {}

ConflictProgress::~ConflictProgress() {
  CleanupSets();
}

IdToConflictSetMap::const_iterator ConflictProgress::IdToConflictSetFind(
    const syncable::Id& the_id) const {
  return id_to_conflict_set_.find(the_id);
}

IdToConflictSetMap::const_iterator
ConflictProgress::IdToConflictSetBegin() const {
  return id_to_conflict_set_.begin();
}

IdToConflictSetMap::const_iterator
ConflictProgress::IdToConflictSetEnd() const {
  return id_to_conflict_set_.end();
}

IdToConflictSetMap::size_type ConflictProgress::IdToConflictSetSize() const {
  return id_to_conflict_set_.size();
}

const ConflictSet* ConflictProgress::IdToConflictSetGet(
    const syncable::Id& the_id) {
  return id_to_conflict_set_[the_id];
}

std::set<ConflictSet*>::const_iterator
ConflictProgress::ConflictSetsBegin() const {
  return conflict_sets_.begin();
}

std::set<ConflictSet*>::const_iterator
ConflictProgress::ConflictSetsEnd() const {
  return conflict_sets_.end();
}

std::set<ConflictSet*>::size_type
ConflictProgress::ConflictSetsSize() const {
  return conflict_sets_.size();
}

std::set<syncable::Id>::iterator
ConflictProgress::ConflictingItemsBegin() {
  return conflicting_item_ids_.begin();
}
std::set<syncable::Id>::const_iterator
ConflictProgress::ConflictingItemsBeginConst() const {
  return conflicting_item_ids_.begin();
}
std::set<syncable::Id>::const_iterator
ConflictProgress::ConflictingItemsEnd() const {
  return conflicting_item_ids_.end();
}

void ConflictProgress::AddConflictingItemById(const syncable::Id& the_id) {
  std::pair<std::set<syncable::Id>::iterator, bool> ret =
    conflicting_item_ids_.insert(the_id);
  if (ret.second)
    *dirty_ = true;
}

void ConflictProgress::EraseConflictingItemById(const syncable::Id& the_id) {
  int items_erased = conflicting_item_ids_.erase(the_id);
  if (items_erased != 0)
    *dirty_ = true;
}

void ConflictProgress::AddNonblockingConflictingItemById(
    const syncable::Id& the_id) {
  std::pair<std::set<syncable::Id>::iterator, bool> ret =
      nonblocking_conflicting_item_ids_.insert(the_id);
  if (ret.second)
    *dirty_ = true;
}

void ConflictProgress::EraseNonblockingConflictingItemById(
    const syncable::Id& the_id) {
  int items_erased = nonblocking_conflicting_item_ids_.erase(the_id);
  if (items_erased != 0)
    *dirty_ = true;
}

void ConflictProgress::MergeSets(const syncable::Id& id1,
                                 const syncable::Id& id2) {
  // There are no single item sets, we just leave those entries == 0
  vector<syncable::Id>* set1 = id_to_conflict_set_[id1];
  vector<syncable::Id>* set2 = id_to_conflict_set_[id2];
  vector<syncable::Id>* rv = 0;
  if (0 == set1 && 0 == set2) {
    // Neither item currently has a set so we build one.
    rv = new vector<syncable::Id>();
    rv->push_back(id1);
    if (id1 != id2) {
      rv->push_back(id2);
    } else {
      LOG(WARNING) << "[BUG] Attempting to merge two identical conflict ids.";
    }
    conflict_sets_.insert(rv);
  } else if (0 == set1) {
    // Add the item to the existing set.
    rv = set2;
    rv->push_back(id1);
  } else if (0 == set2) {
    // Add the item to the existing set.
    rv = set1;
    rv->push_back(id2);
  } else if (set1 == set2) {
    // It's the same set already.
    return;
  } else {
    // Merge the two sets.
    rv = set1;
    // Point all the second sets id's back to the first.
    vector<syncable::Id>::iterator i;
    for (i = set2->begin() ; i != set2->end() ; ++i) {
      id_to_conflict_set_[*i] = rv;
    }
    // Copy the second set to the first.
    rv->insert(rv->end(), set2->begin(), set2->end());
    conflict_sets_.erase(set2);
    delete set2;
  }
  id_to_conflict_set_[id1] = id_to_conflict_set_[id2] = rv;
}

void ConflictProgress::CleanupSets() {
  // Clean up all the sets.
  set<ConflictSet*>::iterator i;
  for (i = conflict_sets_.begin(); i != conflict_sets_.end(); i++) {
    delete *i;
  }
  conflict_sets_.clear();
  id_to_conflict_set_.clear();
}

UpdateProgress::UpdateProgress() {}

UpdateProgress::~UpdateProgress() {}

void UpdateProgress::AddVerifyResult(const VerifyResult& verify_result,
                                     const sync_pb::SyncEntity& entity) {
  verified_updates_.push_back(std::make_pair(verify_result, entity));
}

void UpdateProgress::AddAppliedUpdate(const UpdateAttemptResponse& response,
    const syncable::Id& id) {
  applied_updates_.push_back(std::make_pair(response, id));
}

std::vector<AppliedUpdate>::iterator UpdateProgress::AppliedUpdatesBegin() {
  return applied_updates_.begin();
}

std::vector<VerifiedUpdate>::const_iterator
UpdateProgress::VerifiedUpdatesBegin() const {
  return verified_updates_.begin();
}

std::vector<AppliedUpdate>::const_iterator
UpdateProgress::AppliedUpdatesEnd() const {
  return applied_updates_.end();
}

std::vector<VerifiedUpdate>::const_iterator
UpdateProgress::VerifiedUpdatesEnd() const {
  return verified_updates_.end();
}

int UpdateProgress::SuccessfullyAppliedUpdateCount() const {
  int count = 0;
  for (std::vector<AppliedUpdate>::const_iterator it =
       applied_updates_.begin();
       it != applied_updates_.end();
       ++it) {
    if (it->first == SUCCESS)
      count++;
  }
  return count;
}

// Returns true if at least one update application failed due to a conflict
// during this sync cycle.
bool UpdateProgress::HasConflictingUpdates() const {
  std::vector<AppliedUpdate>::const_iterator it;
  for (it = applied_updates_.begin(); it != applied_updates_.end(); ++it) {
    if (it->first == CONFLICT) {
      return true;
    }
  }
  return false;
}

AllModelTypeState::AllModelTypeState(bool* dirty_flag)
    : unsynced_handles(dirty_flag),
      syncer_status(dirty_flag),
      error_counters(dirty_flag),
      num_server_changes_remaining(dirty_flag, 0),
      commit_set(ModelSafeRoutingInfo()) {
}

AllModelTypeState::~AllModelTypeState() {}

PerModelSafeGroupState::PerModelSafeGroupState(bool* dirty_flag)
    : conflict_progress(dirty_flag) {
}

PerModelSafeGroupState::~PerModelSafeGroupState() {
}

}  // namespace sessions
}  // namespace browser_sync
