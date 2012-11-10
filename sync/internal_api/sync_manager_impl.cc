// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sync/internal_api/sync_manager_impl.h"

#include <string>

#include "base/base64.h"
#include "base/bind.h"
#include "base/callback.h"
#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/json/json_writer.h"
#include "base/memory/ref_counted.h"
#include "base/metrics/histogram.h"
#include "base/observer_list.h"
#include "base/string_number_conversions.h"
#include "base/values.h"
#include "sync/engine/sync_scheduler.h"
#include "sync/engine/syncer_types.h"
#include "sync/internal_api/change_reorder_buffer.h"
#include "sync/internal_api/public/base/model_type.h"
#include "sync/internal_api/public/base/model_type_payload_map.h"
#include "sync/internal_api/public/base_node.h"
#include "sync/internal_api/public/configure_reason.h"
#include "sync/internal_api/public/engine/polling_constants.h"
#include "sync/internal_api/public/http_post_provider_factory.h"
#include "sync/internal_api/public/internal_components_factory.h"
#include "sync/internal_api/public/read_node.h"
#include "sync/internal_api/public/read_transaction.h"
#include "sync/internal_api/public/user_share.h"
#include "sync/internal_api/public/util/experiments.h"
#include "sync/internal_api/public/write_node.h"
#include "sync/internal_api/public/write_transaction.h"
#include "sync/internal_api/syncapi_internal.h"
#include "sync/internal_api/syncapi_server_connection_manager.h"
#include "sync/js/js_arg_list.h"
#include "sync/js/js_event_details.h"
#include "sync/js/js_event_handler.h"
#include "sync/js/js_reply_handler.h"
#include "sync/notifier/invalidation_util.h"
#include "sync/notifier/sync_notifier.h"
#include "sync/protocol/encryption.pb.h"
#include "sync/protocol/proto_value_conversions.h"
#include "sync/protocol/sync.pb.h"
#include "sync/syncable/directory.h"
#include "sync/syncable/entry.h"
#include "sync/syncable/in_memory_directory_backing_store.h"
#include "sync/syncable/nigori_util.h"
#include "sync/syncable/on_disk_directory_backing_store.h"
#include "sync/util/get_session_name.h"

using base::TimeDelta;
using sync_pb::GetUpdatesCallerInfo;

namespace syncer {

using sessions::SyncSessionContext;
using syncable::ImmutableWriteTransactionInfo;
using syncable::SPECIFICS;

namespace {

// Delays for syncer nudges.
static const int kDefaultNudgeDelayMilliseconds = 200;
static const int kPreferencesNudgeDelayMilliseconds = 2000;
static const int kSyncRefreshDelayMsec = 500;
static const int kSyncSchedulerDelayMsec = 250;

// The maximum number of times we will automatically overwrite the nigori node
// because the encryption keys don't match (per chrome instantiation).
static const int kNigoriOverwriteLimit = 10;

// Maximum count and size for traffic recorder.
static const unsigned int kMaxMessagesToRecord = 10;
static const unsigned int kMaxMessageSizeToRecord = 5 * 1024;

GetUpdatesCallerInfo::GetUpdatesSource GetSourceFromReason(
    ConfigureReason reason) {
  switch (reason) {
    case CONFIGURE_REASON_RECONFIGURATION:
      return GetUpdatesCallerInfo::RECONFIGURATION;
    case CONFIGURE_REASON_MIGRATION:
      return GetUpdatesCallerInfo::MIGRATION;
    case CONFIGURE_REASON_NEW_CLIENT:
      return GetUpdatesCallerInfo::NEW_CLIENT;
    case CONFIGURE_REASON_NEWLY_ENABLED_DATA_TYPE:
      return GetUpdatesCallerInfo::NEWLY_SUPPORTED_DATATYPE;
    default:
      NOTREACHED();
  }
  return GetUpdatesCallerInfo::UNKNOWN;
}

}  // namespace

// A class to calculate nudge delays for types.
class NudgeStrategy {
 public:
  static TimeDelta GetNudgeDelayTimeDelta(const ModelType& model_type,
                                          SyncManagerImpl* core) {
    NudgeDelayStrategy delay_type = GetNudgeDelayStrategy(model_type);
    return GetNudgeDelayTimeDeltaFromType(delay_type,
                                          model_type,
                                          core);
  }

 private:
  // Possible types of nudge delay for datatypes.
  // Note: These are just hints. If a sync happens then all dirty entries
  // would be committed as part of the sync.
  enum NudgeDelayStrategy {
    // Sync right away.
    IMMEDIATE,

    // Sync this change while syncing another change.
    ACCOMPANY_ONLY,

    // The datatype does not use one of the predefined wait times but defines
    // its own wait time logic for nudge.
    CUSTOM,
  };

  static NudgeDelayStrategy GetNudgeDelayStrategy(const ModelType& type) {
    switch (type) {
     case AUTOFILL:
       return ACCOMPANY_ONLY;
     case PREFERENCES:
     case SESSIONS:
       return CUSTOM;
     default:
       return IMMEDIATE;
    }
  }

  static TimeDelta GetNudgeDelayTimeDeltaFromType(
      const NudgeDelayStrategy& delay_type, const ModelType& model_type,
      const SyncManagerImpl* core) {
    CHECK(core);
    TimeDelta delay = TimeDelta::FromMilliseconds(
       kDefaultNudgeDelayMilliseconds);
    switch (delay_type) {
     case IMMEDIATE:
       delay = TimeDelta::FromMilliseconds(
           kDefaultNudgeDelayMilliseconds);
       break;
     case ACCOMPANY_ONLY:
       delay = TimeDelta::FromSeconds(kDefaultShortPollIntervalSeconds);
       break;
     case CUSTOM:
       switch (model_type) {
         case PREFERENCES:
           delay = TimeDelta::FromMilliseconds(
               kPreferencesNudgeDelayMilliseconds);
           break;
         case SESSIONS:
           delay = core->scheduler()->GetSessionsCommitDelay();
           break;
         default:
           NOTREACHED();
       }
       break;
     default:
       NOTREACHED();
    }
    return delay;
  }
};

SyncManagerImpl::SyncManagerImpl(const std::string& name)
    : name_(name),
      weak_ptr_factory_(ALLOW_THIS_IN_INITIALIZER_LIST(this)),
      change_delegate_(NULL),
      initialized_(false),
      observing_ip_address_changes_(false),
      notifications_disabled_reason_(TRANSIENT_NOTIFICATION_ERROR),
      throttled_data_type_tracker_(&allstatus_),
      traffic_recorder_(kMaxMessagesToRecord, kMaxMessageSizeToRecord),
      encryptor_(NULL),
      unrecoverable_error_handler_(NULL),
      report_unrecoverable_error_function_(NULL),
      nigori_overwrite_count_(0) {
  // Pre-fill |notification_info_map_|.
  for (int i = FIRST_REAL_MODEL_TYPE; i < MODEL_TYPE_COUNT; ++i) {
    notification_info_map_.insert(
        std::make_pair(ModelTypeFromInt(i), NotificationInfo()));
  }

  // Bind message handlers.
  BindJsMessageHandler(
      "getNotificationState",
      &SyncManagerImpl::GetNotificationState);
  BindJsMessageHandler(
      "getNotificationInfo",
      &SyncManagerImpl::GetNotificationInfo);
  BindJsMessageHandler(
      "getRootNodeDetails",
      &SyncManagerImpl::GetRootNodeDetails);
  BindJsMessageHandler(
      "getNodeSummariesById",
      &SyncManagerImpl::GetNodeSummariesById);
  BindJsMessageHandler(
     "getNodeDetailsById",
      &SyncManagerImpl::GetNodeDetailsById);
  BindJsMessageHandler(
      "getAllNodes",
      &SyncManagerImpl::GetAllNodes);
  BindJsMessageHandler(
      "getChildNodeIds",
      &SyncManagerImpl::GetChildNodeIds);
  BindJsMessageHandler(
      "getClientServerTraffic",
      &SyncManagerImpl::GetClientServerTraffic);
}

SyncManagerImpl::~SyncManagerImpl() {
  DCHECK(thread_checker_.CalledOnValidThread());
  CHECK(!initialized_);
}

SyncManagerImpl::NotificationInfo::NotificationInfo() : total_count(0) {}
SyncManagerImpl::NotificationInfo::~NotificationInfo() {}

DictionaryValue* SyncManagerImpl::NotificationInfo::ToValue() const {
  DictionaryValue* value = new DictionaryValue();
  value->SetInteger("totalCount", total_count);
  value->SetString("payload", payload);
  return value;
}

bool SyncManagerImpl::VisiblePositionsDiffer(
    const syncable::EntryKernelMutation& mutation) const {
  const syncable::EntryKernel& a = mutation.original;
  const syncable::EntryKernel& b = mutation.mutated;
  // If the datatype isn't one where the browser model cares about position,
  // don't bother notifying that data model of position-only changes.
  if (!ShouldMaintainPosition(GetModelTypeFromSpecifics(b.ref(SPECIFICS)))) {
    return false;
  }
  if (a.ref(syncable::NEXT_ID) != b.ref(syncable::NEXT_ID))
    return true;
  if (a.ref(syncable::PARENT_ID) != b.ref(syncable::PARENT_ID))
    return true;
  return false;
}

bool SyncManagerImpl::VisiblePropertiesDiffer(
    const syncable::EntryKernelMutation& mutation,
    Cryptographer* cryptographer) const {
  const syncable::EntryKernel& a = mutation.original;
  const syncable::EntryKernel& b = mutation.mutated;
  const sync_pb::EntitySpecifics& a_specifics = a.ref(SPECIFICS);
  const sync_pb::EntitySpecifics& b_specifics = b.ref(SPECIFICS);
  DCHECK_EQ(GetModelTypeFromSpecifics(a_specifics),
            GetModelTypeFromSpecifics(b_specifics));
  ModelType model_type = GetModelTypeFromSpecifics(b_specifics);
  // Suppress updates to items that aren't tracked by any browser model.
  if (model_type < FIRST_REAL_MODEL_TYPE ||
      !a.ref(syncable::UNIQUE_SERVER_TAG).empty()) {
    return false;
  }
  if (a.ref(syncable::IS_DIR) != b.ref(syncable::IS_DIR))
    return true;
  if (!AreSpecificsEqual(cryptographer,
                         a.ref(syncable::SPECIFICS),
                         b.ref(syncable::SPECIFICS))) {
    return true;
  }
  // We only care if the name has changed if neither specifics is encrypted
  // (encrypted nodes blow away the NON_UNIQUE_NAME).
  if (!a_specifics.has_encrypted() && !b_specifics.has_encrypted() &&
      a.ref(syncable::NON_UNIQUE_NAME) != b.ref(syncable::NON_UNIQUE_NAME))
    return true;
  if (VisiblePositionsDiffer(mutation))
    return true;
  return false;
}

bool SyncManagerImpl::ChangeBuffersAreEmpty() {
  for (int i = 0; i < MODEL_TYPE_COUNT; ++i) {
    if (!change_buffers_[i].IsEmpty())
      return false;
  }
  return true;
}

void SyncManagerImpl::ThrowUnrecoverableError() {
  DCHECK(thread_checker_.CalledOnValidThread());
  ReadTransaction trans(FROM_HERE, GetUserShare());
  trans.GetWrappedTrans()->OnUnrecoverableError(
      FROM_HERE, "Simulating unrecoverable error for testing purposes.");
}

ModelTypeSet SyncManagerImpl::InitialSyncEndedTypes() {
  return directory()->initial_sync_ended_types();
}

ModelTypeSet SyncManagerImpl::GetTypesWithEmptyProgressMarkerToken(
    ModelTypeSet types) {
  ModelTypeSet result;
  for (ModelTypeSet::Iterator i = types.First(); i.Good(); i.Inc()) {
    sync_pb::DataTypeProgressMarker marker;
    directory()->GetDownloadProgress(i.Get(), &marker);

    if (marker.token().empty())
      result.Put(i.Get());

  }
  return result;
}

void SyncManagerImpl::EnableEncryptEverything() {
  DCHECK(thread_checker_.CalledOnValidThread());
  {
    // Update the cryptographer to know we're now encrypting everything.
    WriteTransaction trans(FROM_HERE, GetUserShare());
    Cryptographer* cryptographer = trans.GetCryptographer();
    // Only set encrypt everything if we know we can encrypt. This allows the
    // user to cancel encryption if they have forgotten their passphrase.
    if (cryptographer->is_ready())
      cryptographer->set_encrypt_everything();
  }

  // Reads from cryptographer so will automatically encrypt all
  // datatypes and update the nigori node as necessary. Will trigger
  // OnPassphraseRequired if necessary.
  RefreshEncryption();
}

void SyncManagerImpl::ConfigureSyncer(
    ConfigureReason reason,
    const ModelTypeSet& types_to_config,
    const ModelSafeRoutingInfo& new_routing_info,
    const base::Closure& ready_task,
    const base::Closure& retry_task) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(!ready_task.is_null());
  DCHECK(!retry_task.is_null());

