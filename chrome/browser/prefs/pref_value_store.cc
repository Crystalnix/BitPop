// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/prefs/pref_value_store.h"

#include "base/logging.h"
#include "chrome/browser/prefs/pref_model_associator.h"
#include "chrome/browser/prefs/pref_notifier.h"

PrefValueStore::PrefStoreKeeper::PrefStoreKeeper()
    : pref_value_store_(NULL),
      type_(PrefValueStore::INVALID_STORE) {
}

PrefValueStore::PrefStoreKeeper::~PrefStoreKeeper() {
  if (pref_store_.get()) {
    pref_store_->RemoveObserver(this);
    pref_store_ = NULL;
  }
  pref_value_store_ = NULL;
}

void PrefValueStore::PrefStoreKeeper::Initialize(
    PrefValueStore* store,
    PrefStore* pref_store,
    PrefValueStore::PrefStoreType type) {
  if (pref_store_.get())
    pref_store_->RemoveObserver(this);
  type_ = type;
  pref_value_store_ = store;
  pref_store_ = pref_store;
  if (pref_store_.get())
    pref_store_->AddObserver(this);
}

void PrefValueStore::PrefStoreKeeper::OnPrefValueChanged(
    const std::string& key) {
  pref_value_store_->OnPrefValueChanged(type_, key);
}

void PrefValueStore::PrefStoreKeeper::OnInitializationCompleted(
    bool succeeded) {
  pref_value_store_->OnInitializationCompleted(type_, succeeded);
}

PrefValueStore::PrefValueStore(PrefStore* managed_platform_prefs,
                               PrefStore* managed_cloud_prefs,
                               PrefStore* extension_prefs,
                               PrefStore* command_line_prefs,
                               PrefStore* user_prefs,
                               PrefStore* recommended_platform_prefs,
                               PrefStore* recommended_cloud_prefs,
                               PrefStore* default_prefs,
                               PrefModelAssociator* pref_sync_associator,
                               PrefNotifier* pref_notifier)
    : pref_sync_associator_(pref_sync_associator),
      pref_notifier_(pref_notifier),
      initialization_failed_(false) {
  InitPrefStore(MANAGED_PLATFORM_STORE, managed_platform_prefs);
  InitPrefStore(MANAGED_CLOUD_STORE, managed_cloud_prefs);
  InitPrefStore(EXTENSION_STORE, extension_prefs);
  InitPrefStore(COMMAND_LINE_STORE, command_line_prefs);
  InitPrefStore(USER_STORE, user_prefs);
  InitPrefStore(RECOMMENDED_PLATFORM_STORE, recommended_platform_prefs);
  InitPrefStore(RECOMMENDED_CLOUD_STORE, recommended_cloud_prefs);
  InitPrefStore(DEFAULT_STORE, default_prefs);

  CheckInitializationCompleted();
}

PrefValueStore::~PrefValueStore() {}

PrefValueStore* PrefValueStore::CloneAndSpecialize(
    PrefStore* managed_platform_prefs,
    PrefStore* managed_cloud_prefs,
    PrefStore* extension_prefs,
    PrefStore* command_line_prefs,
    PrefStore* user_prefs,
    PrefStore* recommended_platform_prefs,
    PrefStore* recommended_cloud_prefs,
    PrefStore* default_prefs,
    PrefModelAssociator* pref_sync_associator,
    PrefNotifier* pref_notifier) {
  DCHECK(pref_notifier);
  if (!managed_platform_prefs)
    managed_platform_prefs = GetPrefStore(MANAGED_PLATFORM_STORE);
  if (!managed_cloud_prefs)
    managed_cloud_prefs = GetPrefStore(MANAGED_CLOUD_STORE);
  if (!extension_prefs)
    extension_prefs = GetPrefStore(EXTENSION_STORE);
  if (!command_line_prefs)
    command_line_prefs = GetPrefStore(COMMAND_LINE_STORE);
  if (!user_prefs)
    user_prefs = GetPrefStore(USER_STORE);
  if (!recommended_platform_prefs)
    recommended_platform_prefs = GetPrefStore(RECOMMENDED_PLATFORM_STORE);
  if (!recommended_cloud_prefs)
    recommended_cloud_prefs = GetPrefStore(RECOMMENDED_CLOUD_STORE);
  if (!default_prefs)
    default_prefs = GetPrefStore(DEFAULT_STORE);

  return new PrefValueStore(
      managed_platform_prefs, managed_cloud_prefs, extension_prefs,
      command_line_prefs, user_prefs, recommended_platform_prefs,
      recommended_cloud_prefs, default_prefs, pref_sync_associator,
      pref_notifier);
}

bool PrefValueStore::GetValue(const std::string& name,
                              Value::ValueType type,
                              const Value** out_value) const {
  *out_value = NULL;
  // Check the |PrefStore|s in order of their priority from highest to lowest
  // to find the value of the preference described by the given preference name.
  for (size_t i = 0; i <= PREF_STORE_TYPE_MAX; ++i) {
    if (GetValueFromStore(name.c_str(), static_cast<PrefStoreType>(i),
                          out_value)) {
      if (!(*out_value)->IsType(type)) {
        LOG(WARNING) << "Expected type for " << name << " is " << type
                     << " but got " << (*out_value)->GetType()
                     << " in store " << i;
        continue;
      }
      return true;
    }
  }
  return false;
}

