// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/glue/syncable_service_adapter.h"

#include "chrome/browser/sync/api/syncable_service.h"
#include "chrome/browser/sync/api/sync_data.h"
#include "chrome/browser/sync/glue/generic_change_processor.h"

namespace browser_sync {

SyncableServiceAdapter::SyncableServiceAdapter(
    syncable::ModelType type,
    SyncableService* service,
    GenericChangeProcessor* sync_processor)
    : syncing_(false),
      type_(type),
      service_(service),
      sync_processor_(sync_processor) {
}

SyncableServiceAdapter::~SyncableServiceAdapter() {
  if (syncing_) {
    NOTREACHED();
    LOG(ERROR) << "SyncableServiceAdapter for "
               << syncable::ModelTypeToString(type_) << " destroyed before "
               << "without being shut down properly.";
    service_->StopSyncing(type_);
  }
}

bool SyncableServiceAdapter::AssociateModels(SyncError* error) {
  syncing_ = true;
  SyncDataList initial_sync_data;
  SyncError temp_error =
      sync_processor_->GetSyncDataForType(type_, &initial_sync_data);
  if (temp_error.IsSet()) {
    *error = temp_error;
    return false;
  }

  // TODO(zea): Have all datatypes take ownership of the sync_processor_.
  // Further, refactor the DTC's to not need this class at all
  // (crbug.com/100114).
  temp_error = service_->MergeDataAndStartSyncing(type_,
                                                  initial_sync_data,
                                                  sync_processor_);
  if (temp_error.IsSet()) {
    *error = temp_error;
    return false;
  }
  return true;
}

bool SyncableServiceAdapter::DisassociateModels(SyncError* error) {
  service_->StopSyncing(type_);
  syncing_ = false;
  return true;
}

bool SyncableServiceAdapter::SyncModelHasUserCreatedNodes(bool* has_nodes) {
  return sync_processor_->SyncModelHasUserCreatedNodes(type_, has_nodes);
}

void SyncableServiceAdapter::AbortAssociation() {
  NOTIMPLEMENTED();
}

bool SyncableServiceAdapter::CryptoReadyIfNecessary() {
  return sync_processor_->CryptoReadyIfNecessary(type_);
}

}  // namespace browser_sync
