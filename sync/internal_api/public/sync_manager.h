// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYNC_INTERNAL_API_PUBLIC_SYNC_MANAGER_H_
#define SYNC_INTERNAL_API_PUBLIC_SYNC_MANAGER_H_

#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/callback_forward.h"
#include "base/file_path.h"
#include "base/memory/ref_counted.h"
#include "base/task_runner.h"
#include "base/threading/thread_checker.h"
#include "base/time.h"
#include "sync/internal_api/public/base/model_type.h"
#include "sync/internal_api/public/change_record.h"
#include "sync/internal_api/public/configure_reason.h"
#include "sync/internal_api/public/engine/model_safe_worker.h"
#include "sync/internal_api/public/engine/sync_status.h"
#include "sync/internal_api/public/util/report_unrecoverable_error_function.h"
#include "sync/internal_api/public/util/weak_handle.h"
#include "sync/notifier/invalidation_util.h"
#include "sync/protocol/sync_protocol_error.h"

namespace sync_pb {
class EncryptedData;
}  // namespace sync_pb

namespace syncer {

class BaseTransaction;
class Encryptor;
struct Experiments;
class ExtensionsActivityMonitor;
class HttpPostProviderFactory;
class InternalComponentsFactory;
class JsBackend;
class JsEventHandler;
class SyncNotifier;
class SyncNotifierObserver;
class SyncScheduler;
class UnrecoverableErrorHandler;
struct UserShare;

namespace sessions {
class SyncSessionSnapshot;
}  // namespace sessions

// Used by SyncManager::OnConnectionStatusChange().
enum ConnectionStatus {
  CONNECTION_OK,
  CONNECTION_AUTH_ERROR,
  CONNECTION_SERVER_ERROR
};

// Reasons due to which Cryptographer might require a passphrase.
enum PassphraseRequiredReason {
  REASON_PASSPHRASE_NOT_REQUIRED = 0,  // Initial value.
  REASON_ENCRYPTION = 1,               // The cryptographer requires a
                                       // passphrase for its first attempt at
                                       // encryption. Happens only during
                                       // migration or upgrade.
  REASON_DECRYPTION = 2,               // The cryptographer requires a
                                       // passphrase for its first attempt at
                                       // decryption.
};


// Contains everything needed to talk to and identify a user account.
struct SyncCredentials {
  std::string email;
  std::string sync_token;
};

// SyncManager encapsulates syncable::Directory and serves as the parent of all
// other objects in the sync API.  If multiple threads interact with the same
// local sync repository (i.e. the same sqlite database), they should share a
// single SyncManager instance.  The caller should typically create one
// SyncManager for the lifetime of a user session.
//
// Unless stated otherwise, all methods of SyncManager should be called on the
// same thread.
class SyncManager {
 public:
  // An interface the embedding application implements to be notified
  // on change events.  Note that these methods may be called on *any*
  // thread.
  class ChangeDelegate {
   public:
    // Notify the delegate that changes have been applied to the sync model.
    //
    // This will be invoked on the same thread as on which ApplyChanges was
    // called. |changes| is an array of size |change_count|, and contains the
    // ID of each individual item that was changed. |changes| exists only for
    // the duration of the call. If items of multiple data types change at
    // the same time, this method is invoked once per data type and |changes|
    // is restricted to items of the ModelType indicated by |model_type|.
    // Because the observer is passed a |trans|, the observer can assume a
    // read lock on the sync model that will be released after the function
    // returns.
    //
    // The SyncManager constructs |changes| in the following guaranteed order:
    //
    // 1. Deletions, from leaves up to parents.
    // 2. Updates to existing items with synced parents & predecessors.
    // 3. New items with synced parents & predecessors.
    // 4. Items with parents & predecessors in |changes|.
    // 5. Repeat #4 until all items are in |changes|.
    //
    // Thus, an implementation of OnChangesApplied should be able to
    // process the change records in the order without having to worry about
    // forward dependencies.  But since deletions come before reparent
    // operations, a delete may temporarily orphan a node that is
    // updated later in the list.
    virtual void OnChangesApplied(
        ModelType model_type,
        const BaseTransaction* trans,
        const ImmutableChangeRecordList& changes) = 0;

