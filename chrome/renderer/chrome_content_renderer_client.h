// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_RENDERER_CHROME_CONTENT_RENDERER_CLIENT_H_
#define CHROME_RENDERER_CHROME_CONTENT_RENDERER_CLIENT_H_
#pragma once

#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "content/renderer/content_renderer_client.h"

class ChromeRenderProcessObserver;
class ExtensionDispatcher;
class RendererHistogramSnapshots;
class RendererNetPredictor;
class SpellCheck;
class VisitedLinkSlave;

namespace safe_browsing {
class PhishingClassifierFilter;
}

namespace webkit {
namespace npapi {
class PluginGroup;
}
}

namespace chrome {

class ChromeContentRendererClient : public content::ContentRendererClient {
 public:
  ChromeContentRendererClient();
  ~ChromeContentRendererClient();

  virtual void RenderThreadStarted() OVERRIDE;
  virtual void RenderViewCreated(RenderView* render_view) OVERRIDE;
  virtual void SetNumberOfViews(int number_of_views) OVERRIDE;
  virtual SkBitmap* GetSadPluginBitmap() OVERRIDE;
  virtual std::string GetDefaultEncoding() OVERRIDE;
  virtual WebKit::WebPlugin* CreatePlugin(
      RenderView* render_view,
      WebKit::WebFrame* frame,
      const WebKit::WebPluginParams& params) OVERRIDE;
  virtual void ShowErrorPage(RenderView* render_view,
                             WebKit::WebFrame* frame,
                             int http_status_code) OVERRIDE;
  virtual std::string GetNavigationErrorHtml(
      const WebKit::WebURLRequest& failed_request,
      const WebKit::WebURLError& error) OVERRIDE;
  virtual bool RunIdleHandlerWhenWidgetsHidden() OVERRIDE;
  virtual bool AllowPopup(const GURL& creator) OVERRIDE;
  virtual bool ShouldFork(WebKit::WebFrame* frame,
                          const GURL& url,
                          bool is_content_initiated,
                          bool* send_referrer) OVERRIDE;
  virtual bool WillSendRequest(WebKit::WebFrame* frame,
                               const GURL& url,
                               GURL* new_url) OVERRIDE;
  virtual FilePath GetMediaLibraryPath() OVERRIDE;
  virtual bool ShouldPumpEventsDuringCookieMessage() OVERRIDE;
  virtual void DidCreateScriptContext(WebKit::WebFrame* frame) OVERRIDE;
  virtual void DidDestroyScriptContext(WebKit::WebFrame* frame) OVERRIDE;
  virtual void DidCreateIsolatedScriptContext(WebKit::WebFrame* frame) OVERRIDE;
  virtual unsigned long long VisitedLinkHash(const char* canonical_url,
                                             size_t length) OVERRIDE;
  virtual bool IsLinkVisited(unsigned long long link_hash) OVERRIDE;
  virtual void PrefetchHostName(const char* hostname, size_t length) OVERRIDE;
  virtual bool ShouldOverridePageVisibilityState(
      const RenderView* render_view,
      WebKit::WebPageVisibilityState* override_state) const OVERRIDE;

  // For testing.
  void SetExtensionDispatcher(ExtensionDispatcher* extension_dispatcher);

 private:
  WebKit::WebPlugin* CreatePluginPlaceholder(
      RenderView* render_view,
      WebKit::WebFrame* frame,
      const WebKit::WebPluginParams& params,
      const webkit::npapi::PluginGroup& group,
      int resource_id,
      int message_id,
      bool is_blocked_for_prerendering,
      bool allow_loading);

  // Returns true if the frame is navigating to an URL either into or out of an
  // extension app's extent.
  bool CrossesExtensionExtents(WebKit::WebFrame* frame, const GURL& new_url);

  scoped_ptr<ChromeRenderProcessObserver> chrome_observer_;
  scoped_ptr<ExtensionDispatcher> extension_dispatcher_;
  scoped_ptr<RendererHistogramSnapshots> histogram_snapshots_;
  scoped_ptr<RendererNetPredictor> net_predictor_;
  scoped_ptr<SpellCheck> spellcheck_;
  scoped_ptr<VisitedLinkSlave> visited_link_slave_;
  scoped_ptr<safe_browsing::PhishingClassifierFilter> phishing_classifier_;
};

}  // namespace chrome

#endif  // CHROME_RENDERER_CHROME_CONTENT_RENDERER_CLIENT_H_
