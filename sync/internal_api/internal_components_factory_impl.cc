// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sync/internal_api/public/internal_components_factory_impl.h"

#include "sync/engine/syncer.h"
#include "sync/engine/sync_scheduler_impl.h"
#include "sync/sessions/sync_session_context.h"
#include "sync/syncable/on_disk_directory_backing_store.h"

namespace syncer {

InternalComponentsFactoryImpl::InternalComponentsFactoryImpl() { }
InternalComponentsFactoryImpl::~InternalComponentsFactoryImpl() { }

scoped_ptr<SyncScheduler> InternalComponentsFactoryImpl::BuildScheduler(
    const std::string& name, sessions::SyncSessionContext* context) {
  return scoped_ptr<SyncScheduler>(
      new SyncSchedulerImpl(name, context, new Syncer()));
}

scoped_ptr<sessions::SyncSessionContext>
InternalComponentsFactoryImpl::BuildContext(
    ServerConnectionManager* connection_manager,
    syncable::Directory* directory,
    const std::vector<ModelSafeWorker*> workers,
    ExtensionsActivityMonitor* monitor,
    ThrottledDataTypeTracker* throttled_data_type_tracker,
    const std::vector<SyncEngineEventListener*>& listeners,
    sessions::DebugInfoGetter* debug_info_getter,
    TrafficRecorder* traffic_recorder,
    bool keystore_encryption_enabled) {
  return scoped_ptr<sessions::SyncSessionContext>(
      new sessions::SyncSessionContext(
          connection_manager, directory, workers, monitor,
          throttled_data_type_tracker, listeners, debug_info_getter,
          traffic_recorder,
          keystore_encryption_enabled));
}

scoped_ptr<syncable::DirectoryBackingStore>
InternalComponentsFactoryImpl::BuildDirectoryBackingStore(
      const std::string& dir_name, const FilePath& backing_filepath) {
  return scoped_ptr<syncable::DirectoryBackingStore>(
      new syncable::OnDiskDirectoryBackingStore(dir_name, backing_filepath));
}

}  // namespace syncer
