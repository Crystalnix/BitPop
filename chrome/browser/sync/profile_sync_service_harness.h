// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_PROFILE_SYNC_SERVICE_HARNESS_H_
#define CHROME_BROWSER_SYNC_PROFILE_SYNC_SERVICE_HARNESS_H_
#pragma once

#include <string>
#include <vector>

#include "base/basictypes.h"
#include "chrome/browser/sync/profile_sync_service.h"
#include "chrome/browser/sync/profile_sync_service_observer.h"
#include "chrome/browser/sync/syncable/model_type.h"

class Profile;

namespace browser_sync {
  namespace sessions {
    struct SyncSessionSnapshot;
  }
}

// An instance of this class is basically our notion of a "sync client" for
// automation purposes. It harnesses the ProfileSyncService member of the
// profile passed to it on construction and automates certain things like setup
// and authentication. It provides ways to "wait" adequate periods of time for
// several clients to get to the same state.
class ProfileSyncServiceHarness : public ProfileSyncServiceObserver {
 public:
  ProfileSyncServiceHarness(Profile* profile,
                            const std::string& username,
                            const std::string& password,
                            int id);

  virtual ~ProfileSyncServiceHarness() {}

  // Creates a ProfileSyncServiceHarness object and attaches it to |profile|, a
  // profile that is assumed to have been signed into sync in the past. Caller
  // takes ownership.
  static ProfileSyncServiceHarness* CreateAndAttach(Profile* profile);

  // Sets the GAIA credentials with which to sign in to sync.
  void SetCredentials(const std::string& username, const std::string& password);

  // Returns true if sync has been enabled on |profile_|.
  bool IsSyncAlreadySetup();

  // Creates a ProfileSyncService for the profile passed at construction and
  // enables sync for all available datatypes. Returns true only after sync has
  // been fully initialized and authenticated, and we are ready to process
  // changes.
  bool SetupSync();

  // Same as the above method, but enables sync only for the datatypes contained
  // in |synced_datatypes|.
  bool SetupSync(const syncable::ModelTypeSet& synced_datatypes);

  // ProfileSyncServiceObserver implementation.
  virtual void OnStateChanged();

  // Blocks the caller until the sync backend host associated with this harness
  // has been initialized.  Returns true if the wait was successful.
  bool AwaitBackendInitialized();

  // Blocks the caller until the datatype manager is configured and sync has
  // been initialized (for example, after a browser restart).  Returns true if
  // the wait was successful.
  bool AwaitSyncRestart();

  // Blocks the caller until this harness has completed a single sync cycle
  // since the previous one.  Returns true if a sync cycle has completed.
  bool AwaitSyncCycleCompletion(const std::string& reason);

  // Blocks the caller until this harness has observed that the sync engine
  // has downloaded all the changes seen by the |partner| harness's client.
  bool WaitUntilTimestampMatches(
      ProfileSyncServiceHarness* partner, const std::string& reason);

  // Calling this acts as a barrier and blocks the caller until |this| and
  // |partner| have both completed a sync cycle.  When calling this method,
  // the |partner| should be the passive responder who responds to the actions
  // of |this|.  This method relies upon the synchronization of callbacks
  // from the message queue. Returns true if two sync cycles have completed.
  // Note: Use this method when exactly one client makes local change(s), and
  // exactly one client is waiting to receive those changes.
  bool AwaitMutualSyncCycleCompletion(ProfileSyncServiceHarness* partner);

  // Blocks the caller until |this| completes its ongoing sync cycle and every
  // other client in |partners| have achieved identical download progresses.
  // Note: Use this method when exactly one client makes local change(s),
  // and more than one client is waiting to receive those changes.
  bool AwaitGroupSyncCycleCompletion(
      std::vector<ProfileSyncServiceHarness*>& partners);

  // Blocks the caller until every client in |clients| completes its ongoing
  // sync cycle and all the clients' timestamps match.  Note: Use this method
  // when more than one client makes local change(s), and more than one client
  // is waiting to receive those changes.
  static bool AwaitQuiescence(
      std::vector<ProfileSyncServiceHarness*>& clients);

  // Blocks the caller until |service_| indicates that a passphrase is required.
  bool AwaitPassphraseRequired();

  // Blocks the caller until |service_| indicates that the passphrase set by
  // calling SetPassphrase has been accepted.
  bool AwaitPassphraseAccepted();

  // Returns the ProfileSyncService member of the the sync client.
  ProfileSyncService* service() { return service_; }

  // Returns the status of the ProfileSyncService member of the the sync client.
  ProfileSyncService::Status GetStatus();

  // See ProfileSyncService::ShouldPushChanges().
  bool ServiceIsPushingChanges() { return service_->ShouldPushChanges(); }

  // Enables sync for a particular sync datatype. Returns true on success.
  bool EnableSyncForDatatype(syncable::ModelType datatype);