  // Cleanup any types that might have just been disabled.
  ModelTypeSet previous_types = ModelTypeSet::All();
  if (!session_context_->routing_info().empty())
    previous_types = GetRoutingInfoTypes(session_context_->routing_info());
  if (!PurgeDisabledTypes(previous_types,
                          GetRoutingInfoTypes(new_routing_info))) {
    // We failed to cleanup the types. Invoke the ready task without actually
    // configuring any types. The caller should detect this as a configuration
    // failure and act appropriately.
    ready_task.Run();
    return;
  }

  ConfigurationParams params(GetSourceFromReason(reason),
                             types_to_config,
                             new_routing_info,
                             ready_task);

  scheduler_->Start(SyncScheduler::CONFIGURATION_MODE);
  if (!scheduler_->ScheduleConfiguration(params))
    retry_task.Run();

}

bool SyncManagerImpl::Init(
    const FilePath& database_location,
    const WeakHandle<JsEventHandler>& event_handler,
    const std::string& sync_server_and_path,
    int port,
    bool use_ssl,
    const scoped_refptr<base::TaskRunner>& blocking_task_runner,
    scoped_ptr<HttpPostProviderFactory> post_factory,
    const std::vector<ModelSafeWorker*>& workers,
    ExtensionsActivityMonitor* extensions_activity_monitor,
    SyncManager::ChangeDelegate* change_delegate,
    const SyncCredentials& credentials,
    scoped_ptr<SyncNotifier> sync_notifier,
    const std::string& restored_key_for_bootstrapping,
    const std::string& restored_keystore_key_for_bootstrapping,
    bool keystore_encryption_enabled,
    scoped_ptr<InternalComponentsFactory> internal_components_factory,
    Encryptor* encryptor,
    UnrecoverableErrorHandler* unrecoverable_error_handler,
    ReportUnrecoverableErrorFunction report_unrecoverable_error_function) {
  CHECK(!initialized_);
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(post_factory.get());
  DCHECK(!credentials.email.empty());
  DCHECK(!credentials.sync_token.empty());
  DVLOG(1) << "SyncManager starting Init...";

  weak_handle_this_ = MakeWeakHandle(weak_ptr_factory_.GetWeakPtr());

  blocking_task_runner_ = blocking_task_runner;

  change_delegate_ = change_delegate;

  sync_notifier_ = sync_notifier.Pass();
  sync_notifier_->RegisterHandler(this);

  AddObserver(&js_sync_manager_observer_);
  SetJsEventHandler(event_handler);

  AddObserver(&debug_info_event_listener_);

  database_path_ = database_location.Append(
      syncable::Directory::kSyncDatabaseFilename);
  encryptor_ = encryptor;
  unrecoverable_error_handler_ = unrecoverable_error_handler;
  report_unrecoverable_error_function_ = report_unrecoverable_error_function;

  FilePath absolute_db_path(database_path_);
  file_util::AbsolutePath(&absolute_db_path);
  scoped_ptr<syncable::DirectoryBackingStore> backing_store =
      internal_components_factory->BuildDirectoryBackingStore(
          credentials.email, absolute_db_path).Pass();

  DCHECK(backing_store.get());
  share_.name = credentials.email;
  share_.directory.reset(
      new syncable::Directory(encryptor_,
                              unrecoverable_error_handler_,
                              report_unrecoverable_error_function_,
                              backing_store.release()));

  DVLOG(1) << "Username: " << username_for_share();
  if (!OpenDirectory()) {
    FOR_EACH_OBSERVER(SyncManager::Observer, observers_,
                      OnInitializationComplete(
                          MakeWeakHandle(weak_ptr_factory_.GetWeakPtr()),
                          false, syncer::ModelTypeSet()));
    return false;
  }

  connection_manager_.reset(new SyncAPIServerConnectionManager(
      sync_server_and_path, port, use_ssl, post_factory.release()));
  connection_manager_->set_client_id(directory()->cache_guid());
  connection_manager_->AddListener(this);

  // Retrieve and set the sync notifier state.
  std::string unique_id = directory()->cache_guid();
  DVLOG(1) << "Read notification unique ID: " << unique_id;
  allstatus_.SetUniqueId(unique_id);
  sync_notifier_->SetUniqueId(unique_id);

  std::string state = directory()->GetNotificationState();
  if (VLOG_IS_ON(1)) {
    std::string encoded_state;
    base::Base64Encode(state, &encoded_state);
    DVLOG(1) << "Read notification state: " << encoded_state;
  }

  // TODO(tim): Remove once invalidation state has been migrated to new
  // InvalidationStateTracker store. Bug 124140.
  sync_notifier_->SetStateDeprecated(state);

  // Build a SyncSessionContext and store the worker in it.
  DVLOG(1) << "Sync is bringing up SyncSessionContext.";
  std::vector<SyncEngineEventListener*> listeners;
  listeners.push_back(&allstatus_);
  listeners.push_back(this);
  session_context_ = internal_components_factory->BuildContext(
      connection_manager_.get(),
      directory(),
      workers,
      extensions_activity_monitor,
      &throttled_data_type_tracker_,
      listeners,
      &debug_info_event_listener_,
      &traffic_recorder_,
      keystore_encryption_enabled).Pass();
  session_context_->set_account_name(credentials.email);
  scheduler_ = internal_components_factory->BuildScheduler(
      name_, session_context_.get()).Pass();

  scheduler_->Start(SyncScheduler::CONFIGURATION_MODE);

  initialized_ = true;

  net::NetworkChangeNotifier::AddIPAddressObserver(this);
  observing_ip_address_changes_ = true;

  UpdateCredentials(credentials);

  // Cryptographer should only be accessed while holding a
  // transaction.  Grabbing the user share for the transaction
  // checks the initialization state, so this must come after
  // |initialized_| is set to true.
  ReadTransaction trans(FROM_HERE, GetUserShare());
  trans.GetCryptographer()->Bootstrap(restored_key_for_bootstrapping);
  trans.GetCryptographer()->BootstrapKeystoreKey(
      restored_keystore_key_for_bootstrapping);
  trans.GetCryptographer()->AddObserver(this);

  FOR_EACH_OBSERVER(SyncManager::Observer, observers_,
                    OnInitializationComplete(
                        MakeWeakHandle(weak_ptr_factory_.GetWeakPtr()),
                        true, InitialSyncEndedTypes()));
  return true;
}

