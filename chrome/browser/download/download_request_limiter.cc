// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/download/download_request_limiter.h"

#include "base/bind.h"
#include "base/stl_util.h"
#include "chrome/browser/download/download_request_infobar_delegate.h"
#include "chrome/browser/infobars/infobar_tab_helper.h"
#include "chrome/browser/tab_contents/tab_util.h"
#include "chrome/browser/ui/blocked_content/blocked_content_tab_helper.h"
#include "chrome/browser/ui/blocked_content/blocked_content_tab_helper_delegate.h"
#include "chrome/browser/ui/tab_contents/tab_contents_wrapper.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"

using content::BrowserThread;
using content::NavigationController;
using content::NavigationEntry;
using content::WebContents;

// TabDownloadState ------------------------------------------------------------

DownloadRequestLimiter::TabDownloadState::TabDownloadState(
    DownloadRequestLimiter* host,
    NavigationController* controller,
    NavigationController* originating_controller)
    : host_(host),
      controller_(controller),
      status_(DownloadRequestLimiter::ALLOW_ONE_DOWNLOAD),
      download_count_(0),
      infobar_(NULL) {
  content::Source<NavigationController> notification_source(controller);
  registrar_.Add(this, content::NOTIFICATION_NAV_ENTRY_PENDING,
                 notification_source);
  registrar_.Add(this, content::NOTIFICATION_TAB_CLOSED, notification_source);

  NavigationEntry* active_entry = originating_controller ?
      originating_controller->GetActiveEntry() : controller->GetActiveEntry();
  if (active_entry)
    initial_page_host_ = active_entry->GetURL().host();
}

DownloadRequestLimiter::TabDownloadState::~TabDownloadState() {
  // We should only be destroyed after the callbacks have been notified.
  DCHECK(callbacks_.empty());

  // And we should have closed the infobar.
  DCHECK(!infobar_);
}

void DownloadRequestLimiter::TabDownloadState::OnUserGesture() {
  if (is_showing_prompt()) {
    // Don't change the state if the user clicks on the page some where.
    return;
  }

  if (status_ != DownloadRequestLimiter::ALLOW_ALL_DOWNLOADS &&
      status_ != DownloadRequestLimiter::DOWNLOADS_NOT_ALLOWED) {
    // Revert to default status.
    host_->Remove(this);
    // WARNING: We've been deleted.
    return;
  }
}

void DownloadRequestLimiter::TabDownloadState::PromptUserForDownload(
    WebContents* tab,
    const DownloadRequestLimiter::Callback& callback) {
  callbacks_.push_back(callback);

  if (is_showing_prompt())
    return;  // Already showing prompt.

  if (DownloadRequestLimiter::delegate_) {
    NotifyCallbacks(DownloadRequestLimiter::delegate_->ShouldAllowDownload());
  } else {
    InfoBarTabHelper* infobar_helper =
        TabContentsWrapper::GetCurrentWrapperForContents(tab)->
            infobar_tab_helper();
    infobar_ = new DownloadRequestInfoBarDelegate(infobar_helper, this);
    infobar_helper->AddInfoBar(infobar_);
  }
}

void DownloadRequestLimiter::TabDownloadState::Cancel() {
  NotifyCallbacks(false);
}

void DownloadRequestLimiter::TabDownloadState::Accept() {
  NotifyCallbacks(true);
}

void DownloadRequestLimiter::TabDownloadState::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  if ((type != content::NOTIFICATION_NAV_ENTRY_PENDING &&
       type != content::NOTIFICATION_TAB_CLOSED) ||
      content::Source<NavigationController>(source).ptr() != controller_) {
    NOTREACHED();
    return;
  }

  switch (type) {
    case content::NOTIFICATION_NAV_ENTRY_PENDING: {
      // NOTE: resetting state on a pending navigate isn't ideal. In particular
      // it is possible that queued up downloads for the page before the
      // pending navigate will be delivered to us after we process this
      // request. If this happens we may let a download through that we
      // shouldn't have. But this is rather rare, and it is difficult to get
      // 100% right, so we don't deal with it.
      NavigationEntry* entry = controller_->GetPendingEntry();
      if (!entry)
        return;

      if (content::PageTransitionIsRedirect(entry->GetTransitionType())) {
        // Redirects don't count.
        return;
      }

      if (status_ == DownloadRequestLimiter::ALLOW_ALL_DOWNLOADS ||
          status_ == DownloadRequestLimiter::DOWNLOADS_NOT_ALLOWED) {
        // User has either allowed all downloads or canceled all downloads. Only
        // reset the download state if the user is navigating to a different
        // host (or host is empty).
        if (!initial_page_host_.empty() && !entry->GetURL().host().empty() &&
            entry->GetURL().host() == initial_page_host_) {
          return;
        }
      }
      break;
    }

    case content::NOTIFICATION_TAB_CLOSED:
      // Tab closed, no need to handle closing the dialog as it's owned by the
      // TabContents, break so that we get deleted after switch.
      break;

    default:
      NOTREACHED();
  }

  NotifyCallbacks(false);
  host_->Remove(this);
}

