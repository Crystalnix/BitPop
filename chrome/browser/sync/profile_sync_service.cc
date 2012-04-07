// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/profile_sync_service.h"

#include <cstddef>
#include <map>
#include <set>
#include <utility>

#include "base/basictypes.h"
#include "base/bind.h"
#include "base/callback.h"
#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/message_loop.h"
#include "base/metrics/histogram.h"
#include "base/string16.h"
#include "base/stringprintf.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/about_flags.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/net/chrome_cookie_notification_details.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/signin/signin_manager.h"
#include "chrome/browser/signin/token_service.h"
#include "chrome/browser/sync/api/sync_error.h"
#include "chrome/browser/sync/backend_migrator.h"
#include "chrome/browser/sync/glue/change_processor.h"
#include "chrome/browser/sync/glue/data_type_controller.h"
#include "chrome/browser/sync/glue/session_data_type_controller.h"
#include "chrome/browser/sync/glue/session_model_associator.h"
#include "chrome/browser/sync/glue/typed_url_data_type_controller.h"
#include "chrome/browser/sync/internal_api/configure_reason.h"
#include "chrome/browser/sync/internal_api/sync_manager.h"
#include "chrome/browser/sync/js/js_arg_list.h"
#include "chrome/browser/sync/js/js_event_details.h"
#include "chrome/browser/sync/profile_sync_components_factory_impl.h"
#include "chrome/browser/sync/sync_global_error.h"
#include "chrome/browser/sync/util/cryptographer.h"
#include "chrome/browser/sync/util/oauth.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/global_error_service.h"
#include "chrome/browser/ui/global_error_service_factory.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/chrome_version_info.h"
#include "chrome/common/net/gaia/gaia_constants.h"
#include "chrome/common/time_format.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_source.h"
#include "grit/generated_resources.h"
#include "net/base/cookie_monster.h"
#include "ui/base/l10n/l10n_util.h"

using browser_sync::ChangeProcessor;
using browser_sync::DataTypeController;
using browser_sync::DataTypeManager;
using browser_sync::JsBackend;
using browser_sync::JsController;
using browser_sync::JsEventDetails;
using browser_sync::JsEventHandler;
using browser_sync::SyncBackendHost;
using browser_sync::SyncProtocolError;
using browser_sync::WeakHandle;
using sync_api::SyncCredentials;

typedef GoogleServiceAuthError AuthError;

const char* ProfileSyncService::kSyncServerUrl =
    "https://clients4.google.com/chrome-sync";

const char* ProfileSyncService::kDevServerUrl =
    "https://clients4.google.com/chrome-sync/dev";

static const int kSyncClearDataTimeoutInSeconds = 60;  // 1 minute.

static const char* kRelevantTokenServices[] = {
    GaiaConstants::kSyncService,
    GaiaConstants::kGaiaOAuth2LoginRefreshToken};
static const int kRelevantTokenServicesCount =
    arraysize(kRelevantTokenServices);

// Helper to check if the given token service is relevant for sync.
static bool IsTokenServiceRelevant(const std::string& service) {
  for (int i = 0; i < kRelevantTokenServicesCount; ++i) {
    if (service == kRelevantTokenServices[i])
      return true;
  }
  return false;
}

bool ShouldShowActionOnUI(
    const browser_sync::SyncProtocolError& error) {
  return (error.action != browser_sync::UNKNOWN_ACTION &&
          error.action != browser_sync::DISABLE_SYNC_ON_CLIENT);
}

ProfileSyncService::ProfileSyncService(ProfileSyncComponentsFactory* factory,
                                       Profile* profile,
                                       SigninManager* signin_manager,
                                       StartBehavior start_behavior)
    : last_auth_error_(AuthError::None()),
      passphrase_required_reason_(sync_api::REASON_PASSPHRASE_NOT_REQUIRED),
      factory_(factory),
      profile_(profile),
      // |profile| may be NULL in unit tests.
      sync_prefs_(profile_ ? profile_->GetPrefs() : NULL),
      sync_service_url_(kDevServerUrl),
      backend_initialized_(false),
      is_auth_in_progress_(false),
      wizard_(ALLOW_THIS_IN_INITIALIZER_LIST(this)),
      signin_(signin_manager),
      unrecoverable_error_detected_(false),
      weak_factory_(ALLOW_THIS_IN_INITIALIZER_LIST(this)),
      expect_sync_configuration_aborted_(false),
      clear_server_data_state_(CLEAR_NOT_STARTED),
      encrypted_types_(browser_sync::Cryptographer::SensitiveTypes()),
      encrypt_everything_(false),
      encryption_pending_(false),
      auto_start_enabled_(start_behavior == AUTO_START),
      failed_datatypes_handler_(ALLOW_THIS_IN_INITIALIZER_LIST(this)),
      configure_status_(DataTypeManager::UNKNOWN) {
  // By default, dev, canary, and unbranded Chromium users will go to the
  // development servers. Development servers have more features than standard
  // sync servers. Users with officially-branded Chrome stable and beta builds
  // will go to the standard sync servers.
  //
  // GetChannel hits the registry on Windows. See http://crbug.com/70380.
  base::ThreadRestrictions::ScopedAllowIO allow_io;
  chrome::VersionInfo::Channel channel = chrome::VersionInfo::GetChannel();
  if (channel == chrome::VersionInfo::CHANNEL_STABLE ||
      channel == chrome::VersionInfo::CHANNEL_BETA) {
    sync_service_url_ = GURL(kSyncServerUrl);
  }
}

ProfileSyncService::~ProfileSyncService() {
  sync_prefs_.RemoveSyncPrefObserver(this);
  Shutdown();
}

bool ProfileSyncService::AreCredentialsAvailable() {
  return AreCredentialsAvailable(false);
}

bool ProfileSyncService::AreCredentialsAvailable(
    bool check_oauth_login_token) {
  if (IsManaged()) {
    return false;
  }

  // CrOS user is always logged in. Chrome uses signin_ to check logged in.
  if (signin_->GetAuthenticatedUsername().empty())
    return false;

  TokenService* token_service = profile()->GetTokenService();
  if (!token_service)
    return false;

  // TODO(chron): Verify CrOS unit test behavior.
  if (!token_service->HasTokenForService(browser_sync::SyncServiceName()))
    return false;
  return !check_oauth_login_token || token_service->HasOAuthLoginToken();
}

void ProfileSyncService::Initialize() {
  InitSettings();

  // We clear this here (vs Shutdown) because we want to remember that an error
  // happened on shutdown so we can display details (message, location) about it
  // in about:sync.
  ClearStaleErrors();

  sync_prefs_.AddSyncPrefObserver(this);

  // For now, the only thing we can do through policy is to turn sync off.
  if (IsManaged()) {
    DisableForUser();
    return;
  }

  RegisterAuthNotifications();

  if (!HasSyncSetupCompleted())
    DisableForUser();  // Clean up in case of previous crash / setup abort.

  TryStart();
}

void ProfileSyncService::TryStart() {
  if (!sync_prefs_.IsStartSuppressed() && AreCredentialsAvailable()) {
    if (HasSyncSetupCompleted() || auto_start_enabled_) {
      // If sync setup has completed we always start the backend.
      // If autostart is enabled, but we haven't completed sync setup, we try to
      // start sync anyway, since it's possible we crashed/shutdown after
      // logging in but before the backend finished initializing the last time.
      // Note that if we haven't finished setting up sync, backend bring up will
      // be done by the wizard.
      StartUp();
    }
  }
}