void SyncManagerImpl::RefreshNigori(const std::string& chrome_version,
                                    const base::Closure& done_callback) {
  DCHECK(initialized_);
  DCHECK(thread_checker_.CalledOnValidThread());
  GetSessionName(
      blocking_task_runner_,
      base::Bind(
          &SyncManagerImpl::UpdateCryptographerAndNigoriCallback,
          weak_ptr_factory_.GetWeakPtr(),
          chrome_version,
          done_callback));
}

void SyncManagerImpl::UpdateNigoriEncryptionState(
    Cryptographer* cryptographer,
    WriteNode* nigori_node) {
  DCHECK(nigori_node);
  sync_pb::NigoriSpecifics nigori = nigori_node->GetNigoriSpecifics();

  if (cryptographer->is_ready() &&
      nigori_overwrite_count_ < kNigoriOverwriteLimit) {
    // Does not modify the encrypted blob if the unencrypted data already
    // matches what is about to be written.
    sync_pb::EncryptedData original_keys = nigori.encrypted();
    if (!cryptographer->GetKeys(nigori.mutable_encrypted()))
      NOTREACHED();

    if (nigori.encrypted().SerializeAsString() !=
        original_keys.SerializeAsString()) {
      // We've updated the nigori node's encryption keys. In order to prevent
      // a possible looping of two clients constantly overwriting each other,
      // we limit the absolute number of overwrites per client instantiation.
      nigori_overwrite_count_++;
      UMA_HISTOGRAM_COUNTS("Sync.AutoNigoriOverwrites",
                           nigori_overwrite_count_);
    }

    // Note: we don't try to set using_explicit_passphrase here since if that
    // is lost the user can always set it again. The main point is to preserve
    // the encryption keys so all data remains decryptable.
  }
  cryptographer->UpdateNigoriFromEncryptedTypes(&nigori);

  // If nothing has changed, this is a no-op.
  nigori_node->SetNigoriSpecifics(nigori);
}

void SyncManagerImpl::UpdateCryptographerAndNigoriCallback(
    const std::string& chrome_version,
    const base::Closure& done_callback,
    const std::string& session_name) {
  if (!directory()->initial_sync_ended_for_type(NIGORI)) {
    done_callback.Run();  // Should only happen during first time sync.
    return;
  }

  bool success = false;
  {
    WriteTransaction trans(FROM_HERE, GetUserShare());
    Cryptographer* cryptographer = trans.GetCryptographer();
    WriteNode node(&trans);

    if (node.InitByTagLookup(kNigoriTag) == BaseNode::INIT_OK) {
      sync_pb::NigoriSpecifics nigori(node.GetNigoriSpecifics());
      Cryptographer::UpdateResult result = cryptographer->Update(nigori);
      if (result == Cryptographer::NEEDS_PASSPHRASE) {
        sync_pb::EncryptedData pending_keys;
        if (cryptographer->has_pending_keys())
          pending_keys = cryptographer->GetPendingKeys();
        FOR_EACH_OBSERVER(SyncManager::Observer, observers_,
                          OnPassphraseRequired(REASON_DECRYPTION,
                                               pending_keys));
      }

      // Add or update device information.
      bool contains_this_device = false;
      for (int i = 0; i < nigori.device_information_size(); ++i) {
        const sync_pb::DeviceInformation& device_information =
            nigori.device_information(i);
        if (device_information.cache_guid() == directory()->cache_guid()) {
          // Update the version number in case it changed due to an update.
          if (device_information.chrome_version() != chrome_version) {
            sync_pb::DeviceInformation* mutable_device_information =
                nigori.mutable_device_information(i);
            mutable_device_information->set_chrome_version(
                chrome_version);
          }
          contains_this_device = true;
        }
      }

      if (!contains_this_device) {
        sync_pb::DeviceInformation* device_information =
            nigori.add_device_information();
        device_information->set_cache_guid(directory()->cache_guid());
#if defined(OS_CHROMEOS)
        device_information->set_platform("ChromeOS");
#elif defined(OS_LINUX)
        device_information->set_platform("Linux");
#elif defined(OS_MACOSX)
        device_information->set_platform("Mac");
#elif defined(OS_WIN)
        device_information->set_platform("Windows");
#endif
        device_information->set_name(session_name);
        device_information->set_chrome_version(chrome_version);
      }
      // Disabled to avoid nigori races. TODO(zea): re-enable. crbug.com/122837
      // node.SetNigoriSpecifics(nigori);

      // Make sure the nigori node has the up to date encryption info.
      UpdateNigoriEncryptionState(cryptographer, &node);

      NotifyCryptographerState(cryptographer);
      allstatus_.SetEncryptedTypes(cryptographer->GetEncryptedTypes());

      success = cryptographer->is_ready();
    } else {
      NOTREACHED();
    }
  }

  if (success)
    RefreshEncryption();
  done_callback.Run();
}

void SyncManagerImpl::NotifyCryptographerState(Cryptographer * cryptographer) {
  // TODO(lipalani): Explore the possibility of hooking this up to
  // SyncManager::Observer and making |AllStatus| a listener for that.
  allstatus_.SetCryptographerReady(cryptographer->is_ready());
  allstatus_.SetCryptoHasPendingKeys(cryptographer->has_pending_keys());
  debug_info_event_listener_.SetCryptographerReady(cryptographer->is_ready());
  debug_info_event_listener_.SetCrytographerHasPendingKeys(
      cryptographer->has_pending_keys());
}

void SyncManagerImpl::StartSyncingNormally(
    const ModelSafeRoutingInfo& routing_info) {
  // Start the sync scheduler.
  // TODO(sync): We always want the newest set of routes when we switch back
  // to normal mode. Figure out how to enforce set_routing_info is always
  // appropriately set and that it's only modified when switching to normal
  // mode.
  DCHECK(thread_checker_.CalledOnValidThread());
  session_context_->set_routing_info(routing_info);
  scheduler_->Start(SyncScheduler::NORMAL_MODE);
}

syncable::Directory* SyncManagerImpl::directory() {
  return share_.directory.get();
}

const SyncScheduler* SyncManagerImpl::scheduler() const {
  return scheduler_.get();
}

bool SyncManagerImpl::OpenDirectory() {
  DCHECK(!initialized_) << "Should only happen once";

  // Set before Open().
  change_observer_ = MakeWeakHandle(js_mutation_event_observer_.AsWeakPtr());
  WeakHandle<syncable::TransactionObserver> transaction_observer(
      MakeWeakHandle(js_mutation_event_observer_.AsWeakPtr()));

  syncable::DirOpenResult open_result = syncable::NOT_INITIALIZED;
  open_result = directory()->Open(username_for_share(), this,
                                  transaction_observer);
  if (open_result != syncable::OPENED) {
    LOG(ERROR) << "Could not open share for:" << username_for_share();
    return false;
  }

  // Unapplied datatypes (those that do not have initial sync ended set) get
  // re-downloaded during any configuration. But, it's possible for a datatype
  // to have a progress marker but not have initial sync ended yet, making
  // it a candidate for migration. This is a problem, as the DataTypeManager
  // does not support a migration while it's already in the middle of a
  // configuration. As a result, any partially synced datatype can stall the
  // DTM, waiting for the configuration to complete, which it never will due
  // to the migration error. In addition, a partially synced nigori will
  // trigger the migration logic before the backend is initialized, resulting
  // in crashes. We therefore detect and purge any partially synced types as
  // part of initialization.
  if (!PurgePartiallySyncedTypes())
    return false;

  return true;
}

bool SyncManagerImpl::PurgePartiallySyncedTypes() {
  ModelTypeSet partially_synced_types = ModelTypeSet::All();
  partially_synced_types.RemoveAll(InitialSyncEndedTypes());
  partially_synced_types.RemoveAll(GetTypesWithEmptyProgressMarkerToken(
      ModelTypeSet::All()));

  UMA_HISTOGRAM_COUNTS("Sync.PartiallySyncedTypes",
                       partially_synced_types.Size());
  if (partially_synced_types.Empty())
    return true;
  return directory()->PurgeEntriesWithTypeIn(partially_synced_types);
}

bool SyncManagerImpl::PurgeDisabledTypes(
    ModelTypeSet previously_enabled_types,
    ModelTypeSet currently_enabled_types) {
  ModelTypeSet disabled_types = Difference(previously_enabled_types,
                                           currently_enabled_types);
  if (disabled_types.Empty())
    return true;

  DVLOG(1) << "Purging disabled types "
           << ModelTypeSetToString(disabled_types);
  return directory()->PurgeEntriesWithTypeIn(disabled_types);
}

