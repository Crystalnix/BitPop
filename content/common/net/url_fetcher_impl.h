// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains URLFetcher, a wrapper around net::URLRequest that handles
// low-level details like thread safety, ref counting, and incremental buffer
// reading.  This is useful for callers who simply want to get the data from a
// URL and don't care about all the nitty-gritty details.
//
// NOTE(willchan): Only one "IO" thread is supported for URLFetcher.  This is a
// temporary situation.  We will work on allowing support for multiple "io"
// threads per process.

#ifndef CONTENT_COMMON_NET_URL_FETCHER_IMPL_H_
#define CONTENT_COMMON_NET_URL_FETCHER_IMPL_H_
#pragma once

#include "base/compiler_specific.h"
#include "base/message_loop.h"
#include "base/time.h"
#include "content/public/common/url_fetcher.h"

namespace content {
class URLFetcherFactory;
}

class CONTENT_EXPORT URLFetcherImpl : public content::URLFetcher{
 public:
  // |url| is the URL to send the request to.
  // |request_type| is the type of request to make.
  // |d| the object that will receive the callback on fetch completion.
  URLFetcherImpl(const GURL& url,
                 RequestType request_type,
                 content::URLFetcherDelegate* d);
  virtual ~URLFetcherImpl();

  // content::URLFetcher implementation:
  virtual void SetUploadData(const std::string& upload_content_type,
                             const std::string& upload_content) OVERRIDE;
  virtual void SetChunkedUpload(
      const std::string& upload_content_type) OVERRIDE;
  virtual void AppendChunkToUpload(const std::string& data,
                                   bool is_last_chunk) OVERRIDE;
  virtual void SetLoadFlags(int load_flags) OVERRIDE;
  virtual int GetLoadFlags() const OVERRIDE;
  virtual void SetReferrer(const std::string& referrer) OVERRIDE;
  virtual void SetExtraRequestHeaders(
      const std::string& extra_request_headers) OVERRIDE;
  virtual void GetExtraRequestHeaders(
      net::HttpRequestHeaders* headers) OVERRIDE;
  virtual void SetRequestContext(
      net::URLRequestContextGetter* request_context_getter) OVERRIDE;
  virtual void SetAutomaticallyRetryOn5xx(bool retry) OVERRIDE;
  virtual void SetMaxRetries(int max_retries) OVERRIDE;
  virtual int GetMaxRetries() const OVERRIDE;
  virtual base::TimeDelta GetBackoffDelay() const OVERRIDE;
  virtual void SaveResponseToTemporaryFile(
      scoped_refptr<base::MessageLoopProxy> file_message_loop_proxy) OVERRIDE;
  virtual net::HttpResponseHeaders* GetResponseHeaders() const OVERRIDE;
  virtual net::HostPortPair GetSocketAddress() const OVERRIDE;
  virtual bool WasFetchedViaProxy() const OVERRIDE;
  virtual void Start() OVERRIDE;
  virtual void StartWithRequestContextGetter(
      net::URLRequestContextGetter* request_context_getter) OVERRIDE;
  virtual const GURL& GetOriginalURL() const OVERRIDE;
  virtual const GURL& GetURL() const OVERRIDE;
  virtual const net::URLRequestStatus& GetStatus() const OVERRIDE;
  virtual int GetResponseCode() const OVERRIDE;
  virtual const net::ResponseCookies& GetCookies() const OVERRIDE;
  virtual bool FileErrorOccurred(
      base::PlatformFileError* out_error_code) const OVERRIDE;
  virtual void ReceivedContentWasMalformed() OVERRIDE;
  virtual bool GetResponseAsString(
      std::string* out_response_string) const OVERRIDE;
  virtual bool GetResponseAsFilePath(
      bool take_ownership,
      FilePath* out_response_path) const OVERRIDE;

  static void CancelAll();

 protected:
  // How should the response be stored?
  enum ResponseDestinationType {
    STRING,  // Default: In a std::string
    TEMP_FILE  // Write to a temp file
  };

  // Returns the delegate.
  content::URLFetcherDelegate* delegate() const;

  // Used by tests.
  const std::string& upload_data() const;

  // Used by tests.
  void set_was_fetched_via_proxy(bool flag);

  // Used by tests.
  void set_response_headers(scoped_refptr<net::HttpResponseHeaders> headers);

 private:
  friend class ScopedURLFetcherFactory;
  friend class TestURLFetcher;
  friend class URLFetcherTest;

  // Only used by URLFetcherTest, returns the number of URLFetcher::Core objects
  // actively running.
  static int GetNumFetcherCores();

  static content::URLFetcherFactory* factory();

  // Sets the factory used by the static method Create to create a URLFetcher.
  // URLFetcher does not take ownership of |factory|. A value of NULL results
  // in a URLFetcher being created directly.
  //
  // NOTE: for safety, this should only be used through ScopedURLFetcherFactory!
  static void set_factory(content::URLFetcherFactory* factory);

  class Core;
  scoped_refptr<Core> core_;

  DISALLOW_COPY_AND_ASSIGN(URLFetcherImpl);
};

#endif  // CONTENT_COMMON_NET_URL_FETCHER_IMPL_H_