void ProfileSyncService::RegisterAuthNotifications() {
  registrar_.Add(this,
                 chrome::NOTIFICATION_TOKEN_AVAILABLE,
                 content::Source<TokenService>(profile_->GetTokenService()));
  registrar_.Add(this,
                 chrome::NOTIFICATION_TOKEN_LOADING_FINISHED,
                 content::Source<TokenService>(profile_->GetTokenService()));
  registrar_.Add(this,
                 chrome::NOTIFICATION_TOKEN_REQUEST_FAILED,
                 content::Source<TokenService>(profile_->GetTokenService()));
  registrar_.Add(this,
                 chrome::NOTIFICATION_GOOGLE_SIGNIN_FAILED,
                 content::Source<Profile>(profile_));
  registrar_.Add(this,
                 chrome::NOTIFICATION_GOOGLE_SIGNIN_SUCCESSFUL,
                 content::Source<Profile>(profile_));
}

void ProfileSyncService::RegisterDataTypeController(
    DataTypeController* data_type_controller) {
  DCHECK_EQ(data_type_controllers_.count(data_type_controller->type()), 0U);
  data_type_controllers_[data_type_controller->type()] =
      data_type_controller;
}

browser_sync::SessionModelAssociator*
    ProfileSyncService::GetSessionModelAssociator() {
  if (data_type_controllers_.find(syncable::SESSIONS) ==
      data_type_controllers_.end() ||
      data_type_controllers_.find(syncable::SESSIONS)->second->state() !=
      DataTypeController::RUNNING) {
    return NULL;
  }
  return static_cast<browser_sync::SessionDataTypeController*>(
      data_type_controllers_.find(
      syncable::SESSIONS)->second.get())->GetModelAssociator();
}

void ProfileSyncService::ResetClearServerDataState() {
  clear_server_data_state_ = CLEAR_NOT_STARTED;
}

ProfileSyncService::ClearServerDataState
    ProfileSyncService::GetClearServerDataState() {
  return clear_server_data_state_;
}

void ProfileSyncService::GetDataTypeControllerStates(
  browser_sync::DataTypeController::StateMap* state_map) const {
    for (browser_sync::DataTypeController::TypeMap::const_iterator iter =
         data_type_controllers_.begin(); iter != data_type_controllers_.end();
         ++iter)
      (*state_map)[iter->first] = iter->second.get()->state();
}

void ProfileSyncService::InitSettings() {
  const CommandLine& command_line = *CommandLine::ForCurrentProcess();

  // Override the sync server URL from the command-line, if sync server
  // command-line argument exists.
  if (command_line.HasSwitch(switches::kSyncServiceURL)) {
    std::string value(command_line.GetSwitchValueASCII(
        switches::kSyncServiceURL));
    if (!value.empty()) {
      GURL custom_sync_url(value);
      if (custom_sync_url.is_valid()) {
        sync_service_url_ = custom_sync_url;
      } else {
        LOG(WARNING) << "The following sync URL specified at the command-line "
                     << "is invalid: " << value;
      }
    }
  }
}

SyncCredentials ProfileSyncService::GetCredentials() {
  SyncCredentials credentials;
  credentials.email = signin_->GetAuthenticatedUsername();
  DCHECK(!credentials.email.empty());
  TokenService* service = profile_->GetTokenService();
  credentials.sync_token = service->GetTokenForService(
      browser_sync::SyncServiceName());
  return credentials;
}

void ProfileSyncService::InitializeBackend(bool delete_stale_data) {
  if (!backend_.get()) {
    NOTREACHED();
    return;
  }

  syncable::ModelTypeSet initial_types;
  // If sync setup hasn't finished, we don't want to initialize routing info
  // for any data types so that we don't download updates for types that the
  // user chooses not to sync on the first DownloadUpdatesCommand.
  if (HasSyncSetupCompleted()) {
    initial_types = GetPreferredDataTypes();
  }

  SyncCredentials credentials = GetCredentials();

  scoped_refptr<net::URLRequestContextGetter> request_context_getter(
      profile_->GetRequestContext());

  if (delete_stale_data)
    ClearStaleErrors();

  backend_unrecoverable_error_handler_.reset(
    new browser_sync::BackendUnrecoverableErrorHandler(
        MakeWeakHandle(AsWeakPtr())));

  backend_->Initialize(
      this,
      MakeWeakHandle(sync_js_controller_.AsWeakPtr()),
      sync_service_url_,
      initial_types,
      credentials,
      delete_stale_data,
      backend_unrecoverable_error_handler_.get());
}

void ProfileSyncService::CreateBackend() {
  backend_.reset(
      new SyncBackendHost(profile_->GetDebugName(),
                          profile_, sync_prefs_.AsWeakPtr()));
}

bool ProfileSyncService::IsEncryptedDatatypeEnabled() const {
  if (encryption_pending())
    return true;
  const syncable::ModelTypeSet preferred_types = GetPreferredDataTypes();
  const syncable::ModelTypeSet encrypted_types = GetEncryptedDataTypes();
  DCHECK(encrypted_types.Has(syncable::PASSWORDS));
  return !Intersection(preferred_types, encrypted_types).Empty();
}

void ProfileSyncService::OnSyncConfigureDone(
    DataTypeManager::ConfigureResult result) {
  if (failed_datatypes_handler_.UpdateFailedDatatypes(result)) {
    ReconfigureDatatypeManager();
  }
}

void ProfileSyncService::OnSyncConfigureRetry() {
  // In platforms with auto start we would just wait for the
  // configure to finish. In other platforms we would throw
  // an unrecoverable error. The reason we do this is so that
  // the login dialog would show an error and the user would have
  // to relogin.
  // Also if backend has been initialized(the user is authenticated
  // and nigori is downloaded) we would simply wait rather than going into
  // unrecoverable error, even if the platform has auto start disabled.
  // Note: In those scenarios the UI does not wait for the configuration
  // to finish.
  if (!auto_start_enabled_ && !backend_initialized_) {
    OnUnrecoverableError(FROM_HERE,
                         "Configure failed to download.");
  }

  NotifyObservers();
}


void ProfileSyncService::StartUp() {
  // Don't start up multiple times.
  if (backend_.get()) {
    DVLOG(1) << "Skipping bringing up backend host.";
    return;
  }

  DCHECK(AreCredentialsAvailable());

  last_synced_time_ = sync_prefs_.GetLastSyncedTime();

  CreateBackend();

  // Initialize the backend.  Every time we start up a new SyncBackendHost,
  // we'll want to start from a fresh SyncDB, so delete any old one that might
  // be there.
  InitializeBackend(!HasSyncSetupCompleted());

  if (!sync_global_error_.get()) {
    sync_global_error_.reset(new SyncGlobalError(this));
    GlobalErrorServiceFactory::GetForProfile(profile_)->AddGlobalError(
        sync_global_error_.get());
    AddObserver(sync_global_error_.get());
  }
}

void ProfileSyncService::Shutdown() {
  ShutdownImpl(false);
}