void SyncManagerImpl::UpdateCredentials(
    const SyncCredentials& credentials) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(initialized_);
  DCHECK_EQ(credentials.email, share_.name);
  DCHECK(!credentials.email.empty());
  DCHECK(!credentials.sync_token.empty());

  observing_ip_address_changes_ = true;
  if (!connection_manager_->set_auth_token(credentials.sync_token))
    return;  // Auth token is known to be invalid, so exit early.

  sync_notifier_->UpdateCredentials(credentials.email, credentials.sync_token);
  scheduler_->OnCredentialsUpdated();
}

void SyncManagerImpl::UpdateEnabledTypes(
    const ModelTypeSet& enabled_types) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(initialized_);
  sync_notifier_->UpdateRegisteredIds(
      this,
      ModelTypeSetToObjectIdSet(enabled_types));
}

void SyncManagerImpl::RegisterInvalidationHandler(
    SyncNotifierObserver* handler) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(initialized_);
  sync_notifier_->RegisterHandler(handler);
}

void SyncManagerImpl::UpdateRegisteredInvalidationIds(
    SyncNotifierObserver* handler,
    const ObjectIdSet& ids) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(initialized_);
  sync_notifier_->UpdateRegisteredIds(handler, ids);
}

void SyncManagerImpl::UnregisterInvalidationHandler(
    SyncNotifierObserver* handler) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(initialized_);
  sync_notifier_->UnregisterHandler(handler);
}

void SyncManagerImpl::SetEncryptionPassphrase(
    const std::string& passphrase,
    bool is_explicit) {
  DCHECK(thread_checker_.CalledOnValidThread());
  // We do not accept empty passphrases.
  if (passphrase.empty()) {
    NOTREACHED() << "Cannot encrypt with an empty passphrase.";
    return;
  }

  // All accesses to the cryptographer are protected by a transaction.
  WriteTransaction trans(FROM_HERE, GetUserShare());
  Cryptographer* cryptographer = trans.GetCryptographer();
  KeyParams key_params = {"localhost", "dummy", passphrase};
  WriteNode node(&trans);
  if (node.InitByTagLookup(kNigoriTag) != BaseNode::INIT_OK) {
    // TODO(albertb): Plumb an UnrecoverableError all the way back to the PSS.
    NOTREACHED();
    return;
  }

  bool nigori_has_explicit_passphrase =
      node.GetNigoriSpecifics().using_explicit_passphrase();
  std::string bootstrap_token;
  sync_pb::EncryptedData pending_keys;
  if (cryptographer->has_pending_keys())
    pending_keys = cryptographer->GetPendingKeys();
  bool success = false;


  // There are six cases to handle here:
  // 1. The user has no pending keys and is setting their current GAIA password
  //    as the encryption passphrase. This happens either during first time sync
  //    with a clean profile, or after re-authenticating on a profile that was
  //    already signed in with the cryptographer ready.
  // 2. The user has no pending keys, and is overwriting an (already provided)
  //    implicit passphrase with an explicit (custom) passphrase.
  // 3. The user has pending keys for an explicit passphrase that is somehow set
  //    to their current GAIA passphrase.
  // 4. The user has pending keys encrypted with their current GAIA passphrase
  //    and the caller passes in the current GAIA passphrase.
  // 5. The user has pending keys encrypted with an older GAIA passphrase
  //    and the caller passes in the current GAIA passphrase.
  // 6. The user has previously done encryption with an explicit passphrase.
  // Furthermore, we enforce the fact that the bootstrap encryption token will
  // always be derived from the newest GAIA password if the account is using
  // an implicit passphrase (even if the data is encrypted with an old GAIA
  // password). If the account is using an explicit (custom) passphrase, the
  // bootstrap token will be derived from the most recently provided explicit
  // passphrase (that was able to decrypt the data).
  if (!nigori_has_explicit_passphrase) {
    if (!cryptographer->has_pending_keys()) {
      if (cryptographer->AddKey(key_params)) {
        // Case 1 and 2. We set a new GAIA passphrase when there are no pending
        // keys (1), or overwriting an implicit passphrase with a new explicit
        // one (2) when there are no pending keys.
        DVLOG(1) << "Setting " << (is_explicit ? "explicit" : "implicit" )
                 << " passphrase for encryption.";
        cryptographer->GetBootstrapToken(&bootstrap_token);
        success = true;
      } else {
        NOTREACHED() << "Failed to add key to cryptographer.";
        success = false;
      }
    } else {  // cryptographer->has_pending_keys() == true
      if (is_explicit) {
        // This can only happen if the nigori node is updated with a new
        // implicit passphrase while a client is attempting to set a new custom
        // passphrase (race condition).
        DVLOG(1) << "Failing because an implicit passphrase is already set.";
        success = false;
      } else {  // is_explicit == false
        if (cryptographer->DecryptPendingKeys(key_params)) {
          // Case 4. We successfully decrypted with the implicit GAIA passphrase
          // passed in.
          DVLOG(1) << "Implicit internal passphrase accepted for decryption.";
          cryptographer->GetBootstrapToken(&bootstrap_token);
          success = true;
        } else {
          // Case 5. Encryption was done with an old GAIA password, but we were
          // provided with the current GAIA password. We need to generate a new
          // bootstrap token to preserve it. We build a temporary cryptographer
          // to allow us to extract these params without polluting our current
          // cryptographer.
          DVLOG(1) << "Implicit internal passphrase failed to decrypt, adding "
                   << "anyways as default passphrase and persisting via "
                   << "bootstrap token.";
          Cryptographer temp_cryptographer(encryptor_);
          temp_cryptographer.AddKey(key_params);
          temp_cryptographer.GetBootstrapToken(&bootstrap_token);
          // We then set the new passphrase as the default passphrase of the
          // real cryptographer, even though we have pending keys. This is safe,
          // as although Cryptographer::is_initialized() will now be true,
          // is_ready() will remain false due to having pending keys.
          cryptographer->AddKey(key_params);
          success = false;
        }
      }  // is_explicit
    }  // cryptographer->has_pending_keys()
  } else {  // nigori_has_explicit_passphrase == true
    // Case 6. We do not want to override a previously set explicit passphrase,
    // so we return a failure.
    DVLOG(1) << "Failing because an explicit passphrase is already set.";
    success = false;
  }

  DVLOG_IF(1, !success)
      << "Failure in SetEncryptionPassphrase; notifying and returning.";
  DVLOG_IF(1, success)
      << "Successfully set encryption passphrase; updating nigori and "
         "reencrypting.";

  FinishSetPassphrase(
      success, bootstrap_token, is_explicit, &trans, &node);
}

void SyncManagerImpl::SetDecryptionPassphrase(
    const std::string& passphrase) {
  DCHECK(thread_checker_.CalledOnValidThread());
  // We do not accept empty passphrases.
  if (passphrase.empty()) {
    NOTREACHED() << "Cannot decrypt with an empty passphrase.";
    return;
  }

  // All accesses to the cryptographer are protected by a transaction.
  WriteTransaction trans(FROM_HERE, GetUserShare());
  Cryptographer* cryptographer = trans.GetCryptographer();
  KeyParams key_params = {"localhost", "dummy", passphrase};
  WriteNode node(&trans);
  if (node.InitByTagLookup(kNigoriTag) != BaseNode::INIT_OK) {
    // TODO(albertb): Plumb an UnrecoverableError all the way back to the PSS.
    NOTREACHED();
    return;
  }

  if (!cryptographer->has_pending_keys()) {
    // Note that this *can* happen in a rare situation where data is
    // re-encrypted on another client while a SetDecryptionPassphrase() call is
    // in-flight on this client. It is rare enough that we choose to do nothing.
    NOTREACHED() << "Attempt to set decryption passphrase failed because there "
                 << "were no pending keys.";
    return;
  }

  bool nigori_has_explicit_passphrase =
      node.GetNigoriSpecifics().using_explicit_passphrase();
  std::string bootstrap_token;
  sync_pb::EncryptedData pending_keys;
  pending_keys = cryptographer->GetPendingKeys();
  bool success = false;

  // There are three cases to handle here:
  // 7. We're using the current GAIA password to decrypt the pending keys. This
  //    happens when signing in to an account with a previously set implicit
  //    passphrase, where the data is already encrypted with the newest GAIA
  //    password.
  // 8. The user is providing an old GAIA password to decrypt the pending keys.
  //    In this case, the user is using an implicit passphrase, but has changed
  //    their password since they last encrypted their data, and therefore
  //    their current GAIA password was unable to decrypt the data. This will
  //    happen when the user is setting up a new profile with a previously
  //    encrypted account (after changing passwords).
  // 9. The user is providing a previously set explicit passphrase to decrypt
  //    the pending keys.
  if (!nigori_has_explicit_passphrase) {
    if (cryptographer->is_initialized()) {
      // We only want to change the default encryption key to the pending
      // one if the pending keybag already contains the current default.
      // This covers the case where a different client re-encrypted
      // everything with a newer gaia passphrase (and hence the keybag
      // contains keys from all previously used gaia passphrases).
      // Otherwise, we're in a situation where the pending keys are
      // encrypted with an old gaia passphrase, while the default is the
      // current gaia passphrase. In that case, we preserve the default.
      Cryptographer temp_cryptographer(encryptor_);
      temp_cryptographer.SetPendingKeys(cryptographer->GetPendingKeys());
      if (temp_cryptographer.DecryptPendingKeys(key_params)) {
        // Check to see if the pending bag of keys contains the current
        // default key.
        sync_pb::EncryptedData encrypted;
        cryptographer->GetKeys(&encrypted);
        if (temp_cryptographer.CanDecrypt(encrypted)) {
          DVLOG(1) << "Implicit user provided passphrase accepted for "
                   << "decryption, overwriting default.";
          // Case 7. The pending keybag contains the current default. Go ahead
          // and update the cryptographer, letting the default change.
          cryptographer->DecryptPendingKeys(key_params);
          cryptographer->GetBootstrapToken(&bootstrap_token);
          success = true;
        } else {
          // Case 8. The pending keybag does not contain the current default
          // encryption key. We decrypt the pending keys here, and in
          // FinishSetPassphrase, re-encrypt everything with the current GAIA
          // passphrase instead of the passphrase just provided by the user.
          DVLOG(1) << "Implicit user provided passphrase accepted for "
                   << "decryption, restoring implicit internal passphrase "
                   << "as default.";
          std::string bootstrap_token_from_current_key;
          cryptographer->GetBootstrapToken(
              &bootstrap_token_from_current_key);
          cryptographer->DecryptPendingKeys(key_params);
          // Overwrite the default from the pending keys.
          cryptographer->AddKeyFromBootstrapToken(
              bootstrap_token_from_current_key);
          success = true;
        }
      } else {  // !temp_cryptographer.DecryptPendingKeys(..)
        DVLOG(1) << "Implicit user provided passphrase failed to decrypt.";
        success = false;
      }  // temp_cryptographer.DecryptPendingKeys(...)
    } else {  // cryptographer->is_initialized() == false
      if (cryptographer->DecryptPendingKeys(key_params)) {
        // This can happpen in two cases:
        // - First time sync on android, where we'll never have a
        //   !user_provided passphrase.
        // - This is a restart for a client that lost their bootstrap token.
        // In both cases, we should go ahead and initialize the cryptographer
        // and persist the new bootstrap token.
        //
        // Note: at this point, we cannot distinguish between cases 7 and 8
        // above. This user provided passphrase could be the current or the
        // old. But, as long as we persist the token, there's nothing more
        // we can do.
        cryptographer->GetBootstrapToken(&bootstrap_token);
        DVLOG(1) << "Implicit user provided passphrase accepted, initializing"
                 << " cryptographer.";
        success = true;
      } else {
        DVLOG(1) << "Implicit user provided passphrase failed to decrypt.";
        success = false;
      }
    }  // cryptographer->is_initialized()
  } else {  // nigori_has_explicit_passphrase == true
    // Case 9. Encryption was done with an explicit passphrase, and we decrypt
    // with the passphrase provided by the user.
    if (cryptographer->DecryptPendingKeys(key_params)) {
      DVLOG(1) << "Explicit passphrase accepted for decryption.";
      cryptographer->GetBootstrapToken(&bootstrap_token);
      success = true;
    } else {
      DVLOG(1) << "Explicit passphrase failed to decrypt.";
      success = false;
    }
  }  // nigori_has_explicit_passphrase

  DVLOG_IF(1, !success)
      << "Failure in SetDecryptionPassphrase; notifying and returning.";
  DVLOG_IF(1, success)
      << "Successfully set decryption passphrase; updating nigori and "
         "reencrypting.";

  FinishSetPassphrase(success,
                      bootstrap_token,
                      nigori_has_explicit_passphrase,
                      &trans,
                      &node);
}