    // OnChangesComplete gets called when the TransactionComplete event is
    // posted (after OnChangesApplied finishes), after the transaction lock
    // and the change channel mutex are released.
    //
    // The purpose of this function is to support processors that require
    // split-transactions changes. For example, if a model processor wants to
    // perform blocking I/O due to a change, it should calculate the changes
    // while holding the transaction lock (from within OnChangesApplied), buffer
    // those changes, let the transaction fall out of scope, and then commit
    // those changes from within OnChangesComplete (postponing the blocking
    // I/O to when it no longer holds any lock).
    virtual void OnChangesComplete(ModelType model_type) = 0;

   protected:
    virtual ~ChangeDelegate();
  };

  // Like ChangeDelegate, except called only on the sync thread and
  // not while a transaction is held.  For objects that want to know
  // when changes happen, but don't need to process them.
  class ChangeObserver {
   public:
    // Ids referred to in |changes| may or may not be in the write
    // transaction specified by |write_transaction_id|.  If they're
    // not, that means that the node didn't actually change, but we
    // marked them as changed for some other reason (e.g., siblings of
    // re-ordered nodes).
    //
    // TODO(sync, long-term): Ideally, ChangeDelegate/Observer would
    // be passed a transformed version of EntryKernelMutation instead
    // of a transaction that would have to be used to look up the
    // changed nodes.  That is, ChangeDelegate::OnChangesApplied()
    // would still be called under the transaction, but all the needed
    // data will be passed down.
    //
    // Even more ideally, we would have sync semantics such that we'd
    // be able to apply changes without being under a transaction.
    // But that's a ways off...
    virtual void OnChangesApplied(
        ModelType model_type,
        int64 write_transaction_id,
        const ImmutableChangeRecordList& changes) = 0;

    virtual void OnChangesComplete(ModelType model_type) = 0;

   protected:
    virtual ~ChangeObserver();
  };

  // An interface the embedding application implements to receive
  // notifications from the SyncManager.  Register an observer via
  // SyncManager::AddObserver.  All methods are called only on the
  // sync thread.
  class Observer {
   public:
    // A round-trip sync-cycle took place and the syncer has resolved any
    // conflicts that may have arisen.
    virtual void OnSyncCycleCompleted(
        const sessions::SyncSessionSnapshot& snapshot) = 0;

    // Called when the status of the connection to the sync server has
    // changed.
    virtual void OnConnectionStatusChange(ConnectionStatus status) = 0;

    // Called when a new auth token is provided by the sync server.
    virtual void OnUpdatedToken(const std::string& token) = 0;

    // Called when user interaction is required to obtain a valid passphrase.
    // - If the passphrase is required for encryption, |reason| will be
    //   REASON_ENCRYPTION.
    // - If the passphrase is required for the decryption of data that has
    //   already been encrypted, |reason| will be REASON_DECRYPTION.
    // - If the passphrase is required because decryption failed, and a new
    //   passphrase is required, |reason| will be REASON_SET_PASSPHRASE_FAILED.
    //
    // |pending_keys| is a copy of the cryptographer's pending keys, that may be
    // cached by the frontend for subsequent use by the UI.
    virtual void OnPassphraseRequired(
        PassphraseRequiredReason reason,
        const sync_pb::EncryptedData& pending_keys) = 0;

    // Called when the passphrase provided by the user has been accepted and is
    // now used to encrypt sync data.
    virtual void OnPassphraseAccepted() = 0;

    // |bootstrap_token| is an opaque base64 encoded representation of the key
    // generated by the current passphrase, and is provided to the observer for
    // persistence purposes and use in a future initialization of sync (e.g.
    // after restart). The boostrap token will always be derived from the most
    // recent GAIA password (for accounts with implicit passphrases), even if
    // the data is still encrypted with an older GAIA password. For accounts
    // with explicit passphrases, it will be the most recently seen custom
    // passphrase.
    virtual void OnBootstrapTokenUpdated(
        const std::string& bootstrap_token) = 0;

