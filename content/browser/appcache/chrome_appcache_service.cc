// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/appcache/chrome_appcache_service.h"

#include "base/file_path.h"
#include "base/file_util.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_types.h"
#include "net/base/net_errors.h"
#include "webkit/quota/quota_manager.h"

using content::BrowserThread;

ChromeAppCacheService::ChromeAppCacheService(
    quota::QuotaManagerProxy* quota_manager_proxy)
    : AppCacheService(quota_manager_proxy),
      resource_context_(NULL) {
}

void ChromeAppCacheService::InitializeOnIOThread(
    const FilePath& cache_path,
    const content::ResourceContext* resource_context,
    scoped_refptr<quota::SpecialStoragePolicy> special_storage_policy) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  cache_path_ = cache_path;
  resource_context_ = resource_context;
  registrar_.Add(
      this, content::NOTIFICATION_PURGE_MEMORY,
      content::NotificationService::AllSources());

  // Init our base class.
  Initialize(
      cache_path_,
      BrowserThread::GetMessageLoopProxyForThread(
          BrowserThread::FILE_USER_BLOCKING),
      BrowserThread::GetMessageLoopProxyForThread(BrowserThread::CACHE));
  set_appcache_policy(this);
  set_special_storage_policy(special_storage_policy);
}

ChromeAppCacheService::~ChromeAppCacheService() {
}

bool ChromeAppCacheService::CanLoadAppCache(const GURL& manifest_url,
                                            const GURL& first_party) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  // We don't prompt for read access.
  return content::GetContentClient()->browser()->AllowAppCache(
      manifest_url, first_party, *resource_context_);
}

bool ChromeAppCacheService::CanCreateAppCache(
    const GURL& manifest_url, const GURL& first_party) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  return content::GetContentClient()->browser()->AllowAppCache(
      manifest_url, first_party, *resource_context_);
}

void ChromeAppCacheService::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(type == content::NOTIFICATION_PURGE_MEMORY);
  PurgeMemory();
}
