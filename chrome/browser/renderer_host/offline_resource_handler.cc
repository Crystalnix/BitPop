// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/renderer_host/offline_resource_handler.h"

#include <vector>

#include "base/bind.h"
#include "base/logging.h"
#include "base/memory/singleton.h"
#include "base/metrics/histogram.h"
#include "base/string_util.h"
#include "chrome/browser/chromeos/offline/offline_load_page.h"
#include "chrome/browser/net/chrome_url_request_context.h"
#include "chrome/common/url_constants.h"
#include "content/browser/appcache/chrome_appcache_service.h"
#include "content/browser/renderer_host/render_view_host.h"
#include "content/browser/renderer_host/resource_dispatcher_host.h"
#include "content/browser/renderer_host/resource_dispatcher_host_request_info.h"
#include "content/public/browser/browser_thread.h"
#include "net/base/net_errors.h"
#include "net/base/network_change_notifier.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_context.h"

using content::BrowserThread;
using content::WebContents;

namespace {

void ShowOfflinePage(
    int render_process_id,
    int render_view_id,
    const GURL& url,
    const chromeos::OfflineLoadPage::CompletionCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  // Check again on UI thread and proceed if it's connected.
  if (!net::NetworkChangeNotifier::IsOffline()) {
    BrowserThread::PostTask(
        BrowserThread::IO, FROM_HERE, base::Bind(callback, true));
  } else {
    RenderViewHost* render_view_host =
        RenderViewHost::FromID(render_process_id, render_view_id);
    WebContents* web_contents = render_view_host ?
        render_view_host->delegate()->GetAsWebContents() : NULL;
    // There is a chance that the tab closed after we decided to show
    // the offline page on the IO thread and before we actually show the
    // offline page here on the UI thread.
    if (web_contents)
      (new chromeos::OfflineLoadPage(web_contents, url, callback))->Show();
  }
}

}  // namespace

OfflineResourceHandler::OfflineResourceHandler(
    ResourceHandler* handler,
    int host_id,
    int route_id,
    ResourceDispatcherHost* rdh,
    net::URLRequest* request,
    ChromeAppCacheService* appcache_service)
    : next_handler_(handler),
      process_host_id_(host_id),
      render_view_id_(route_id),
      rdh_(rdh),
      request_(request),
      appcache_service_(appcache_service),
      deferred_request_id_(-1) {
  DCHECK(appcache_service_);
}

OfflineResourceHandler::~OfflineResourceHandler() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(appcache_completion_callback_.IsCancelled());
}

bool OfflineResourceHandler::OnUploadProgress(int request_id,
                                              uint64 position,
                                              uint64 size) {
  return next_handler_->OnUploadProgress(request_id, position, size);
}

bool OfflineResourceHandler::OnRequestRedirected(
    int request_id,
    const GURL& new_url,
    content::ResourceResponse* response,
    bool* defer) {
  return next_handler_->OnRequestRedirected(
      request_id, new_url, response, defer);
}

bool OfflineResourceHandler::OnResponseStarted(
    int request_id,
    content::ResourceResponse* response) {
  return next_handler_->OnResponseStarted(request_id, response);
}

bool OfflineResourceHandler::OnResponseCompleted(
    int request_id,
    const net::URLRequestStatus& status,
    const std::string& security_info) {
  return next_handler_->OnResponseCompleted(request_id, status, security_info);
}

void OfflineResourceHandler::OnRequestClosed() {
  if (!appcache_completion_callback_.IsCancelled())
    appcache_completion_callback_.Cancel();

  next_handler_->OnRequestClosed();
}

void OfflineResourceHandler::OnCanHandleOfflineComplete(int rv) {
  // Cancel() to break the circular reference cycle.
  appcache_completion_callback_.Cancel();

  if (deferred_request_id_ == -1) {
    DLOG(FATAL) << "OnCanHandleOfflineComplete called after completion: "
                << " this=" << this;
    return;
  }

  if (rv == net::OK) {
    Resume();
  } else {
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        base::Bind(&ShowOfflinePage,
                   process_host_id_,
                   render_view_id_,
                   request_->url(),
                   base::Bind(&OfflineResourceHandler::OnBlockingPageComplete,
                              this)));
  }
}

bool OfflineResourceHandler::OnWillStart(int request_id,
                                         const GURL& url,
                                         bool* defer) {
  if (!ShouldShowOfflinePage(url))
    return next_handler_->OnWillStart(request_id, url, defer);

  deferred_request_id_ = request_id;
  deferred_url_ = url;
  DVLOG(1) << "OnWillStart: this=" << this << ", request id=" << request_id
           << ", url=" << url;

  DCHECK(appcache_completion_callback_.IsCancelled());

  // |appcache_completion_callback_| holds a reference to |this|, so there is a
  // circular reference; however, either
  // OfflineResourceHandler::OnCanHandleOfflineComplete cancels the callback
  // (thus dropping the reference), or CanHandleMainResourceOffline calls the
  // callback which Resets it.
  appcache_completion_callback_.Reset(
      base::Bind(&OfflineResourceHandler::OnCanHandleOfflineComplete, this));
  appcache_service_->CanHandleMainResourceOffline(
      url, request_->first_party_for_cookies(),
      appcache_completion_callback_.callback());

  *defer = true;
  return true;
}

// We'll let the original event handler provide a buffer, and reuse it for
// subsequent reads until we're done buffering.
bool OfflineResourceHandler::OnWillRead(int request_id, net::IOBuffer** buf,
                                         int* buf_size, int min_size) {
  return next_handler_->OnWillRead(request_id, buf, buf_size, min_size);
}

bool OfflineResourceHandler::OnReadCompleted(int request_id, int* bytes_read) {
  return next_handler_->OnReadCompleted(request_id, bytes_read);
}

void OfflineResourceHandler::OnBlockingPageComplete(bool proceed) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  if (deferred_request_id_ == -1) {
    LOG(WARNING) << "OnBlockingPageComplete called after completion: "
                 << " this=" << this;
    NOTREACHED();
    return;
  }
  if (proceed) {
    Resume();
  } else {
    int request_id = deferred_request_id_;
    ClearRequestInfo();
    rdh_->CancelRequest(process_host_id_, request_id, false);
  }
}

void OfflineResourceHandler::ClearRequestInfo() {
  deferred_url_ = GURL();
  deferred_request_id_ = -1;
}

bool OfflineResourceHandler::IsRemote(const GURL& url) const {
  return url.SchemeIs(chrome::kFtpScheme) ||
         url.SchemeIs(chrome::kHttpScheme) ||
         url.SchemeIs(chrome::kHttpsScheme);
}

bool OfflineResourceHandler::ShouldShowOfflinePage(const GURL& url) const {
  // Only check main frame. If the network is disconnected while
  // loading other resources, we'll simply show broken link/images.
  return IsRemote(url) &&
      net::NetworkChangeNotifier::IsOffline() &&
      ResourceDispatcherHost::InfoForRequest(request_)->resource_type()
        == ResourceType::MAIN_FRAME;
}

void OfflineResourceHandler::Resume() {
  const GURL url = deferred_url_;
  int request_id = deferred_request_id_;
  ClearRequestInfo();

  bool defer = false;
  DVLOG(1) << "Resume load: this=" << this << ", request id=" << request_id;
  next_handler_->OnWillStart(request_id, url, &defer);
  if (!defer)
    rdh_->StartDeferredRequest(process_host_id_, request_id);
}
