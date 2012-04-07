// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_ENGINE_DOWNLOAD_UPDATES_COMMAND_H_
#define CHROME_BROWSER_SYNC_ENGINE_DOWNLOAD_UPDATES_COMMAND_H_
#pragma once

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/gtest_prod_util.h"
#include "chrome/browser/sync/engine/model_safe_worker.h"
#include "chrome/browser/sync/engine/syncer_command.h"
#include "chrome/browser/sync/protocol/sync.pb.h"
#include "chrome/browser/sync/syncable/model_type.h"

namespace sync_pb {
class EntitySpecifics;
}

namespace browser_sync {

// Determine the enabled datatypes, download a batch of updates for them
// from the server, place the result in the SyncSession for further processing.
//
// The main inputs to this operation are the download_progress state
// in the syncable::Directory, and the set of enabled types as indicated by
// the SyncSession. DownloadUpdatesCommand will fetch updates for
// all the enabled types, using download_progress to indicate the starting
// point to the server. DownloadUpdatesCommand stores the server response
// in the SyncSession. Only one server request is performed per Execute
// operation. A loop that causes multiple Execute operations within a sync
// session can be found in the Syncer logic. When looping, the
// DownloadUpdatesCommand consumes the information stored by the
// StoreTimestampsCommand.
//
// In practice, DownloadUpdatesCommand should loop until all updates are
// downloaded for all enabled datatypes (i.e., until the server indicates
// changes_remaining == 0 in the GetUpdates response), or until an error
// is encountered.
class DownloadUpdatesCommand : public SyncerCommand {
 public:
  DownloadUpdatesCommand();
  virtual ~DownloadUpdatesCommand();

  // SyncerCommand implementation.
  virtual SyncerError ExecuteImpl(sessions::SyncSession* session) OVERRIDE;

 private:
  FRIEND_TEST_ALL_PREFIXES(DownloadUpdatesCommandTest, VerifyAppendDebugInfo);
  void AppendClientDebugInfoIfNeeded(sessions::SyncSession* session,
      sync_pb::DebugInfo* debug_info);
  DISALLOW_COPY_AND_ASSIGN(DownloadUpdatesCommand);
};

}  // namespace browser_sync

#endif  // CHROME_BROWSER_SYNC_ENGINE_DOWNLOAD_UPDATES_COMMAND_H_

