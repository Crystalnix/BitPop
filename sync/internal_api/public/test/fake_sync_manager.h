// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYNC_INTERNAL_API_PUBLIC_TEST_FAKE_SYNC_MANAGER_H_
#define SYNC_INTERNAL_API_PUBLIC_TEST_FAKE_SYNC_MANAGER_H_

#include <string>

#include "base/memory/ref_counted.h"
#include "base/observer_list.h"
#include "sync/internal_api/public/sync_manager.h"
#include "sync/notifier/sync_notifier_registrar.h"

namespace base {
class SequencedTaskRunner;
}

namespace syncer {

class FakeSyncManager : public SyncManager {
 public:
  // |initial_sync_ended_types|: The set of types that have initial_sync_ended
  // set to true. This value will be used by InitialSyncEndedTypes() until the
  // next configuration is performed.
  //
  // |progress_marker_types|: The set of types that have valid progress
  // markers. This will be used by GetTypesWithEmptyProgressMarkerToken() until
  // the next configuration is performed.
  //
  // |configure_fail_types|: The set of types that will fail
  // configuration. Once ConfigureSyncer is called, the
  // |initial_sync_ended_types_| and |progress_marker_types_| will be updated
  // to include those types that didn't fail.
  FakeSyncManager(ModelTypeSet initial_sync_ended_types,
                  ModelTypeSet progress_marker_types,
                  ModelTypeSet configure_fail_types);
  virtual ~FakeSyncManager();

  // Returns those types that have been cleaned (purged from the directory)
  // since the last call to GetAndResetCleanedTypes(), or since startup if never
  // called.
  ModelTypeSet GetAndResetCleanedTypes();

  // Returns those types that have been downloaded since the last call to
  // GetAndResetDownloadedTypes(), or since startup if never called.
  ModelTypeSet GetAndResetDownloadedTypes();

  // Returns those types that have been marked as enabled since the
  // last call to GetAndResetEnabledTypes(), or since startup if never
  // called.
  ModelTypeSet GetAndResetEnabledTypes();

  // Posts a method to invalidate the given IDs on the sync thread.
  void Invalidate(const ObjectIdPayloadMap& id_payloads,
                  IncomingNotificationSource source);

  // Posts a method to enable notifications on the sync thread.
  void EnableNotifications();

  // Posts a method to disable notifications on the sync thread.
  void DisableNotifications(NotificationsDisabledReason reason);

  // Block until the sync thread has finished processing any pending messages.
  void WaitForSyncThread();

  // SyncManager implementation.
  // Note: we treat whatever message loop this is called from as the sync
  // loop for purposes of callbacks.
  virtual bool Init(
      const FilePath& database_location,
      const WeakHandle<JsEventHandler>& event_handler,
      const std::string& sync_server_and_path,
      int sync_server_port,
      bool use_ssl,
      const scoped_refptr<base::TaskRunner>& blocking_task_runner,
      scoped_ptr<HttpPostProviderFactory> post_factory,
      const std::vector<ModelSafeWorker*>& workers,
      ExtensionsActivityMonitor* extensions_activity_monitor,
      ChangeDelegate* change_delegate,
      const SyncCredentials& credentials,
      scoped_ptr<SyncNotifier> sync_notifier,
      const std::string& restored_key_for_bootstrapping,
      const std::string& restored_keystore_key_for_bootstrapping,
      bool keystore_encryption_enabled,
      scoped_ptr<InternalComponentsFactory> internal_components_factory,
      Encryptor* encryptor,
      UnrecoverableErrorHandler* unrecoverable_error_handler,
      ReportUnrecoverableErrorFunction
          report_unrecoverable_error_function) OVERRIDE;
  virtual void ThrowUnrecoverableError() OVERRIDE;
  virtual ModelTypeSet InitialSyncEndedTypes() OVERRIDE;
  virtual ModelTypeSet GetTypesWithEmptyProgressMarkerToken(
      ModelTypeSet types) OVERRIDE;
  virtual bool PurgePartiallySyncedTypes() OVERRIDE;
  virtual void UpdateCredentials(const SyncCredentials& credentials) OVERRIDE;
  virtual void UpdateEnabledTypes(const ModelTypeSet& types) OVERRIDE;
  virtual void RegisterInvalidationHandler(
      SyncNotifierObserver* handler) OVERRIDE;
  virtual void UpdateRegisteredInvalidationIds(
      SyncNotifierObserver* handler,
      const ObjectIdSet& ids) OVERRIDE;
  virtual void UnregisterInvalidationHandler(
      SyncNotifierObserver* handler) OVERRIDE;
  virtual void StartSyncingNormally(
      const ModelSafeRoutingInfo& routing_info) OVERRIDE;
  virtual void SetEncryptionPassphrase(const std::string& passphrase,
                                       bool is_explicit) OVERRIDE;
  virtual void SetDecryptionPassphrase(const std::string& passphrase) OVERRIDE;
  virtual void ConfigureSyncer(
      ConfigureReason reason,
      const ModelTypeSet& types_to_config,
      const ModelSafeRoutingInfo& new_routing_info,
      const base::Closure& ready_task,
      const base::Closure& retry_task) OVERRIDE;
  virtual void AddObserver(Observer* observer) OVERRIDE;
  virtual void RemoveObserver(Observer* observer) OVERRIDE;
  virtual SyncStatus GetDetailedStatus() const OVERRIDE;
  virtual bool IsUsingExplicitPassphrase() OVERRIDE;
  virtual bool GetKeystoreKeyBootstrapToken(std::string* token) OVERRIDE;
  virtual void SaveChanges() OVERRIDE;
  virtual void StopSyncingForShutdown(const base::Closure& callback) OVERRIDE;
  virtual void ShutdownOnSyncThread() OVERRIDE;
  virtual UserShare* GetUserShare() OVERRIDE;
  virtual void RefreshNigori(const std::string& chrome_version,
                             const base::Closure& done_callback) OVERRIDE;
  virtual void EnableEncryptEverything() OVERRIDE;
  virtual bool ReceivedExperiment(Experiments* experiments) OVERRIDE;
  virtual bool HasUnsyncedItems() OVERRIDE;

 private:
  void InvalidateOnSyncThread(
      const ObjectIdPayloadMap& id_payloads,
      IncomingNotificationSource source);
  void EnableNotificationsOnSyncThread();
  void DisableNotificationsOnSyncThread(NotificationsDisabledReason reason);

  scoped_refptr<base::SequencedTaskRunner> sync_task_runner_;

  ObserverList<SyncManager::Observer> observers_;

  // Faked directory state.
  ModelTypeSet initial_sync_ended_types_;
  ModelTypeSet progress_marker_types_;

  // Test specific state.
  // The types that should fail configuration attempts. These types will not
  // have their progress markers or initial_sync_ended bits set.
  ModelTypeSet configure_fail_types_;
  // The set of types that have been cleaned up.
  ModelTypeSet cleaned_types_;
  // The set of types that have been downloaded.
  ModelTypeSet downloaded_types_;
  // The set of types that have been enabled.
  ModelTypeSet enabled_types_;

  // Faked notifier state.
  SyncNotifierRegistrar registrar_;

  DISALLOW_COPY_AND_ASSIGN(FakeSyncManager);
};

}  // namespace syncer

#endif  // SYNC_INTERNAL_API_PUBLIC_TEST_FAKE_SYNC_MANAGER_H_
