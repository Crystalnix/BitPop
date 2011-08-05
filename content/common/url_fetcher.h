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

#ifndef CONTENT_COMMON_NET_URL_FETCHER_H_
#define CONTENT_COMMON_NET_URL_FETCHER_H_
#pragma once

#include <string>
#include <vector>

#include "base/memory/ref_counted.h"
#include "base/message_loop.h"
#include "base/platform_file.h"
#include "base/time.h"

class FilePath;
class GURL;

namespace base {
class MessageLoopProxy;
}  // namespace base

namespace net {
class HostPortPair;
class HttpResponseHeaders;
class URLRequestContextGetter;
class URLRequestStatus;
typedef std::vector<std::string> ResponseCookies;
}  // namespace net

// To use this class, create an instance with the desired URL and a pointer to
// the object to be notified when the URL has been loaded:
//   URLFetcher* fetcher = new URLFetcher("http://www.google.com",
//                                        URLFetcher::GET, this);
//
// Then, optionally set properties on this object, like the request context or
// extra headers:
//   fetcher->SetExtraRequestHeaders("X-Foo: bar");
//
// Finally, start the request:
//   fetcher->Start();
//
//
// The object you supply as a delegate must inherit from URLFetcher::Delegate;
// when the fetch is completed, OnURLFetchComplete() will be called with a
// pointer to the URLFetcher.  From that point until the original URLFetcher
// instance is destroyed, you may use accessor methods to see the result of
// the fetch. You should copy these objects if you need them to live longer
// than the URLFetcher instance. If the URLFetcher instance is destroyed
// before the callback happens, the fetch will be canceled and no callback
// will occur.
//
// You may create the URLFetcher instance on any thread; OnURLFetchComplete()
// will be called back on the same thread you use to create the instance.
//
//
// NOTE: By default URLFetcher requests are NOT intercepted, except when
// interception is explicitly enabled in tests.

class URLFetcher {
 public:
  enum RequestType {
    GET,
    POST,
    HEAD,
  };

  // Imposible http response code. Used to signal that no http response code
  // was received.
  static const int kInvalidHttpResponseCode;

  class Delegate {
   public:
    // TODO(skerner): This will be removed in favor of the |source|-only
    // version below. Leaving this for now to make the initial code review
    // easy to read.
    virtual void OnURLFetchComplete(const URLFetcher* source,
                                    const GURL& url,
                                    const net::URLRequestStatus& status,
                                    int response_code,
                                    const net::ResponseCookies& cookies,
                                    const std::string& data);

    // This will be called when the URL has been fetched, successfully or not.
    // Use accessor methods on |source| to get the results.
    virtual void OnURLFetchComplete(const URLFetcher* source);

   protected:
    virtual ~Delegate() {}
  };

  // URLFetcher::Create uses the currently registered Factory to create the
  // URLFetcher. Factory is intended for testing.
  class Factory {
   public:
    virtual URLFetcher* CreateURLFetcher(int id,
                                         const GURL& url,
                                         RequestType request_type,
                                         Delegate* d) = 0;

   protected:
    virtual ~Factory() {}
  };

  // |url| is the URL to send the request to.
  // |request_type| is the type of request to make.
  // |d| the object that will receive the callback on fetch completion.
  URLFetcher(const GURL& url, RequestType request_type, Delegate* d);

  virtual ~URLFetcher();

  // Sets the factory used by the static method Create to create a URLFetcher.
  // URLFetcher does not take ownership of |factory|. A value of NULL results
  // in a URLFetcher being created directly.
#if defined(UNIT_TEST)
  static void set_factory(Factory* factory) { factory_ = factory; }
#endif

  // Normally interception is disabled for URLFetcher, but you can use this
  // to enable it for tests. Also see the set_factory method for another way
  // of testing code that uses an URLFetcher.
  static void enable_interception_for_tests(bool enabled) {
    g_interception_enabled = enabled;
  }

  // Creates a URLFetcher, ownership returns to the caller. If there is no
  // Factory (the default) this creates and returns a new URLFetcher. See the
  // constructor for a description of the args. |id| may be used during testing
  // to identify who is creating the URLFetcher.
  static URLFetcher* Create(int id, const GURL& url, RequestType request_type,
                            Delegate* d);

  // Sets data only needed by POSTs.  All callers making POST requests should
  // call this before the request is started.  |upload_content_type| is the MIME
  // type of the content, while |upload_content| is the data to be sent (the
  // Content-Length header value will be set to the length of this data).
  void set_upload_data(const std::string& upload_content_type,
                       const std::string& upload_content);

  // Indicates that the POST data is sent via chunked transfer encoding.
  // This may only be called before calling Start().
  // Use AppendChunkToUpload() to give the data chunks after calling Start().
  void set_chunked_upload(const std::string& upload_content_type);

  // Adds the given bytes to a request's POST data transmitted using chunked
  // transfer encoding.
  // This method should be called ONLY after calling Start().
  virtual void AppendChunkToUpload(const std::string& data, bool is_last_chunk);

  // Set one or more load flags as defined in net/base/load_flags.h.  Must be
  // called before the request is started.
  void set_load_flags(int load_flags);

  // Returns the current load flags.
  int load_flags() const;

  // The referrer URL for the request. Must be called before the request is
  // started.
  void set_referrer(const std::string& referrer);

