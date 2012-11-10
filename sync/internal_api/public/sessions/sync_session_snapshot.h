// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYNC_INTERNAL_API_PUBLIC_SESSIONS_SYNC_SESSION_SNAPSHOT_H_
#define SYNC_INTERNAL_API_PUBLIC_SESSIONS_SYNC_SESSION_SNAPSHOT_H_

#include <string>

#include "base/basictypes.h"
#include "base/time.h"
#include "sync/internal_api/public/base/model_type.h"
#include "sync/internal_api/public/base/model_type_payload_map.h"
#include "sync/internal_api/public/sessions/model_neutral_state.h"
#include "sync/internal_api/public/sessions/sync_source_info.h"

namespace base {
class DictionaryValue;
}

namespace syncer {
namespace sessions {

// An immutable snapshot of state from a SyncSession.  Convenient to use as
// part of notifications as it is inherently thread-safe.
// TODO(zea): if copying this all over the place starts getting expensive,
// consider passing around immutable references instead of values.
// Default copy and assign welcome.
class SyncSessionSnapshot {
 public:
  SyncSessionSnapshot();
  SyncSessionSnapshot(
      const ModelNeutralState& model_neutral_state,
      bool is_share_usable,
      ModelTypeSet initial_sync_ended,
      const ModelTypePayloadMap& download_progress_markers,
      bool more_to_sync,
      bool is_silenced,
      int num_encryption_conflicts,
      int num_hierarchy_conflicts,
      int num_simple_conflicts,
      int num_server_conflicts,
      const SyncSourceInfo& source,
      bool notifications_enabled,
      size_t num_entries,
      base::Time sync_start_time,
      bool retry_scheduled);
  ~SyncSessionSnapshot();

  // Caller takes ownership of the returned dictionary.
  base::DictionaryValue* ToValue() const;

  std::string ToString() const;

  ModelNeutralState model_neutral_state() const {
    return model_neutral_state_;
  }
  int64 num_server_changes_remaining() const;
  bool is_share_usable() const;
  ModelTypeSet initial_sync_ended() const;
  ModelTypePayloadMap download_progress_markers() const;
  bool has_more_to_sync() const;
  bool is_silenced() const;
  int num_encryption_conflicts() const;
  int num_hierarchy_conflicts() const;
  int num_simple_conflicts() const;
  int num_server_conflicts() const;
  SyncSourceInfo source() const;
  bool notifications_enabled() const;
  size_t num_entries() const;
  base::Time sync_start_time() const;
  bool retry_scheduled() const;

  // Set iff this snapshot was not built using the default constructor.
  bool is_initialized() const;

 private:
  ModelNeutralState model_neutral_state_;
  bool is_share_usable_;
  ModelTypeSet initial_sync_ended_;
  ModelTypePayloadMap download_progress_markers_;
  bool has_more_to_sync_;
  bool is_silenced_;
  int num_encryption_conflicts_;
  int num_hierarchy_conflicts_;
  int num_simple_conflicts_;
  int num_server_conflicts_;
  SyncSourceInfo source_;
  bool notifications_enabled_;
  size_t num_entries_;
  base::Time sync_start_time_;
  bool retry_scheduled_;

  bool is_initialized_;
};

}  // namespace sessions
}  // namespace syncer

#endif  // SYNC_INTERNAL_API_PUBLIC_SESSIONS_SYNC_SESSION_SNAPSHOT_H_