    // Called when initialization is complete to the point that SyncManager can
    // process changes. This does not necessarily mean authentication succeeded
    // or that the SyncManager is online.
    // IMPORTANT: Creating any type of transaction before receiving this
    // notification is illegal!
    // WARNING: Calling methods on the SyncManager before receiving this
    // message, unless otherwise specified, produces undefined behavior.
    //
    // |js_backend| is what about:sync interacts with.  It can emit
    // the following events:

    /**
     * @param {{ enabled: boolean }} details A dictionary containing:
     *     - enabled: whether or not notifications are enabled.
     */
    // function onNotificationStateChange(details);

    /**
     * @param {{ changedTypes: Array.<string> }} details A dictionary
     *     containing:
     *     - changedTypes: a list of types (as strings) for which there
             are new updates.
     */
    // function onIncomingNotification(details);

    // Also, it responds to the following messages (all other messages
    // are ignored):

    /**
     * Gets the current notification state.
     *
     * @param {function(boolean)} callback Called with whether or not
     *     notifications are enabled.
     */
    // function getNotificationState(callback);

    /**
     * Gets details about the root node.
     *
     * @param {function(!Object)} callback Called with details about the
     *     root node.
     */
    // TODO(akalin): Change this to getRootNodeId or eliminate it
    // entirely.
    // function getRootNodeDetails(callback);

    /**
     * Gets summary information for a list of ids.
     *
     * @param {Array.<string>} idList List of 64-bit ids in decimal
     *     string form.
     * @param {Array.<{id: string, title: string, isFolder: boolean}>}
     * callback Called with summaries for the nodes in idList that
     *     exist.
     */
    // function getNodeSummariesById(idList, callback);

    /**
     * Gets detailed information for a list of ids.
     *
     * @param {Array.<string>} idList List of 64-bit ids in decimal
     *     string form.
     * @param {Array.<!Object>} callback Called with detailed
     *     information for the nodes in idList that exist.
     */
    // function getNodeDetailsById(idList, callback);

    /**
     * Gets child ids for a given id.
     *
     * @param {string} id 64-bit id in decimal string form of the parent
     *     node.
     * @param {Array.<string>} callback Called with the (possibly empty)
     *     list of child ids.
     */
    // function getChildNodeIds(id);

    virtual void OnInitializationComplete(
        const WeakHandle<syncer::JsBackend>& js_backend,
        bool success,
        syncer::ModelTypeSet restored_types) = 0;

    // We are no longer permitted to communicate with the server. Sync should
    // be disabled and state cleaned up at once.  This can happen for a number
    // of reasons, e.g. swapping from a test instance to production, or a
    // global stop syncing operation has wiped the store.
    virtual void OnStopSyncingPermanently() = 0;

    // Called when the set of encrypted types or the encrypt
    // everything flag has been changed.  Note that encryption isn't
    // complete until the OnEncryptionComplete() notification has been
    // sent (see below).
    //
    // |encrypted_types| will always be a superset of
    // Cryptographer::SensitiveTypes().  If |encrypt_everything| is
    // true, |encrypted_types| will be the set of all known types.
    //
    // Until this function is called, observers can assume that the
    // set of encrypted types is Cryptographer::SensitiveTypes() and
    // that the encrypt everything flag is false.
    //
    // Called from within a transaction.
    virtual void OnEncryptedTypesChanged(
        ModelTypeSet encrypted_types,
        bool encrypt_everything) = 0;

    // Called after we finish encrypting the current set of encrypted
    // types.
    //
    // Called from within a transaction.
    virtual void OnEncryptionComplete() = 0;

    virtual void OnActionableError(
        const SyncProtocolError& sync_protocol_error) = 0;

   protected:
    virtual ~Observer();
  };

  SyncManager();
  virtual ~SyncManager();

