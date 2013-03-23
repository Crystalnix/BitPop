// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/public/test/layouttest_support.h"

#include "base/callback.h"
#include "base/lazy_instance.h"
#include "content/renderer/render_view_impl.h"
#include "content/renderer/renderer_webapplicationcachehost_impl.h"
#include "content/renderer/renderer_webkitplatformsupport_impl.h"
#include "third_party/WebKit/Source/Platform/chromium/public/WebGamepads.h"
#include "third_party/WebKit/Tools/DumpRenderTree/chromium/TestRunner/public/WebTestProxy.h"

using WebKit::WebGamepads;
using WebTestRunner::WebTestProxy;
using WebTestRunner::WebTestProxyBase;

namespace content {

namespace {

base::LazyInstance<base::Callback<void(RenderView*, WebTestProxyBase*)> >::Leaky
    g_callback = LAZY_INSTANCE_INITIALIZER;

RenderViewImpl* CreateWebTestProxy(RenderViewImplParams* params) {
  typedef WebTestProxy<RenderViewImpl, RenderViewImplParams*> ProxyType;
  ProxyType* render_view_proxy = new ProxyType(
      reinterpret_cast<RenderViewImplParams*>(params));
  if (g_callback == 0)
    return render_view_proxy;
  g_callback.Get().Run(
      static_cast<RenderView*>(render_view_proxy), render_view_proxy);
  return render_view_proxy;
}

}  // namespace


void EnableWebTestProxyCreation(
    const base::Callback<void(RenderView*, WebTestProxyBase*)>& callback) {
  g_callback.Get() = callback;
  RenderViewImpl::InstallCreateHook(CreateWebTestProxy);
}

void SetMockGamepads(const WebGamepads& pads) {
  RendererWebKitPlatformSupportImpl::SetMockGamepadsForTesting(pads);
}

void DisableAppCacheLogging() {
  RendererWebApplicationCacheHostImpl::DisableLoggingForTesting();
}

}  // namespace content
