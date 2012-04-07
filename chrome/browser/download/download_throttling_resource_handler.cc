// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/download/download_throttling_resource_handler.h"

#include "base/logging.h"
#include "chrome/browser/download/download_request_limiter.h"
#include "chrome/browser/download/download_util.h"
#include "content/browser/renderer_host/resource_dispatcher_host.h"
#include "content/public/common/resource_response.h"
#include "net/base/io_buffer.h"
#include "net/base/mime_sniffer.h"

DownloadThrottlingResourceHandler::DownloadThrottlingResourceHandler(
    ResourceHandler* next_handler,
    ResourceDispatcherHost* host,
    DownloadRequestLimiter* limiter,
    net::URLRequest* request,
    int render_process_host_id,
    int render_view_id,
    int request_id)
    : host_(host),
      request_(request),
      render_process_host_id_(render_process_host_id),
      render_view_id_(render_view_id),
      request_id_(request_id),
      next_handler_(next_handler),
      request_allowed_(false),
      request_closed_(false) {
  // Pause the request.
  host_->PauseRequest(render_process_host_id_, request_id_, true);

  limiter->CanDownloadOnIOThread(
      render_process_host_id_,
      render_view_id,
      request_id,
      base::Bind(&DownloadThrottlingResourceHandler::ContinueDownload, this));
}

DownloadThrottlingResourceHandler::~DownloadThrottlingResourceHandler() {
}

bool DownloadThrottlingResourceHandler::OnUploadProgress(int request_id,
                                                         uint64 position,
                                                         uint64 size) {
  DCHECK(!request_closed_);
  if (request_allowed_)
    return next_handler_->OnUploadProgress(request_id, position, size);
  return true;
}

bool DownloadThrottlingResourceHandler::OnRequestRedirected(
    int request_id,
    const GURL& url,
    content::ResourceResponse* response,
    bool* defer) {
  DCHECK(!request_closed_);
  if (request_allowed_) {
    return next_handler_->OnRequestRedirected(request_id, url, response, defer);
  }
  return true;
}

bool DownloadThrottlingResourceHandler::OnResponseStarted(
    int request_id,
    content::ResourceResponse* response) {
  DCHECK(!request_closed_);
  if (request_allowed_)
    return next_handler_->OnResponseStarted(request_id, response);

  NOTREACHED();
  return false;
}

bool DownloadThrottlingResourceHandler::OnWillStart(int request_id,
                                                    const GURL& url,
                                                    bool* defer) {
  DCHECK(!request_closed_);
  if (request_allowed_)
    return next_handler_->OnWillStart(request_id, url, defer);
  return true;
}

bool DownloadThrottlingResourceHandler::OnWillRead(int request_id,
                                                   net::IOBuffer** buf,
                                                   int* buf_size,
                                                   int min_size) {
  DCHECK(!request_closed_);
  if (request_allowed_)
    return next_handler_->OnWillRead(request_id, buf, buf_size, min_size);

  NOTREACHED();
  return false;
}

bool DownloadThrottlingResourceHandler::OnReadCompleted(int request_id,
                                                        int* bytes_read) {
  DCHECK(!request_closed_);
  if (!*bytes_read)
    return true;

  if (request_allowed_)
    return next_handler_->OnReadCompleted(request_id, bytes_read);

  NOTREACHED();
  return false;
}

bool DownloadThrottlingResourceHandler::OnResponseCompleted(
    int request_id,
    const net::URLRequestStatus& status,
    const std::string& security_info) {
  DCHECK(!request_closed_);
  if (request_allowed_)
    return next_handler_->OnResponseCompleted(request_id, status,
                                              security_info);

  // For a download, if ResourceDispatcher::Read() fails,
  // ResourceDispatcher::OnresponseStarted() will call
  // OnResponseCompleted(), and we will end up here with an error
  // status.
  if (!status.is_success())
    return false;
  NOTREACHED();
  return true;
}

void DownloadThrottlingResourceHandler::OnRequestClosed() {
  DCHECK(!request_closed_);
  if (request_allowed_)
    next_handler_->OnRequestClosed();
  request_closed_ = true;
}

void DownloadThrottlingResourceHandler::ContinueDownload(bool allow) {
  download_util::RecordDownloadCount(
        download_util::INITIATED_BY_NAVIGATION_COUNT);
  if (request_closed_)
    return;

  request_allowed_ = allow;
  if (allow) {
    // And let the request continue.
    host_->PauseRequest(render_process_host_id_, request_id_, false);
  } else {
    host_->CancelRequest(render_process_host_id_, request_id_, false);
  }
}
