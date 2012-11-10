// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sync/engine/verify_updates_command.h"

#include <string>

#include "base/location.h"
#include "sync/engine/syncer.h"
#include "sync/engine/syncer_proto_util.h"
#include "sync/engine/syncer_types.h"
#include "sync/engine/syncer_util.h"
#include "sync/internal_api/public/engine/model_safe_worker.h"
#include "sync/protocol/bookmark_specifics.pb.h"
#include "sync/protocol/sync.pb.h"
#include "sync/syncable/entry.h"
#include "sync/syncable/mutable_entry.h"
#include "sync/syncable/syncable_proto_util.h"
#include "sync/syncable/write_transaction.h"

namespace syncer {

using syncable::GET_BY_ID;
using syncable::SYNCER;
using syncable::WriteTransaction;

namespace {

// This function attempts to determine whether or not this update is genuinely
// new, or if it is a reflection of one of our own commits.
//
// There is a known inaccuracy in its implementation.  If this update ends up
// being applied to a local item with a different ID, we will count the change
// as being a non-reflection update.  Fortunately, the server usually updates
// our IDs correctly in its commit response, so a new ID during GetUpdate should
// be rare.
//
// The only secnarios I can think of where this might happen are:
// - We commit a  new item to the server, but we don't persist the
// server-returned new ID to the database before we shut down.  On the GetUpdate
// following the next restart, we will receive an update from the server that
// updates its local ID.
// - When two attempts to create an item with identical UNIQUE_CLIENT_TAG values
// collide at the server.  I have seen this in testing.  When it happens, the
// test server will send one of the clients a response to upate its local ID so
// that both clients will refer to the item using the same ID going forward.  In
// this case, we're right to assume that the update is not a reflection.
//
// For more information, see FindLocalIdToUpdate().
bool UpdateContainsNewVersion(syncable::BaseTransaction *trans,
                              const sync_pb::SyncEntity &update) {
  int64 existing_version = -1; // The server always sends positive versions.
  syncable::Entry existing_entry(trans, GET_BY_ID,
                                 SyncableIdFromProto(update.id_string()));
  if (existing_entry.good())
    existing_version = existing_entry.Get(syncable::BASE_VERSION);

  if (!existing_entry.good() && update.deleted()) {
    // There are several possible explanations for this.  The most common cases
    // will be first time sync and the redelivery of deletions we've already
    // synced, accepted, and purged from our database.  In either case, the
    // update is useless to us.  Let's count them all as "not new", even though
    // that may not always be entirely accurate.
    return false;
  }

  if (existing_entry.good() &&
      !existing_entry.Get(syncable::UNIQUE_CLIENT_TAG).empty() &&
      existing_entry.Get(syncable::IS_DEL) &&
      update.deleted()) {
    // Unique client tags will have their version set to zero when they're
    // deleted.  The usual version comparison logic won't be able to detect
    // reflections of these items.  Instead, we assume any received tombstones
    // are reflections.  That should be correct most of the time.
    return false;
  }

  return existing_version < update.version();
}

// In the event that IDs match, but tags differ AttemptReuniteClient tag
// will have refused to unify the update.
// We should not attempt to apply it at all since it violates consistency
// rules.
VerifyResult VerifyTagConsistency(const sync_pb::SyncEntity& entry,
                                  const syncable::MutableEntry& same_id) {
  if (entry.has_client_defined_unique_tag() &&
      entry.client_defined_unique_tag() !=
          same_id.Get(syncable::UNIQUE_CLIENT_TAG)) {
    return VERIFY_FAIL;
  }
  return VERIFY_UNDECIDED;
}
}  // namespace

VerifyUpdatesCommand::VerifyUpdatesCommand() {}
VerifyUpdatesCommand::~VerifyUpdatesCommand() {}

std::set<ModelSafeGroup> VerifyUpdatesCommand::GetGroupsToChange(
    const sessions::SyncSession& session) const {
  std::set<ModelSafeGroup> groups_with_updates;

  const sync_pb::GetUpdatesResponse& updates =
      session.status_controller().updates_response().get_updates();
  for (int i = 0; i < updates.entries().size(); i++) {
    groups_with_updates.insert(
        GetGroupForModelType(GetModelType(updates.entries(i)),
                             session.routing_info()));
  }

  return groups_with_updates;
}

SyncerError VerifyUpdatesCommand::ModelChangingExecuteImpl(
    sessions::SyncSession* session) {
  DVLOG(1) << "Beginning Update Verification";
  syncable::Directory* dir = session->context()->directory();
  WriteTransaction trans(FROM_HERE, SYNCER, dir);
  sessions::StatusController* status = session->mutable_status_controller();
  const sync_pb::GetUpdatesResponse& updates =
      status->updates_response().get_updates();
  int update_count = updates.entries().size();

  ModelTypeSet requested_types = GetRoutingInfoTypes(
      session->routing_info());

  DVLOG(1) << update_count << " entries to verify";
  for (int i = 0; i < update_count; i++) {
    const sync_pb::SyncEntity& update = updates.entries(i);
    ModelSafeGroup g = GetGroupForModelType(GetModelType(update),
                                            session->routing_info());
    if (g != status->group_restriction())
      continue;

    VerifyUpdateResult result = VerifyUpdate(&trans, update,
                                             requested_types,
                                             session->routing_info());
    status->mutable_update_progress()->AddVerifyResult(result.value, update);
    status->increment_num_updates_downloaded_by(1);
    if (!UpdateContainsNewVersion(&trans, update))
      status->increment_num_reflected_updates_downloaded_by(1);
    if (update.deleted())
      status->increment_num_tombstone_updates_downloaded_by(1);
  }

  return SYNCER_OK;
}

VerifyUpdatesCommand::VerifyUpdateResult VerifyUpdatesCommand::VerifyUpdate(
    syncable::WriteTransaction* trans, const sync_pb::SyncEntity& entry,
    const ModelTypeSet& requested_types,
    const ModelSafeRoutingInfo& routes) {
  syncable::Id id = SyncableIdFromProto(entry.id_string());
  VerifyUpdateResult result = {VERIFY_FAIL, GROUP_PASSIVE};

  const bool deleted = entry.has_deleted() && entry.deleted();
  const bool is_directory = IsFolder(entry);
  const ModelType model_type = GetModelType(entry);

  if (!id.ServerKnows()) {
    LOG(ERROR) << "Illegal negative id in received updates";
    return result;
  }
  {
    const std::string name = SyncerProtoUtil::NameFromSyncEntity(entry);
    if (name.empty() && !deleted) {
      LOG(ERROR) << "Zero length name in non-deleted update";
      return result;
    }
  }

  syncable::MutableEntry same_id(trans, GET_BY_ID, id);
  result.value = VerifyNewEntry(entry, &same_id, deleted);

  ModelType placement_type = !deleted ? GetModelType(entry)
      : same_id.good() ? same_id.GetModelType() : UNSPECIFIED;
  result.placement = GetGroupForModelType(placement_type, routes);

  if (VERIFY_UNDECIDED == result.value) {
    result.value = VerifyTagConsistency(entry, same_id);
  }

  if (VERIFY_UNDECIDED == result.value) {
    if (deleted) {
      // For deletes the server could send tombostones for items that
      // the client did not request. If so ignore those items.
      if (IsRealDataType(placement_type) &&
          !requested_types.Has(placement_type)) {
        result.value = VERIFY_SKIP;
      } else {
        result.value = VERIFY_SUCCESS;
      }
    }
  }

  // If we have an existing entry, we check here for updates that break
  // consistency rules.
  if (VERIFY_UNDECIDED == result.value) {
    result.value = VerifyUpdateConsistency(trans, entry, &same_id,
        deleted, is_directory, model_type);
  }

  if (VERIFY_UNDECIDED == result.value)
    result.value = VERIFY_SUCCESS;  // No news is good news.

  return result;  // This might be VERIFY_SUCCESS as well
}

}  // namespace syncer
