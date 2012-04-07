// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/proxy/proxy_script_fetcher_impl.h"

#include <string>

#include "base/file_path.h"
#include "base/compiler_specific.h"
#include "base/path_service.h"
#include "base/utf_string_conversions.h"
#include "net/base/net_util.h"
#include "net/base/load_flags.h"
#include "net/base/ssl_config_service_defaults.h"
#include "net/base/test_completion_callback.h"
#include "net/disk_cache/disk_cache.h"
#include "net/http/http_cache.h"
#include "net/http/http_network_session.h"
#include "net/http/http_server_properties_impl.h"
#include "net/test/test_server.h"
#include "net/url_request/url_request_context_storage.h"
#include "net/url_request/url_request_job_factory.h"
#include "net/url_request/url_request_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/platform_test.h"

namespace net {

// TODO(eroman):
//   - Test canceling an outstanding request.
//   - Test deleting ProxyScriptFetcher while a request is in progress.

namespace {

const FilePath::CharType kDocRoot[] =
    FILE_PATH_LITERAL("net/data/proxy_script_fetcher_unittest");

struct FetchResult {
  int code;
  string16 text;
};

// CheckNoRevocationFlagSetInterceptor causes a test failure if a request is
// seen that doesn't set a load flag to bypass revocation checking.
class CheckNoRevocationFlagSetInterceptor :
    public URLRequestJobFactory::Interceptor {
 public:
  virtual URLRequestJob* MaybeIntercept(URLRequest* request) const OVERRIDE {
    EXPECT_TRUE(request->load_flags() & LOAD_DISABLE_CERT_REVOCATION_CHECKING);
    return NULL;
  }

  virtual URLRequestJob* MaybeInterceptRedirect(const GURL& location,
                                                URLRequest* request)
      const OVERRIDE {
    return NULL;
  }

  virtual URLRequestJob* MaybeInterceptResponse(URLRequest* request)
      const OVERRIDE{
    return NULL;
  }
};

// A non-mock URL request which can access http:// and file:// urls.
class RequestContext : public URLRequestContext {
 public:
  RequestContext() : ALLOW_THIS_IN_INITIALIZER_LIST(storage_(this)) {
    ProxyConfig no_proxy;
    storage_.set_host_resolver(
        CreateSystemHostResolver(HostResolver::kDefaultParallelism,
                                 HostResolver::kDefaultRetryAttempts,
                                 NULL));
    storage_.set_cert_verifier(new CertVerifier);
    storage_.set_proxy_service(ProxyService::CreateFixed(no_proxy));
    storage_.set_ssl_config_service(new SSLConfigServiceDefaults);
    storage_.set_http_server_properties(new HttpServerPropertiesImpl);

    HttpNetworkSession::Params params;
    params.host_resolver = host_resolver();
    params.cert_verifier = cert_verifier();
    params.proxy_service = proxy_service();
    params.ssl_config_service = ssl_config_service();
    params.http_server_properties = http_server_properties();
    scoped_refptr<HttpNetworkSession> network_session(
        new HttpNetworkSession(params));
    storage_.set_http_transaction_factory(new HttpCache(
        network_session,
        HttpCache::DefaultBackend::InMemory(0)));
    url_request_job_factory_.reset(new URLRequestJobFactory);
    set_job_factory(url_request_job_factory_.get());
    url_request_job_factory_->AddInterceptor(
        new CheckNoRevocationFlagSetInterceptor);
  }

 private:
  ~RequestContext() {
  }

  URLRequestContextStorage storage_;
  scoped_ptr<URLRequestJobFactory> url_request_job_factory_;
};

// Get a file:// url relative to net/data/proxy/proxy_script_fetcher_unittest.
GURL GetTestFileUrl(const std::string& relpath) {
  FilePath path;
  PathService::Get(base::DIR_SOURCE_ROOT, &path);
  path = path.AppendASCII("net");
  path = path.AppendASCII("data");
  path = path.AppendASCII("proxy_script_fetcher_unittest");
  GURL base_url = FilePathToFileURL(path);
  return GURL(base_url.spec() + "/" + relpath);
}

}  // namespace

class ProxyScriptFetcherImplTest : public PlatformTest {
 public:
  ProxyScriptFetcherImplTest()
      : test_server_(TestServer::TYPE_HTTP, FilePath(kDocRoot)) {
  }