void SyncManagerImpl::FinishSetPassphrase(
    bool success,
    const std::string& bootstrap_token,
    bool is_explicit,
    WriteTransaction* trans,
    WriteNode* nigori_node) {
  Cryptographer* cryptographer = trans->GetCryptographer();
  NotifyCryptographerState(cryptographer);

  // It's possible we need to change the bootstrap token even if we failed to
  // set the passphrase (for example if we need to preserve the new GAIA
  // passphrase).
  if (!bootstrap_token.empty()) {
    DVLOG(1) << "Bootstrap token updated.";
    FOR_EACH_OBSERVER(SyncManager::Observer, observers_,
                      OnBootstrapTokenUpdated(bootstrap_token));
  }

  if (!success) {
    if (cryptographer->is_ready()) {
      LOG(ERROR) << "Attempt to change passphrase failed while cryptographer "
                 << "was ready.";
    } else if (cryptographer->has_pending_keys()) {
      FOR_EACH_OBSERVER(SyncManager::Observer, observers_,
                        OnPassphraseRequired(REASON_DECRYPTION,
                                             cryptographer->GetPendingKeys()));
    } else {
      FOR_EACH_OBSERVER(SyncManager::Observer, observers_,
                        OnPassphraseRequired(REASON_ENCRYPTION,
                                             sync_pb::EncryptedData()));
    }
    return;
  }

  FOR_EACH_OBSERVER(SyncManager::Observer, observers_,
                    OnPassphraseAccepted());
  DCHECK(cryptographer->is_ready());

  // TODO(tim): Bug 58231. It would be nice if setting a passphrase didn't
  // require messing with the Nigori node, because we can't set a passphrase
  // until download conditions are met vs Cryptographer init.  It seems like
  // it's safe to defer this work.
  sync_pb::NigoriSpecifics specifics(nigori_node->GetNigoriSpecifics());
  // Does not modify specifics.encrypted() if the original decrypted data was
  // the same.
  if (!cryptographer->GetKeys(specifics.mutable_encrypted())) {
    NOTREACHED();
    return;
  }
  specifics.set_using_explicit_passphrase(is_explicit);
  nigori_node->SetNigoriSpecifics(specifics);

  // Does nothing if everything is already encrypted or the cryptographer has
  // pending keys.
  ReEncryptEverything(trans);
}

bool SyncManagerImpl::IsUsingExplicitPassphrase() {
  ReadTransaction trans(FROM_HERE, &share_);
  ReadNode node(&trans);
  if (node.InitByTagLookup(kNigoriTag) != BaseNode::INIT_OK) {
    // TODO(albertb): Plumb an UnrecoverableError all the way back to the PSS.
    NOTREACHED();
    return false;
  }

  return node.GetNigoriSpecifics().using_explicit_passphrase();
}

bool SyncManagerImpl::GetKeystoreKeyBootstrapToken(std::string* token) {
  ReadTransaction trans(FROM_HERE, GetUserShare());
  return trans.GetCryptographer()->GetKeystoreKeyBootstrapToken(token);
}

void SyncManagerImpl::RefreshEncryption() {
  DCHECK(initialized_);

  WriteTransaction trans(FROM_HERE, GetUserShare());
  WriteNode node(&trans);
  if (node.InitByTagLookup(kNigoriTag) != BaseNode::INIT_OK) {
    NOTREACHED() << "Unable to set encrypted datatypes because Nigori node not "
                 << "found.";
    return;
  }

  Cryptographer* cryptographer = trans.GetCryptographer();

  if (!cryptographer->is_ready()) {
    DVLOG(1) << "Attempting to encrypt datatypes when cryptographer not "
             << "initialized, prompting for passphrase.";
    // TODO(zea): this isn't really decryption, but that's the only way we have
    // to prompt the user for a passsphrase. See http://crbug.com/91379.
    sync_pb::EncryptedData pending_keys;
    if (cryptographer->has_pending_keys())
      pending_keys = cryptographer->GetPendingKeys();
    FOR_EACH_OBSERVER(SyncManager::Observer, observers_,
                      OnPassphraseRequired(REASON_DECRYPTION,
                                           pending_keys));
    return;
  }

  UpdateNigoriEncryptionState(cryptographer, &node);

  allstatus_.SetEncryptedTypes(cryptographer->GetEncryptedTypes());

  // We reencrypt everything regardless of whether the set of encrypted
  // types changed to ensure that any stray unencrypted entries are overwritten.
  ReEncryptEverything(&trans);
}

// This function iterates over all encrypted types.  There are many scenarios in
// which data for some or all types is not currently available.  In that case,
// the lookup of the root node will fail and we will skip encryption for that
// type.
void SyncManagerImpl::ReEncryptEverything(
      WriteTransaction* trans) {
  Cryptographer* cryptographer = trans->GetCryptographer();
  if (!cryptographer || !cryptographer->is_ready())
    return;
  ModelTypeSet encrypted_types = GetEncryptedTypes(trans);
  for (ModelTypeSet::Iterator iter = encrypted_types.First();
       iter.Good(); iter.Inc()) {
    if (iter.Get() == PASSWORDS || iter.Get() == NIGORI)
      continue; // These types handle encryption differently.

    ReadNode type_root(trans);
    std::string tag = ModelTypeToRootTag(iter.Get());
    if (type_root.InitByTagLookup(tag) != BaseNode::INIT_OK)
      continue; // Don't try to reencrypt if the type's data is unavailable.

    // Iterate through all children of this datatype.
    std::queue<int64> to_visit;
    int64 child_id = type_root.GetFirstChildId();
    to_visit.push(child_id);
    while (!to_visit.empty()) {
      child_id = to_visit.front();
      to_visit.pop();
      if (child_id == kInvalidId)
        continue;

      WriteNode child(trans);
      if (child.InitByIdLookup(child_id) != BaseNode::INIT_OK) {
        NOTREACHED();
        continue;
      }
      if (child.GetIsFolder()) {
        to_visit.push(child.GetFirstChildId());
      }
      if (child.GetEntry()->Get(syncable::UNIQUE_SERVER_TAG).empty()) {
      // Rewrite the specifics of the node with encrypted data if necessary
      // (only rewrite the non-unique folders).
        child.ResetFromSpecifics();
      }
      to_visit.push(child.GetSuccessorId());
    }
  }

  // Passwords are encrypted with their own legacy scheme.  Passwords are always
  // encrypted so we don't need to check GetEncryptedTypes() here.
  ReadNode passwords_root(trans);
  std::string passwords_tag = ModelTypeToRootTag(PASSWORDS);
  if (passwords_root.InitByTagLookup(passwords_tag) == BaseNode::INIT_OK) {
    int64 child_id = passwords_root.GetFirstChildId();
    while (child_id != kInvalidId) {
      WriteNode child(trans);
      if (child.InitByIdLookup(child_id) != BaseNode::INIT_OK) {
        NOTREACHED();
        return;
      }
      child.SetPasswordSpecifics(child.GetPasswordSpecifics());
      child_id = child.GetSuccessorId();
    }
  }

  // NOTE: We notify from within a transaction.
  FOR_EACH_OBSERVER(SyncManager::Observer, observers_,
                    OnEncryptionComplete());
}

