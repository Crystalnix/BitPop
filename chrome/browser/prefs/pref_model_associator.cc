// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/prefs/pref_model_associator.h"

#include "base/auto_reset.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/sync/api/sync_change.h"
#include "chrome/browser/sync/protocol/preference_specifics.pb.h"
#include "chrome/common/pref_names.h"
#include "content/common/json_value_serializer.h"
#include "content/common/notification_service.h"

using syncable::PREFERENCES;

PrefModelAssociator::PrefModelAssociator()
    : models_associated_(false),
      processing_syncer_changes_(false),
      pref_service_(NULL),
      sync_processor_(NULL) {
}

PrefModelAssociator::PrefModelAssociator(
    PrefService* pref_service)
    : models_associated_(false),
      processing_syncer_changes_(false),
      pref_service_(pref_service),
      sync_processor_(NULL) {
  DCHECK(CalledOnValidThread());
}

PrefModelAssociator::~PrefModelAssociator() {
  DCHECK(CalledOnValidThread());
  sync_processor_ = NULL;
  pref_service_ = NULL;
}

void PrefModelAssociator::InitPrefAndAssociate(
    const SyncData& sync_pref,
    const std::string& pref_name,
    SyncChangeList* sync_changes) {
  const PrefService::Preference* pref =
      pref_service_->FindPreference(pref_name.c_str());
  VLOG(1) << "Associating preference " << pref_name;
  if (!pref->IsUserModifiable()) {
    // This preference is controlled by policy. We don't need sync it, but if
    // there is sync data we want to track it for possible future use.
    if (sync_pref.IsValid())
      untracked_pref_sync_data_[pref_name] = sync_pref;
    return;
  }

  base::JSONReader reader;
  if (sync_pref.IsValid()) {
    // The server has a value for the preference, we have to reconcile it with
    // ours.
    const sync_pb::PreferenceSpecifics& preference =
        sync_pref.GetSpecifics().GetExtension(sync_pb::preference);
    DCHECK_EQ(pref->name(), preference.name());

    scoped_ptr<Value> value(
        reader.JsonToValue(preference.value(), false, false));
    if (!value.get()) {
      LOG(ERROR) << "Failed to deserialize preference value: "
                 << reader.GetErrorMessage();
      return;
    }

    // Merge the server value of this preference with the local value.
    scoped_ptr<Value> new_value(MergePreference(*pref, *value));

    // Update the local preference based on what we got from the
    // sync server.
    if (new_value->IsType(Value::TYPE_NULL)) {
      pref_service_->ClearPref(pref_name.c_str());
    } else if (!new_value->IsType(pref->GetType())) {
      LOG(WARNING) << "Synced value for " << preference.name()
                   << " is of type " << new_value->GetType()
                   << " which doesn't match pref type " << pref->GetType();
    } else if (!pref->GetValue()->Equals(new_value.get())) {
      pref_service_->Set(pref_name.c_str(), *new_value);
    }

    SendUpdateNotificationsIfNecessary(pref_name);

    // If the merge resulted in an updated value, inform the syncer.
    if (!value->Equals(new_value.get())) {
      SyncData sync_data;
      if (!CreatePrefSyncData(pref->name(), *new_value, &sync_data)) {
        LOG(ERROR) << "Failed to update preference.";
        return;
      }
      sync_changes->push_back(SyncChange(SyncChange::ACTION_UPDATE, sync_data));
    }
  } else if (pref->IsUserControlled()) {
    // The server does not know about this preference and should be added
    // to the syncer's database.
    SyncData sync_data;
    if (!CreatePrefSyncData(pref->name(), *pref->GetValue(), &sync_data)) {
      LOG(ERROR) << "Failed to update preference.";
      return;
    }
    sync_changes->push_back(SyncChange(SyncChange::ACTION_ADD, sync_data));
  } else {
    // This pref has a default value, we can ignore it. Once it gets changed,
    // we'll send the new custom value to the syncer.
    return;
  }

  // Make sure we add it to our list of synced preferences so we know what
  // the server is aware of.
  synced_preferences_.insert(pref_name);
  return;
}

