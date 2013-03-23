// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_FILE_SYSTEM_MOCK_REMOTE_FILE_SYNC_SERVICE_H_
#define CHROME_BROWSER_SYNC_FILE_SYSTEM_MOCK_REMOTE_FILE_SYNC_SERVICE_H_

#include <map>

#include "base/memory/scoped_ptr.h"
#include "base/observer_list.h"
#include "chrome/browser/sync_file_system/mock_local_change_processor.h"
#include "chrome/browser/sync_file_system/remote_change_processor.h"
#include "chrome/browser/sync_file_system/remote_file_sync_service.h"
#include "googleurl/src/gurl.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "webkit/fileapi/syncable/sync_callbacks.h"
#include "webkit/fileapi/syncable/sync_file_metadata.h"

namespace sync_file_system {

class MockRemoteFileSyncService : public RemoteFileSyncService {
 public:
  static const char kServiceName[];

  MockRemoteFileSyncService();
  virtual ~MockRemoteFileSyncService();

  // RemoteFileSyncService overrides.
  MOCK_METHOD1(AddObserver, void(RemoteFileSyncService::Observer* observer));
  MOCK_METHOD1(RemoveObserver, void(RemoteFileSyncService::Observer* observer));
  MOCK_METHOD2(RegisterOriginForTrackingChanges,
               void(const GURL& origin,
                    const fileapi::SyncStatusCallback& callback));
  MOCK_METHOD2(UnregisterOriginForTrackingChanges,
               void(const GURL& origin,
                    const fileapi::SyncStatusCallback& callback));
  MOCK_METHOD2(ProcessRemoteChange,
               void(RemoteChangeProcessor* processor,
                    const fileapi::SyncOperationCallback& callback));
  MOCK_METHOD0(GetLocalChangeProcessor, LocalChangeProcessor*());
  MOCK_METHOD1(IsConflicting, bool(const fileapi::FileSystemURL& url));
  MOCK_METHOD2(GetConflictFiles,
               void(const GURL& origin,
                    const fileapi::SyncFileSetCallback& callback));
  MOCK_METHOD2(GetRemoteFileMetadata,
               void(const fileapi::FileSystemURL& url,
                    const fileapi::SyncFileMetadataCallback& callback));
  MOCK_CONST_METHOD0(GetCurrentState,
                     RemoteServiceState());
  MOCK_CONST_METHOD0(GetServiceName, const char*());


  // Send notifications to the observers.
  // Can be used in the mock implementation.
  void NotifyRemoteChangeQueueUpdated(int64 pending_changes);
  void NotifyRemoteServiceStateUpdated(
      RemoteServiceState state,
      const std::string& description);

  // Sets a mock local change processor. The value is returned by
  // the default action for GetLocalChangeProcessor.
  // Sets conflict file information.  The information is returned by
  // the default action for GetConflictFiles and GetRemoteConflictFileInfo.
  void add_conflict_file(const fileapi::FileSystemURL& url,
                         const fileapi::SyncFileMetadata& metadata) {
    conflict_file_urls_[url.origin()].insert(url);
    conflict_file_metadata_[url] = metadata;
  }

  void reset_conflict_files() {
    conflict_file_urls_.clear();
    conflict_file_metadata_.clear();
  }

 private:
  typedef std::map<GURL, fileapi::FileSystemURLSet> OriginToURLSetMap;
  typedef std::map<fileapi::FileSystemURL, fileapi::SyncFileMetadata,
                   fileapi::FileSystemURL::Comparator> FileMetadataMap;

  void AddObserverStub(Observer* observer);
  void RemoveObserverStub(Observer* observer);
  void RegisterOriginForTrackingChangesStub(
      const GURL& origin,
      const fileapi::SyncStatusCallback& callback);
  void UnregisterOriginForTrackingChangesStub(
      const GURL& origin,
      const fileapi::SyncStatusCallback& callback);
  void ProcessRemoteChangeStub(
      RemoteChangeProcessor* processor,
      const fileapi::SyncOperationCallback& callback);
  void GetConflictFilesStub(
      const GURL& origin,
      const fileapi::SyncFileSetCallback& callback);
  void GetRemoteFileMetadataStub(
      const fileapi::FileSystemURL& url,
      const fileapi::SyncFileMetadataCallback& callback);

  OriginToURLSetMap conflict_file_urls_;
  FileMetadataMap conflict_file_metadata_;

  // For default implementation.
  MockLocalChangeProcessor mock_local_change_processor_;

  ObserverList<Observer> observers_;

  DISALLOW_COPY_AND_ASSIGN(MockRemoteFileSyncService);
};

}  // namespace sync_file_system

#endif  // CHROME_BROWSER_SYNC_FILE_SYSTEM_MOCK_REMOTE_FILE_SYNC_SERVICE_H_
