// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// An InternalComponentsFactory implementation designed for real production /
// normal use.

#ifndef SYNC_INTERNAL_API_PUBLIC_INTERNAL_COMPONENTS_FACTORY_IMPL_H_
#define SYNC_INTERNAL_API_PUBLIC_INTERNAL_COMPONENTS_FACTORY_IMPL_H_

#include "sync/internal_api/public/internal_components_factory.h"

namespace syncer {

class InternalComponentsFactoryImpl : public InternalComponentsFactory {
 public:
  InternalComponentsFactoryImpl();
  virtual ~InternalComponentsFactoryImpl();

  virtual scoped_ptr<SyncScheduler> BuildScheduler(
      const std::string& name,
      sessions::SyncSessionContext* context) OVERRIDE;

  virtual scoped_ptr<sessions::SyncSessionContext> BuildContext(
      ServerConnectionManager* connection_manager,
      syncable::Directory* directory,
      const std::vector<ModelSafeWorker*> workers,
      ExtensionsActivityMonitor* monitor,
      ThrottledDataTypeTracker* throttled_data_type_tracker,
      const std::vector<SyncEngineEventListener*>& listeners,
      sessions::DebugInfoGetter* debug_info_getter,
      TrafficRecorder* traffic_recorder,
      bool keystore_encryption_enabled) OVERRIDE;

  virtual scoped_ptr<syncable::DirectoryBackingStore>
  BuildDirectoryBackingStore(
      const std::string& dir_name,
      const FilePath& backing_filepath) OVERRIDE;

 private:
  DISALLOW_COPY_AND_ASSIGN(InternalComponentsFactoryImpl);
};

}  // namespace syncer

#endif  // SYNC_INTERNAL_API_PUBLIC_INTERNAL_COMPONENTS_FACTORY_IMPL_H_
