// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/public/browser/web_contents_observer.h"

#include "content/browser/renderer_host/render_view_host.h"
#include "content/browser/tab_contents/tab_contents.h"
#include "content/public/browser/navigation_details.h"

namespace content {

WebContentsObserver::WebContentsObserver(WebContents* web_contents)
    : tab_contents_(NULL) {
  Observe(web_contents);
}

WebContentsObserver::WebContentsObserver()
    : tab_contents_(NULL) {
}

WebContentsObserver::~WebContentsObserver() {
  if (tab_contents_)
    tab_contents_->RemoveObserver(this);
}

WebContents* WebContentsObserver::web_contents() const {
  return tab_contents_;
}

void WebContentsObserver::Observe(WebContents* web_contents) {
  if (tab_contents_)
    tab_contents_->RemoveObserver(this);
  tab_contents_ = static_cast<TabContents*>(web_contents);
  if (tab_contents_) {
    tab_contents_->AddObserver(this);
  }
}

bool WebContentsObserver::OnMessageReceived(const IPC::Message& message) {
  return false;
}

bool WebContentsObserver::Send(IPC::Message* message) {
  if (!tab_contents_ || !tab_contents_->GetRenderViewHost()) {
    delete message;
    return false;
  }

  return tab_contents_->GetRenderViewHost()->Send(message);
}

int WebContentsObserver::routing_id() const {
  if (!tab_contents_ || !tab_contents_->GetRenderViewHost())
    return MSG_ROUTING_NONE;

  return tab_contents_->GetRenderViewHost()->routing_id();
}

void WebContentsObserver::TabContentsDestroyed() {
  // Do cleanup so that 'this' can safely be deleted from WebContentsDestroyed.
  tab_contents_->RemoveObserver(this);
  TabContents* tab = tab_contents_;
  tab_contents_ = NULL;
  WebContentsDestroyed(tab);
}

}  // namespace content