  // Initialize the sync manager.  |database_location| specifies the path of
  // the directory in which to locate a sqlite repository storing the syncer
  // backend state. Initialization will open the database, or create it if it
  // does not already exist. Returns false on failure.
  // |event_handler| is the JsEventHandler used to propagate events to
  // chrome://sync-internals.  |event_handler| may be uninitialized.
  // |sync_server_and_path| and |sync_server_port| represent the Chrome sync
  // server to use, and |use_ssl| specifies whether to communicate securely;
  // the default is false.
  // |blocking_task_runner| is a TaskRunner to be used for tasks that
  // may block on disk I/O.
  // |post_factory| will be owned internally and used to create
  // instances of an HttpPostProvider.
  // |model_safe_worker| ownership is given to the SyncManager.
  // |user_agent| is a 7-bit ASCII string suitable for use as the User-Agent
  // HTTP header. Used internally when collecting stats to classify clients.
  // |sync_notifier| is owned and used to listen for notifications.
  // |restored_key_for_bootstrapping| is the key used to boostrap the
  // cryptographer
  // |keystore_encryption_enabled| determines whether we enable the keystore
  // encryption functionality in the cryptographer/nigori.
  // |report_unrecoverable_error_function| may be NULL.
  //
  // TODO(akalin): Replace the |post_factory| parameter with a
  // URLFetcher parameter.
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
      ReportUnrecoverableErrorFunction report_unrecoverable_error_function) = 0;

  // Throw an unrecoverable error from a transaction (mostly used for
  // testing).
  virtual void ThrowUnrecoverableError() = 0;

  virtual ModelTypeSet InitialSyncEndedTypes() = 0;

  // Returns those types within |types| that have an empty progress marker
  // token.
  virtual ModelTypeSet GetTypesWithEmptyProgressMarkerToken(
      ModelTypeSet types) = 0;

  // Purge from the directory those types with non-empty progress markers
  // but without initial synced ended set.
  // Returns false if an error occurred, true otherwise.
  virtual bool PurgePartiallySyncedTypes() = 0;

  // Update tokens that we're using in Sync. Email must stay the same.
  virtual void UpdateCredentials(const SyncCredentials& credentials) = 0;

  // Called when the user disables or enables a sync type.
  virtual void UpdateEnabledTypes(
      const ModelTypeSet& enabled_types) = 0;

  // Forwards to the underlying notifier (see comments in sync_notifier.h).
  virtual void RegisterInvalidationHandler(
      SyncNotifierObserver* handler) = 0;

  // Forwards to the underlying notifier (see comments in sync_notifier.h).
  virtual void UpdateRegisteredInvalidationIds(
      SyncNotifierObserver* handler,
      const ObjectIdSet& ids) = 0;

  // Forwards to the underlying notifier (see comments in sync_notifier.h).
  virtual void UnregisterInvalidationHandler(
      SyncNotifierObserver* handler) = 0;

  // Put the syncer in normal mode ready to perform nudges and polls.
  virtual void StartSyncingNormally(
      const ModelSafeRoutingInfo& routing_info) = 0;

  // Attempts to re-encrypt encrypted data types using the passphrase provided.
  // Notifies observers of the result of the operation via OnPassphraseAccepted
  // or OnPassphraseRequired, updates the nigori node, and does re-encryption as
  // appropriate. If an explicit password has been set previously, we drop
  // subsequent requests to set a passphrase. If the cryptographer has pending
  // keys, and a new implicit passphrase is provided, we try decrypting the
  // pending keys with it, and if that fails, we cache the passphrase for
  // re-encryption once the pending keys are decrypted.
  virtual void SetEncryptionPassphrase(const std::string& passphrase,
                                       bool is_explicit) = 0;

  // Provides a passphrase for decrypting the user's existing sync data.
  // Notifies observers of the result of the operation via OnPassphraseAccepted
  // or OnPassphraseRequired, updates the nigori node, and does re-encryption as
  // appropriate if there is a previously cached encryption passphrase. It is an
  // error to call this when we don't have pending keys.
  virtual void SetDecryptionPassphrase(const std::string& passphrase) = 0;