void PrefValueStore::NotifyPrefChanged(
    const char* path,
    PrefValueStore::PrefStoreType new_store) {
  DCHECK(new_store != INVALID_STORE);

  // If the pref is controlled by a higher-priority store, its effective value
  // cannot have changed.
  PrefStoreType controller = ControllingPrefStoreForPref(path);
  if (controller == INVALID_STORE || controller >= new_store) {
    pref_notifier_->OnPreferenceChanged(path);
    if (pref_sync_associator_)
      pref_sync_associator_->ProcessPrefChange(path);
  }
}

bool PrefValueStore::PrefValueInManagedStore(const char* name) const {
  return PrefValueInStore(name, MANAGED_PLATFORM_STORE) ||
         PrefValueInStore(name, MANAGED_CLOUD_STORE);
}

bool PrefValueStore::PrefValueInExtensionStore(const char* name) const {
  return PrefValueInStore(name, EXTENSION_STORE);
}

bool PrefValueStore::PrefValueInUserStore(const char* name) const {
  return PrefValueInStore(name, USER_STORE);
}

bool PrefValueStore::PrefValueFromExtensionStore(const char* name) const {
  return ControllingPrefStoreForPref(name) == EXTENSION_STORE;
}

bool PrefValueStore::PrefValueFromUserStore(const char* name) const {
  return ControllingPrefStoreForPref(name) == USER_STORE;
}

bool PrefValueStore::PrefValueFromDefaultStore(const char* name) const {
  return ControllingPrefStoreForPref(name) == DEFAULT_STORE;
}

bool PrefValueStore::PrefValueUserModifiable(const char* name) const {
  PrefStoreType effective_store = ControllingPrefStoreForPref(name);
  return effective_store >= USER_STORE ||
         effective_store == INVALID_STORE;
}

bool PrefValueStore::PrefValueExtensionModifiable(const char* name) const {
  PrefStoreType effective_store = ControllingPrefStoreForPref(name);
  return effective_store >= EXTENSION_STORE ||
         effective_store == INVALID_STORE;
}

bool PrefValueStore::PrefValueInStore(
    const char* name,
    PrefValueStore::PrefStoreType store) const {
  // Declare a temp Value* and call GetValueFromStore,
  // ignoring the output value.
  const Value* tmp_value = NULL;
  return GetValueFromStore(name, store, &tmp_value);
}

bool PrefValueStore::PrefValueInStoreRange(
    const char* name,
    PrefValueStore::PrefStoreType first_checked_store,
    PrefValueStore::PrefStoreType last_checked_store) const {
  if (first_checked_store > last_checked_store) {
    NOTREACHED();
    return false;
  }

  for (size_t i = first_checked_store;
       i <= static_cast<size_t>(last_checked_store); ++i) {
    if (PrefValueInStore(name, static_cast<PrefStoreType>(i)))
      return true;
  }
  return false;
}

PrefValueStore::PrefStoreType PrefValueStore::ControllingPrefStoreForPref(
    const char* name) const {
  for (size_t i = 0; i <= PREF_STORE_TYPE_MAX; ++i) {
    if (PrefValueInStore(name, static_cast<PrefStoreType>(i)))
      return static_cast<PrefStoreType>(i);
  }
  return INVALID_STORE;
}

bool PrefValueStore::GetValueFromStore(const char* name,
                                       PrefValueStore::PrefStoreType store_type,
                                       const Value** out_value) const {
  // Only return true if we find a value and it is the correct type, so stale
  // values with the incorrect type will be ignored.
  const PrefStore* store = GetPrefStore(static_cast<PrefStoreType>(store_type));
  if (store) {
    switch (store->GetValue(name, out_value)) {
      case PrefStore::READ_USE_DEFAULT:
        store = GetPrefStore(DEFAULT_STORE);
        if (!store || store->GetValue(name, out_value) != PrefStore::READ_OK) {
          *out_value = NULL;
          return false;
        }
        // Fall through...
      case PrefStore::READ_OK:
        return true;
      case PrefStore::READ_NO_VALUE:
        break;
    }
  }

  // No valid value found for the given preference name: set the return false.
  *out_value = NULL;
  return false;
}

void PrefValueStore::OnPrefValueChanged(PrefValueStore::PrefStoreType type,
                                        const std::string& key) {
  NotifyPrefChanged(key.c_str(), type);
}

void PrefValueStore::OnInitializationCompleted(
    PrefValueStore::PrefStoreType type, bool succeeded) {
  if (initialization_failed_)
    return;
  if (!succeeded) {
    initialization_failed_ = true;
    pref_notifier_->OnInitializationCompleted(false);
    return;
  }
  CheckInitializationCompleted();
}

void PrefValueStore::InitPrefStore(PrefValueStore::PrefStoreType type,
                                   PrefStore* pref_store) {
  pref_stores_[type].Initialize(this, pref_store, type);
}

void PrefValueStore::CheckInitializationCompleted() {
  if (initialization_failed_)
    return;
  for (size_t i = 0; i <= PREF_STORE_TYPE_MAX; ++i) {
    scoped_refptr<PrefStore> store =
        GetPrefStore(static_cast<PrefStoreType>(i));
    if (store && !store->IsInitializationComplete())
      return;
  }
  pref_notifier_->OnInitializationCompleted(true);
}
