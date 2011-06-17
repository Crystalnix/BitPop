// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/render_view_observer.h"

#include "content/renderer/render_view.h"

using WebKit::WebFrame;

RenderViewObserver::RenderViewObserver(RenderView* render_view)
    : render_view_(render_view),
      routing_id_(render_view ? render_view->routing_id() : 0) {
  // |render_view| can be NULL on unit testing.
  if (render_view_)
    render_view_->AddObserver(this);
}

RenderViewObserver::~RenderViewObserver() {
  if (render_view_)
    render_view_->RemoveObserver(this);
}

void RenderViewObserver::OnDestruct() {
  delete this;
}

bool RenderViewObserver::OnMessageReceived(const IPC::Message& message) {
  return false;
}

bool RenderViewObserver::Send(IPC::Message* message) {
  if (render_view_)
    return render_view_->Send(message);

  delete message;
  return false;
}

bool RenderViewObserver::AllowImages(WebFrame* frame,
                                     bool enabled_per_settings) {
  return true;
}

bool RenderViewObserver::AllowPlugins(WebFrame* frame,
                                      bool enabled_per_settings) {
  return true;
}

bool RenderViewObserver::AllowScript(WebFrame* frame,
                                     bool enabled_per_settings) {
  return true;
}