  static void SetUpTestCase() {
    URLRequest::AllowFileAccess();
  }

 protected:
  TestServer test_server_;
};

TEST_F(ProxyScriptFetcherImplTest, FileUrl) {
  scoped_refptr<URLRequestContext> context(new RequestContext);
  ProxyScriptFetcherImpl pac_fetcher(context);

  { // Fetch a non-existent file.
    string16 text;
    TestCompletionCallback callback;
    int result = pac_fetcher.Fetch(GetTestFileUrl("does-not-exist"),
                                   &text, callback.callback());
    EXPECT_EQ(ERR_IO_PENDING, result);
    EXPECT_EQ(ERR_FILE_NOT_FOUND, callback.WaitForResult());
    EXPECT_TRUE(text.empty());
  }
  { // Fetch a file that exists.
    string16 text;
    TestCompletionCallback callback;
    int result = pac_fetcher.Fetch(GetTestFileUrl("pac.txt"),
                                   &text, callback.callback());
    EXPECT_EQ(ERR_IO_PENDING, result);
    EXPECT_EQ(OK, callback.WaitForResult());
    EXPECT_EQ(ASCIIToUTF16("-pac.txt-\n"), text);
  }
}

// Note that all mime types are allowed for PAC file, to be consistent
// with other browsers.
TEST_F(ProxyScriptFetcherImplTest, HttpMimeType) {
  ASSERT_TRUE(test_server_.Start());

  scoped_refptr<URLRequestContext> context(new RequestContext);
  ProxyScriptFetcherImpl pac_fetcher(context);

  { // Fetch a PAC with mime type "text/plain"
    GURL url(test_server_.GetURL("files/pac.txt"));
    string16 text;
    TestCompletionCallback callback;
    int result = pac_fetcher.Fetch(url, &text, callback.callback());
    EXPECT_EQ(ERR_IO_PENDING, result);
    EXPECT_EQ(OK, callback.WaitForResult());
    EXPECT_EQ(ASCIIToUTF16("-pac.txt-\n"), text);
  }
  { // Fetch a PAC with mime type "text/html"
    GURL url(test_server_.GetURL("files/pac.html"));
    string16 text;
    TestCompletionCallback callback;
    int result = pac_fetcher.Fetch(url, &text, callback.callback());
    EXPECT_EQ(ERR_IO_PENDING, result);
    EXPECT_EQ(OK, callback.WaitForResult());
    EXPECT_EQ(ASCIIToUTF16("-pac.html-\n"), text);
  }
  { // Fetch a PAC with mime type "application/x-ns-proxy-autoconfig"
    GURL url(test_server_.GetURL("files/pac.nsproxy"));
    string16 text;
    TestCompletionCallback callback;
    int result = pac_fetcher.Fetch(url, &text, callback.callback());
    EXPECT_EQ(ERR_IO_PENDING, result);
    EXPECT_EQ(OK, callback.WaitForResult());
    EXPECT_EQ(ASCIIToUTF16("-pac.nsproxy-\n"), text);
  }
}

TEST_F(ProxyScriptFetcherImplTest, HttpStatusCode) {
  ASSERT_TRUE(test_server_.Start());

  scoped_refptr<URLRequestContext> context(new RequestContext);
  ProxyScriptFetcherImpl pac_fetcher(context);

  { // Fetch a PAC which gives a 500 -- FAIL
    GURL url(test_server_.GetURL("files/500.pac"));
    string16 text;
    TestCompletionCallback callback;
    int result = pac_fetcher.Fetch(url, &text, callback.callback());
    EXPECT_EQ(ERR_IO_PENDING, result);
    EXPECT_EQ(ERR_PAC_STATUS_NOT_OK, callback.WaitForResult());
    EXPECT_TRUE(text.empty());
  }
  { // Fetch a PAC which gives a 404 -- FAIL
    GURL url(test_server_.GetURL("files/404.pac"));
    string16 text;
    TestCompletionCallback callback;
    int result = pac_fetcher.Fetch(url, &text, callback.callback());
    EXPECT_EQ(ERR_IO_PENDING, result);
    EXPECT_EQ(ERR_PAC_STATUS_NOT_OK, callback.WaitForResult());
    EXPECT_TRUE(text.empty());
  }
}

TEST_F(ProxyScriptFetcherImplTest, ContentDisposition) {
  ASSERT_TRUE(test_server_.Start());

  scoped_refptr<URLRequestContext> context(new RequestContext);
  ProxyScriptFetcherImpl pac_fetcher(context);

  // Fetch PAC scripts via HTTP with a Content-Disposition header -- should
  // have no effect.
  GURL url(test_server_.GetURL("files/downloadable.pac"));
  string16 text;
  TestCompletionCallback callback;
  int result = pac_fetcher.Fetch(url, &text, callback.callback());
  EXPECT_EQ(ERR_IO_PENDING, result);
  EXPECT_EQ(OK, callback.WaitForResult());
  EXPECT_EQ(ASCIIToUTF16("-downloadable.pac-\n"), text);
}

TEST_F(ProxyScriptFetcherImplTest, NoCache) {
  ASSERT_TRUE(test_server_.Start());

  scoped_refptr<URLRequestContext> context(new RequestContext);
  ProxyScriptFetcherImpl pac_fetcher(context);

  // Fetch a PAC script whose HTTP headers make it cacheable for 1 hour.
  GURL url(test_server_.GetURL("files/cacheable_1hr.pac"));
  {
    string16 text;
    TestCompletionCallback callback;
    int result = pac_fetcher.Fetch(url, &text, callback.callback());
    EXPECT_EQ(ERR_IO_PENDING, result);
    EXPECT_EQ(OK, callback.WaitForResult());
    EXPECT_EQ(ASCIIToUTF16("-cacheable_1hr.pac-\n"), text);
  }

  // Now kill the HTTP server.
  ASSERT_TRUE(test_server_.Stop());

  // Try to fetch the file again -- if should fail, since the server is not
  // running anymore. (If it were instead being loaded from cache, we would
  // get a success.
  {
    string16 text;
    TestCompletionCallback callback;
    int result = pac_fetcher.Fetch(url, &text, callback.callback());
    EXPECT_EQ(ERR_IO_PENDING, result);
    EXPECT_EQ(ERR_CONNECTION_REFUSED, callback.WaitForResult());
  }
}

TEST_F(ProxyScriptFetcherImplTest, TooLarge) {
  ASSERT_TRUE(test_server_.Start());

  scoped_refptr<URLRequestContext> context(new RequestContext);
  ProxyScriptFetcherImpl pac_fetcher(context);

  // Set the maximum response size to 50 bytes.
  int prev_size = pac_fetcher.SetSizeConstraint(50);

  // These two URLs are the same file, but are http:// vs file://
  GURL urls[] = {
    test_server_.GetURL("files/large-pac.nsproxy"),
    GetTestFileUrl("large-pac.nsproxy")
  };

  // Try fetching URLs that are 101 bytes large. We should abort the request
  // after 50 bytes have been read, and fail with a too large error.
  for (size_t i = 0; i < arraysize(urls); ++i) {
    const GURL& url = urls[i];
    string16 text;
    TestCompletionCallback callback;
    int result = pac_fetcher.Fetch(url, &text, callback.callback());
    EXPECT_EQ(ERR_IO_PENDING, result);
    EXPECT_EQ(ERR_FILE_TOO_BIG, callback.WaitForResult());
    EXPECT_TRUE(text.empty());
  }

  // Restore the original size bound.
  pac_fetcher.SetSizeConstraint(prev_size);

  { // Make sure we can still fetch regular URLs.
    GURL url(test_server_.GetURL("files/pac.nsproxy"));
    string16 text;
    TestCompletionCallback callback;
    int result = pac_fetcher.Fetch(url, &text, callback.callback());
    EXPECT_EQ(ERR_IO_PENDING, result);
    EXPECT_EQ(OK, callback.WaitForResult());
    EXPECT_EQ(ASCIIToUTF16("-pac.nsproxy-\n"), text);
  }
}

TEST_F(ProxyScriptFetcherImplTest, Hang) {
  ASSERT_TRUE(test_server_.Start());

  scoped_refptr<URLRequestContext> context(new RequestContext);
  ProxyScriptFetcherImpl pac_fetcher(context);

  // Set the timeout period to 0.5 seconds.
  base::TimeDelta prev_timeout = pac_fetcher.SetTimeoutConstraint(
      base::TimeDelta::FromMilliseconds(500));

  // Try fetching a URL which takes 1.2 seconds. We should abort the request
  // after 500 ms, and fail with a timeout error.
  { GURL url(test_server_.GetURL("slow/proxy.pac?1.2"));
    string16 text;
    TestCompletionCallback callback;
    int result = pac_fetcher.Fetch(url, &text, callback.callback());
    EXPECT_EQ(ERR_IO_PENDING, result);
    EXPECT_EQ(ERR_TIMED_OUT, callback.WaitForResult());
    EXPECT_TRUE(text.empty());
  }

  // Restore the original timeout period.
  pac_fetcher.SetTimeoutConstraint(prev_timeout);

  { // Make sure we can still fetch regular URLs.
    GURL url(test_server_.GetURL("files/pac.nsproxy"));
    string16 text;
    TestCompletionCallback callback;
    int result = pac_fetcher.Fetch(url, &text, callback.callback());
    EXPECT_EQ(ERR_IO_PENDING, result);
    EXPECT_EQ(OK, callback.WaitForResult());
    EXPECT_EQ(ASCIIToUTF16("-pac.nsproxy-\n"), text);
  }
}

// The ProxyScriptFetcher should decode any content-codings
// (like gzip, bzip, etc.), and apply any charset conversions to yield
// UTF8.
TEST_F(ProxyScriptFetcherImplTest, Encodings) {
  ASSERT_TRUE(test_server_.Start());

  scoped_refptr<URLRequestContext> context(new RequestContext);
  ProxyScriptFetcherImpl pac_fetcher(context);

  // Test a response that is gzip-encoded -- should get inflated.
  {
    GURL url(test_server_.GetURL("files/gzipped_pac"));
    string16 text;
    TestCompletionCallback callback;
    int result = pac_fetcher.Fetch(url, &text, callback.callback());
    EXPECT_EQ(ERR_IO_PENDING, result);
    EXPECT_EQ(OK, callback.WaitForResult());
    EXPECT_EQ(ASCIIToUTF16("This data was gzipped.\n"), text);
  }

  // Test a response that was served as UTF-16 (BE). It should
  // be converted to UTF8.
  {
    GURL url(test_server_.GetURL("files/utf16be_pac"));
    string16 text;
    TestCompletionCallback callback;
    int result = pac_fetcher.Fetch(url, &text, callback.callback());
    EXPECT_EQ(ERR_IO_PENDING, result);
    EXPECT_EQ(OK, callback.WaitForResult());
    EXPECT_EQ(ASCIIToUTF16("This was encoded as UTF-16BE.\n"), text);
  }
}

TEST_F(ProxyScriptFetcherImplTest, DataURLs) {
  scoped_refptr<URLRequestContext> context(new RequestContext);
  ProxyScriptFetcherImpl pac_fetcher(context);

  const char kEncodedUrl[] =
      "data:application/x-ns-proxy-autoconfig;base64,ZnVuY3Rpb24gRmluZFByb3h5R"
      "m9yVVJMKHVybCwgaG9zdCkgewogIGlmIChob3N0ID09ICdmb29iYXIuY29tJykKICAgIHJl"
      "dHVybiAnUFJPWFkgYmxhY2tob2xlOjgwJzsKICByZXR1cm4gJ0RJUkVDVCc7Cn0=";
  const char kPacScript[] =
      "function FindProxyForURL(url, host) {\n"
      "  if (host == 'foobar.com')\n"
      "    return 'PROXY blackhole:80';\n"
      "  return 'DIRECT';\n"
      "}";

  // Test fetching a "data:"-url containing a base64 encoded PAC script.
  {
    GURL url(kEncodedUrl);
    string16 text;
    TestCompletionCallback callback;
    int result = pac_fetcher.Fetch(url, &text, callback.callback());
    EXPECT_EQ(OK, result);
    EXPECT_EQ(ASCIIToUTF16(kPacScript), text);
  }

  const char kEncodedUrlBroken[] =
      "data:application/x-ns-proxy-autoconfig;base64,ZnVuY3Rpb24gRmluZFByb3h5R";

  // Test a broken "data:"-url containing a base64 encoded PAC script.
  {
    GURL url(kEncodedUrlBroken);
    string16 text;
    TestCompletionCallback callback;
    int result = pac_fetcher.Fetch(url, &text, callback.callback());
    EXPECT_EQ(ERR_FAILED, result);
  }
}

}  // namespace net