bool PrefModelAssociator::MergeDataAndStartSyncing(
    syncable::ModelType type,
    const SyncDataList& initial_sync_data,
    SyncChangeProcessor* sync_processor) {
  DCHECK_EQ(type, PREFERENCES);
  DCHECK(CalledOnValidThread());
  DCHECK(!sync_processor_);
  sync_processor_ = sync_processor;

  SyncChangeList new_changes;
  std::set<std::string> remaining_preferences = registered_preferences_;

  // Go through and check for all preferences we care about that sync already
  // knows about.
  for (SyncDataList::const_iterator sync_iter = initial_sync_data.begin();
       sync_iter != initial_sync_data.end();
       ++sync_iter) {
    DCHECK_EQ(PREFERENCES, sync_iter->GetDataType());
    std::string sync_pref_name = sync_iter->GetSpecifics().
        GetExtension(sync_pb::preference).name();
    if (remaining_preferences.count(sync_pref_name) == 0) {
      // We're not syncing this preference locally, ignore the sync data.
      // TODO(zea): Eventually we want to be able to have the syncable service
      // reconstruct all sync data for it's datatype (therefore having
      // GetAllSyncData be a complete representation). We should store this data
      // somewhere, even if we don't use it.
      continue;
    }

    remaining_preferences.erase(sync_pref_name);
    InitPrefAndAssociate(*sync_iter, sync_pref_name, &new_changes);
  }

  // Go through and build sync data for any remaining preferences.
  for (std::set<std::string>::iterator pref_name_iter =
          remaining_preferences.begin();
       pref_name_iter != remaining_preferences.end();
       ++pref_name_iter) {
    InitPrefAndAssociate(SyncData(), *pref_name_iter, &new_changes);
  }

  // Push updates to sync.
  sync_processor_->ProcessSyncChanges(new_changes);
  models_associated_ = true;
  return true;
}

void PrefModelAssociator::StopSyncing(syncable::ModelType type) {
  DCHECK_EQ(type, PREFERENCES);
  models_associated_ = false;
  sync_processor_ = NULL;
  untracked_pref_sync_data_.clear();
}

Value* PrefModelAssociator::MergePreference(
    const PrefService::Preference& local_pref,
    const Value& server_value) {
  const std::string& name(local_pref.name());
  if (name == prefs::kURLsToRestoreOnStartup ||
      name == prefs::kDesktopNotificationAllowedOrigins ||
      name == prefs::kDesktopNotificationDeniedOrigins) {
    return MergeListValues(*local_pref.GetValue(), server_value);
  }

  if (name == prefs::kContentSettingsPatterns ||
      name == prefs::kGeolocationContentSettings) {
    return MergeDictionaryValues(*local_pref.GetValue(), server_value);
  }

  // If this is not a specially handled preference, server wins.
  return server_value.DeepCopy();
}

bool PrefModelAssociator::CreatePrefSyncData(
    const std::string& name,
    const Value& value,
    SyncData* sync_data) {
  std::string serialized;
  // TODO(zea): consider JSONWriter::Write since you don't have to check
  // failures to deserialize.
  JSONStringValueSerializer json(&serialized);
  if (!json.Serialize(value)) {
    LOG(ERROR) << "Failed to serialize preference value.";
    return false;
  }

  sync_pb::EntitySpecifics specifics;
  sync_pb::PreferenceSpecifics* pref_specifics = specifics.MutableExtension(
      sync_pb::preference);
  pref_specifics->set_name(name);
  pref_specifics->set_value(serialized);
  *sync_data = SyncData::CreateLocalData(name, specifics);
  return true;
}

Value* PrefModelAssociator::MergeListValues(const Value& from_value,
                                            const Value& to_value) {
  if (from_value.GetType() == Value::TYPE_NULL)
    return to_value.DeepCopy();
  if (to_value.GetType() == Value::TYPE_NULL)
    return from_value.DeepCopy();

  DCHECK(from_value.GetType() == Value::TYPE_LIST);
  DCHECK(to_value.GetType() == Value::TYPE_LIST);
  const ListValue& from_list_value = static_cast<const ListValue&>(from_value);
  const ListValue& to_list_value = static_cast<const ListValue&>(to_value);
  ListValue* result = to_list_value.DeepCopy();

  for (ListValue::const_iterator i = from_list_value.begin();
       i != from_list_value.end(); ++i) {
    Value* value = (*i)->DeepCopy();
    result->AppendIfNotPresent(value);
  }
  return result;
}

