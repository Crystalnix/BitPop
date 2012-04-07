// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_ENGINE_SYNCER_TYPES_H_
#define CHROME_BROWSER_SYNC_ENGINE_SYNCER_TYPES_H_
#pragma once

#include <map>
#include <string>
#include <vector>

#include "base/observer_list.h"
#include "chrome/browser/sync/syncable/model_type.h"

namespace syncable {
class Id;
}

// The intent of this is to keep all shared data types and enums for the syncer
// in a single place without having dependencies between other files.
namespace browser_sync {

namespace sessions {
struct SyncSessionSnapshot;
}
class Syncer;

enum UpdateAttemptResponse {
  // Update was applied or safely ignored.
  SUCCESS,

  // Conflicts with the local data representation. This can also mean that the
  // entry doesn't currently make sense if we applied it.
  CONFLICT,

  // We were unable to decrypt/encrypt this server data. As such, we can't make
  // forward progress on this node, but because the passphrase may not arrive
  // until later we don't want to get the syncer stuck. See UpdateApplicator
  // for how this is handled.
  CONFLICT_ENCRYPTION
};

enum ServerUpdateProcessingResult {
  // Success. Update applied and stored in SERVER_* fields or dropped if
  // irrelevant.
  SUCCESS_PROCESSED,

  // Success. Update details stored in SERVER_* fields, but wasn't applied.
  SUCCESS_STORED,

  // Update is illegally inconsistent with earlier updates. e.g. A bookmark
  // becoming a folder.
  FAILED_INCONSISTENT,

  // Update is illegal when considered alone. e.g. broken UTF-8 in the name.
  FAILED_CORRUPT,

  // Only used by VerifyUpdate. Indicates that an update is valid. As
  // VerifyUpdate cannot return SUCCESS_STORED, we reuse the value.
  SUCCESS_VALID = SUCCESS_STORED
};

// Different results from the verify phase will yield different methods of
// processing in the ProcessUpdates phase. The SKIP result means the entry
// doesn't go to the ProcessUpdates phase.
enum VerifyResult {
  VERIFY_FAIL,
  VERIFY_SUCCESS,
  VERIFY_UNDELETE,
  VERIFY_SKIP,
  VERIFY_UNDECIDED
};

enum VerifyCommitResult {
  VERIFY_UNSYNCABLE,
  VERIFY_OK,
};

struct SyncEngineEvent {
  enum EventCause {
    ////////////////////////////////////////////////////////////////
    // Sent on entry of Syncer state machine
    SYNC_CYCLE_BEGIN,

    // SyncerCommand generated events.
    STATUS_CHANGED,

    // We have reached the SYNCER_END state in the main sync loop.
    SYNC_CYCLE_ENDED,

    ////////////////////////////////////////////////////////////////
    // Generated in response to specific protocol actions or events.

    // New token in updated_token.
    UPDATED_TOKEN,

    // This is sent after the Syncer (and SyncerThread) have initiated self
    // halt due to no longer being permitted to communicate with the server.
    // The listener should sever the sync / browser connections and delete sync
    // data (i.e. as if the user clicked 'Stop Syncing' in the browser.
    STOP_SYNCING_PERMANENTLY,

    // These events are sent to indicate when we know the clearing of
    // server data have failed or succeeded.
    CLEAR_SERVER_DATA_SUCCEEDED,
    CLEAR_SERVER_DATA_FAILED,

    // This event is sent when we receive an actionable error. It is upto
    // the listeners to figure out the action to take using the snapshot sent.
    ACTIONABLE_ERROR,
  };

  explicit SyncEngineEvent(EventCause cause);
  ~SyncEngineEvent();

  EventCause what_happened;

  // The last session used for syncing.
  const sessions::SyncSessionSnapshot* snapshot;

  // Update-Client-Auth returns a new token for sync use.
  std::string updated_token;
};

class SyncEngineEventListener {
 public:
  // TODO(tim): Consider splitting this up to multiple callbacks, rather than
  // have to do Event e(type); OnSyncEngineEvent(e); at all callsites,
  virtual void OnSyncEngineEvent(const SyncEngineEvent& event) = 0;
 protected:
  virtual ~SyncEngineEventListener() {}
};

// This struct is passed between parts of the syncer during the processing of
// one sync loop. It lives on the stack. We don't expose the number of
// conflicts during SyncShare as the conflicts may be solved automatically
// by the conflict resolver.
typedef std::vector<syncable::Id> ConflictSet;

typedef std::map<syncable::Id, ConflictSet*> IdToConflictSetMap;

}  // namespace browser_sync

#endif  // CHROME_BROWSER_SYNC_ENGINE_SYNCER_TYPES_H_
