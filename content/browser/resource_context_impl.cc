// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/resource_context_impl.h"

#include "base/logging.h"
#include "content/browser/fileapi/chrome_blob_storage_context.h"
#include "content/browser/host_zoom_map_impl.h"
#include "content/browser/loader/resource_dispatcher_host_impl.h"
#include "content/browser/loader/resource_request_info_impl.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"

// Key names on ResourceContext.
static const char* kBlobStorageContextKeyName = "content_blob_storage_context";
static const char* kHostZoomMapKeyName = "content_host_zoom_map";

using base::UserDataAdapter;

namespace content {

namespace {

class NonOwningZoomData : public base::SupportsUserData::Data {
 public:
  explicit NonOwningZoomData(HostZoomMap* hzm) : host_zoom_map_(hzm) {}
  HostZoomMap* host_zoom_map() { return host_zoom_map_; }

 private:
  HostZoomMap* host_zoom_map_;
};

}  // namespace


ResourceContext::ResourceContext() {
  if (ResourceDispatcherHostImpl::Get())
    ResourceDispatcherHostImpl::Get()->AddResourceContext(this);
}

ResourceContext::~ResourceContext() {
  ResourceDispatcherHostImpl* rdhi = ResourceDispatcherHostImpl::Get();
  if (rdhi) {
    rdhi->CancelRequestsForContext(this);
    rdhi->RemoveResourceContext(this);
  }
}

ChromeBlobStorageContext* GetChromeBlobStorageContextForResourceContext(
    ResourceContext* resource_context) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  return UserDataAdapter<ChromeBlobStorageContext>::Get(
      resource_context, kBlobStorageContextKeyName);
}

HostZoomMap* GetHostZoomMapForResourceContext(ResourceContext* context) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  return static_cast<NonOwningZoomData*>(
      context->GetUserData(kHostZoomMapKeyName))->host_zoom_map();
}

void InitializeResourceContext(BrowserContext* browser_context) {
  ResourceContext* resource_context = browser_context->GetResourceContext();
  DCHECK(!resource_context->GetUserData(kHostZoomMapKeyName));

  resource_context->SetUserData(
      kBlobStorageContextKeyName,
      new UserDataAdapter<ChromeBlobStorageContext>(
          ChromeBlobStorageContext::GetFor(browser_context)));

  // This object is owned by the BrowserContext and not ResourceContext, so
  // store a non-owning pointer here.
  resource_context->SetUserData(
      kHostZoomMapKeyName,
      new NonOwningZoomData(
          HostZoomMap::GetForBrowserContext(browser_context)));
  resource_context->DetachUserDataThread();
}

}  // namespace content
