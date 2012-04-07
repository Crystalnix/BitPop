// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_GLUE_SHARED_CHANGE_PROCESSOR_H_
#define CHROME_BROWSER_SYNC_GLUE_SHARED_CHANGE_PROCESSOR_H_
#pragma once

#include "base/location.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/synchronization/lock.h"
#include "base/threading/thread_checker.h"
#include "chrome/browser/sync/api/sync_change_processor.h"
#include "chrome/browser/sync/api/sync_error.h"
#include "chrome/browser/sync/engine/model_safe_worker.h"

class ProfileSyncComponentsFactory;
class ProfileSyncService;
class SyncData;
class SyncableService;

typedef std::vector<SyncData> SyncDataList;

namespace browser_sync {

class GenericChangeProcessor;
class UnrecoverableErrorHandler;

// A ref-counted wrapper around a GenericChangeProcessor for use with datatypes
// that don't live on the UI thread.
//
// We need to make it refcounted as the ownership transfer from the
// DataTypeController is dependent on threading, and hence racy. The
// SharedChangeProcessor should be created on the UI thread, but should only be
// connected and used on the same thread as the datatype it interacts with.
//
// The only thread-safe method is Disconnect, which will disconnect from the
// generic change processor, letting us shut down the syncer/datatype without
// waiting for non-UI threads.
//
// Note: since we control the work being done while holding the lock, we ensure
// no I/O or other intensive work is done while blocking the UI thread (all
// the work is in-memory sync interactions).
//
// We use virtual methods so that we can use mock's in testing.
class SharedChangeProcessor
    : public base::RefCountedThreadSafe<SharedChangeProcessor>,
      public base::ThreadChecker {
 public:
  // Create an uninitialized SharedChangeProcessor (to be later connected).
  SharedChangeProcessor();

  // Connect to the Syncer. Will create and hold a new GenericChangeProcessor.
  // Returns: true if successful, false if disconnected or |local_service| was
  // NULL.
  virtual bool Connect(
    ProfileSyncComponentsFactory* sync_factory,
    ProfileSyncService* sync_service,
    UnrecoverableErrorHandler* error_handler,
    const base::WeakPtr<SyncableService>& local_service);

  // Disconnects from the generic change processor. May be called from any
  // thread. After this, all attempts to interact with the change processor by
  // |local_service_| are dropped and return errors. The syncer will be safe to
  // shut down from the point of view of this datatype.
  // Note: Once disconnected, you cannot reconnect without creating a new
  // SharedChangeProcessor.
  // Returns: true if we were previously succesfully connected, false if we were
  // already disconnected.
  virtual bool Disconnect();

  // GenericChangeProcessor stubs (with disconnect support).
  // Should only be called on the same thread the datatype resides.
  virtual SyncError ProcessSyncChanges(
      const tracked_objects::Location& from_here,
      const SyncChangeList& change_list);
  virtual SyncError GetSyncDataForType(syncable::ModelType type,
                                       SyncDataList* current_sync_data);
  virtual bool SyncModelHasUserCreatedNodes(syncable::ModelType type,
                                            bool* has_nodes);
  virtual bool CryptoReadyIfNecessary(syncable::ModelType type);

  // Register |generic_change_processor_| as the change processor for
  // |model_type| with the |sync_service|.
  // Does nothing if |disconnected_| is true.
  virtual void ActivateDataType(
      ProfileSyncService* sync_service,
      syncable::ModelType model_type,
      browser_sync::ModelSafeGroup model_safe_group);

 protected:
  friend class base::RefCountedThreadSafe<SharedChangeProcessor>;
  virtual ~SharedChangeProcessor();

 private:
  // Monitor lock for this object. All methods that interact with the change
  // processor must aquire this lock and check whether we're disconnected or
  // not. Once disconnected, all attempted changes to or loads from the change
  // processor return errors. This enables us to shut down the syncer without
  // having to wait for possibly non-UI thread datatypes to complete work.
  mutable base::Lock monitor_lock_;
  bool disconnected_;

  scoped_ptr<GenericChangeProcessor> generic_change_processor_;

  DISALLOW_COPY_AND_ASSIGN(SharedChangeProcessor);
};

}  // namespace browser_sync

#endif  // CHROME_BROWSER_SYNC_GLUE_SHARED_CHANGE_PROCESSOR_H_