void ProfileSyncService::ShutdownImpl(bool sync_disabled) {
  // First, we spin down the backend and wait for it to stop syncing completely
  // before we Stop the data type manager.  This is to avoid a late sync cycle
  // applying changes to the sync db that wouldn't get applied via
  // ChangeProcessors, leading to back-from-the-dead bugs.
  if (backend_.get())
    backend_->StopSyncingForShutdown();

  // Stop all data type controllers, if needed.  Note that until Stop
  // completes, it is possible in theory to have a ChangeProcessor apply a
  // change from a native model.  In that case, it will get applied to the sync
  // database (which doesn't get destroyed until we destroy the backend below)
  // as an unsynced change.  That will be persisted, and committed on restart.
  if (data_type_manager_.get()) {
    if (data_type_manager_->state() != DataTypeManager::STOPPED) {
      // When aborting as part of shutdown, we should expect an aborted sync
      // configure result, else we'll dcheck when we try to read the sync error.
      expect_sync_configuration_aborted_ = true;
      data_type_manager_->Stop();
    }

    registrar_.Remove(
        this,
        chrome::NOTIFICATION_SYNC_CONFIGURE_START,
        content::Source<DataTypeManager>(data_type_manager_.get()));
    registrar_.Remove(
        this,
        chrome::NOTIFICATION_SYNC_CONFIGURE_DONE,
        content::Source<DataTypeManager>(data_type_manager_.get()));
    data_type_manager_.reset();
  }

  // Shutdown the migrator before the backend to ensure it doesn't pull a null
  // snapshot.
  migrator_.reset();
  sync_js_controller_.AttachJsBackend(WeakHandle<JsBackend>());

  // Move aside the backend so nobody else tries to use it while we are
  // shutting it down.
  scoped_ptr<SyncBackendHost> doomed_backend(backend_.release());
  if (doomed_backend.get()) {
    doomed_backend->Shutdown(sync_disabled);

    doomed_backend.reset();
  }

  weak_factory_.InvalidateWeakPtrs();

  // Clear various flags.
  expect_sync_configuration_aborted_ = false;
  is_auth_in_progress_ = false;
  backend_initialized_ = false;
  cached_passphrases_ = CachedPassphrases();
  encryption_pending_ = false;
  encrypt_everything_ = false;
  encrypted_types_ = browser_sync::Cryptographer::SensitiveTypes();
  passphrase_required_reason_ = sync_api::REASON_PASSPHRASE_NOT_REQUIRED;
  last_auth_error_ = GoogleServiceAuthError::None();

  if (sync_global_error_.get()) {
    GlobalErrorServiceFactory::GetForProfile(profile_)->RemoveGlobalError(
        sync_global_error_.get());
    RemoveObserver(sync_global_error_.get());
    sync_global_error_.reset(NULL);
  }
}

void ProfileSyncService::ClearServerData() {
  clear_server_data_state_ = CLEAR_CLEARING;
  clear_server_data_timer_.Start(FROM_HERE,
      base::TimeDelta::FromSeconds(kSyncClearDataTimeoutInSeconds), this,
      &ProfileSyncService::OnClearServerDataTimeout);
  backend_->RequestClearServerData();
}

void ProfileSyncService::DisableForUser() {
  if (SetupInProgress())
    wizard_.Step(SyncSetupWizard::ABORT);

  // Clear prefs (including SyncSetupHasCompleted) before shutting down so
  // PSS clients don't think we're set up while we're shutting down.
  sync_prefs_.ClearPreferences();
  ClearUnrecoverableError();
  ShutdownImpl(true);

  // TODO(atwilson): Don't call SignOut() on *any* platform - move this into
  // the UI layer if needed (sync activity should never result in the user
  // being logged out of all chrome services).
  if (!auto_start_enabled_)
    signin_->SignOut();

  NotifyObservers();
}

bool ProfileSyncService::HasSyncSetupCompleted() const {
  return sync_prefs_.HasSyncSetupCompleted();
}

void ProfileSyncService::SetSyncSetupCompleted() {
  sync_prefs_.SetSyncSetupCompleted();
}

void ProfileSyncService::UpdateLastSyncedTime() {
  last_synced_time_ = base::Time::Now();
  sync_prefs_.SetLastSyncedTime(last_synced_time_);
}

void ProfileSyncService::NotifyObservers() {
  FOR_EACH_OBSERVER(Observer, observers_, OnStateChanged());
  // TODO(akalin): Make an Observer subclass that listens and does the
  // event routing.
  sync_js_controller_.HandleJsEvent(
      "onServiceStateChanged", JsEventDetails());
}

void ProfileSyncService::ClearStaleErrors() {
  ClearUnrecoverableError();
  last_actionable_error_ = SyncProtocolError();
}

void ProfileSyncService::ClearUnrecoverableError() {
  unrecoverable_error_detected_ = false;
  unrecoverable_error_message_.clear();
  unrecoverable_error_location_ = tracked_objects::Location();
}

// static
std::string ProfileSyncService::GetExperimentNameForDataType(
    syncable::ModelType data_type) {
  switch (data_type) {
    case syncable::SESSIONS:
      return "sync-tabs";
    default:
      break;
  }
  NOTREACHED();
  return "";
}

void ProfileSyncService::RegisterNewDataType(syncable::ModelType data_type) {
  if (data_type_controllers_.count(data_type) > 0)
    return;
  switch (data_type) {
    case syncable::SESSIONS:
      RegisterDataTypeController(
          new browser_sync::SessionDataTypeController(factory_.get(),
                                                      profile_,
                                                      this));
      return;
    case syncable::TYPED_URLS:
          new browser_sync::TypedUrlDataTypeController(factory_.get(),
                                                       profile_,
                                                       this);
      return;
    default:
      break;
  }
  NOTREACHED();
}

// An invariant has been violated.  Transition to an error state where we try
// to do as little work as possible, to avoid further corruption or crashes.
void ProfileSyncService::OnUnrecoverableError(
    const tracked_objects::Location& from_here,
    const std::string& message) {
  unrecoverable_error_detected_ = true;
  unrecoverable_error_message_ = message;
  unrecoverable_error_location_ = from_here;

  // Tell the wizard so it can inform the user only if it is already open.
  wizard_.Step(SyncSetupWizard::FATAL_ERROR);

  NotifyObservers();
  std::string location;
  from_here.Write(true, true, &location);
  LOG(ERROR)
      << "Unrecoverable error detected at " << location
      << " -- ProfileSyncService unusable: " << message;

  // Shut all data types down.
  MessageLoop::current()->PostTask(FROM_HERE,
      base::Bind(&ProfileSyncService::ShutdownImpl, weak_factory_.GetWeakPtr(),
                 true));
}

void ProfileSyncService::OnBackendInitialized(
    const WeakHandle<JsBackend>& js_backend, bool success) {
  if (!HasSyncSetupCompleted()) {
    UMA_HISTOGRAM_BOOLEAN("Sync.BackendInitializeFirstTimeSuccess", success);
  } else {
    UMA_HISTOGRAM_BOOLEAN("Sync.BackendInitializeRestoreSuccess", success);
  }

  if (!success) {
    // Something went unexpectedly wrong.  Play it safe: nuke our current state
    // and prepare ourselves to try again later.
    DisableForUser();
    return;
  }

  backend_initialized_ = true;

  sync_js_controller_.AttachJsBackend(js_backend);

  // The very first time the backend initializes is effectively the first time
  // we can say we successfully "synced".  last_synced_time_ will only be null
  // in this case, because the pref wasn't restored on StartUp.
  if (last_synced_time_.is_null()) {
    UpdateLastSyncedTime();
  }
  NotifyObservers();

  if (auto_start_enabled_ && !SetupInProgress()) {
    // Backend is initialized but we're not in sync setup, so this must be an
    // autostart - mark our sync setup as completed.
    if (sync_prefs_.IsStartSuppressed()) {
      // TODO(sync): This call to ShowConfigure() should go away in favor
      // of the code below that calls wizard_.Step() - http://crbug.com/95269.
      ShowConfigure(true);
      return;
    } else {
      SetSyncSetupCompleted();
      NotifyObservers();
    }
  }

  if (HasSyncSetupCompleted()) {
    ConfigureDataTypeManager();
  } else if (SetupInProgress()) {
    wizard_.Step(SyncSetupWizard::SYNC_EVERYTHING);
  } else {
    // This should only be hit during integration tests, but there's no good
    // way to assert this.
    DVLOG(1) << "Setup not complete, no wizard - integration tests?";
  }
}