void SyncManagerImpl::AddObserver(SyncManager::Observer* observer) {
  DCHECK(thread_checker_.CalledOnValidThread());
  observers_.AddObserver(observer);
}

void SyncManagerImpl::RemoveObserver(SyncManager::Observer* observer) {
  DCHECK(thread_checker_.CalledOnValidThread());
  observers_.RemoveObserver(observer);
}

void SyncManagerImpl::StopSyncingForShutdown(const base::Closure& callback) {
  DVLOG(2) << "StopSyncingForShutdown";
  scheduler_->RequestStop(callback);
  if (connection_manager_.get())
    connection_manager_->TerminateAllIO();
}

void SyncManagerImpl::ShutdownOnSyncThread() {
  DCHECK(thread_checker_.CalledOnValidThread());

  // Prevent any in-flight method calls from running.  Also
  // invalidates |weak_handle_this_| and |change_observer_|.
  weak_ptr_factory_.InvalidateWeakPtrs();
  js_mutation_event_observer_.InvalidateWeakPtrs();

  scheduler_.reset();
  session_context_.reset();

  SetJsEventHandler(WeakHandle<JsEventHandler>());
  RemoveObserver(&js_sync_manager_observer_);

  RemoveObserver(&debug_info_event_listener_);

  // |sync_notifier_| and |connection_manager_| may end up being NULL here in
  // tests (in synchronous initialization mode).
  //
  // TODO(akalin): Fix this behavior.

  if (sync_notifier_.get())
    sync_notifier_->UnregisterHandler(this);
  sync_notifier_.reset();

  if (connection_manager_.get())
    connection_manager_->RemoveListener(this);
  connection_manager_.reset();

  net::NetworkChangeNotifier::RemoveIPAddressObserver(this);
  observing_ip_address_changes_ = false;

  if (initialized_ && directory()) {
    {
      // Cryptographer should only be accessed while holding a
      // transaction.
      ReadTransaction trans(FROM_HERE, GetUserShare());
      trans.GetCryptographer()->RemoveObserver(this);
    }
    directory()->SaveChanges();
  }

  share_.directory.reset();

  change_delegate_ = NULL;

  initialized_ = false;

  // We reset these here, since only now we know they will not be
  // accessed from other threads (since we shut down everything).
  change_observer_.Reset();
  weak_handle_this_.Reset();
}

void SyncManagerImpl::OnIPAddressChanged() {
  DVLOG(1) << "IP address change detected";
  if (!observing_ip_address_changes_) {
    DVLOG(1) << "IP address change dropped.";
    return;
  }

  OnIPAddressChangedImpl();
}

void SyncManagerImpl::OnIPAddressChangedImpl() {
  DCHECK(thread_checker_.CalledOnValidThread());
  scheduler_->OnConnectionStatusChange();
}

void SyncManagerImpl::OnServerConnectionEvent(
    const ServerConnectionEvent& event) {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (event.connection_code ==
      HttpResponse::SERVER_CONNECTION_OK) {
    FOR_EACH_OBSERVER(SyncManager::Observer, observers_,
                      OnConnectionStatusChange(CONNECTION_OK));
  }

  if (event.connection_code == HttpResponse::SYNC_AUTH_ERROR) {
    observing_ip_address_changes_ = false;
    FOR_EACH_OBSERVER(SyncManager::Observer, observers_,
                      OnConnectionStatusChange(CONNECTION_AUTH_ERROR));
  }

  if (event.connection_code == HttpResponse::SYNC_SERVER_ERROR) {
    FOR_EACH_OBSERVER(SyncManager::Observer, observers_,
                      OnConnectionStatusChange(CONNECTION_SERVER_ERROR));
  }
}

void SyncManagerImpl::HandleTransactionCompleteChangeEvent(
    ModelTypeSet models_with_changes) {
  // This notification happens immediately after the transaction mutex is
  // released. This allows work to be performed without blocking other threads
  // from acquiring a transaction.
  if (!change_delegate_)
    return;

  // Call commit.
  for (ModelTypeSet::Iterator it = models_with_changes.First();
       it.Good(); it.Inc()) {
    change_delegate_->OnChangesComplete(it.Get());
    change_observer_.Call(
        FROM_HERE,
        &SyncManager::ChangeObserver::OnChangesComplete,
        it.Get());
  }
}

ModelTypeSet
SyncManagerImpl::HandleTransactionEndingChangeEvent(
    const ImmutableWriteTransactionInfo& write_transaction_info,
    syncable::BaseTransaction* trans) {
  // This notification happens immediately before a syncable WriteTransaction
  // falls out of scope. It happens while the channel mutex is still held,
  // and while the transaction mutex is held, so it cannot be re-entrant.
  if (!change_delegate_ || ChangeBuffersAreEmpty())
    return ModelTypeSet();

  // This will continue the WriteTransaction using a read only wrapper.
  // This is the last chance for read to occur in the WriteTransaction
  // that's closing. This special ReadTransaction will not close the
  // underlying transaction.
  ReadTransaction read_trans(GetUserShare(), trans);

  ModelTypeSet models_with_changes;
  for (int i = FIRST_REAL_MODEL_TYPE; i < MODEL_TYPE_COUNT; ++i) {
    const ModelType type = ModelTypeFromInt(i);
    if (change_buffers_[type].IsEmpty())
      continue;

    ImmutableChangeRecordList ordered_changes;
    // TODO(akalin): Propagate up the error further (see
    // http://crbug.com/100907).
    CHECK(change_buffers_[type].GetAllChangesInTreeOrder(&read_trans,
                                                         &ordered_changes));
    if (!ordered_changes.Get().empty()) {
      change_delegate_->
          OnChangesApplied(type, &read_trans, ordered_changes);
      change_observer_.Call(FROM_HERE,
          &SyncManager::ChangeObserver::OnChangesApplied,
          type, write_transaction_info.Get().id, ordered_changes);
      models_with_changes.Put(type);
    }
    change_buffers_[i].Clear();
  }
  return models_with_changes;
}

void SyncManagerImpl::HandleCalculateChangesChangeEventFromSyncApi(
    const ImmutableWriteTransactionInfo& write_transaction_info,
    syncable::BaseTransaction* trans) {
  // We have been notified about a user action changing a sync model.
  LOG_IF(WARNING, !ChangeBuffersAreEmpty()) <<
      "CALCULATE_CHANGES called with unapplied old changes.";

  // The mutated model type, or UNSPECIFIED if nothing was mutated.
  ModelTypeSet mutated_model_types;

  const syncable::ImmutableEntryKernelMutationMap& mutations =
      write_transaction_info.Get().mutations;
  for (syncable::EntryKernelMutationMap::const_iterator it =
           mutations.Get().begin(); it != mutations.Get().end(); ++it) {
    if (!it->second.mutated.ref(syncable::IS_UNSYNCED)) {
      continue;
    }

    ModelType model_type =
        GetModelTypeFromSpecifics(it->second.mutated.ref(SPECIFICS));
    if (model_type < FIRST_REAL_MODEL_TYPE) {
      NOTREACHED() << "Permanent or underspecified item changed via syncapi.";
      continue;
    }

    // Found real mutation.
    if (model_type != UNSPECIFIED) {
      mutated_model_types.Put(model_type);
    }
  }

  // Nudge if necessary.
  if (!mutated_model_types.Empty()) {
    if (weak_handle_this_.IsInitialized()) {
      weak_handle_this_.Call(FROM_HERE,
                             &SyncManagerImpl::RequestNudgeForDataTypes,
                             FROM_HERE,
                             mutated_model_types);
    } else {
      NOTREACHED();
    }
  }
}

void SyncManagerImpl::SetExtraChangeRecordData(int64 id,
    ModelType type, ChangeReorderBuffer* buffer,
    Cryptographer* cryptographer, const syncable::EntryKernel& original,
    bool existed_before, bool exists_now) {
  // If this is a deletion and the datatype was encrypted, we need to decrypt it
  // and attach it to the buffer.
  if (!exists_now && existed_before) {
    sync_pb::EntitySpecifics original_specifics(original.ref(SPECIFICS));
    if (type == PASSWORDS) {
      // Passwords must use their own legacy ExtraPasswordChangeRecordData.
      scoped_ptr<sync_pb::PasswordSpecificsData> data(
          DecryptPasswordSpecifics(original_specifics, cryptographer));
      if (!data.get()) {
        NOTREACHED();
        return;
      }
      buffer->SetExtraDataForId(id, new ExtraPasswordChangeRecordData(*data));
    } else if (original_specifics.has_encrypted()) {
      // All other datatypes can just create a new unencrypted specifics and
      // attach it.
      const sync_pb::EncryptedData& encrypted = original_specifics.encrypted();
      if (!cryptographer->Decrypt(encrypted, &original_specifics)) {
        NOTREACHED();
        return;
      }
    }
    buffer->SetSpecificsForId(id, original_specifics);
  }
}