  // Switches the mode of operation to CONFIGURATION_MODE and performs
  // any configuration tasks needed as determined by the params. Once complete,
  // syncer will remain in CONFIGURATION_MODE until StartSyncingNormally is
  // called.
  // |ready_task| is invoked when the configuration completes.
  // |retry_task| is invoked if the configuration job could not immediately
  //              execute. |ready_task| will still be called when it eventually
  //              does finish.
  virtual void ConfigureSyncer(
      ConfigureReason reason,
      const ModelTypeSet& types_to_config,
      const ModelSafeRoutingInfo& new_routing_info,
      const base::Closure& ready_task,
      const base::Closure& retry_task) = 0;

  // Adds a listener to be notified of sync events.
  // NOTE: It is OK (in fact, it's probably a good idea) to call this before
  // having received OnInitializationCompleted.
  virtual void AddObserver(Observer* observer) = 0;

  // Remove the given observer.  Make sure to call this if the
  // Observer is being destroyed so the SyncManager doesn't
  // potentially dereference garbage.
  virtual void RemoveObserver(Observer* observer) = 0;

  // Status-related getter.  May be called on any thread.
  virtual SyncStatus GetDetailedStatus() const = 0;

  // Whether or not the Nigori node is encrypted using an explicit passphrase.
  // May be called on any thread.
  virtual bool IsUsingExplicitPassphrase() = 0;

  // Extracts the keystore encryption bootstrap token if a keystore key existed.
  // Returns true if bootstrap token successfully extracted, false otherwise.
  virtual bool GetKeystoreKeyBootstrapToken(std::string* token) = 0;

  // Call periodically from a database-safe thread to persist recent changes
  // to the syncapi model.
  virtual void SaveChanges() = 0;

  // Initiates shutdown of various components in the sync engine.  Must be
  // called from the main thread to allow preempting ongoing tasks on the sync
  // loop (that may be blocked on I/O).  The semantics of |callback| are the
  // same as with StartConfigurationMode. If provided and a scheduler / sync
  // loop exists, it will be invoked from the sync loop by the scheduler to
  // notify that all work has been flushed + cancelled, and it is idle.
  // If no scheduler exists, the callback is run immediately (from the loop
  // this was created on, which is the sync loop), as sync is effectively
  // stopped.
  virtual void StopSyncingForShutdown(const base::Closure& callback) = 0;

  // Issue a final SaveChanges, and close sqlite handles.
  virtual void ShutdownOnSyncThread() = 0;

  // May be called from any thread.
  virtual UserShare* GetUserShare() = 0;

  // Inform the cryptographer of the most recent passphrase and set of
  // encrypted types (from nigori node), then ensure all data that
  // needs encryption is encrypted with the appropriate passphrase.
  //
  // May trigger OnPassphraseRequired().  Otherwise, it will trigger
  // OnEncryptedTypesChanged() if necessary (see comments for
  // OnEncryptedTypesChanged()), and then OnEncryptionComplete().
  //
  // Also updates or adds device information to the nigori node.
  //
  // Note: opens a transaction, so must only be called after syncapi
  // has been initialized.
  virtual void RefreshNigori(const std::string& chrome_version,
                             const base::Closure& done_callback) = 0;

  // Enable encryption of all sync data. Once enabled, it can never be
  // disabled without clearing the server data.
  //
  // This will trigger OnEncryptedTypesChanged() if necessary (see
  // comments for OnEncryptedTypesChanged()).  It then may trigger
  // OnPassphraseRequired(), but otherwise it will trigger
  // OnEncryptionComplete().
  virtual void EnableEncryptEverything() = 0;

  // Reads the nigori node to determine if any experimental features should
  // be enabled.
  // Note: opens a transaction.  May be called on any thread.
  virtual bool ReceivedExperiment(Experiments* experiments) = 0;

  // Uses a read-only transaction to determine if the directory being synced has
  // any remaining unsynced items.  May be called on any thread.
  virtual bool HasUnsyncedItems() = 0;
};

}  // namespace syncer

#endif  // SYNC_INTERNAL_API_PUBLIC_SYNC_MANAGER_H_
