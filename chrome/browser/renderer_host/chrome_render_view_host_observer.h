// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_RENDERER_HOST_CHROME_RENDER_VIEW_HOST_OBSERVER_H_
#define CHROME_BROWSER_RENDERER_HOST_CHROME_RENDER_VIEW_HOST_OBSERVER_H_
#pragma once

#include "content/browser/renderer_host/render_view_host_observer.h"

// This class holds the Chrome specific parts of RenderViewHost, and has the
// same lifetime.
class ChromeRenderViewHostObserver : public RenderViewHostObserver {
 public:
  explicit ChromeRenderViewHostObserver(RenderViewHost* render_view_host);
  virtual ~ChromeRenderViewHostObserver();

  // RenderViewHostObserver overrides.
  virtual void Navigate(const ViewMsg_Navigate_Params& params) OVERRIDE;
  virtual bool OnMessageReceived(const IPC::Message& message) OVERRIDE;

 private:
  void OnDomOperationResponse(const std::string& json_string,
                              int automation_id);

  DISALLOW_COPY_AND_ASSIGN(ChromeRenderViewHostObserver);
};

#endif  // CHROME_BROWSER_RENDERER_HOST_CHROME_RENDER_VIEW_HOST_OBSERVER_H_
