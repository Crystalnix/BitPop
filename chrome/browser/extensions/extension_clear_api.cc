// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Defines the Chrome Extensions Clear API functions, which entail
// clearing browsing data, and clearing the browser's cache (which, let's be
// honest, are the same thing), as specified in the extension API JSON.

#include "chrome/browser/extensions/extension_clear_api.h"

#include <string>

#include "base/values.h"
#include "chrome/browser/browsing_data_remover.h"
#include "chrome/browser/plugin_data_remover_helper.h"
#include "chrome/browser/plugin_prefs.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_error_utils.h"
#include "content/public/browser/browser_thread.h"

using content::BrowserThread;

namespace extension_clear_api_constants {

// Keys.
const char kAppCacheKey[] = "appcache";
const char kCacheKey[] = "cache";
const char kCookiesKey[] = "cookies";
const char kDownloadsKey[] = "downloads";
const char kFileSystemsKey[] = "fileSystems";
const char kFormDataKey[] = "formData";
const char kHistoryKey[] = "history";
const char kIndexedDBKey[] = "indexedDB";
const char kLocalStorageKey[] = "localStorage";
const char kOriginBoundCertsKey[] = "originBoundCerts";
const char kPasswordsKey[] = "passwords";
const char kPluginDataKey[] = "pluginData";
const char kWebSQLKey[] = "webSQL";

// Errors!
const char kOneAtATimeError[] = "Only one 'clear' API call can run at a time.";

}  // namespace extension_clear_api_constants

namespace {
// Converts the JavaScript API's numeric input (miliseconds since epoch) into an
// appropriate base::Time that we can pass into the BrowsingDataRemove.
bool ParseTimeFromValue(const double& ms_since_epoch, base::Time* time) {
  return true;
}

// Given a DictionaryValue |dict|, returns either the value stored as |key|, or
// false, if the given key doesn't exist in the dictionary.
bool DataRemovalRequested(base::DictionaryValue* dict, const std::string& key) {
  bool value = false;
  if (!dict->GetBoolean(key, &value))
    return false;
  else
    return value;
}

// Convert the JavaScript API's object input ({ cookies: true }) into the
// appropriate removal mask for the BrowsingDataRemover object.
int ParseRemovalMask(base::DictionaryValue* value) {
  int GetRemovalMask = 0;
  if (DataRemovalRequested(value, extension_clear_api_constants::kAppCacheKey))
    GetRemovalMask |= BrowsingDataRemover::REMOVE_APPCACHE;
  if (DataRemovalRequested(value, extension_clear_api_constants::kCacheKey))
    GetRemovalMask |= BrowsingDataRemover::REMOVE_CACHE;
  if (DataRemovalRequested(value, extension_clear_api_constants::kCookiesKey))
    GetRemovalMask |= BrowsingDataRemover::REMOVE_COOKIES;
  if (DataRemovalRequested(value, extension_clear_api_constants::kDownloadsKey))
    GetRemovalMask |= BrowsingDataRemover::REMOVE_DOWNLOADS;
  if (DataRemovalRequested(value,
                           extension_clear_api_constants::kFileSystemsKey))
    GetRemovalMask |= BrowsingDataRemover::REMOVE_FILE_SYSTEMS;
  if (DataRemovalRequested(value, extension_clear_api_constants::kFormDataKey))
    GetRemovalMask |= BrowsingDataRemover::REMOVE_FORM_DATA;
  if (DataRemovalRequested(value, extension_clear_api_constants::kHistoryKey))
    GetRemovalMask |= BrowsingDataRemover::REMOVE_HISTORY;
  if (DataRemovalRequested(value, extension_clear_api_constants::kIndexedDBKey))
    GetRemovalMask |= BrowsingDataRemover::REMOVE_INDEXEDDB;
  if (DataRemovalRequested(value,
                           extension_clear_api_constants::kLocalStorageKey))
    GetRemovalMask |= BrowsingDataRemover::REMOVE_LOCAL_STORAGE;
  if (DataRemovalRequested(value,
                           extension_clear_api_constants::kOriginBoundCertsKey))
    GetRemovalMask |= BrowsingDataRemover::REMOVE_ORIGIN_BOUND_CERTS;
  if (DataRemovalRequested(value, extension_clear_api_constants::kPasswordsKey))
    GetRemovalMask |= BrowsingDataRemover::REMOVE_PASSWORDS;
  if (DataRemovalRequested(value,
                           extension_clear_api_constants::kPluginDataKey))
    GetRemovalMask |= BrowsingDataRemover::REMOVE_PLUGIN_DATA;
  if (DataRemovalRequested(value, extension_clear_api_constants::kWebSQLKey))
    GetRemovalMask |= BrowsingDataRemover::REMOVE_WEBSQL;

  return GetRemovalMask;
}

}  // Namespace.