void DownloadRequestLimiter::TabDownloadState::NotifyCallbacks(bool allow) {
  status_ = allow ?
      DownloadRequestLimiter::ALLOW_ALL_DOWNLOADS :
      DownloadRequestLimiter::DOWNLOADS_NOT_ALLOWED;
  std::vector<DownloadRequestLimiter::Callback> callbacks;
  bool change_status = false;

  // Selectively send first few notifications only if number of downloads exceed
  // kMaxDownloadsAtOnce. In that case, we also retain the infobar instance and
  // don't close it. If allow is false, we send all the notifications to cancel
  // all remaining downloads and close the infobar.
  if (!allow || (callbacks_.size() < kMaxDownloadsAtOnce)) {
    if (infobar_) {
      // Reset the delegate so we don't get notified again.
      infobar_->set_host(NULL);
      infobar_ = NULL;
    }
    callbacks.swap(callbacks_);
  } else {
    std::vector<DownloadRequestLimiter::Callback>::iterator start, end;
    start = callbacks_.begin();
    end = callbacks_.begin() + kMaxDownloadsAtOnce;
    callbacks.assign(start, end);
    callbacks_.erase(start, end);
    change_status = true;
  }

  for (size_t i = 0; i < callbacks.size(); ++i)
    host_->ScheduleNotification(callbacks[i], allow);

  if (change_status)
    status_ = DownloadRequestLimiter::PROMPT_BEFORE_DOWNLOAD;
}

// DownloadRequestLimiter ------------------------------------------------------

DownloadRequestLimiter::DownloadRequestLimiter() {
}

DownloadRequestLimiter::~DownloadRequestLimiter() {
  // All the tabs should have closed before us, which sends notification and
  // removes from state_map_. As such, there should be no pending callbacks.
  DCHECK(state_map_.empty());
}

DownloadRequestLimiter::DownloadStatus
    DownloadRequestLimiter::GetDownloadStatus(WebContents* tab) {
  TabDownloadState* state = GetDownloadState(&tab->GetController(), NULL, false);
  return state ? state->download_status() : ALLOW_ONE_DOWNLOAD;
}

void DownloadRequestLimiter::CanDownloadOnIOThread(int render_process_host_id,
                                                   int render_view_id,
                                                   int request_id,
                                                   const Callback& callback) {
  // This is invoked on the IO thread. Schedule the task to run on the UI
  // thread so that we can query UI state.
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&DownloadRequestLimiter::CanDownload, this,
                 render_process_host_id, render_view_id, request_id, callback));
}

void DownloadRequestLimiter::OnUserGesture(WebContents* tab) {
  TabDownloadState* state =
      GetDownloadState(&tab->GetController(), NULL, false);
  if (!state)
    return;

  state->OnUserGesture();
}

// static
void DownloadRequestLimiter::SetTestingDelegate(TestingDelegate* delegate) {
  delegate_ = delegate;
}

DownloadRequestLimiter::TabDownloadState* DownloadRequestLimiter::
    GetDownloadState(NavigationController* controller,
                     NavigationController* originating_controller,
                     bool create) {
  DCHECK(controller);
  StateMap::iterator i = state_map_.find(controller);
  if (i != state_map_.end())
    return i->second;

  if (!create)
    return NULL;

  TabDownloadState* state =
      new TabDownloadState(this, controller, originating_controller);
  state_map_[controller] = state;
  return state;
}

void DownloadRequestLimiter::CanDownload(int render_process_host_id,
                                         int render_view_id,
                                         int request_id,
                                         const Callback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  WebContents* originating_tab =
      tab_util::GetWebContentsByID(render_process_host_id, render_view_id);
  if (!originating_tab) {
    // The tab was closed, don't allow the download.
    ScheduleNotification(callback, false);
    return;
  }

  CanDownloadImpl(
      TabContentsWrapper::GetCurrentWrapperForContents(originating_tab),
      request_id,
      callback);
}

void DownloadRequestLimiter::CanDownloadImpl(
    TabContentsWrapper* originating_tab,
    int request_id,
    const Callback& callback) {
  DCHECK(originating_tab);

  // FYI: Chrome Frame overrides CanDownload in ExternalTabContainer in order
  // to cancel the download operation in chrome and let the host browser
  // take care of it.
  WebContents* tab = originating_tab->web_contents();
  if (tab->GetDelegate() && !tab->GetDelegate()->CanDownload(tab, request_id)) {
    ScheduleNotification(callback, false);
    return;
  }

  // If the tab requesting the download is a constrained popup that is not
  // shown, treat the request as if it came from the parent.
  TabContentsWrapper* effective_wrapper = originating_tab;
  if (effective_wrapper->blocked_content_tab_helper()->delegate()) {
    effective_wrapper = effective_wrapper->blocked_content_tab_helper()->
        delegate()->GetConstrainingContentsWrapper(effective_wrapper);
  }

  TabDownloadState* state = GetDownloadState(
      &effective_wrapper->web_contents()->GetController(),
      &tab->GetController(), true);
  switch (state->download_status()) {
    case ALLOW_ALL_DOWNLOADS:
      if (state->download_count() && !(state->download_count() %
            DownloadRequestLimiter::kMaxDownloadsAtOnce))
        state->set_download_status(PROMPT_BEFORE_DOWNLOAD);
      ScheduleNotification(callback, true);
      state->increment_download_count();
      break;

    case ALLOW_ONE_DOWNLOAD:
      state->set_download_status(PROMPT_BEFORE_DOWNLOAD);
      ScheduleNotification(callback, true);
      break;

    case DOWNLOADS_NOT_ALLOWED:
      ScheduleNotification(callback, false);
      break;

    case PROMPT_BEFORE_DOWNLOAD:
      state->PromptUserForDownload(effective_wrapper->web_contents(), callback);
      state->increment_download_count();
      break;

    default:
      NOTREACHED();
  }
}

void DownloadRequestLimiter::ScheduleNotification(const Callback& callback,
                                                  bool allow) {
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE, base::Bind(callback, allow));
}

void DownloadRequestLimiter::Remove(TabDownloadState* state) {
  DCHECK(ContainsKey(state_map_, state->controller()));
  state_map_.erase(state->controller());
  delete state;
}

// static
DownloadRequestLimiter::TestingDelegate* DownloadRequestLimiter::delegate_ =
    NULL;
