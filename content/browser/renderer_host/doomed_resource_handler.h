// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RENDERER_HOST_DOOMED_RESOURCE_HANDLER_H_
#define CONTENT_BROWSER_RENDERER_HOST_DOOMED_RESOURCE_HANDLER_H_
#pragma once

#include "base/memory/ref_counted.h"
#include "content/browser/renderer_host/resource_handler.h"

// ResourceHandler that DCHECKs on all events but canceling and failing of
// requests while activated for a URLRequest.
class DoomedResourceHandler : public ResourceHandler {
 public:
  // As the DoomedResourceHandler is constructed and substituted from code
  // of another ResourceHandler, we need to make sure that this other handler
  // does not lose its last reference and gets destroyed by being substituted.
  // Therefore, we retain a reference to |old_handler| that prevents the
  // destruction.
  explicit DoomedResourceHandler(ResourceHandler* old_handler);

  // ResourceHandler implementation:
  virtual bool OnUploadProgress(int request_id,
                                uint64 position,
                                uint64 size) OVERRIDE;
  virtual bool OnRequestRedirected(int request_id,
                                   const GURL& new_url,
                                   content::ResourceResponse* response,
                                   bool* defer) OVERRIDE;
  virtual bool OnResponseStarted(int request_id,
                                 content::ResourceResponse* response) OVERRIDE;
  virtual bool OnWillStart(int request_id,
                           const GURL& url,
                           bool* defer) OVERRIDE;
  virtual bool OnWillRead(int request_id,
                          net::IOBuffer** buf,
                          int* buf_size,
                          int min_size) OVERRIDE;
  virtual bool OnReadCompleted(int request_id,
                               int* bytes_read) OVERRIDE;
  virtual bool OnResponseCompleted(int request_id,
                                   const net::URLRequestStatus& status,
                                   const std::string& security_info) OVERRIDE;
  virtual void OnRequestClosed() OVERRIDE;
  virtual void OnDataDownloaded(int request_id,
                                int bytes_downloaded) OVERRIDE;

 private:
  virtual ~DoomedResourceHandler();

  scoped_refptr<ResourceHandler> old_handler_;

  DISALLOW_COPY_AND_ASSIGN(DoomedResourceHandler);
};

#endif  // CONTENT_BROWSER_RENDERER_HOST_DOOMED_RESOURCE_HANDLER_H_
