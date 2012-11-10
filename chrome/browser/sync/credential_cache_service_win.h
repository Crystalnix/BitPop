// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_CREDENTIAL_CACHE_SERVICE_WIN_H_
#define CHROME_BROWSER_SYNC_CREDENTIAL_CACHE_SERVICE_WIN_H_

#include <string>

#include "base/basictypes.h"
#include "base/cancelable_callback.h"
#include "base/file_path.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/prefs/pref_change_registrar.h"
#include "chrome/browser/profiles/profile_keyed_service.h"
#include "chrome/browser/sync/sync_prefs.h"
#include "chrome/common/json_pref_store.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"

namespace base {
class StringValue;
class Value;
}

class Profile;

namespace syncer {

// On Windows 8, Chrome must maintain separate profile directories for Metro and
// Desktop modes. When the user signs in to sync in one of the modes, we would
// like to automatically start sync in the other mode.
//
// This class implements a caching service for sync credentials. It listens for
// updates to the PrefService and TokenService that pertain to the user
// signing in and out of sync, and persists the credentials to a separate file
// in the default profile directory. It also contains functionality to bootstrap
// sync using credentials that were cached due to signing in on the other
// (alternate) mode.
class CredentialCacheService : public ProfileKeyedService,
                               public content::NotificationObserver,
                               public PrefStore::Observer {
 public:
  explicit CredentialCacheService(Profile* profile);
  virtual ~CredentialCacheService();

  // ProfileKeyedService implementation.
  virtual void Shutdown() OVERRIDE;

  // PrefStore::Observer implementation.
  virtual void OnInitializationCompleted(bool succeeded) OVERRIDE;
  virtual void OnPrefValueChanged(const std::string& key) OVERRIDE;

  // content::NotificationObserver implementation.
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

 protected:
  // Returns true if the credential cache represented by |store| contains a
  // value for |pref_name|.
  bool HasPref(scoped_refptr<JsonPrefStore> store,
               const std::string& pref_name);

  // Encrypts and base 64 encodes |credential|, converts the result to a
  // StringValue, and returns the result. Caller owns the StringValue returned.
  static base::StringValue* PackCredential(const std::string& credential);

  // Extracts a string from the Value |packed|, base 64 decodes and decrypts it,
  // and returns the result.
  static std::string UnpackCredential(const base::Value& packed);

  // Writes the timestamp at which the last update was made to the credential
  // cache of the local profile. Used to make sure that we only copy credentials
  // from a more recently updated cache to an older cache.
  void WriteLastUpdatedTime();

  // Updates the value of |pref_name| to |new_value|, unless the user has signed
  // out, in which case we write an empty string value to |pref_name|.
  void PackAndUpdateStringPref(const std::string& pref_name,
                               const std::string& new_value);

  // Updates the value of |pref_name| to |new_value|, unless the user has signed
  // out, in which case we write "false" to |pref_name|.
  void UpdateBooleanPref(const std::string& pref_name, bool new_value);

  // Returns the time at which the credential cache represented by |store| was
  // last updated. Used to make sure that we only copy credentials from a more
  // recently updated cache to an older cache.
  int64 GetLastUpdatedTime(scoped_refptr<JsonPrefStore> store);

  // Returns the string pref value contained in |store| for |pref_name|. Assumes
  // that |store| contains a value for |pref_name|.
  std::string GetAndUnpackStringPref(scoped_refptr<JsonPrefStore> store,
                                     const std::string& pref_name);

  // Returns the boolean pref value contained in |store| for |pref_name|.
  // Assumes that |store| contains a value for |pref_name|.
  bool GetBooleanPref(scoped_refptr<JsonPrefStore> store,
                      const std::string& pref_name);

  // Getter for unit tests.
  const scoped_refptr<JsonPrefStore>& local_store() const {
    return local_store_;
  }

  // Setter for unit tests
  void set_local_store(JsonPrefStore* new_local_store) {
    local_store_ = new_local_store;
  }

 private:
  // Returns the path to the sync credentials file in the current profile
  // directory.
  FilePath GetCredentialPathInCurrentProfile() const;

  // Returns the path to the sync credentials file in the default Desktop
  // profile directory if we are running in Metro mode, and vice versa.
  FilePath GetCredentialPathInAlternateProfile() const;

  // Determines if the local credential cache writer should be initialized,
  // based on the OS version and relevant sync preferences. Returns true if the
  // writer must be initialized, and false if not.
  bool ShouldInitializeLocalCredentialCacheWriter() const;

  // Determines if we must look for credentials in the alternate profile, based
  // on relevant sync preferences, in addition the to conditions in
  // ShouldInitializeLocalCredentialCacheWriter(). Returns true if we must look
  // for cached credentials, and false if not.
  bool ShouldLookForCachedCredentialsInAlternateProfile() const;

  // Initializes the JsonPrefStore object for the local profile directory.
  void InitializeLocalCredentialCacheWriter();

  // Initializes the JsonPrefStore object for the alternate profile directory
  // if |should_initialize| is true. We take a bool* instead of a bool since
  // this is a callback, and base::Owned needs to clean up the flag.
  void InitializeAlternateCredentialCacheReader(bool* should_initialize);

  // Returns true if there is an empty value for kGoogleServicesUsername in the
  // credential cache for the local profile (indicating that the user first
  // signed in and then signed out). Returns false if there's no value at all
  // (indicating that the user has never signed in) or if there's a non-empty
  // value (indicating that the user is currently signed in).
  bool HasUserSignedOut();

  // Asynchronously looks for a cached credential file in the alternate profile
  // and initiates start up using cached credentials if the file was found.
  // Called by ProfileSyncService when it tries to start up on Windows 8 and
  // cannot auto-start.
  void LookForCachedCredentialsInAlternateProfile();

  // Loads cached sync credentials from the alternate profile and calls
  // ApplyCachedCredentials if the load was successful.
  void ReadCachedCredentialsFromAlternateProfile();

  // Initiates sync sign in using credentials read from the alternate profile by
  // persisting |google_services_username|, |encryption_bootstrap_token|,
  // |keep_everything_synced| and |preferred_types| to the local pref store, and
  // preparing ProfileSyncService for sign in.
  void InitiateSignInWithCachedCredentials(
      const std::string& google_services_username,
      const std::string& encryption_bootstrap_token,
      bool keep_everything_synced,
      ModelTypeSet preferred_types);

  // Updates the TokenService credentials with |lsid| and |sid| and triggers the
  // minting of new tokens for all Chrome services. ProfileSyncService is
  // automatically notified when tokens are minted, and goes on to consume the
  // updated credentials.
  void UpdateTokenServiceCredentials(const std::string& lsid,
                                     const std::string& sid);

  // Initiates a sign out of sync. Called when we notice that the user has
  // signed out from the alternate mode by reading its credential cache.
  void InitiateSignOut();

  // Compares the sync preferences in the local profile with values that were
  // read from the alternate profile -- |keep_everything_synced| and
  // |preferred_types|. Returns true if the prefs have changed, and false
  // otherwise.
  bool HaveSyncPrefsChanged(bool keep_everything_synced,
                            ModelTypeSet preferred_types) const;

  // Compares the token service credentials in the local profile with values
  // that were read from the alternate profile -- |lsid| and |sid|. Returns true
  // if the credentials have changed, and false otherwise.
  bool HaveTokenServiceCredentialsChanged(const std::string& lsid,
                                          const std::string& sid);

  // Determines if the user must be signed out of the local profile or not.
  // Called when updated settings are noticed in the alternate credential cache
  // for |google_services_username|. Returns true if we should sign out, and
  // false if not.
  bool ShouldSignOutOfSync(const std::string& google_services_username);

  // Determines if sync settings may be reconfigured or not. Called when
  // updated settings are noticed in the alternate credential cache for
  // |google_services_username|. Returns true if we may reconfigure, and false
  // if not.
  bool MayReconfigureSync(const std::string& google_services_username);

  // Determines if the user must be signed in to the local profile or not.
  // Called when updated settings are noticed in the alternate credential cache
  // for |google_services_username|, with new values for |lsid|, |sid| and
  // |encryption_bootstrap_token|. Returns true if we should sign in, and
  // false if not.
  bool ShouldSignInToSync(const std::string& google_services_username,
                          const std::string& lsid,
                          const std::string& sid,
                          const std::string& encryption_bootstrap_token);

  // Resets |alternate_store_| and schedules the next read from the alternate
  // credential cache.
  void ScheduleNextReadFromAlternateCredentialCache();

  // Profile for which credentials are being cached.
  Profile* profile_;

  // Used to access sync specific preferences in the PrefStore of |profile_|.
  browser_sync::SyncPrefs sync_prefs_;

  // Used for write operations to the credential cache file in the local profile
  // directory. This is separate from the chrome pref store. Protected so that
  // it can be accessed by unit tests.
  scoped_refptr<JsonPrefStore> local_store_;

  // Used for read operations on the credential cache file in the alternate
  // profile directory. This is separate from the chrome pref store.
  scoped_refptr<JsonPrefStore> alternate_store_;

  // Registrar for notifications from the PrefService.
  PrefChangeRegistrar pref_registrar_;

  // Registrar for notifications from the TokenService.
  content::NotificationRegistrar registrar_;

  // WeakPtr implementation.
  base::WeakPtrFactory<CredentialCacheService> weak_factory_;

  // Used to make sure that there is always at most one future read scheduled
  // on the alternate credential cache.
  base::CancelableClosure next_read_;

  DISALLOW_COPY_AND_ASSIGN(CredentialCacheService);
};

}  // namespace syncer

#endif  // CHROME_BROWSER_SYNC_CREDENTIAL_CACHE_SERVICE_WIN_H_