void BrowsingDataExtensionFunction::OnBrowsingDataRemoverDone() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  this->SendResponse(true);

  Release();  // Balanced in RunImpl.
}

bool BrowsingDataExtensionFunction::RunImpl() {
  if (BrowsingDataRemover::is_removing()) {
    error_ = extension_clear_api_constants::kOneAtATimeError;
    return false;
  }

  double ms_since_epoch;
  EXTENSION_FUNCTION_VALIDATE(args_->GetDouble(0, &ms_since_epoch));
  // base::Time takes a double that represents seconds since epoch. JavaScript
  // gives developers milliseconds, so do a quick conversion before populating
  // the object. Also, Time::FromDoubleT converts double time 0 to empty Time
  // object. So we need to do special handling here.
  remove_since_ = (ms_since_epoch == 0) ?
      base::Time::UnixEpoch() :
      base::Time::FromDoubleT(ms_since_epoch / 1000.0);

  removal_mask_ = GetRemovalMask();

  if (removal_mask_ & BrowsingDataRemover::REMOVE_PLUGIN_DATA) {
    // If we're being asked to remove plugin data, check whether it's actually
    // supported.
    Profile* profile = GetCurrentBrowser()->profile();
    BrowserThread::PostTask(
        BrowserThread::FILE, FROM_HERE,
        base::Bind(
            &BrowsingDataExtensionFunction::CheckRemovingPluginDataSupported,
            this,
            make_scoped_refptr(PluginPrefs::GetForProfile(profile))));
  } else {
    StartRemoving();
  }

  // Will finish asynchronously.
  return true;
}

void BrowsingDataExtensionFunction::CheckRemovingPluginDataSupported(
    scoped_refptr<PluginPrefs> plugin_prefs) {
  if (!PluginDataRemoverHelper::IsSupported(plugin_prefs))
    removal_mask_ &= ~BrowsingDataRemover::REMOVE_PLUGIN_DATA;

  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&BrowsingDataExtensionFunction::StartRemoving, this));
}

void BrowsingDataExtensionFunction::StartRemoving() {
  // If we're good to go, add a ref (Balanced in OnBrowsingDataRemoverDone)
  AddRef();

  // Create a BrowsingDataRemover, set the current object as an observer (so
  // that we're notified after removal) and call remove() with the arguments
  // we've generated above. We can use a raw pointer here, as the browsing data
  // remover is responsible for deleting itself once data removal is complete.
  BrowsingDataRemover* remover = new BrowsingDataRemover(
      GetCurrentBrowser()->profile(), remove_since_, base::Time::Now());
  remover->AddObserver(this);
  remover->Remove(removal_mask_);
}

int ClearBrowsingDataFunction::GetRemovalMask() const {
  // Parse the |dataToRemove| argument to generate the removal mask.
  base::DictionaryValue* data_to_remove;
  if (args_->GetDictionary(1, &data_to_remove))
    return ParseRemovalMask(data_to_remove);
  else
    return 0;
}

int ClearAppCacheFunction::GetRemovalMask() const {
  return BrowsingDataRemover::REMOVE_APPCACHE;
}

int ClearCacheFunction::GetRemovalMask() const {
  return BrowsingDataRemover::REMOVE_CACHE;
}

int ClearCookiesFunction::GetRemovalMask() const {
  return BrowsingDataRemover::REMOVE_COOKIES;
}

int ClearDownloadsFunction::GetRemovalMask() const {
  return BrowsingDataRemover::REMOVE_DOWNLOADS;
}

int ClearFileSystemsFunction::GetRemovalMask() const {
  return BrowsingDataRemover::REMOVE_FILE_SYSTEMS;
}

int ClearFormDataFunction::GetRemovalMask() const {
  return BrowsingDataRemover::REMOVE_FORM_DATA;
}

int ClearHistoryFunction::GetRemovalMask() const {
  return BrowsingDataRemover::REMOVE_HISTORY;
}

int ClearIndexedDBFunction::GetRemovalMask() const {
  return BrowsingDataRemover::REMOVE_INDEXEDDB;
}

int ClearLocalStorageFunction::GetRemovalMask() const {
  return BrowsingDataRemover::REMOVE_LOCAL_STORAGE;
}

int ClearOriginBoundCertsFunction::GetRemovalMask() const {
  return BrowsingDataRemover::REMOVE_ORIGIN_BOUND_CERTS;
}

int ClearPluginDataFunction::GetRemovalMask() const {
  return BrowsingDataRemover::REMOVE_PLUGIN_DATA;
}

int ClearPasswordsFunction::GetRemovalMask() const {
  return BrowsingDataRemover::REMOVE_PASSWORDS;
}

int ClearWebSQLFunction::GetRemovalMask() const {
  return BrowsingDataRemover::REMOVE_WEBSQL;
}
