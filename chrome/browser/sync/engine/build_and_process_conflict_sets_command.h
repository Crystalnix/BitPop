// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_ENGINE_BUILD_AND_PROCESS_CONFLICT_SETS_COMMAND_H_
#define CHROME_BROWSER_SYNC_ENGINE_BUILD_AND_PROCESS_CONFLICT_SETS_COMMAND_H_
#pragma once

#include <vector>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "chrome/browser/sync/engine/model_changing_syncer_command.h"
#include "chrome/browser/sync/engine/model_safe_worker.h"

namespace syncable {
class BaseTransaction;
class Entry;
class Id;
class WriteTransaction;
}  // namespace syncable

namespace browser_sync {

class ConflictResolver;
class Cryptographer;

namespace sessions {
class ConflictProgress;
class StatusController;
}

class BuildAndProcessConflictSetsCommand : public ModelChangingSyncerCommand {
 public:
  BuildAndProcessConflictSetsCommand();
  virtual ~BuildAndProcessConflictSetsCommand();

 protected:
  // ModelChangingSyncerCommand implementation.
  virtual std::set<ModelSafeGroup> GetGroupsToChange(
      const sessions::SyncSession& session) const OVERRIDE;
  virtual SyncerError ModelChangingExecuteImpl(
      sessions::SyncSession* session) OVERRIDE;

 private:
  void BuildConflictSets(syncable::BaseTransaction* trans,
                         sessions::ConflictProgress* conflict_progress);

  void MergeSetsForIntroducedLoops(syncable::BaseTransaction* trans,
      syncable::Entry* entry,
      sessions::ConflictProgress* conflict_progress);
  void MergeSetsForNonEmptyDirectories(syncable::BaseTransaction* trans,
      syncable::Entry* entry,
      sessions::ConflictProgress* conflict_progress);
  void MergeSetsForPositionUpdate(syncable::BaseTransaction* trans,
      syncable::Entry* entry,
      sessions::ConflictProgress* conflict_progress);

  DISALLOW_COPY_AND_ASSIGN(BuildAndProcessConflictSetsCommand);
};

}  // namespace browser_sync

#endif  // CHROME_BROWSER_SYNC_ENGINE_BUILD_AND_PROCESS_CONFLICT_SETS_COMMAND_H_
