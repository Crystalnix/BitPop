// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_FILE_SYSTEM_REMOTE_FILE_SYNC_SERVICE_H_
#define CHROME_BROWSER_SYNC_FILE_SYSTEM_REMOTE_FILE_SYNC_SERVICE_H_

#include <string>

#include "base/basictypes.h"
#include "webkit/fileapi/file_system_url.h"
#include "webkit/fileapi/syncable/sync_callbacks.h"

class GURL;

namespace sync_file_system {

class RemoteChangeProcessor;
class LocalChangeProcessor;

enum RemoteServiceState {
  // Remote service is up and running, or has not seen any errors yet.
  // The consumer of this service can make new requests while the
  // service is in this state.
  REMOTE_SERVICE_OK,

  // Remote service is temporarily unavailable due to network,
  // authentication or some other temporary failure.
  // This state may be automatically resolved when the underlying
  // network condition or service condition changes.
  // The consumer of this service can still make new requests but
  // they may fail (with recoverable error code).
  REMOTE_SERVICE_TEMPORARY_UNAVAILABLE,

  // Remote service is temporarily unavailable due to authentication failure.
  // This state may be automatically resolved when the authentication token
  // has been refreshed internally (e.g. when the user signed in etc).
  // The consumer of this service can still make new requests but
  // they may fail (with recoverable error code).
  REMOTE_SERVICE_AUTHENTICATION_REQUIRED,

  // Remote service is disabled due to unrecoverable errors, e.g.
  // local database corruption.
  // Any new requests will immediately fail when the service is in
  // this state.
  REMOTE_SERVICE_DISABLED,
};

// This class represents a backing service of the sync filesystem.
// This also maintains conflict information, i.e. a list of conflicting files
// (at least in the current design).
// Owned by SyncFileSystemService.
class RemoteFileSyncService {
 public:
  class Observer {
   public:
    Observer() {}
    virtual ~Observer() {}

    // This is called when RemoteFileSyncService updates its internal queue
    // of pending remote changes.
    // |pending_changes_hint| indicates the pending queue length to help sync
    // scheduling but the value may not be accurately reflect the real-time
    // value.
    virtual void OnRemoteChangeQueueUpdated(int64 pending_changes_hint) = 0;

    // This is called when RemoteFileSyncService updates its state.
    virtual void OnRemoteServiceStateUpdated(
        RemoteServiceState state,
        const std::string& description) {}

   private:
    DISALLOW_COPY_AND_ASSIGN(Observer);
  };

  RemoteFileSyncService() {}
  virtual ~RemoteFileSyncService() {}

  virtual void AddObserver(Observer* observer) = 0;
  virtual void RemoveObserver(Observer* observer) = 0;

  // Registers |origin| to track remote side changes for the |origin|.
  // Upon completion, invokes |callback|.
  // The caller may call this method again when the remote service state
  // migrates to REMOTE_SERVICE_OK state if the error code returned via
  // |callback| was retriable ones.
  virtual void RegisterOriginForTrackingChanges(
      const GURL& origin,
      const fileapi::SyncStatusCallback& callback) = 0;

  // Unregisters |origin| to track remote side changes for the |origin|.
  // Upon completion, invokes |callback|.
  // The caller may call this method again when the remote service state
  // migrates to REMOTE_SERVICE_OK state if the error code returned via
  // |callback| was retriable ones.
  virtual void UnregisterOriginForTrackingChanges(
      const GURL& origin,
      const fileapi::SyncStatusCallback& callback) = 0;

  // Called by the sync engine to process one remote change.
  // After a change is processed |callback| will be called (to return
  // the control to the sync engine).
  virtual void ProcessRemoteChange(
      RemoteChangeProcessor* processor,
      const fileapi::SyncOperationCallback& callback) = 0;

  // Returns a LocalChangeProcessor that applies a local change to the remote
  // storage backed by this service.
  virtual LocalChangeProcessor* GetLocalChangeProcessor() = 0;

  // Returns true if the file |url| is marked conflicted in the remote service.
  virtual bool IsConflicting(const fileapi::FileSystemURL& url) = 0;

  // TODO(kinuko,tzik): Clean up unused interface methods when we fix
  // the manual conflict resolution API.
  // Returns a list of conflicting files for the given origin.
  virtual void GetConflictFiles(
      const GURL& origin,
      const fileapi::SyncFileSetCallback& callback) = 0;

  // Returns the metadata of a remote file pointed by |url|.
  virtual void GetRemoteFileMetadata(
      const fileapi::FileSystemURL& url,
      const fileapi::SyncFileMetadataCallback& callback) = 0;

  // Returns the current remote service state (should equal to the value
  // returned by the last OnRemoteServiceStateUpdated notification.
  virtual RemoteServiceState GetCurrentState() const = 0;

  // Returns the service name that backs this remote_file_sync_service.
  virtual const char* GetServiceName() const = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(RemoteFileSyncService);
};

}  // namespace sync_file_system

#endif  // CHROME_BROWSER_SYNC_FILE_SYSTEM_REMOTE_FILE_SYNC_SERVICE_H_