void SyncManagerImpl::HandleCalculateChangesChangeEventFromSyncer(
    const ImmutableWriteTransactionInfo& write_transaction_info,
    syncable::BaseTransaction* trans) {
  // We only expect one notification per sync step, so change_buffers_ should
  // contain no pending entries.
  LOG_IF(WARNING, !ChangeBuffersAreEmpty()) <<
      "CALCULATE_CHANGES called with unapplied old changes.";

  Cryptographer* crypto = directory()->GetCryptographer(trans);
  const syncable::ImmutableEntryKernelMutationMap& mutations =
      write_transaction_info.Get().mutations;
  for (syncable::EntryKernelMutationMap::const_iterator it =
           mutations.Get().begin(); it != mutations.Get().end(); ++it) {
    bool existed_before = !it->second.original.ref(syncable::IS_DEL);
    bool exists_now = !it->second.mutated.ref(syncable::IS_DEL);

    // Omit items that aren't associated with a model.
    ModelType type =
        GetModelTypeFromSpecifics(it->second.mutated.ref(SPECIFICS));
    if (type < FIRST_REAL_MODEL_TYPE)
      continue;

    int64 handle = it->first;
    if (exists_now && !existed_before)
      change_buffers_[type].PushAddedItem(handle);
    else if (!exists_now && existed_before)
      change_buffers_[type].PushDeletedItem(handle);
    else if (exists_now && existed_before &&
             VisiblePropertiesDiffer(it->second, crypto)) {
      change_buffers_[type].PushUpdatedItem(
          handle, VisiblePositionsDiffer(it->second));
    }

    SetExtraChangeRecordData(handle, type, &change_buffers_[type], crypto,
                             it->second.original, existed_before, exists_now);
  }
}

TimeDelta SyncManagerImpl::GetNudgeDelayTimeDelta(
    const ModelType& model_type) {
  return NudgeStrategy::GetNudgeDelayTimeDelta(model_type, this);
}

void SyncManagerImpl::RequestNudgeForDataTypes(
    const tracked_objects::Location& nudge_location,
    ModelTypeSet types) {
  debug_info_event_listener_.OnNudgeFromDatatype(types.First().Get());

  // TODO(lipalani) : Calculate the nudge delay based on all types.
  base::TimeDelta nudge_delay = NudgeStrategy::GetNudgeDelayTimeDelta(
      types.First().Get(),
      this);
  scheduler_->ScheduleNudgeAsync(nudge_delay,
                                 NUDGE_SOURCE_LOCAL,
                                 types,
                                 nudge_location);
}

void SyncManagerImpl::OnSyncEngineEvent(const SyncEngineEvent& event) {
  DCHECK(thread_checker_.CalledOnValidThread());
  // Only send an event if this is due to a cycle ending and this cycle
  // concludes a canonical "sync" process; that is, based on what is known
  // locally we are "all happy" and up-to-date.  There may be new changes on
  // the server, but we'll get them on a subsequent sync.
  //
  // Notifications are sent at the end of every sync cycle, regardless of
  // whether we should sync again.
  if (event.what_happened == SyncEngineEvent::SYNC_CYCLE_ENDED) {
    {
      // Check to see if we need to notify the frontend that we have newly
      // encrypted types or that we require a passphrase.
      ReadTransaction trans(FROM_HERE, GetUserShare());
      Cryptographer* cryptographer = trans.GetCryptographer();
      // If we've completed a sync cycle and the cryptographer isn't ready
      // yet, prompt the user for a passphrase.
      if (cryptographer->has_pending_keys()) {
        DVLOG(1) << "OnPassPhraseRequired Sent";
        sync_pb::EncryptedData pending_keys = cryptographer->GetPendingKeys();
        FOR_EACH_OBSERVER(SyncManager::Observer, observers_,
                          OnPassphraseRequired(REASON_DECRYPTION,
                                               pending_keys));
      } else if (!cryptographer->is_ready() &&
                 event.snapshot.initial_sync_ended().Has(NIGORI)) {
        DVLOG(1) << "OnPassphraseRequired sent because cryptographer is not "
                 << "ready";
        FOR_EACH_OBSERVER(SyncManager::Observer, observers_,
                          OnPassphraseRequired(REASON_ENCRYPTION,
                                               sync_pb::EncryptedData()));
      }

      NotifyCryptographerState(cryptographer);
      allstatus_.SetEncryptedTypes(cryptographer->GetEncryptedTypes());
    }

    if (!initialized_) {
      LOG(INFO) << "OnSyncCycleCompleted not sent because sync api is not "
                << "initialized";
      return;
    }

    if (!event.snapshot.has_more_to_sync()) {
      {
        // To account for a nigori node arriving with stale/bad data, we ensure
        // that the nigori node is up to date at the end of each cycle.
        WriteTransaction trans(FROM_HERE, GetUserShare());
        WriteNode nigori_node(&trans);
        if (nigori_node.InitByTagLookup(kNigoriTag) == BaseNode::INIT_OK) {
          Cryptographer* cryptographer = trans.GetCryptographer();
          UpdateNigoriEncryptionState(cryptographer, &nigori_node);
        }
      }

      DVLOG(1) << "Sending OnSyncCycleCompleted";
      FOR_EACH_OBSERVER(SyncManager::Observer, observers_,
                        OnSyncCycleCompleted(event.snapshot));
    }

    // This is here for tests, which are still using p2p notifications.
    //
    // TODO(chron): Consider changing this back to track has_more_to_sync
    // only notify peers if a successful commit has occurred.
    bool is_notifiable_commit =
        (event.snapshot.model_neutral_state().num_successful_commits > 0);
    if (is_notifiable_commit) {
      if (sync_notifier_.get()) {
        const ModelTypeSet changed_types =
            ModelTypePayloadMapToEnumSet(event.snapshot.source().types);
        sync_notifier_->SendNotification(changed_types);
      } else {
        DVLOG(1) << "Not sending notification: sync_notifier_ is NULL";
      }
    }
  }

  if (event.what_happened == SyncEngineEvent::STOP_SYNCING_PERMANENTLY) {
    FOR_EACH_OBSERVER(SyncManager::Observer, observers_,
                      OnStopSyncingPermanently());
    return;
  }

  if (event.what_happened == SyncEngineEvent::UPDATED_TOKEN) {
    FOR_EACH_OBSERVER(SyncManager::Observer, observers_,
                      OnUpdatedToken(event.updated_token));
    return;
  }

  if (event.what_happened == SyncEngineEvent::ACTIONABLE_ERROR) {
    FOR_EACH_OBSERVER(
        SyncManager::Observer, observers_,
        OnActionableError(
            event.snapshot.model_neutral_state().sync_protocol_error));
    return;
  }

}

void SyncManagerImpl::SetJsEventHandler(
    const WeakHandle<JsEventHandler>& event_handler) {
  js_event_handler_ = event_handler;
  js_sync_manager_observer_.SetJsEventHandler(js_event_handler_);
  js_mutation_event_observer_.SetJsEventHandler(js_event_handler_);
}

void SyncManagerImpl::ProcessJsMessage(
    const std::string& name, const JsArgList& args,
    const WeakHandle<JsReplyHandler>& reply_handler) {
  if (!initialized_) {
    NOTREACHED();
    return;
  }

  if (!reply_handler.IsInitialized()) {
    DVLOG(1) << "Uninitialized reply handler; dropping unknown message "
            << name << " with args " << args.ToString();
    return;
  }

  JsMessageHandler js_message_handler = js_message_handlers_[name];
  if (js_message_handler.is_null()) {
    DVLOG(1) << "Dropping unknown message " << name
             << " with args " << args.ToString();
    return;
  }

  reply_handler.Call(FROM_HERE,
                     &JsReplyHandler::HandleJsReply,
                     name, js_message_handler.Run(args));
}

void SyncManagerImpl::BindJsMessageHandler(
    const std::string& name,
    UnboundJsMessageHandler unbound_message_handler) {
  js_message_handlers_[name] =
      base::Bind(unbound_message_handler, base::Unretained(this));
}

void SyncManagerImpl::OnNotificationStateChange(
    NotificationsDisabledReason reason) {
  const std::string& reason_str = NotificationsDisabledReasonToString(reason);
  notifications_disabled_reason_ = reason;
  DVLOG(1) << "Notification state changed to: " << reason_str;
  const bool notifications_enabled =
      (notifications_disabled_reason_ == NO_NOTIFICATION_ERROR);
  allstatus_.SetNotificationsEnabled(notifications_enabled);
  scheduler_->SetNotificationsEnabled(notifications_enabled);

  // TODO(akalin): Treat a CREDENTIALS_REJECTED state as an auth
  // error.

  if (js_event_handler_.IsInitialized()) {
    DictionaryValue details;
    details.Set("state", Value::CreateStringValue(reason_str));
    js_event_handler_.Call(FROM_HERE,
                           &JsEventHandler::HandleJsEvent,
                           "onNotificationStateChange",
                           JsEventDetails(&details));
  }
}