void ProfileSyncService::OnSyncCycleCompleted() {
  UpdateLastSyncedTime();
  if (GetSessionModelAssociator()) {
    // Trigger garbage collection of old sessions now that we've downloaded
    // any new session data. TODO(zea): Have this be a notification the session
    // model associator listens too. Also consider somehow plumbing the current
    // server time as last reported by CheckServerReachable, so we don't have to
    // rely on the local clock, which may be off significantly.
    MessageLoop::current()->PostTask(FROM_HERE,
        base::Bind(&browser_sync::SessionModelAssociator::DeleteStaleSessions,
                   GetSessionModelAssociator()->AsWeakPtr()));
  }
  DVLOG(2) << "Notifying observers sync cycle completed";
  NotifyObservers();
}

// TODO(sync): eventually support removing datatypes too.
void ProfileSyncService::OnDataTypesChanged(
    syncable::ModelTypeSet to_add) {
  // If this is a first time sync for a client, this will be called before
  // OnBackendInitialized() to ensure the new datatypes are available at sync
  // setup. As a result, the migrator won't exist yet. This is fine because for
  // first time sync cases we're only concerned with making the datatype
  // available.
  if (migrator_.get() &&
      migrator_->state() != browser_sync::BackendMigrator::IDLE) {
    DVLOG(1) << "Dropping OnDataTypesChanged due to migrator busy.";
    return;
  }

  DVLOG(2) << "OnDataTypesChanged called with types: "
           << syncable::ModelTypeSetToString(to_add);

  const syncable::ModelTypeSet registered_types = GetRegisteredDataTypes();

  const syncable::ModelTypeSet to_register =
      Difference(to_add, registered_types);

  DVLOG(2) << "Enabling types: " << syncable::ModelTypeSetToString(to_register);

  for (syncable::ModelTypeSet::Iterator it = to_register.First();
       it.Good(); it.Inc()) {
    // Received notice to enable experimental type. Check if the type is
    // registered, and if not register a new datatype controller.
    RegisterNewDataType(it.Get());
    // Enable the about:flags switch for the experimental type so we don't have
    // to always perform this reconfiguration. Once we set this, the type will
    // remain registered on restart, so we will no longer go down this code
    // path.
    std::string experiment_name = GetExperimentNameForDataType(it.Get());
    if (experiment_name.empty())
      continue;
    about_flags::SetExperimentEnabled(g_browser_process->local_state(),
                                      experiment_name,
                                      true);
  }

  // Check if the user has "Keep Everything Synced" enabled. If so, we want
  // to turn on all experimental types if they're not already on. Otherwise we
  // leave them off.
  // Note: if any types are already registered, we don't turn them on. This
  // covers the case where we're already in the process of reconfiguring
  // to turn an experimental type on.
  if (sync_prefs_.HasKeepEverythingSynced()) {
    // Mark all data types as preferred.
    sync_prefs_.SetPreferredDataTypes(registered_types, registered_types);

    // Only automatically turn on types if we have already finished set up.
    // Otherwise, just leave the experimental types on by default.
    if (!to_register.Empty() && HasSyncSetupCompleted() && migrator_.get()) {
      DVLOG(1) << "Dynamically enabling new datatypes: "
               << syncable::ModelTypeSetToString(to_register);
      OnMigrationNeededForTypes(to_register);
    }
  }
}

void ProfileSyncService::UpdateAuthErrorState(
    const GoogleServiceAuthError& error) {
  last_auth_error_ = error;
  // Protect against the in-your-face dialogs that pop out of nowhere.
  // Require the user to click somewhere to run the setup wizard in the case
  // of a steady-state auth failure.
  if (WizardIsVisible()) {
    wizard_.Step(last_auth_error_.state() == AuthError::NONE ?
        SyncSetupWizard::GAIA_SUCCESS : SyncSetupWizard::GetLoginState());
  } else {
    auth_error_time_ = base::TimeTicks::Now();
  }

  if (!auth_start_time_.is_null()) {
    UMA_HISTOGRAM_TIMES("Sync.AuthorizationTimeInNetwork",
                    base::TimeTicks::Now() - auth_start_time_);
    auth_start_time_ = base::TimeTicks();
  }

  // Fan the notification out to interested UI-thread components.
  NotifyObservers();
}

void ProfileSyncService::OnAuthError() {
  UpdateAuthErrorState(backend_->GetAuthError());
}

void ProfileSyncService::OnStopSyncingPermanently() {
  if (SetupInProgress()) {
    wizard_.Step(SyncSetupWizard::SETUP_ABORTED_BY_PENDING_CLEAR);
    expect_sync_configuration_aborted_ = true;
  }
  sync_prefs_.SetStartSuppressed(true);
  DisableForUser();
}

void ProfileSyncService::OnClearServerDataTimeout() {
  if (clear_server_data_state_ != CLEAR_SUCCEEDED &&
      clear_server_data_state_ != CLEAR_FAILED) {
    clear_server_data_state_ = CLEAR_FAILED;
    NotifyObservers();
  }
}

void ProfileSyncService::OnClearServerDataFailed() {
  clear_server_data_timer_.Stop();

  // Only once clear has succeeded there is no longer a need to transition to
  // a failed state as sync is disabled locally.  Also, no need to fire off
  // the observers if the state didn't change (i.e. it was FAILED before).
  if (clear_server_data_state_ != CLEAR_SUCCEEDED &&
      clear_server_data_state_ != CLEAR_FAILED) {
    clear_server_data_state_ = CLEAR_FAILED;
    NotifyObservers();
  }
}

void ProfileSyncService::OnClearServerDataSucceeded() {
  clear_server_data_timer_.Stop();

  // Even if the timout fired, we still transition to the succeeded state as
  // we want UI to update itself and no longer allow the user to press "clear"
  if (clear_server_data_state_ != CLEAR_SUCCEEDED) {
    clear_server_data_state_ = CLEAR_SUCCEEDED;
    NotifyObservers();
  }
}