Value* PrefModelAssociator::MergeDictionaryValues(
    const Value& from_value,
    const Value& to_value) {
  if (from_value.GetType() == Value::TYPE_NULL)
    return to_value.DeepCopy();
  if (to_value.GetType() == Value::TYPE_NULL)
    return from_value.DeepCopy();

  DCHECK_EQ(from_value.GetType(), Value::TYPE_DICTIONARY);
  DCHECK_EQ(to_value.GetType(), Value::TYPE_DICTIONARY);
  const DictionaryValue& from_dict_value =
      static_cast<const DictionaryValue&>(from_value);
  const DictionaryValue& to_dict_value =
      static_cast<const DictionaryValue&>(to_value);
  DictionaryValue* result = to_dict_value.DeepCopy();

  for (DictionaryValue::key_iterator key = from_dict_value.begin_keys();
       key != from_dict_value.end_keys(); ++key) {
    Value* from_value;
    bool success = from_dict_value.GetWithoutPathExpansion(*key, &from_value);
    DCHECK(success);

    Value* to_key_value;
    if (result->GetWithoutPathExpansion(*key, &to_key_value)) {
      if (to_key_value->GetType() == Value::TYPE_DICTIONARY) {
        Value* merged_value = MergeDictionaryValues(*from_value, *to_key_value);
        result->SetWithoutPathExpansion(*key, merged_value);
      }
      // Note that for all other types we want to preserve the "to"
      // values so we do nothing here.
    } else {
      result->SetWithoutPathExpansion(*key, from_value->DeepCopy());
    }
  }
  return result;
}

void PrefModelAssociator::SendUpdateNotificationsIfNecessary(
    const std::string& pref_name) {
  // The bookmark bar visibility preference requires a special
  // notification to update the UI.
  if (0 == pref_name.compare(prefs::kShowBookmarkBar)) {
    NotificationService::current()->Notify(
        NotificationType::BOOKMARK_BAR_VISIBILITY_PREF_CHANGED,
        Source<PrefModelAssociator>(this),
        NotificationService::NoDetails());
  }
}

// Note: This will build a model of all preferences registered as syncable
// with user controlled data. We do not track any information for preferences
// not registered locally as syncable and do not inform the syncer of
// non-user controlled preferences.
SyncDataList PrefModelAssociator::GetAllSyncData(syncable::ModelType type)
    const {
  DCHECK_EQ(PREFERENCES, type);
  SyncDataList current_data;
  for (PreferenceSet::const_iterator iter = synced_preferences_.begin();
       iter != synced_preferences_.end();
       ++iter) {
    std::string name = *iter;
    const PrefService::Preference* pref =
      pref_service_->FindPreference(name.c_str());
    SyncData sync_data;
    if (!CreatePrefSyncData(name, *pref->GetValue(), &sync_data))
      continue;
    current_data.push_back(sync_data);
  }
  for (SyncDataMap::const_iterator iter = untracked_pref_sync_data_.begin();
       iter != untracked_pref_sync_data_.end();
       ++iter) {
    current_data.push_back(iter->second);
  }
  return current_data;
}