DictionaryValue* SyncManagerImpl::NotificationInfoToValue(
    const NotificationInfoMap& notification_info) {
  DictionaryValue* value = new DictionaryValue();

  for (NotificationInfoMap::const_iterator it = notification_info.begin();
      it != notification_info.end(); ++it) {
    const std::string& model_type_str = ModelTypeToString(it->first);
    value->Set(model_type_str, it->second.ToValue());
  }

  return value;
}

std::string SyncManagerImpl::NotificationInfoToString(
    const NotificationInfoMap& notification_info) {
  scoped_ptr<DictionaryValue> value(
      NotificationInfoToValue(notification_info));
  std::string str;
  base::JSONWriter::Write(value.get(), &str);
  return str;
}

JsArgList SyncManagerImpl::GetNotificationState(
    const JsArgList& args) {
  const std::string& notification_state =
      NotificationsDisabledReasonToString(notifications_disabled_reason_);
  DVLOG(1) << "GetNotificationState: " << notification_state;
  ListValue return_args;
  return_args.Append(Value::CreateStringValue(notification_state));
  return JsArgList(&return_args);
}

JsArgList SyncManagerImpl::GetNotificationInfo(
    const JsArgList& args) {
  DVLOG(1) << "GetNotificationInfo: "
           << NotificationInfoToString(notification_info_map_);
  ListValue return_args;
  return_args.Append(NotificationInfoToValue(notification_info_map_));
  return JsArgList(&return_args);
}

JsArgList SyncManagerImpl::GetRootNodeDetails(
    const JsArgList& args) {
  ReadTransaction trans(FROM_HERE, GetUserShare());
  ReadNode root(&trans);
  root.InitByRootLookup();
  ListValue return_args;
  return_args.Append(root.GetDetailsAsValue());
  return JsArgList(&return_args);
}

JsArgList SyncManagerImpl::GetClientServerTraffic(
    const JsArgList& args) {
  ListValue return_args;
  ListValue* value = traffic_recorder_.ToValue();
  if (value != NULL)
    return_args.Append(value);
  return JsArgList(&return_args);
}

namespace {

int64 GetId(const ListValue& ids, int i) {
  std::string id_str;
  if (!ids.GetString(i, &id_str)) {
    return kInvalidId;
  }
  int64 id = kInvalidId;
  if (!base::StringToInt64(id_str, &id)) {
    return kInvalidId;
  }
  return id;
}

JsArgList GetNodeInfoById(const JsArgList& args,
                          UserShare* user_share,
                          DictionaryValue* (BaseNode::*info_getter)() const) {
  CHECK(info_getter);
  ListValue return_args;
  ListValue* node_summaries = new ListValue();
  return_args.Append(node_summaries);
  const ListValue* id_list = NULL;
  ReadTransaction trans(FROM_HERE, user_share);
  if (args.Get().GetList(0, &id_list)) {
    CHECK(id_list);
    for (size_t i = 0; i < id_list->GetSize(); ++i) {
      int64 id = GetId(*id_list, i);
      if (id == kInvalidId) {
        continue;
      }
      ReadNode node(&trans);
      if (node.InitByIdLookup(id) != BaseNode::INIT_OK) {
        continue;
      }
      node_summaries->Append((node.*info_getter)());
    }
  }
  return JsArgList(&return_args);
}

}  // namespace

JsArgList SyncManagerImpl::GetNodeSummariesById(const JsArgList& args) {
  return GetNodeInfoById(args, GetUserShare(), &BaseNode::GetSummaryAsValue);
}

JsArgList SyncManagerImpl::GetNodeDetailsById(const JsArgList& args) {
  return GetNodeInfoById(args, GetUserShare(), &BaseNode::GetDetailsAsValue);
}

JsArgList SyncManagerImpl::GetAllNodes(const JsArgList& args) {
  ListValue return_args;
  ListValue* result = new ListValue();
  return_args.Append(result);

  ReadTransaction trans(FROM_HERE, GetUserShare());
  std::vector<const syncable::EntryKernel*> entry_kernels;
  trans.GetDirectory()->GetAllEntryKernels(trans.GetWrappedTrans(),
                                           &entry_kernels);

  for (std::vector<const syncable::EntryKernel*>::const_iterator it =
           entry_kernels.begin(); it != entry_kernels.end(); ++it) {
    result->Append((*it)->ToValue());
  }

  return JsArgList(&return_args);
}

JsArgList SyncManagerImpl::GetChildNodeIds(const JsArgList& args) {
  ListValue return_args;
  ListValue* child_ids = new ListValue();
  return_args.Append(child_ids);
  int64 id = GetId(args.Get(), 0);
  if (id != kInvalidId) {
    ReadTransaction trans(FROM_HERE, GetUserShare());
    syncable::Directory::ChildHandles child_handles;
    trans.GetDirectory()->GetChildHandlesByHandle(trans.GetWrappedTrans(),
                                                  id, &child_handles);
    for (syncable::Directory::ChildHandles::const_iterator it =
             child_handles.begin(); it != child_handles.end(); ++it) {
      child_ids->Append(Value::CreateStringValue(
          base::Int64ToString(*it)));
    }
  }
  return JsArgList(&return_args);
}

void SyncManagerImpl::OnEncryptedTypesChanged(
    ModelTypeSet encrypted_types,
    bool encrypt_everything) {
  // NOTE: We're in a transaction.
  FOR_EACH_OBSERVER(
      SyncManager::Observer, observers_,
      OnEncryptedTypesChanged(encrypted_types, encrypt_everything));
}

void SyncManagerImpl::UpdateNotificationInfo(
    const ModelTypePayloadMap& type_payloads) {
  for (ModelTypePayloadMap::const_iterator it = type_payloads.begin();
       it != type_payloads.end(); ++it) {
    NotificationInfo* info = &notification_info_map_[it->first];
    info->total_count++;
    info->payload = it->second;
  }
}

void SyncManagerImpl::OnNotificationsEnabled() {
  OnNotificationStateChange(NO_NOTIFICATION_ERROR);
}

void SyncManagerImpl::OnNotificationsDisabled(
    NotificationsDisabledReason reason) {
  OnNotificationStateChange(reason);
}

void SyncManagerImpl::OnIncomingNotification(
    const ObjectIdPayloadMap& id_payloads,
    IncomingNotificationSource source) {
  DCHECK(thread_checker_.CalledOnValidThread());
  const ModelTypePayloadMap& type_payloads =
      ObjectIdPayloadMapToModelTypePayloadMap(id_payloads);
  if (source == LOCAL_NOTIFICATION) {
    scheduler_->ScheduleNudgeWithPayloadsAsync(
        TimeDelta::FromMilliseconds(kSyncRefreshDelayMsec),
        NUDGE_SOURCE_LOCAL_REFRESH,
        type_payloads, FROM_HERE);
  } else if (!type_payloads.empty()) {
    scheduler_->ScheduleNudgeWithPayloadsAsync(
        TimeDelta::FromMilliseconds(kSyncSchedulerDelayMsec),
        NUDGE_SOURCE_NOTIFICATION,
        type_payloads, FROM_HERE);
    allstatus_.IncrementNotificationsReceived();
    UpdateNotificationInfo(type_payloads);
    debug_info_event_listener_.OnIncomingNotification(type_payloads);
  } else {
    LOG(WARNING) << "Sync received notification without any type information.";
  }

  if (js_event_handler_.IsInitialized()) {
    DictionaryValue details;
    ListValue* changed_types = new ListValue();
    details.Set("changedTypes", changed_types);
    for (ModelTypePayloadMap::const_iterator it = type_payloads.begin();
         it != type_payloads.end(); ++it) {
      const std::string& model_type_str =
          ModelTypeToString(it->first);
      changed_types->Append(Value::CreateStringValue(model_type_str));
    }
    details.SetString("source", (source == LOCAL_NOTIFICATION) ?
        "LOCAL_NOTIFICATION" : "REMOTE_NOTIFICATION");
    js_event_handler_.Call(FROM_HERE,
                           &JsEventHandler::HandleJsEvent,
                           "onIncomingNotification",
                           JsEventDetails(&details));
  }
}

SyncStatus SyncManagerImpl::GetDetailedStatus() const {
  return allstatus_.status();
}

void SyncManagerImpl::SaveChanges() {
  directory()->SaveChanges();
}

const std::string& SyncManagerImpl::username_for_share() const {
  return share_.name;
}

UserShare* SyncManagerImpl::GetUserShare() {
  DCHECK(initialized_);
  return &share_;
}

bool SyncManagerImpl::ReceivedExperiment(Experiments* experiments) {
  ReadTransaction trans(FROM_HERE, GetUserShare());
  ReadNode node(&trans);
  if (node.InitByTagLookup(kNigoriTag) != BaseNode::INIT_OK) {
    DVLOG(1) << "Couldn't find Nigori node.";
    return false;
  }
  bool found_experiment = false;
  if (node.GetNigoriSpecifics().sync_tab_favicons()) {
    experiments->sync_tab_favicons = true;
    found_experiment = true;
  }
  return found_experiment;
}

bool SyncManagerImpl::HasUnsyncedItems() {
  ReadTransaction trans(FROM_HERE, GetUserShare());
  return (trans.GetWrappedTrans()->directory()->unsynced_entity_count() != 0);
}

// static.
int SyncManagerImpl::GetDefaultNudgeDelay() {
  return kDefaultNudgeDelayMilliseconds;
}

// static.
int SyncManagerImpl::GetPreferencesNudgeDelay() {
  return kPreferencesNudgeDelayMilliseconds;
}

}  // namespace syncer
