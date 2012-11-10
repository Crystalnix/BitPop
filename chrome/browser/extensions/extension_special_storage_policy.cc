// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_special_storage_policy.h"

#include "base/bind.h"
#include "base/logging.h"
#include "base/stl_util.h"
#include "chrome/browser/content_settings/cookie_settings.h"
#include "chrome/common/content_settings.h"
#include "chrome/common/content_settings_types.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/browser_thread.h"

using content::BrowserThread;
using extensions::APIPermission;

ExtensionSpecialStoragePolicy::ExtensionSpecialStoragePolicy(
    CookieSettings* cookie_settings)
    : cookie_settings_(cookie_settings) {}

ExtensionSpecialStoragePolicy::~ExtensionSpecialStoragePolicy() {}

bool ExtensionSpecialStoragePolicy::IsStorageProtected(const GURL& origin) {
  if (origin.SchemeIs(chrome::kExtensionScheme))
    return true;
  base::AutoLock locker(lock_);
  return protected_apps_.Contains(origin);
}

bool ExtensionSpecialStoragePolicy::IsStorageUnlimited(const GURL& origin) {
  base::AutoLock locker(lock_);
  return unlimited_extensions_.Contains(origin);
}

bool ExtensionSpecialStoragePolicy::IsStorageSessionOnly(const GURL& origin) {
  if (cookie_settings_ == NULL)
    return false;
  return cookie_settings_->IsCookieSessionOnly(origin);
}

bool ExtensionSpecialStoragePolicy::HasSessionOnlyOrigins() {
  if (cookie_settings_ == NULL)
    return false;
  if (cookie_settings_->GetDefaultCookieSetting(NULL) ==
      CONTENT_SETTING_SESSION_ONLY)
    return true;
  ContentSettingsForOneType entries;
  cookie_settings_->GetCookieSettings(&entries);
  for (size_t i = 0; i < entries.size(); ++i) {
    if (entries[i].setting == CONTENT_SETTING_SESSION_ONLY)
      return true;
  }
  return false;
}

bool ExtensionSpecialStoragePolicy::IsFileHandler(
    const std::string& extension_id) {
  base::AutoLock locker(lock_);
  return file_handler_extensions_.ContainsExtension(extension_id);
}

bool ExtensionSpecialStoragePolicy::NeedsProtection(
    const extensions::Extension* extension) {
  return extension->is_hosted_app() && !extension->from_bookmark();
}

const ExtensionSet* ExtensionSpecialStoragePolicy::ExtensionsProtectingOrigin(
    const GURL& origin) {
  base::AutoLock locker(lock_);
  return protected_apps_.ExtensionsContaining(origin);
}

void ExtensionSpecialStoragePolicy::GrantRightsForExtension(
    const extensions::Extension* extension) {
  DCHECK(extension);
  if (!NeedsProtection(extension) &&
      !extension->HasAPIPermission(
          APIPermission::kUnlimitedStorage) &&
      !extension->HasAPIPermission(
          APIPermission::kFileBrowserHandler)) {
    return;
  }
  {
    base::AutoLock locker(lock_);
    if (NeedsProtection(extension))
      protected_apps_.Add(extension);
    if (extension->HasAPIPermission(APIPermission::kUnlimitedStorage))
      unlimited_extensions_.Add(extension);
    if (extension->HasAPIPermission(
            APIPermission::kFileBrowserHandler)) {
      file_handler_extensions_.Add(extension);
    }
  }
  NotifyChanged();
}

void ExtensionSpecialStoragePolicy::RevokeRightsForExtension(
    const extensions::Extension* extension) {
  DCHECK(extension);
  if (!NeedsProtection(extension) &&
      !extension->HasAPIPermission(
          APIPermission::kUnlimitedStorage) &&
      !extension->HasAPIPermission(
          APIPermission::kFileBrowserHandler)) {
    return;
  }
  {
    base::AutoLock locker(lock_);
    if (NeedsProtection(extension))
      protected_apps_.Remove(extension);
    if (extension->HasAPIPermission(APIPermission::kUnlimitedStorage))
      unlimited_extensions_.Remove(extension);
    if (extension->HasAPIPermission(APIPermission::kFileBrowserHandler))
      file_handler_extensions_.Remove(extension);
  }
  NotifyChanged();
}

void ExtensionSpecialStoragePolicy::RevokeRightsForAllExtensions() {
  {
    base::AutoLock locker(lock_);
    protected_apps_.Clear();
    unlimited_extensions_.Clear();
    file_handler_extensions_.Clear();
  }
  NotifyChanged();
}

void ExtensionSpecialStoragePolicy::NotifyChanged() {
  if (!BrowserThread::CurrentlyOn(BrowserThread::IO)) {
    BrowserThread::PostTask(BrowserThread::IO, FROM_HERE,
        base::Bind(&ExtensionSpecialStoragePolicy::NotifyChanged, this));
    return;
  }
  SpecialStoragePolicy::NotifyObservers();
}

//-----------------------------------------------------------------------------
// SpecialCollection helper class
//-----------------------------------------------------------------------------

ExtensionSpecialStoragePolicy::SpecialCollection::SpecialCollection() {}

ExtensionSpecialStoragePolicy::SpecialCollection::~SpecialCollection() {
  STLDeleteValues(&cached_results_);
}

bool ExtensionSpecialStoragePolicy::SpecialCollection::Contains(
    const GURL& origin) {
  return !ExtensionsContaining(origin)->is_empty();
}

const ExtensionSet*
ExtensionSpecialStoragePolicy::SpecialCollection::ExtensionsContaining(
    const GURL& origin) {
  CachedResults::const_iterator found = cached_results_.find(origin);
  if (found != cached_results_.end())
    return found->second;

  ExtensionSet* result = new ExtensionSet();
  for (ExtensionSet::const_iterator iter = extensions_.begin();
       iter != extensions_.end(); ++iter) {
    if ((*iter)->OverlapsWithOrigin(origin))
      result->Insert(*iter);
  }
  cached_results_[origin] = result;
  return result;
}

bool ExtensionSpecialStoragePolicy::SpecialCollection::ContainsExtension(
    const std::string& extension_id) {
  return extensions_.Contains(extension_id);
}

void ExtensionSpecialStoragePolicy::SpecialCollection::Add(
    const extensions::Extension* extension) {
  ClearCache();
  extensions_.Insert(extension);
}

void ExtensionSpecialStoragePolicy::SpecialCollection::Remove(
    const extensions::Extension* extension) {
  ClearCache();
  extensions_.Remove(extension->id());
}

void ExtensionSpecialStoragePolicy::SpecialCollection::Clear() {
  ClearCache();
  extensions_.Clear();
}

void ExtensionSpecialStoragePolicy::SpecialCollection::ClearCache() {
  STLDeleteValues(&cached_results_);
  cached_results_.clear();
}
