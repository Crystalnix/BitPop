// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_FILE_SYSTEM_LOCAL_CHANGE_PROCESSOR_H_
#define CHROME_BROWSER_SYNC_FILE_SYSTEM_LOCAL_CHANGE_PROCESSOR_H_

#include "base/callback_forward.h"
#include "webkit/fileapi/syncable/sync_callbacks.h"

namespace fileapi {
class FileChange;
class FileChangeList;
class FileSystemURL;
}

namespace sync_file_system {

// Represents an interface to process one local change and applies
// it to the remote server.
// This interface is to be implemented/backed by RemoteSyncFileService.
class LocalChangeProcessor {
 public:
  LocalChangeProcessor() {}
  virtual ~LocalChangeProcessor() {}

  // This is called to apply the local |change|. If the change type is
  // ADD_OR_UPDATE for a file, |local_file_path| points to a local file
  // path that contains the latest file image.
  // When SYNC_STATUS_HAS_CONFLICT is returned the implementation should
  // notify the backing RemoteFileSyncService of the existence of conflict
  // (as the remote service is supposed to maintain a list of conflict files).
  virtual void ApplyLocalChange(
      const fileapi::FileChange& change,
      const FilePath& local_file_path,
      const fileapi::FileSystemURL& url,
      const fileapi::SyncStatusCallback& callback) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(LocalChangeProcessor);
};

}  // namespace sync_file_system

#endif  // CHROME_BROWSER_SYNC_FILE_SYSTEM_LOCAL_CHANGE_PROCESSOR_H_