void ProfileSyncService::OnPassphraseRequired(
    sync_api::PassphraseRequiredReason reason,
    const sync_pb::EncryptedData& pending_keys) {
  DCHECK(backend_.get());
  DCHECK(backend_->IsNigoriEnabled());

  // TODO(lipalani) : add this check to other locations as well.
  if (unrecoverable_error_detected_) {
    // When unrecoverable error is detected we post a task to shutdown the
    // backend. The task might not have executed yet.
    return;
  }

  DVLOG(1) << "Passphrase required with reason: "
           << sync_api::PassphraseRequiredReasonToString(reason);
  passphrase_required_reason_ = reason;

  // First try supplying gaia password as the passphrase.
  // TODO(atwilson): This logic seems odd here - we know what kind of passphrase
  // is required (explicit/gaia) so we should not bother setting the wrong kind
  // of passphrase - http://crbug.com/95269.
  if (!cached_passphrases_.gaia_passphrase.empty()) {
    std::string gaia_passphrase = cached_passphrases_.gaia_passphrase;
    cached_passphrases_.gaia_passphrase.clear();
    DVLOG(1) << "Attempting gaia passphrase.";
    // SetPassphrase will re-cache this passphrase if the syncer isn't ready.
    SetPassphrase(gaia_passphrase, IMPLICIT,
                  (cached_passphrases_.user_provided_gaia ?
                       USER_PROVIDED : INTERNAL));
    return;
  }

  // If the above failed then try the custom passphrase the user might have
  // entered in setup.
  if (!cached_passphrases_.explicit_passphrase.empty()) {
    std::string explicit_passphrase = cached_passphrases_.explicit_passphrase;
    cached_passphrases_.explicit_passphrase.clear();
    DVLOG(1) << "Attempting explicit passphrase.";
    // SetPassphrase will re-cache this passphrase if the syncer isn't ready.
    SetPassphrase(explicit_passphrase, EXPLICIT, USER_PROVIDED);
    return;
  }

  // If no passphrase is required (due to not having any encrypted data types
  // enabled), just act as if we don't have any passphrase error. We still
  // track the auth error in passphrase_required_reason_ in case the user later
  // re-enables an encrypted data type.
  if (!IsPassphraseRequiredForDecryption()) {
    DVLOG(1) << "Decrypting and no encrypted datatypes enabled"
             << ", accepted passphrase.";
    ResolvePassphraseRequired();
  } else if (WizardIsVisible()) {
    // Prompt the user for a password.
    DVLOG(1) << "Prompting user for passphrase.";
    wizard_.Step(SyncSetupWizard::ENTER_PASSPHRASE);
  }

  NotifyObservers();
}

void ProfileSyncService::OnPassphraseAccepted() {
  DVLOG(1) << "Received OnPassphraseAccepted.";
  // Reset passphrase_required_reason_ since we know we no longer require the
  // passphrase. We do this here rather than down in ResolvePassphraseRequired()
  // because that can be called by OnPassphraseRequired() if no encrypted data
  // types are enabled, and we don't want to clobber the true passphrase error.
  passphrase_required_reason_ = sync_api::REASON_PASSPHRASE_NOT_REQUIRED;

  // Make sure the data types that depend on the passphrase are started at
  // this time.
  const syncable::ModelTypeSet types = GetPreferredDataTypes();

  if (data_type_manager_.get()) {
    // Unblock the data type manager if necessary.
    data_type_manager_->Configure(types,
                                  sync_api::CONFIGURE_REASON_RECONFIGURATION);
  }

  ResolvePassphraseRequired();
}

void ProfileSyncService::ResolvePassphraseRequired() {
  DCHECK(!IsPassphraseRequiredForDecryption());
  // Don't hold on to a passphrase in raw form longer than needed.
  cached_passphrases_ = CachedPassphrases();

  // If No encryption is pending and our passphrase has been accepted, tell the
  // wizard we're done (no need to hang around waiting for the sync to
  // complete). If encryption is pending, its successful completion will trigger
  // the done step.
  if (WizardIsVisible() && !encryption_pending())
    wizard_.Step(SyncSetupWizard::DONE);

  NotifyObservers();
}

void ProfileSyncService::OnEncryptedTypesChanged(
    syncable::ModelTypeSet encrypted_types,
    bool encrypt_everything) {
  encrypted_types_ = encrypted_types;
  encrypt_everything_ = encrypt_everything;
  DVLOG(1) << "Encrypted types changed to "
           << syncable::ModelTypeSetToString(encrypted_types_)
           << " (encrypt everything is set to "
           << (encrypt_everything_ ? "true" : "false") << ")";
  DCHECK(encrypted_types_.Has(syncable::PASSWORDS));
}

void ProfileSyncService::OnEncryptionComplete() {
  DVLOG(1) << "Encryption complete";
  if (encryption_pending_ && encrypt_everything_) {
    encryption_pending_ = false;
    // The user had chosen to encrypt datatypes. This is the last thing to
    // complete, so now that we're done notify the UI.
    wizard_.Step(SyncSetupWizard::DONE);
    // This is to nudge the integration tests when encryption is
    // finished.
    NotifyObservers();
  }
}

void ProfileSyncService::OnMigrationNeededForTypes(
    syncable::ModelTypeSet types) {
  DCHECK(backend_initialized_);
  DCHECK(data_type_manager_.get());

  // Migrator must be valid, because we don't sync until it is created and this
  // callback originates from a sync cycle.
  migrator_->MigrateTypes(types);
}

void ProfileSyncService::OnActionableError(const SyncProtocolError& error) {
  last_actionable_error_ = error;
  DCHECK_NE(last_actionable_error_.action,
            browser_sync::UNKNOWN_ACTION);
  switch (error.action) {
    case browser_sync::UPGRADE_CLIENT:
    case browser_sync::CLEAR_USER_DATA_AND_RESYNC:
    case browser_sync::ENABLE_SYNC_ON_ACCOUNT:
    case browser_sync::STOP_AND_RESTART_SYNC:
      // TODO(lipalani) : if setup in progress we want to display these
      // actions in the popup. The current experience might not be optimal for
      // the user. We just dismiss the dialog.
      if (SetupInProgress()) {
        wizard_.Step(SyncSetupWizard::ABORT);
        OnStopSyncingPermanently();
        expect_sync_configuration_aborted_ = true;
      }
      // Trigger an unrecoverable error to stop syncing.
      OnUnrecoverableError(FROM_HERE, last_actionable_error_.error_description);
      break;
    case browser_sync::DISABLE_SYNC_ON_CLIENT:
      OnStopSyncingPermanently();
      break;
    default:
      NOTREACHED();
  }
  NotifyObservers();
}

void ProfileSyncService::ShowLoginDialog() {
  if (WizardIsVisible()) {
    wizard_.Focus();
    // Force the wizard to step to the login screen (which will only actually
    // happen if the transition is valid).
    wizard_.Step(SyncSetupWizard::GetLoginState());
    return;
  }

  if (!auth_error_time_.is_null()) {
    UMA_HISTOGRAM_LONG_TIMES("Sync.ReauthorizationTime",
                             base::TimeTicks::Now() - auth_error_time_);
    auth_error_time_ = base::TimeTicks();  // Reset auth_error_time_ to null.
  }

  ShowSyncSetupWithWizard(SyncSetupWizard::GetLoginState());

  NotifyObservers();
}

void ProfileSyncService::ShowErrorUI() {
  if (WizardIsVisible()) {
    wizard_.Focus();
    return;
  }

  if (last_auth_error_.state() != AuthError::NONE)
    ShowLoginDialog();
  else if (ShouldShowActionOnUI(last_actionable_error_))
    ShowSyncSetup(chrome::kPersonalOptionsSubPage);
  else
    ShowSyncSetupWithWizard(SyncSetupWizard::NONFATAL_ERROR);
}

void ProfileSyncService::ShowConfigure(bool sync_everything) {
  if (!sync_initialized()) {
    LOG(ERROR) << "Attempted to show sync configure before backend ready.";
    return;
  }
  if (WizardIsVisible()) {
    wizard_.Focus();
    return;
  }

  if (sync_everything)
    ShowSyncSetupWithWizard(SyncSetupWizard::SYNC_EVERYTHING);
  else
    ShowSyncSetupWithWizard(SyncSetupWizard::CONFIGURE);
}

void ProfileSyncService::ShowSyncSetup(const std::string& sub_page) {
  Browser* browser = BrowserList::GetLastActiveWithProfile(profile());
  if (!browser) {
    browser = Browser::Create(profile());
    browser->ShowOptionsTab(sub_page);
    browser->window()->Show();
  } else {
    browser->ShowOptionsTab(sub_page);
  }
}


void ProfileSyncService::ShowSyncSetupWithWizard(SyncSetupWizard::State state) {
  wizard_.Step(state);
  ShowSyncSetup(chrome::kSyncSetupSubPage);
}

