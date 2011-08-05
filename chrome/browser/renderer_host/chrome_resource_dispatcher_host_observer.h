// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_RENDERER_HOST_CHROME_RESOURCE_DISPATCHER_HOST_OBSERVER_H_
#define CHROME_BROWSER_RENDERER_HOST_CHROME_RESOURCE_DISPATCHER_HOST_OBSERVER_H_
#pragma once

#include "content/browser/renderer_host/resource_dispatcher_host.h"

namespace prerender {
class PrerenderTracker;
}

// Implements ResourceDispatcherHost::Observer. Currently used by the Prerender
// system to abort requests and add to the load flags when a request begins.
class ChromeResourceDispatcherHostObserver
    : public ResourceDispatcherHost::Observer {
 public:
  // This class does not take ownership of the tracker but merely holds a
  // reference to it to avoid accessing g_browser_process.
  // Both |resource_dispatcher_host| and |prerender_tracker| must outlive
  // |this|.
  ChromeResourceDispatcherHostObserver(
      ResourceDispatcherHost* resource_dispatcher_host,
      prerender::PrerenderTracker* prerender_tracker);
  virtual ~ChromeResourceDispatcherHostObserver();

  // ResourceDispatcherHost::Observer implementation.
  virtual bool ShouldBeginRequest(
      int child_id, int route_id,
      const ResourceHostMsg_Request& request_data,
      const content::ResourceContext& resource_context,
      const GURL& referrer) OVERRIDE;

  virtual bool ShouldDeferStart(
      net::URLRequest* request,
      const content::ResourceContext& resource_context) OVERRIDE;

  virtual void MutateLoadFlags(int child_id, int route_id,
                               int* load_flags) OVERRIDE;

  virtual bool AcceptSSLClientCertificateRequest(
        net::URLRequest* request,
        net::SSLCertRequestInfo* cert_request_info) OVERRIDE;

  virtual bool AcceptAuthRequest(net::URLRequest* request,
                                 net::AuthChallengeInfo* auth_info) OVERRIDE;

 private:
  ResourceDispatcherHost* resource_dispatcher_host_;
  prerender::PrerenderTracker* prerender_tracker_;

  DISALLOW_COPY_AND_ASSIGN(ChromeResourceDispatcherHostObserver);
};

#endif  // CHROME_BROWSER_RENDERER_HOST_CHROME_RESOURCE_DISPATCHER_HOST_OBSERVER_H_
