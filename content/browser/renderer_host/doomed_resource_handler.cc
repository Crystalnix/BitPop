// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/doomed_resource_handler.h"

#include "net/url_request/url_request.h"

DoomedResourceHandler::DoomedResourceHandler(ResourceHandler* old_handler)
    : old_handler_(old_handler) {
}

DoomedResourceHandler::~DoomedResourceHandler() {
}

bool DoomedResourceHandler::OnUploadProgress(int request_id,
                                             uint64 position,
                                             uint64 size) {
  NOTREACHED();
  return true;
}

bool DoomedResourceHandler::OnRequestRedirected(
    int request_id,
    const GURL& new_url,
    content::ResourceResponse* response,
    bool* defer) {
  NOTREACHED();
  return true;
}

bool DoomedResourceHandler::OnResponseStarted(
    int request_id, content::ResourceResponse* response) {
  NOTREACHED();
  return true;
}

bool DoomedResourceHandler::OnWillStart(int request_id,
                                        const GURL& url,
                                        bool* defer) {
  NOTREACHED();
  return true;
}

bool DoomedResourceHandler::OnWillRead(int request_id,
                                       net::IOBuffer** buf,
                                       int* buf_size,
                                       int min_size) {
  NOTREACHED();
  return true;
}

bool DoomedResourceHandler::OnReadCompleted(int request_id,
                                            int* bytes_read) {
  NOTREACHED();
  return true;
}

bool DoomedResourceHandler::OnResponseCompleted(
    int request_id,
    const net::URLRequestStatus& status,
    const std::string& security_info) {
  DCHECK(status.status() == net::URLRequestStatus::CANCELED ||
         status.status() == net::URLRequestStatus::FAILED);
  return true;
}

void DoomedResourceHandler::OnRequestClosed() {
}

void DoomedResourceHandler::OnDataDownloaded(int request_id,
                                             int bytes_downloaded) {
  NOTREACHED();
}