SyncBackendHost::StatusSummary ProfileSyncService::QuerySyncStatusSummary() {
  if (backend_.get() && backend_initialized_)
    return backend_->GetStatusSummary();
  else
    return SyncBackendHost::Status::OFFLINE_UNUSABLE;
}

SyncBackendHost::Status ProfileSyncService::QueryDetailedSyncStatus() {
  if (backend_.get() && backend_initialized_) {
    return backend_->GetDetailedStatus();
  } else {
    SyncBackendHost::Status status;
    status.summary = SyncBackendHost::Status::OFFLINE_UNUSABLE;
    status.sync_protocol_error = last_actionable_error_;
    return status;
  }
}

const GoogleServiceAuthError& ProfileSyncService::GetAuthError() const {
  return last_auth_error_;
}

bool ProfileSyncService::SetupInProgress() const {
  return !HasSyncSetupCompleted() && WizardIsVisible();
}

std::string ProfileSyncService::BuildSyncStatusSummaryText(
    const sync_api::SyncManager::Status::Summary& summary) {
  const char* strings[] = {"INVALID", "OFFLINE", "OFFLINE_UNSYNCED", "SYNCING",
      "READY", "OFFLINE_UNUSABLE"};
  COMPILE_ASSERT(arraysize(strings) ==
                 sync_api::SyncManager::Status::SUMMARY_STATUS_COUNT,
                 enum_indexed_array);
  if (summary < 0 ||
      summary >= sync_api::SyncManager::Status::SUMMARY_STATUS_COUNT) {
    LOG(DFATAL) << "Illegal Summary Value: " << summary;
    return "UNKNOWN";
  }
  return strings[summary];
}

bool ProfileSyncService::sync_initialized() const {
  return backend_initialized_;
}

bool ProfileSyncService::unrecoverable_error_detected() const {
  return unrecoverable_error_detected_;
}

bool ProfileSyncService::UIShouldDepictAuthInProgress() const {
  return is_auth_in_progress_;
}

void ProfileSyncService::SetUIShouldDepictAuthInProgress(
    bool auth_in_progress) {
  is_auth_in_progress_ = auth_in_progress;
  // TODO(atwilson): Figure out if we still need to track this or if we should
  // move this up to the UI (or break it out into two stats that track GAIA
  // auth and sync auth separately).
  if (is_auth_in_progress_)
    auth_start_time_ = base::TimeTicks::Now();
  NotifyObservers();
}

bool ProfileSyncService::IsPassphraseRequired() const {
  return passphrase_required_reason_ !=
      sync_api::REASON_PASSPHRASE_NOT_REQUIRED;
}

// TODO(zea): Rename this IsPassphraseNeededFromUI and ensure it's used
// appropriately (see http://crbug.com/91379).
bool ProfileSyncService::IsPassphraseRequiredForDecryption() const {
  // If there is an encrypted datatype enabled and we don't have the proper
  // passphrase, we must prompt the user for a passphrase. The only way for the
  // user to avoid entering their passphrase is to disable the encrypted types.
  return IsEncryptedDatatypeEnabled() && IsPassphraseRequired();
}

string16 ProfileSyncService::GetLastSyncedTimeString() const {
  if (last_synced_time_.is_null())
    return l10n_util::GetStringUTF16(IDS_SYNC_TIME_NEVER);

  base::TimeDelta last_synced = base::Time::Now() - last_synced_time_;

  if (last_synced < base::TimeDelta::FromMinutes(1))
    return l10n_util::GetStringUTF16(IDS_SYNC_TIME_JUST_NOW);

  return TimeFormat::TimeElapsed(last_synced);
}

void ProfileSyncService::OnUserChoseDatatypes(bool sync_everything,
    syncable::ModelTypeSet chosen_types) {
  if (!backend_.get() &&
      unrecoverable_error_detected_ == false) {
    NOTREACHED();
    return;
  }

  sync_prefs_.SetKeepEverythingSynced(sync_everything);

  failed_datatypes_handler_.OnUserChoseDatatypes();
  ChangePreferredDataTypes(chosen_types);
  AcknowledgeSyncedTypes();
}

void ProfileSyncService::OnUserCancelledDialog() {
  if (!HasSyncSetupCompleted()) {
    // A sync dialog was aborted before authentication.
    // Rollback.
    expect_sync_configuration_aborted_ = true;
    DisableForUser();
  }

  // If the user attempted to encrypt datatypes, but was unable to do so, we
  // allow them to cancel out.
  encryption_pending_ = false;

  NotifyObservers();
}

void ProfileSyncService::ChangePreferredDataTypes(
    syncable::ModelTypeSet preferred_types) {

  DVLOG(1) << "ChangePreferredDataTypes invoked";
  const syncable::ModelTypeSet registered_types = GetRegisteredDataTypes();
  const syncable::ModelTypeSet registered_preferred_types =
      Intersection(registered_types, preferred_types);
  sync_prefs_.SetPreferredDataTypes(registered_types,
                                    registered_preferred_types);

  // Now reconfigure the DTM.
  ReconfigureDatatypeManager();
}

syncable::ModelTypeSet ProfileSyncService::GetPreferredDataTypes() const {
  const syncable::ModelTypeSet registered_types = GetRegisteredDataTypes();
  const syncable::ModelTypeSet preferred_types =
      sync_prefs_.GetPreferredDataTypes(registered_types);
  const syncable::ModelTypeSet failed_types =
      failed_datatypes_handler_.GetFailedTypes();
  return Difference(preferred_types, failed_types);
}

syncable::ModelTypeSet ProfileSyncService::GetRegisteredDataTypes() const {
  syncable::ModelTypeSet registered_types;
  // The data_type_controllers_ are determined by command-line flags; that's
  // effectively what controls the values returned here.
  for (DataTypeController::TypeMap::const_iterator it =
       data_type_controllers_.begin();
       it != data_type_controllers_.end(); ++it) {
    registered_types.Put(it->first);
  }
  return registered_types;
}

bool ProfileSyncService::IsUsingSecondaryPassphrase() const {
  // Should never be called when the backend is not initialized, since at that
  // time we have no idea whether we have an explicit passphrase or not because
  // the nigori node has not been downloaded yet.
  if (!sync_initialized()) {
    NOTREACHED() << "Cannot call IsUsingSecondaryPassphrase() before the sync "
                 << "backend has downloaded the nigori node";
    return false;
  }
  return backend_->IsUsingExplicitPassphrase();
}

bool ProfileSyncService::IsCryptographerReady(
    const sync_api::BaseTransaction* trans) const {
  return backend_.get() && backend_->IsCryptographerReady(trans);
}

SyncBackendHost* ProfileSyncService::GetBackendForTest() {
  // We don't check |backend_initialized_|; we assume the test class
  // knows what it's doing.
  return backend_.get();
}

