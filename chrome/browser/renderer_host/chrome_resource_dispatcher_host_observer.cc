// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/renderer_host/chrome_resource_dispatcher_host_observer.h"

#include "base/logging.h"
#include "chrome/browser/prerender/prerender_manager.h"
#include "chrome/browser/prerender/prerender_tracker.h"
#include "content/browser/browser_thread.h"
#include "content/browser/resource_context.h"
#include "content/browser/renderer_host/resource_dispatcher_host_request_info.h"
#include "content/common/resource_messages.h"
#include "net/base/load_flags.h"

ChromeResourceDispatcherHostObserver::ChromeResourceDispatcherHostObserver(
    ResourceDispatcherHost* resource_dispatcher_host,
    prerender::PrerenderTracker* prerender_tracker)
    : resource_dispatcher_host_(resource_dispatcher_host),
      prerender_tracker_(prerender_tracker) {
}

ChromeResourceDispatcherHostObserver::~ChromeResourceDispatcherHostObserver() {
}

bool ChromeResourceDispatcherHostObserver::ShouldBeginRequest(
    int child_id, int route_id,
    const ResourceHostMsg_Request& request_data,
    const content::ResourceContext& resource_context,
    const GURL& referrer) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  // Handle a PREFETCH resource type. If prefetch is disabled, squelch the
  // request.  Otherwise, do a normal request to warm the cache.
  if (request_data.resource_type == ResourceType::PREFETCH) {
    // All PREFETCH requests should be GETs, but be defensive about it.
    if (request_data.method != "GET")
      return false;

    // If prefetch is disabled, kill the request.
    if (!ResourceDispatcherHost::is_prefetch_enabled())
      return false;
  }

  // Handle a PRERENDER motivated request. Very similar to rel=prefetch, these
  // rel=prerender requests instead launch an early render of the entire page.
  if (request_data.resource_type == ResourceType::PRERENDER) {
    if (prerender::PrerenderManager::IsPrerenderingPossible()) {
      BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
          NewRunnableFunction(prerender::HandleTag,
                              resource_context.prerender_manager(),
                              child_id,
                              route_id,
                              request_data.url,
                              referrer));
    }
    // Prerendering or not, this request should be aborted.
    return false;
  }

  // Abort any prerenders that spawn requests that use invalid HTTP methods.
  if (prerender_tracker_->IsPrerenderingOnIOThread(child_id, route_id) &&
      !prerender::PrerenderManager::IsValidHttpMethod(request_data.method)) {
    prerender_tracker_->TryCancelOnIOThread(
        child_id, route_id,
        prerender::FINAL_STATUS_INVALID_HTTP_METHOD);
    return false;
  }

  return true;
}

bool ChromeResourceDispatcherHostObserver::ShouldDeferStart(
    net::URLRequest* request,
    const content::ResourceContext& resource_context) {
  ResourceDispatcherHostRequestInfo* info =
      resource_dispatcher_host_->InfoForRequest(request);
  return prerender_tracker_->PotentiallyDelayRequestOnIOThread(
      request->url(), resource_context.prerender_manager(),
      info->child_id(), info->route_id(), info->request_id());
}

void ChromeResourceDispatcherHostObserver::MutateLoadFlags(int child_id,
                                                           int route_id,
                                                           int* load_flags) {
  DCHECK(load_flags);
  if (prerender_tracker_->IsPrerenderingOnIOThread(child_id, route_id))
    *load_flags |= net::LOAD_PRERENDERING;
}

bool ChromeResourceDispatcherHostObserver::AcceptSSLClientCertificateRequest(
    net::URLRequest* request, net::SSLCertRequestInfo* cert_request_info) {
  if (request->load_flags() & net::LOAD_PREFETCH)
    return false;

  if (request->load_flags() & net::LOAD_PRERENDERING) {
    int child_id, route_id;
    if (ResourceDispatcherHost::RenderViewForRequest(
            request, &child_id, &route_id)) {
      if (prerender_tracker_->TryCancel(
              child_id, route_id,
              prerender::FINAL_STATUS_SSL_CLIENT_CERTIFICATE_REQUESTED)) {
        return false;
      }
    }
  }

  return true;
}

bool ChromeResourceDispatcherHostObserver::AcceptAuthRequest(
    net::URLRequest* request,
    net::AuthChallengeInfo* auth_info) {
  if (!(request->load_flags() & net::LOAD_PRERENDERING))
    return true;

  int child_id, route_id;
  if (!ResourceDispatcherHost::RenderViewForRequest(
          request, &child_id, &route_id)) {
    NOTREACHED();
    return true;
  }

  if (!prerender_tracker_->TryCancelOnIOThread(
          child_id, route_id, prerender::FINAL_STATUS_AUTH_NEEDED)) {
    return true;
  }

  return false;
}
