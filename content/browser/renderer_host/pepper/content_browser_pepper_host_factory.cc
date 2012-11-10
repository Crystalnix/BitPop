// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/pepper/content_browser_pepper_host_factory.h"

#include "ppapi/host/resource_host.h"
#include "ppapi/proxy/ppapi_messages.h"

using ppapi::host::ResourceHost;

namespace content {

ContentBrowserPepperHostFactory::ContentBrowserPepperHostFactory(
    PepperMessageFilter* filter)
    : filter_(filter) {
}

ContentBrowserPepperHostFactory::~ContentBrowserPepperHostFactory() {
}

scoped_ptr<ResourceHost> ContentBrowserPepperHostFactory::CreateResourceHost(
    ppapi::host::PpapiHost* host,
    const ppapi::proxy::ResourceMessageCallParams& params,
    PP_Instance instance,
    const IPC::Message& message) {
  // TODO(brettw) implement hosts here.
  return scoped_ptr<ResourceHost>();
}

}  // namespace content