void ProfileSyncService::ConfigureDataTypeManager() {
  bool restart = false;
  if (!data_type_manager_.get()) {
    restart = true;
    data_type_manager_.reset(
        factory_->CreateDataTypeManager(backend_.get(),
                                        &data_type_controllers_));
    registrar_.Add(this,
                   chrome::NOTIFICATION_SYNC_CONFIGURE_START,
                   content::Source<DataTypeManager>(data_type_manager_.get()));
    registrar_.Add(this,
                   chrome::NOTIFICATION_SYNC_CONFIGURE_DONE,
                   content::Source<DataTypeManager>(data_type_manager_.get()));

    // We create the migrator at the same time.
    migrator_.reset(
        new browser_sync::BackendMigrator(
            profile_->GetDebugName(), GetUserShare(),
            this, data_type_manager_.get()));
  }

  const syncable::ModelTypeSet types = GetPreferredDataTypes();
  if (IsPassphraseRequiredForDecryption()) {
    // We need a passphrase still. We don't bother to attempt to configure
    // until we receive an OnPassphraseAccepted (which triggers a configure).
    DVLOG(1) << "ProfileSyncService::ConfigureDataTypeManager bailing out "
             << "because a passphrase required";
    return;
  }
  sync_api::ConfigureReason reason = sync_api::CONFIGURE_REASON_UNKNOWN;
  if (!HasSyncSetupCompleted()) {
    reason = sync_api::CONFIGURE_REASON_NEW_CLIENT;
  } else if (restart == false ||
             sync_api::InitialSyncEndedForTypes(types, GetUserShare())) {
    reason = sync_api::CONFIGURE_REASON_RECONFIGURATION;
  } else {
    DCHECK(restart);
    reason = sync_api::CONFIGURE_REASON_NEWLY_ENABLED_DATA_TYPE;
  }
  DCHECK(reason != sync_api::CONFIGURE_REASON_UNKNOWN);

  data_type_manager_->Configure(types, reason);
}

sync_api::UserShare* ProfileSyncService::GetUserShare() const {
  if (backend_.get() && backend_initialized_) {
    return backend_->GetUserShare();
  }
  NOTREACHED();
  return NULL;
}

const browser_sync::sessions::SyncSessionSnapshot*
    ProfileSyncService::GetLastSessionSnapshot() const {
  if (backend_.get() && backend_initialized_) {
    return backend_->GetLastSessionSnapshot();
  }
  NOTREACHED();
  return NULL;
}

bool ProfileSyncService::HasUnsyncedItems() const {
  if (backend_.get() && backend_initialized_) {
    return backend_->HasUnsyncedItems();
  }
  NOTREACHED();
  return false;
}

browser_sync::BackendMigrator*
    ProfileSyncService::GetBackendMigratorForTest() {
  return migrator_.get();
}

void ProfileSyncService::GetModelSafeRoutingInfo(
    browser_sync::ModelSafeRoutingInfo* out) const {
  if (backend_.get() && backend_initialized_) {
    backend_->GetModelSafeRoutingInfo(out);
  } else {
    NOTREACHED();
  }
}

void ProfileSyncService::ActivateDataType(
    syncable::ModelType type, browser_sync::ModelSafeGroup group,
    ChangeProcessor* change_processor) {
  if (!backend_.get()) {
    NOTREACHED();
    return;
  }
  DCHECK(backend_initialized_);
  backend_->ActivateDataType(type, group, change_processor);
}

void ProfileSyncService::DeactivateDataType(syncable::ModelType type) {
  if (!backend_.get())
    return;
  backend_->DeactivateDataType(type);
}

void ProfileSyncService::SetPassphrase(const std::string& passphrase,
                                       PassphraseType type,
                                       PassphraseSource source) {
  DCHECK(source == USER_PROVIDED || type == IMPLICIT);
  if (ShouldPushChanges() || IsPassphraseRequired()) {
    DVLOG(1) << "Setting " << (type == EXPLICIT ? "explicit" : "implicit")
             << " passphrase.";
    backend_->SetPassphrase(passphrase,
                            type == EXPLICIT,
                            source == USER_PROVIDED);
  } else {
    if (type == EXPLICIT) {
      NOTREACHED() << "SetPassphrase should only be called after the backend "
                   << "is initialized.";
      cached_passphrases_.explicit_passphrase = passphrase;
    } else {
      cached_passphrases_.gaia_passphrase = passphrase;
      cached_passphrases_.user_provided_gaia = (source == USER_PROVIDED);
      DVLOG(1) << "Caching "
               << (cached_passphrases_.user_provided_gaia ? "user provided" :
                       "internal")
               << " gaia passphrase.";
    }
  }
}

void ProfileSyncService::EnableEncryptEverything() {
  // Tests override sync_initialized() to always return true, so we
  // must check that instead of |backend_initialized_|.
  // TODO(akalin): Fix the above. :/
  DCHECK(sync_initialized());
  if (!encrypt_everything_)
    encryption_pending_ = true;
}

bool ProfileSyncService::encryption_pending() const {
  // We may be called during the setup process before we're
  // initialized (via IsEncryptedDatatypeEnabled and
  // IsPassphraseRequiredForDecryption).
  return encryption_pending_;
}

bool ProfileSyncService::EncryptEverythingEnabled() const {
  DCHECK(backend_initialized_);
  return encrypt_everything_;
}

syncable::ModelTypeSet ProfileSyncService::GetEncryptedDataTypes() const {
  DCHECK(encrypted_types_.Has(syncable::PASSWORDS));
  // We may be called during the setup process before we're
  // initialized.  In this case, we default to the sensitive types.
  return encrypted_types_;
}

void ProfileSyncService::OnSyncManagedPrefChange(bool is_sync_managed) {
  NotifyObservers();
  if (is_sync_managed) {
    DisableForUser();
  } else if (HasSyncSetupCompleted() && AreCredentialsAvailable()) {
    StartUp();
  }
}

