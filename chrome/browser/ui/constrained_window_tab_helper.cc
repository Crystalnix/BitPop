// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/constrained_window_tab_helper.h"

#include "chrome/browser/ui/constrained_window.h"
#include "chrome/browser/ui/constrained_window_tab_helper_delegate.h"
#include "chrome/browser/ui/tab_contents/tab_contents_wrapper.h"
#include "chrome/common/render_messages.h"
#include "content/browser/renderer_host/render_view_host.h"
#include "content/browser/renderer_host/render_widget_host_view.h"
#include "content/public/browser/navigation_details.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "net/base/registry_controlled_domain.h"

using content::WebContents;

ConstrainedWindowTabHelper::ConstrainedWindowTabHelper(
    TabContentsWrapper* wrapper)
    : content::WebContentsObserver(wrapper->web_contents()),
      wrapper_(wrapper),
      delegate_(NULL) {
}

ConstrainedWindowTabHelper::~ConstrainedWindowTabHelper() {
  DCHECK(child_windows_.empty());
}

void ConstrainedWindowTabHelper::AddConstrainedDialog(
    ConstrainedWindow* window) {
  child_windows_.push_back(window);

  if (child_windows_.size() == 1) {
    window->ShowConstrainedWindow();
    BlockTabContent(true);
  }
}

void ConstrainedWindowTabHelper::CloseConstrainedWindows() {
  // Clear out any constrained windows since we are leaving this page entirely.
  // To ensure that we iterate over every element in child_windows_ we
  // need to use a copy of child_windows_. Otherwise if
  // window->CloseConstrainedWindow() modifies child_windows_ we could end up
  // skipping some elements.
  ConstrainedWindowList child_windows_copy(child_windows_);
  for (ConstrainedWindowList::iterator it = child_windows_copy.begin();
       it != child_windows_copy.end(); ++it) {
    ConstrainedWindow* window = *it;
    if (window) {
      window->CloseConstrainedWindow();
      BlockTabContent(false);
    }
  }
}

void ConstrainedWindowTabHelper::WillClose(ConstrainedWindow* window) {
  ConstrainedWindowList::iterator i(
      std::find(child_windows_.begin(), child_windows_.end(), window));
  bool removed_topmost_window = i == child_windows_.begin();
  if (i != child_windows_.end())
    child_windows_.erase(i);
  if (child_windows_.empty()) {
    BlockTabContent(false);
  } else {
    if (removed_topmost_window)
      child_windows_[0]->ShowConstrainedWindow();
    BlockTabContent(true);
  }
}

void ConstrainedWindowTabHelper::BlockTabContent(bool blocked) {
  WebContents* contents = web_contents();
  if (!contents) {
    // The TabContents has already disconnected.
    return;
  }

  // RenderViewHost may be NULL during shutdown.
  RenderViewHost* host = contents->GetRenderViewHost();
  if (host) {
    host->set_ignore_input_events(blocked);
    host->Send(
        new ChromeViewMsg_SetVisuallyDeemphasized(host->routing_id(), blocked));
  }
  if (delegate_)
    delegate_->SetTabContentBlocked(wrapper_, blocked);
}

void ConstrainedWindowTabHelper::DidNavigateMainFrame(
    const content::LoadCommittedDetails& details,
    const content::FrameNavigateParams& params) {
  // Close constrained windows if necessary.
  if (!net::RegistryControlledDomainService::SameDomainOrHost(
          details.previous_url, details.entry->GetURL()))
    CloseConstrainedWindows();
}

void ConstrainedWindowTabHelper::DidGetIgnoredUIEvent() {
  if (constrained_window_count()) {
    ConstrainedWindow* window = *constrained_window_begin();
    window->FocusConstrainedWindow();
  }
}

void ConstrainedWindowTabHelper::WebContentsDestroyed(WebContents* tab) {
  // First cleanly close all child windows.
  // TODO(mpcomplete): handle case if MaybeCloseChildWindows() already asked
  // some of these to close.  CloseWindows is async, so it might get called
  // twice before it runs.
  CloseConstrainedWindows();
}