  // Set extra headers on the request.  Must be called before the request
  // is started.
  void set_extra_request_headers(const std::string& extra_request_headers);

  // Set the net::URLRequestContext on the request.  Must be called before the
  // request is started.
  void set_request_context(
      net::URLRequestContextGetter* request_context_getter);

  // If |retry| is false, 5xx responses will be propagated to the observer,
  // if it is true URLFetcher will automatically re-execute the request,
  // after backoff_delay() elapses. URLFetcher has it set to true by default.
  void set_automatically_retry_on_5xx(bool retry);

  int max_retries() const { return max_retries_; }

  void set_max_retries(int max_retries) { max_retries_ = max_retries; }

  // Returns the back-off delay before the request will be retried,
  // when a 5xx response was received.
  base::TimeDelta backoff_delay() const { return backoff_delay_; }

  // Sets the back-off delay, allowing to mock 5xx requests in unit-tests.
#if defined(UNIT_TEST)
  void set_backoff_delay(base::TimeDelta backoff_delay) {
    backoff_delay_ = backoff_delay;
  }
#endif  // defined(UNIT_TEST)

  // By default, the response is saved in a string. Call this method to save the
  // response to a temporary file instead. Must be called before Start().
  // |file_message_loop_proxy| will be used for all file operations.
  void SaveResponseToTemporaryFile(
      scoped_refptr<base::MessageLoopProxy> file_message_loop_proxy);

  // Retrieve the response headers from the request.  Must only be called after
  // the OnURLFetchComplete callback has run.
  virtual net::HttpResponseHeaders* response_headers() const;

  // Retrieve the remote socket address from the request.  Must only
  // be called after the OnURLFetchComplete callback has run and if
  // the request has not failed.
  net::HostPortPair socket_address() const;

  // Returns true if the request was delivered through a proxy.  Must only
  // be called after the OnURLFetchComplete callback has run and the request
  // has not failed.
  bool was_fetched_via_proxy() const;

  // Start the request.  After this is called, you may not change any other
  // settings.
  virtual void Start();

  // Return the URL that this fetcher is processing.
  virtual const GURL& url() const;

  // The status of the URL fetch.
  virtual const net::URLRequestStatus& status() const;

  // The http response code received. Will return
  // URLFetcher::kInvalidHttpResponseCode if an error prevented any response
  // from being received.
  virtual int response_code() const;

  // Cookies recieved.
  virtual const net::ResponseCookies& cookies() const;

  // Return true if any file system operation failed.  If so, set |error_code|
  // to the error code. File system errors are only possible if user called
  // SaveResponseToTemporaryFile().
  virtual bool FileErrorOccurred(base::PlatformFileError* out_error_code) const;

  // Reports that the received content was malformed.
  void ReceivedContentWasMalformed();

  // Get the response as a string. Return false if the fetcher was not
  // set to store the response as a string.
  virtual bool GetResponseAsString(std::string* out_response_string) const;

  // Get the path to the file containing the response body. Returns false
  // if the response body was not saved to a file. If take_ownership is
  // true, caller takes responsibility for the temp file, and it will not
  // be removed once the URLFetcher is destroyed.
  virtual bool GetResponseAsFilePath(bool take_ownership,
                                     FilePath* out_response_path) const;

  // Cancels all existing URLFetchers.  Will notify the URLFetcher::Delegates.
  // Note that any new URLFetchers created while this is running will not be
  // cancelled.  Typically, one would call this in the CleanUp() method of an IO
  // thread, so that no new URLRequests would be able to start on the IO thread
  // anyway.  This doesn't prevent new URLFetchers from trying to post to the IO
  // thread though, even though the task won't ever run.
  static void CancelAll();

 protected:
  // How should the response be stored?
  enum ResponseDestinationType {
    STRING,  // Default: In a std::string
    TEMP_FILE  // Write to a temp file
  };

  // Returns the delegate.
  Delegate* delegate() const;

  // Used by tests.
  const std::string& upload_data() const;

  // Return a reference to the string data fetched.  Response type must
  // be STRING, or this will CHECK.  This method exists to support the
  // old signiture to OnURLFetchComplete(), and will be removed as part
  // of crbug.com/83592 .
  const std::string& GetResponseStringRef() const;

  void SetResponseDestinationForTesting(ResponseDestinationType);
  ResponseDestinationType GetResponseDestinationForTesting() const;

 private:
  friend class URLFetcherTest;
  friend class TestURLFetcher;

  // Only used by URLFetcherTest, returns the number of URLFetcher::Core objects
  // actively running.
  static int GetNumFetcherCores();

  class Core;
  scoped_refptr<Core> core_;

  static Factory* factory_;

  // If |automatically_retry_on_5xx_| is false, 5xx responses will be
  // propagated to the observer, if it is true URLFetcher will automatically
  // re-execute the request, after the back-off delay has expired.
  // true by default.
  bool automatically_retry_on_5xx_;
  // Back-off time delay. 0 by default.
  base::TimeDelta backoff_delay_;
  // Maximum retries allowed.
  int max_retries_;

  static bool g_interception_enabled;

  DISALLOW_COPY_AND_ASSIGN(URLFetcher);
};

#endif  // CONTENT_COMMON_NET_URL_FETCHER_H_