void ProfileSyncService::Observe(int type,
                                 const content::NotificationSource& source,
                                 const content::NotificationDetails& details) {
  switch (type) {
    case chrome::NOTIFICATION_SYNC_CONFIGURE_START: {
      NotifyObservers();
      // TODO(sync): Maybe toast?
      break;
    }
    case chrome::NOTIFICATION_SYNC_CONFIGURE_DONE: {
      DataTypeManager::ConfigureResult* result =
          content::Details<DataTypeManager::ConfigureResult>(details).ptr();

      configure_status_ = result->status;
      DVLOG(1) << "PSS SYNC_CONFIGURE_DONE called with status: "
               << configure_status_;

      // The possible status values:
      //    ABORT - Configuration was aborted. This is not an error, if
      //            initiated by user.
      //    RETRY - Configure failed but we are retrying.
      //    OK - Everything succeeded.
      //    PARTIAL_SUCCESS - Some datatypes failed to start.
      //    Everything else is an UnrecoverableError. So treat it as such.

      // First handle the abort case.
      if (configure_status_ == DataTypeManager::ABORTED &&
          expect_sync_configuration_aborted_) {
        DVLOG(0) << "ProfileSyncService::Observe Sync Configure aborted";
        expect_sync_configuration_aborted_ = false;
        return;
      }

      // Handle retry case.
      if (configure_status_ == DataTypeManager::RETRY) {
        OnSyncConfigureRetry();
        return;
      }

      // Handle unrecoverable error.
      if (configure_status_ != DataTypeManager::OK &&
          configure_status_ != DataTypeManager::PARTIAL_SUCCESS) {
        // Something catastrophic had happened. We should only have one
        // error representing it.
        DCHECK(result->errors.size() == 1);
        SyncError error = result->errors.front();
        DCHECK(error.IsSet());
        std::string message =
          "Sync configuration failed with status " +
          DataTypeManager::ConfigureStatusToString(configure_status_) +
          " during " + syncable::ModelTypeToString(error.type()) +
          ": " + error.message();
        LOG(ERROR) << "ProfileSyncService error: "
                   << message;
        // TODO: Don't
        OnUnrecoverableError(error.location(), message);
        return;
      }

      // Now handle partial success and full success.
      MessageLoop::current()->PostTask(FROM_HERE,
          base::Bind(&ProfileSyncService::OnSyncConfigureDone,
                     weak_factory_.GetWeakPtr(), *result));

      // We should never get in a state where we have no encrypted datatypes
      // enabled, and yet we still think we require a passphrase for decryption.
      DCHECK(!(IsPassphraseRequiredForDecryption() &&
               !IsEncryptedDatatypeEnabled()));

      // This must be done before we start syncing with the server to avoid
      // sending unencrypted data up on a first time sync.
      if (!encryption_pending_) {
        wizard_.Step(SyncSetupWizard::DONE);
        NotifyObservers();
      } else {
        backend_->EnableEncryptEverything();
      }

      // In the old world, this would be a no-op.  With new syncer thread,
      // this is the point where it is safe to switch from config-mode to
      // normal operation.
      backend_->StartSyncingWithServer();

      break;
    }
    case chrome::NOTIFICATION_GOOGLE_SIGNIN_FAILED: {
      GoogleServiceAuthError error =
          *(content::Details<const GoogleServiceAuthError>(details).ptr());
      UpdateAuthErrorState(error);
      break;
    }
    case chrome::NOTIFICATION_GOOGLE_SIGNIN_SUCCESSFUL: {
      const GoogleServiceSigninSuccessDetails* successful =
          content::Details<const GoogleServiceSigninSuccessDetails>(
              details).ptr();
      // The user has submitted credentials, which indicates they don't
      // want to suppress start up anymore.
      sync_prefs_.SetStartSuppressed(false);

      // Because we specify IMPLICIT to SetPassphrase, we know it won't override
      // an explicit one.  Thus, we either update the implicit passphrase
      // (idempotent if the passphrase didn't actually change), or the user has
      // an explicit passphrase set so this becomes a no-op.
      if (!successful->password.empty())
        SetPassphrase(successful->password, IMPLICIT, INTERNAL);
      break;
    }
    case chrome::NOTIFICATION_TOKEN_REQUEST_FAILED: {
      const TokenService::TokenRequestFailedDetails& token_details =
          *(content::Details<const TokenService::TokenRequestFailedDetails>(
              details).ptr());
      if (IsTokenServiceRelevant(token_details.service())) {
        GoogleServiceAuthError error(
            GoogleServiceAuthError::INVALID_GAIA_CREDENTIALS);
        UpdateAuthErrorState(error);
      }
      break;
    }
    case chrome::NOTIFICATION_TOKEN_AVAILABLE: {
      const TokenService::TokenAvailableDetails& token_details =
          *(content::Details<const TokenService::TokenAvailableDetails>(
              details).ptr());
      if (IsTokenServiceRelevant(token_details.service()) &&
          AreCredentialsAvailable(true)) {
        if (backend_initialized_) {
          backend_->UpdateCredentials(GetCredentials());
          const GoogleServiceAuthError& last_error = GetAuthError();
          if (GoogleServiceAuthError::NONE == last_error.state()) {
            // SyncBackendHost::UpdateCredentials call does not call back
            // OnAuthError in cases when the underlying syncer state does not
            // change. Due to that if the login dialog is showing up when the
            // credentials have not expired as such (this happens when login
            // dialog is shown by app notifications setup code) the login dialog
            // will show the spinner forever. Hence, we call OnAuthError
            // explicitly here to avoid the infinite spinner in that case.
            // Note that SyncBackendHost::UpdateCredentials may actually end up
            // failing, but in that case an error will be shown to the user in
            // bookmarks bar and preferences.
            OnAuthError();
          }
        }
        if (!sync_prefs_.IsStartSuppressed())
          StartUp();
      }
      break;
    }
    case chrome::NOTIFICATION_TOKEN_LOADING_FINISHED: {
      // This notification gets fired when TokenService loads the tokens
      // from storage. Here we only check if the chromiumsync token is
      // available (versus both chromiumsync and oauth login tokens) to
      // start up sync successfully for already logged in users who may
      // only have chromiumsync token if they logged in before the code
      // to generate oauth login token released.
      if (AreCredentialsAvailable()) {
        // Initialize the backend if sync token was loaded.
        if (backend_initialized_) {
          backend_->UpdateCredentials(GetCredentials());
        }
        if (!sync_prefs_.IsStartSuppressed())
          StartUp();
      } else if (!auto_start_enabled_ &&
                 !signin_->GetAuthenticatedUsername().empty()) {
        // If not in auto-start / Chrome OS mode, and we have a username
        // without tokens, the user will need to signin again. NotifyObservers
        // to trigger errors in the UI that will allow the user to re-login.
        UpdateAuthErrorState(GoogleServiceAuthError(
            GoogleServiceAuthError::INVALID_GAIA_CREDENTIALS));
      }
      break;
    }
    default: {
      NOTREACHED();
    }
  }
}

void ProfileSyncService::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void ProfileSyncService::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

bool ProfileSyncService::HasObserver(Observer* observer) const {
  return observers_.HasObserver(observer);
}

base::WeakPtr<JsController> ProfileSyncService::GetJsController() {
  return sync_js_controller_.AsWeakPtr();
}

void ProfileSyncService::SyncEvent(SyncEventCodes code) {
  UMA_HISTOGRAM_ENUMERATION("Sync.EventCodes", code, MAX_SYNC_EVENT_CODE);
}

// static
bool ProfileSyncService::IsSyncEnabled() {
  // We have switches::kEnableSync just in case we need to change back to
  // sync-disabled-by-default on a platform.
  return !CommandLine::ForCurrentProcess()->HasSwitch(switches::kDisableSync);
}

bool ProfileSyncService::IsManaged() const {
  return sync_prefs_.IsManaged();
}

bool ProfileSyncService::ShouldPushChanges() {
  // True only after all bootstrapping has succeeded: the sync backend
  // is initialized, all enabled data types are consistent with one
  // another, and no unrecoverable error has transpired.
  if (unrecoverable_error_detected_)
    return false;

  if (!data_type_manager_.get())
    return false;

  return data_type_manager_->state() == DataTypeManager::CONFIGURED;
}

void ProfileSyncService::StopAndSuppress() {
  sync_prefs_.SetStartSuppressed(true);
  ShutdownImpl(false);
}

void ProfileSyncService::UnsuppressAndStart() {
  DCHECK(profile_);
  sync_prefs_.SetStartSuppressed(false);
  // Set username in SigninManager, as SigninManager::OnGetUserInfoSuccess
  // is never called for some clients.
  if (signin_->GetAuthenticatedUsername().empty()) {
    signin_->SetAuthenticatedUsername(
        sync_prefs_.GetGoogleServicesUsername());
  }
  TryStart();
}

void ProfileSyncService::AcknowledgeSyncedTypes() {
  sync_prefs_.AcknowledgeSyncedTypes(GetRegisteredDataTypes());
}

void ProfileSyncService::ReconfigureDatatypeManager() {
  // If we haven't initialized yet, don't configure the DTM as it could cause
  // association to start before a Directory has even been created.
  if (backend_initialized_) {
    DCHECK(backend_.get());
    ConfigureDataTypeManager();
  } else if (unrecoverable_error_detected()) {
    // Close the wizard.
    if (WizardIsVisible()) {
       wizard_.Step(SyncSetupWizard::DONE);
    }
    // There is nothing more to configure. So inform the listeners,
    NotifyObservers();

    DVLOG(1) << "ConfigureDataTypeManager not invoked because of an "
             << "Unrecoverable error.";
  } else {
    DVLOG(0) << "ConfigureDataTypeManager not invoked because backend is not "
             << "initialized";
  }
}

const FailedDatatypesHandler& ProfileSyncService::failed_datatypes_handler() {
  return failed_datatypes_handler_;
}
