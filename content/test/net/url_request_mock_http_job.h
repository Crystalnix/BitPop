// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// A net::URLRequestJob class that pulls the content and http headers from disk.

#ifndef CONTENT_TEST_NET_URL_REQUEST_MOCK_HTTP_JOB_H_
#define CONTENT_TEST_NET_URL_REQUEST_MOCK_HTTP_JOB_H_

#include <string>

#include "net/url_request/url_request_file_job.h"

class FilePath;

namespace content {

class URLRequestMockHTTPJob : public net::URLRequestFileJob {
 public:
  URLRequestMockHTTPJob(net::URLRequest* request,
                        net::NetworkDelegate* network_delegate,
                        const FilePath& file_path);

  virtual bool GetMimeType(std::string* mime_type) const OVERRIDE;
  virtual int GetResponseCode() const OVERRIDE;
  virtual bool GetCharset(std::string* charset) OVERRIDE;
  virtual void GetResponseInfo(net::HttpResponseInfo* info) OVERRIDE;
  virtual bool IsRedirectResponse(GURL* location,
                                  int* http_status_code) OVERRIDE;

  static net::URLRequest::ProtocolFactory Factory;

  // Adds the testing URLs to the net::URLRequestFilter.
  static void AddUrlHandler(const FilePath& base_path);

  // Given the path to a file relative to the path passed to AddUrlHandler(),
  // construct a mock URL.
  static GURL GetMockUrl(const FilePath& path);

  // Given the path to a file relative to the path passed to AddUrlHandler(),
  // construct a mock URL for view source.
  static GURL GetMockViewSourceUrl(const FilePath& path);

 protected:
  virtual ~URLRequestMockHTTPJob() { }

  static FilePath GetOnDiskPath(const FilePath& base_path,
                                net::URLRequest* request,
                                const std::string& scheme);

 private:
  void GetResponseInfoConst(net::HttpResponseInfo* info) const;
};

}  // namespace content

#endif  // CONTENT_TEST_NET_URL_REQUEST_MOCK_HTTP_JOB_H_
