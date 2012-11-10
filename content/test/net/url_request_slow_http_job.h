// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// A URLRequestMockHTTPJob class that inserts a time delay in processing.

#ifndef CONTENT_TEST_NET_URL_REQUEST_SLOW_HTTP_JOB_H_
#define CONTENT_TEST_NET_URL_REQUEST_SLOW_HTTP_JOB_H_

#include "base/timer.h"
#include "content/test/net/url_request_mock_http_job.h"

class URLRequestSlowHTTPJob : public URLRequestMockHTTPJob {
 public:
  URLRequestSlowHTTPJob(net::URLRequest* request, const FilePath& file_path);

  static const int kDelayMs;

  static net::URLRequest::ProtocolFactory Factory;

  // Adds the testing URLs to the net::URLRequestFilter.
  static void AddUrlHandler(const FilePath& base_path);

  // Given the path to a file relative to the path passed to AddUrlHandler(),
  // construct a mock URL.
  static GURL GetMockUrl(const FilePath& path);

  virtual void Start() OVERRIDE;

 private:
  virtual ~URLRequestSlowHTTPJob();

  void RealStart();

  base::OneShotTimer<URLRequestSlowHTTPJob> delay_timer_;
};

#endif  // CONTENT_TEST_NET_URL_REQUEST_SLOW_HTTP_JOB_H_
