// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/test/net/url_request_slow_http_job.h"

#include "base/time.h"
#include "base/utf_string_conversions.h"
#include "net/url_request/url_request_filter.h"

static const char kMockHostname[] = "mock.slow.http";

namespace {

// This is the file path leading to the root of the directory to use as the
// root of the http server. This returns a reference that can be assigned to.
FilePath& BasePath() {
  CR_DEFINE_STATIC_LOCAL(FilePath, base_path, ());
  return base_path;
}

}  // namespace

// static
const int URLRequestSlowHTTPJob::kDelayMs = 1000;

using base::TimeDelta;

// static
net::URLRequestJob* URLRequestSlowHTTPJob::Factory(net::URLRequest* request,
                                                   const std::string& scheme) {
  return new URLRequestSlowHTTPJob(request,
                                   GetOnDiskPath(BasePath(), request, scheme));
}

// static
void URLRequestSlowHTTPJob::AddUrlHandler(const FilePath& base_path) {
  BasePath() = base_path;

  // Add kMockHostname to net::URLRequestFilter.
  net::URLRequestFilter* filter = net::URLRequestFilter::GetInstance();
  filter->AddHostnameHandler("http", kMockHostname,
                             URLRequestSlowHTTPJob::Factory);
}

/* static */
GURL URLRequestSlowHTTPJob::GetMockUrl(const FilePath& path) {
  std::string url = "http://";
  url.append(kMockHostname);
  url.append("/");
  std::string path_str = path.MaybeAsASCII();
  DCHECK(!path_str.empty());  // We only expect ASCII paths in tests.
  url.append(path_str);
  return GURL(url);
}

URLRequestSlowHTTPJob::URLRequestSlowHTTPJob(net::URLRequest* request,
                                             const FilePath& file_path)
    : URLRequestMockHTTPJob(request, file_path) { }

void URLRequestSlowHTTPJob::Start() {
  delay_timer_.Start(FROM_HERE, TimeDelta::FromMilliseconds(kDelayMs), this,
                     &URLRequestSlowHTTPJob::RealStart);
}

URLRequestSlowHTTPJob::~URLRequestSlowHTTPJob() {
}

void URLRequestSlowHTTPJob::RealStart() {
  URLRequestMockHTTPJob::Start();
}