  // Disables sync for a particular sync datatype. Returns true on success.
  bool DisableSyncForDatatype(syncable::ModelType datatype);

  // Enables sync for all sync datatypes. Returns true on success.
  bool EnableSyncForAllDatatypes();

  // Disables sync for all sync datatypes. Returns true on success.
  bool DisableSyncForAllDatatypes();

  // Returns a snapshot of the current sync session.
  const browser_sync::sessions::SyncSessionSnapshot*
      GetLastSessionSnapshot() const;

  // Encrypt the datatype |type|. This method will block while the sync backend
  // host performs the encryption or a timeout is reached.
  // PostCondition:
  //   returns: True if |type| was encrypted and we are fully synced.
  //            False if we timed out.
  bool EnableEncryptionForType(syncable::ModelType type);

  // Wait until |type| is encrypted or we time out.
  // PostCondition:
  //   returns: True if |type| is currently encrypted and we are fully synced.
  //            False if we timed out.
  bool WaitForTypeEncryption(syncable::ModelType type);

  // Check if |type| is encrypted.
  bool IsTypeEncrypted(syncable::ModelType type);

 private:
  friend class StateChangeTimeoutEvent;

  enum WaitState {
    // The sync client has just been initialized.
    INITIAL_WAIT_STATE = 0,

    // The sync client awaits the OnBackendInitialized() callback.
    WAITING_FOR_ON_BACKEND_INITIALIZED,

    // The sync client is waiting for the first sync cycle to complete.
    WAITING_FOR_INITIAL_SYNC,

    // The sync client is waiting for an ongoing sync cycle to complete.
    WAITING_FOR_SYNC_TO_FINISH,

    // The sync client anticipates incoming updates leading to a new sync cycle.
    WAITING_FOR_UPDATES,

    // The sync client is waiting for a passphrase to be required by the
    // cryptographer.
    WAITING_FOR_PASSPHRASE_REQUIRED,

    // The sync client is waiting for its passphrase to be accepted by the
    // cryptographer.
    WAITING_FOR_PASSPHRASE_ACCEPTED,

    // The sync client anticipates encryption of new datatypes.
    WAITING_FOR_ENCRYPTION,

    // The sync client is waiting for the datatype manager to be configured and
    // for sync to be fully initialized. Used after a browser restart, where a
    // full sync cycle is not expected to occur.
    WAITING_FOR_SYNC_CONFIGURATION,

    // The sync client needs a passphrase in order to decrypt data.
    SET_PASSPHRASE_FAILED,

    // The sync client cannot reach the server.
    SERVER_UNREACHABLE,

    // The sync client is fully synced and there are no pending updates.
    FULLY_SYNCED,

    // Syncing is disabled for the client.
    SYNC_DISABLED,

    NUMBER_OF_STATES,
  };

  // Called from the observer when the current wait state has been completed.
  void SignalStateCompleteWithNextState(WaitState next_state);

  // Indicates that the operation being waited on is complete.
  void SignalStateComplete();

  // Finite state machine for controlling state.  Returns true only if a state
  // change has taken place.
  bool RunStateChangeMachine();

  // Returns true if a status change took place, false on timeout.
  bool AwaitStatusChangeWithTimeout(int timeout_milliseconds,
                                    const std::string& reason);

  // Returns true if the sync client has no unsynced items.
  bool IsSynced();

  // Returns true if this client has downloaded all the items that the
  // other client has.
  bool MatchesOtherClient(ProfileSyncServiceHarness* partner);

  // Logs message with relevant info about client's sync state (if available).
  // |log_level| denotes the VLOG level.
  void LogClientInfo(const std::string& message, int log_level);

  // Gets the current progress indicator of the current sync session
  // for a particular datatype.
  std::string GetUpdatedTimestamp(syncable::ModelType model_type);

  // Gets detailed status from |service_| in pretty-printable form.
  std::string GetServiceStatus();

  // When in WAITING_FOR_ENCRYPTION state, we check to see if this type is now
  // encrypted to determine if we're done.
  syncable::ModelType waiting_for_encryption_type_;

  // The WaitState in which the sync client currently is. Helps determine what
  // action to take when RunStateChangeMachine() is called.
  WaitState wait_state_;

  // Sync profile associated with this sync client.
  Profile* profile_;

  // ProfileSyncService object associated with |profile_|.
  ProfileSyncService* service_;

  // The harness of the client whose update progress marker we're expecting
  // eventually match.
  ProfileSyncServiceHarness* timestamp_match_partner_;

  // Credentials used for GAIA authentication.
  std::string username_;
  std::string password_;

  // Client ID, used for logging purposes.
  int id_;

  DISALLOW_COPY_AND_ASSIGN(ProfileSyncServiceHarness);
};

#endif  // CHROME_BROWSER_SYNC_PROFILE_SYNC_SERVICE_HARNESS_H_