void PrefModelAssociator::ProcessSyncChanges(
    const SyncChangeList& change_list) {
  if (!models_associated_)
    return;
  AutoReset<bool> processing_changes(&processing_syncer_changes_, true);
  SyncChangeList::const_iterator iter;
  for (iter = change_list.begin(); iter != change_list.end(); ++iter) {
    DCHECK_EQ(PREFERENCES, iter->sync_data().GetDataType());

    std::string name;
    sync_pb::PreferenceSpecifics pref_specifics =
        iter->sync_data().GetSpecifics().GetExtension(sync_pb::preference);
    scoped_ptr<Value> value(ReadPreferenceSpecifics(pref_specifics,
                                                    &name));

    if (iter->change_type() == SyncChange::ACTION_DELETE) {
      // We never delete preferences.
      NOTREACHED() << "Attempted to process sync delete change for " << name
                   << ". Skipping.";
      continue;
    }

    // Skip values we can't deserialize.
    // TODO(zea): consider taking some further action such as erasing the bad
    // data.
    if (!value.get())
      continue;

    // It is possible that we may receive a change to a preference we do not
    // want to sync. For example if the user is syncing a Mac client and a
    // Windows client, the Windows client does not support
    // kConfirmToQuitEnabled. Ignore updates from these preferences.
    const char* pref_name = name.c_str();
    if (!IsPrefRegistered(pref_name))
      continue;

    const PrefService::Preference* pref =
        pref_service_->FindPreference(pref_name);
    DCHECK(pref);
    if (!pref->IsUserModifiable()) {
      // This preference is controlled by policy, ignore for now, but keep
      // the data around for possible later use.
      untracked_pref_sync_data_[name] = iter->sync_data();
      continue;
    }

    pref_service_->Set(pref_name, *value);

    // If this is a newly added node, associate.
    if (iter->change_type() == SyncChange::ACTION_ADD) {
      synced_preferences_.insert(name);
    }

    SendUpdateNotificationsIfNecessary(name);
  }
}

Value* PrefModelAssociator::ReadPreferenceSpecifics(
    const sync_pb::PreferenceSpecifics& preference,
    std::string* name) {
  base::JSONReader reader;
  scoped_ptr<Value> value(reader.JsonToValue(preference.value(), false, false));
  if (!value.get()) {
    std::string err = "Failed to deserialize preference value: " +
        reader.GetErrorMessage();
    LOG(ERROR) << err;
    return NULL;
  }
  *name = preference.name();
  return value.release();
}

std::set<std::string> PrefModelAssociator::registered_preferences() const {
  return registered_preferences_;
}

std::set<std::string> PrefModelAssociator::synced_preferences() const {
  return synced_preferences_;
}

void PrefModelAssociator::RegisterPref(const char* name) {
  DCHECK(!models_associated_ && registered_preferences_.count(name) == 0);
  registered_preferences_.insert(name);
}

bool PrefModelAssociator::IsPrefRegistered(const char* name) {
  return registered_preferences_.count(name) > 0;
}

void PrefModelAssociator::ProcessPrefChange(const std::string& name) {
  if (processing_syncer_changes_)
    return;  // These are changes originating from us, ignore.

  // We only process changes if we've already associated models.
  if (!models_associated_)
    return;

  const PrefService::Preference* preference =
      pref_service_->FindPreference(name.c_str());
  if (!IsPrefRegistered(name.c_str()))
    return;  // We are not syncing this preference.

  SyncChangeList changes;

  if (!preference->IsUserModifiable()) {
    // If the preference is not currently user modifiable, back up the
    // previously synced value and remove it from our list of synced prefs.
    if (synced_preferences_.count(name) > 0) {
      synced_preferences_.erase(name);
      SyncData sync_data;
      if (!CreatePrefSyncData(name, *preference->GetValue(), &sync_data)) {
        LOG(ERROR) << "Failed to update preference.";
        return;
      }
      untracked_pref_sync_data_[name] = sync_data;
    }
    return;
  }

  AutoReset<bool> processing_changes(&processing_syncer_changes_, true);

  if (synced_preferences_.count(name) == 0) {
    // This is a preference that changed locally but we were not syncing. This
    // happens when a preference was previously not user modifiable but now is,
    // or if it had a default value but the user set a custom one. We now care
    // about the preference and must inform the syncer, as well as update our
    // own internal tracking.
    if (untracked_pref_sync_data_.count(name) > 0) {
      // We have sync data for this preference, merge it (this only happens
      // when we went from policy-controlled to user controlled. Default values
      // are always overwritten by syncer values).
      InitPrefAndAssociate(untracked_pref_sync_data_[name], name, &changes);
      untracked_pref_sync_data_.erase(name);
    } else {
      InitPrefAndAssociate(SyncData(), name, &changes);
    }
  } else {
    // We're already syncing this preference, we just need to update the data.
    SyncData sync_data;
    if (!CreatePrefSyncData(name, *preference->GetValue(), &sync_data)) {
      LOG(ERROR) << "Failed to update preference.";
      return;
    }
    changes.push_back(SyncChange(SyncChange::ACTION_UPDATE, sync_data));
  }
  sync_processor_->ProcessSyncChanges(changes);
}
