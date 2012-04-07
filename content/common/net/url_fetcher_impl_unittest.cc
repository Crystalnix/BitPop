// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/net/url_fetcher_impl.h"

#include "base/bind.h"
#include "base/message_loop_proxy.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/thread.h"
#include "build/build_config.h"
#include "content/public/common/url_fetcher_delegate.h"
#include "crypto/nss_util.h"
#include "net/http/http_response_headers.h"
#include "net/test/test_server.h"
#include "net/url_request/url_request_context_getter.h"
#include "net/url_request/url_request_test_util.h"
#include "net/url_request/url_request_throttler_manager.h"
#include "testing/gtest/include/gtest/gtest.h"

#if defined(USE_NSS)
#include "net/ocsp/nss_ocsp.h"
#endif

using base::Time;
using base::TimeDelta;

// TODO(eroman): Add a regression test for http://crbug.com/40505.

namespace {

const FilePath::CharType kDocRoot[] = FILE_PATH_LITERAL("chrome/test/data");
const char kTestServerFilePrefix[] = "files/";

class TestURLRequestContextGetter : public net::URLRequestContextGetter {
 public:
  explicit TestURLRequestContextGetter(
      base::MessageLoopProxy* io_message_loop_proxy)
          : io_message_loop_proxy_(io_message_loop_proxy) {
  }
  virtual net::URLRequestContext* GetURLRequestContext() {
    if (!context_)
      context_ = new TestURLRequestContext();
    return context_;
  }
  virtual scoped_refptr<base::MessageLoopProxy> GetIOMessageLoopProxy() const {
    return io_message_loop_proxy_;
  }

 protected:
  scoped_refptr<base::MessageLoopProxy> io_message_loop_proxy_;

 private:
  virtual ~TestURLRequestContextGetter() {}

  scoped_refptr<net::URLRequestContext> context_;
};

}  // namespace

class URLFetcherTest : public testing::Test,
                       public content::URLFetcherDelegate {
 public:
  URLFetcherTest() : fetcher_(NULL) { }

  static int GetNumFetcherCores() {
    return URLFetcherImpl::GetNumFetcherCores();
  }

  // Creates a URLFetcher, using the program's main thread to do IO.
  virtual void CreateFetcher(const GURL& url);

  // content::URLFetcherDelegate
  virtual void OnURLFetchComplete(const content::URLFetcher* source);

  scoped_refptr<base::MessageLoopProxy> io_message_loop_proxy() {
    return io_message_loop_proxy_;
  }

 protected:
  virtual void SetUp() {
    testing::Test::SetUp();

    io_message_loop_proxy_ = base::MessageLoopProxy::current();

#if defined(USE_NSS)
    crypto::EnsureNSSInit();
    net::EnsureOCSPInit();
#endif
  }

  virtual void TearDown() {
#if defined(USE_NSS)
    net::ShutdownOCSP();
#endif
  }

  // URLFetcher is designed to run on the main UI thread, but in our tests
  // we assume that the current thread is the IO thread where the URLFetcher
  // dispatches its requests to.  When we wish to simulate being used from
  // a UI thread, we dispatch a worker thread to do so.
  MessageLoopForIO io_loop_;
  scoped_refptr<base::MessageLoopProxy> io_message_loop_proxy_;

  URLFetcherImpl* fetcher_;
};

void URLFetcherTest::CreateFetcher(const GURL& url) {
  fetcher_ = new URLFetcherImpl(url, content::URLFetcher::GET, this);
  fetcher_->SetRequestContext(new TestURLRequestContextGetter(
      io_message_loop_proxy()));
  fetcher_->Start();
}

void URLFetcherTest::OnURLFetchComplete(const content::URLFetcher* source) {
  EXPECT_TRUE(source->GetStatus().is_success());
  EXPECT_EQ(200, source->GetResponseCode());  // HTTP OK

  std::string data;
  EXPECT_TRUE(source->GetResponseAsString(&data));
  EXPECT_FALSE(data.empty());

  delete fetcher_;  // Have to delete this here and not in the destructor,
                    // because the destructor won't necessarily run on the
                    // same thread that CreateFetcher() did.

  io_message_loop_proxy()->PostTask(FROM_HERE, MessageLoop::QuitClosure());
  // If the current message loop is not the IO loop, it will be shut down when
  // the main loop returns and this thread subsequently goes out of scope.
}

namespace {

// Version of URLFetcherTest that does a POST instead
class URLFetcherPostTest : public URLFetcherTest {
 public:
  virtual void CreateFetcher(const GURL& url);

  // content::URLFetcherDelegate
  virtual void OnURLFetchComplete(const content::URLFetcher* source);
};

// Version of URLFetcherTest that tests headers.
class URLFetcherHeadersTest : public URLFetcherTest {
 public:
  // content::URLFetcherDelegate
  virtual void OnURLFetchComplete(const content::URLFetcher* source);
};

// Version of URLFetcherTest that tests SocketAddress.
class URLFetcherSocketAddressTest : public URLFetcherTest {
 public:
  // content::URLFetcherDelegate
  virtual void OnURLFetchComplete(const content::URLFetcher* source);
 protected:
  std::string expected_host_;
  uint16 expected_port_;
};

// Version of URLFetcherTest that tests overload protection.
class URLFetcherProtectTest : public URLFetcherTest {
 public:
  virtual void CreateFetcher(const GURL& url);
  // content::URLFetcherDelegate
  virtual void OnURLFetchComplete(const content::URLFetcher* source);
 private:
  Time start_time_;
};

// Version of URLFetcherTest that tests overload protection, when responses
// passed through.
class URLFetcherProtectTestPassedThrough : public URLFetcherTest {
 public:
  virtual void CreateFetcher(const GURL& url);
  // content::URLFetcherDelegate
  virtual void OnURLFetchComplete(const content::URLFetcher* source);
 private:
  Time start_time_;
};

// Version of URLFetcherTest that tests bad HTTPS requests.
class URLFetcherBadHTTPSTest : public URLFetcherTest {
 public:
  URLFetcherBadHTTPSTest();

  // content::URLFetcherDelegate
  virtual void OnURLFetchComplete(const content::URLFetcher* source);

 private:
  FilePath cert_dir_;
};

// Version of URLFetcherTest that tests request cancellation on shutdown.
class URLFetcherCancelTest : public URLFetcherTest {
 public:
  virtual void CreateFetcher(const GURL& url);
  // content::URLFetcherDelegate
  virtual void OnURLFetchComplete(const content::URLFetcher* source);

  void CancelRequest();
};

// Version of TestURLRequestContext that posts a Quit task to the IO
// thread once it is deleted.
class CancelTestURLRequestContext : public TestURLRequestContext {
  virtual ~CancelTestURLRequestContext() {
    // The d'tor should execute in the IO thread. Post the quit task to the
    // current thread.
    MessageLoop::current()->PostTask(FROM_HERE, MessageLoop::QuitClosure());
  }
};

class CancelTestURLRequestContextGetter : public net::URLRequestContextGetter {
 public:
  explicit CancelTestURLRequestContextGetter(
      base::MessageLoopProxy* io_message_loop_proxy)
      : io_message_loop_proxy_(io_message_loop_proxy),
        context_created_(false, false) {
  }
  virtual net::URLRequestContext* GetURLRequestContext() {
    if (!context_) {
      context_ = new CancelTestURLRequestContext();
      context_created_.Signal();
    }
    return context_;
  }
  virtual scoped_refptr<base::MessageLoopProxy> GetIOMessageLoopProxy() const {
    return io_message_loop_proxy_;
  }
  void WaitForContextCreation() {
    context_created_.Wait();
  }

 private:
  ~CancelTestURLRequestContextGetter() {}

  scoped_refptr<base::MessageLoopProxy> io_message_loop_proxy_;
  base::WaitableEvent context_created_;
  scoped_refptr<net::URLRequestContext> context_;
};

// Version of URLFetcherTest that tests retying the same request twice.
class URLFetcherMultipleAttemptTest : public URLFetcherTest {
 public:
  // content::URLFetcherDelegate
  virtual void OnURLFetchComplete(const content::URLFetcher* source);
 private:
  std::string data_;
};

class URLFetcherTempFileTest : public URLFetcherTest {
 public:
  URLFetcherTempFileTest()
      : take_ownership_of_temp_file_(false) {
  }

  // content::URLFetcherDelegate
  virtual void OnURLFetchComplete(const content::URLFetcher* source);

  virtual void CreateFetcher(const GURL& url);

 protected:
  FilePath expected_file_;
  FilePath temp_file_;

  // Set by the test. Used in OnURLFetchComplete() to decide if
  // the URLFetcher should own the temp file, so that we can test
  // disowning prevents the file from being deleted.
  bool take_ownership_of_temp_file_;
};

void URLFetcherTempFileTest::CreateFetcher(const GURL& url) {
  fetcher_ = new URLFetcherImpl(url, content::URLFetcher::GET, this);
  fetcher_->SetRequestContext(new TestURLRequestContextGetter(
      io_message_loop_proxy()));

  // Use the IO message loop to do the file operations in this test.
  fetcher_->SaveResponseToTemporaryFile(io_message_loop_proxy());
  fetcher_->Start();
}

TEST_F(URLFetcherTempFileTest, SmallGet) {
  net::TestServer test_server(net::TestServer::TYPE_HTTP, FilePath(kDocRoot));
  ASSERT_TRUE(test_server.Start());

  // Get a small file.
  const char* kFileToFetch = "simple.html";
  expected_file_ = test_server.document_root().AppendASCII(kFileToFetch);
  CreateFetcher(
      test_server.GetURL(std::string(kTestServerFilePrefix) + kFileToFetch));

  MessageLoop::current()->Run();  // OnURLFetchComplete() will Quit().

  ASSERT_FALSE(file_util::PathExists(temp_file_))
      << temp_file_.value() << " not removed.";
}

TEST_F(URLFetcherTempFileTest, LargeGet) {
  net::TestServer test_server(net::TestServer::TYPE_HTTP, FilePath(kDocRoot));
  ASSERT_TRUE(test_server.Start());

  // Get a file large enough to require more than one read into
  // URLFetcher::Core's IOBuffer.
  const char* kFileToFetch = "animate1.gif";
  expected_file_ = test_server.document_root().AppendASCII(kFileToFetch);
  CreateFetcher(test_server.GetURL(
      std::string(kTestServerFilePrefix) + kFileToFetch));

  MessageLoop::current()->Run();  // OnURLFetchComplete() will Quit().
}

TEST_F(URLFetcherTempFileTest, CanTakeOwnershipOfFile) {
  net::TestServer test_server(net::TestServer::TYPE_HTTP, FilePath(kDocRoot));
  ASSERT_TRUE(test_server.Start());

  // Get a small file.
  const char* kFileToFetch = "simple.html";
  expected_file_ = test_server.document_root().AppendASCII(kFileToFetch);
  CreateFetcher(test_server.GetURL(
      std::string(kTestServerFilePrefix) + kFileToFetch));

  MessageLoop::current()->Run();  // OnURLFetchComplete() will Quit().

  MessageLoop::current()->RunAllPending();
  ASSERT_FALSE(file_util::PathExists(temp_file_))
      << temp_file_.value() << " not removed.";
}

void URLFetcherPostTest::CreateFetcher(const GURL& url) {
  fetcher_ = new URLFetcherImpl(url, content::URLFetcher::POST, this);
  fetcher_->SetRequestContext(new TestURLRequestContextGetter(
      io_message_loop_proxy()));
  fetcher_->SetUploadData("application/x-www-form-urlencoded",
                          "bobsyeruncle");
  fetcher_->Start();
}

void URLFetcherPostTest::OnURLFetchComplete(const content::URLFetcher* source) {
  std::string data;
  EXPECT_TRUE(source->GetResponseAsString(&data));
  EXPECT_EQ(std::string("bobsyeruncle"), data);
  URLFetcherTest::OnURLFetchComplete(source);
}

void URLFetcherHeadersTest::OnURLFetchComplete(
    const content::URLFetcher* source) {
  std::string header;
  EXPECT_TRUE(source->GetResponseHeaders()->GetNormalizedHeader("cache-control",
                                                                &header));
  EXPECT_EQ("private", header);
  URLFetcherTest::OnURLFetchComplete(source);
}

void URLFetcherSocketAddressTest::OnURLFetchComplete(
    const content::URLFetcher* source) {
  EXPECT_EQ("127.0.0.1", source->GetSocketAddress().host());
  EXPECT_EQ(expected_port_, source->GetSocketAddress().port());
  URLFetcherTest::OnURLFetchComplete(source);
}

void URLFetcherProtectTest::CreateFetcher(const GURL& url) {
  fetcher_ = new URLFetcherImpl(url, content::URLFetcher::GET, this);
  fetcher_->SetRequestContext(new TestURLRequestContextGetter(
      io_message_loop_proxy()));
  start_time_ = Time::Now();
  fetcher_->SetMaxRetries(11);
  fetcher_->Start();
}

void URLFetcherProtectTest::OnURLFetchComplete(
    const content::URLFetcher* source) {
  const TimeDelta one_second = TimeDelta::FromMilliseconds(1000);
  if (source->GetResponseCode() >= 500) {
    // Now running ServerUnavailable test.
    // It takes more than 1 second to finish all 11 requests.
    EXPECT_TRUE(Time::Now() - start_time_ >= one_second);
    EXPECT_TRUE(source->GetStatus().is_success());
    std::string data;
    EXPECT_TRUE(source->GetResponseAsString(&data));
    EXPECT_FALSE(data.empty());
    delete fetcher_;
    io_message_loop_proxy()->PostTask(FROM_HERE, MessageLoop::QuitClosure());
  } else {
    // Now running Overload test.
    static int count = 0;
    count++;
    if (count < 20) {
      fetcher_->StartWithRequestContextGetter(new TestURLRequestContextGetter(
          io_message_loop_proxy()));
    } else {
      // We have already sent 20 requests continuously. And we expect that
      // it takes more than 1 second due to the overload protection settings.
      EXPECT_TRUE(Time::Now() - start_time_ >= one_second);
      URLFetcherTest::OnURLFetchComplete(source);
    }
  }
}

void URLFetcherProtectTestPassedThrough::CreateFetcher(const GURL& url) {
  fetcher_ = new URLFetcherImpl(url, content::URLFetcher::GET, this);
  fetcher_->SetRequestContext(new TestURLRequestContextGetter(
      io_message_loop_proxy()));
  fetcher_->SetAutomaticallyRetryOn5xx(false);
  start_time_ = Time::Now();
  fetcher_->SetMaxRetries(11);
  fetcher_->Start();
}

void URLFetcherProtectTestPassedThrough::OnURLFetchComplete(
    const content::URLFetcher* source) {
  const TimeDelta one_minute = TimeDelta::FromMilliseconds(60000);
  if (source->GetResponseCode() >= 500) {
    // Now running ServerUnavailable test.
    // It should get here on the first attempt, so almost immediately and
    // *not* to attempt to execute all 11 requests (2.5 minutes).
    EXPECT_TRUE(Time::Now() - start_time_ < one_minute);
    EXPECT_TRUE(source->GetStatus().is_success());
    // Check that suggested back off time is bigger than 0.
    EXPECT_GT(fetcher_->GetBackoffDelay().InMicroseconds(), 0);
    std::string data;
    EXPECT_TRUE(source->GetResponseAsString(&data));
    EXPECT_FALSE(data.empty());
  } else {
    // We should not get here!
    ADD_FAILURE();
  }

  delete fetcher_;
  io_message_loop_proxy()->PostTask(FROM_HERE, MessageLoop::QuitClosure());
}


URLFetcherBadHTTPSTest::URLFetcherBadHTTPSTest() {
  PathService::Get(base::DIR_SOURCE_ROOT, &cert_dir_);
  cert_dir_ = cert_dir_.AppendASCII("chrome");
  cert_dir_ = cert_dir_.AppendASCII("test");
  cert_dir_ = cert_dir_.AppendASCII("data");
  cert_dir_ = cert_dir_.AppendASCII("ssl");
  cert_dir_ = cert_dir_.AppendASCII("certificates");
}

// The "server certificate expired" error should result in automatic
// cancellation of the request by
// net::URLRequest::Delegate::OnSSLCertificateError.
void URLFetcherBadHTTPSTest::OnURLFetchComplete(
    const content::URLFetcher* source) {
  // This part is different from URLFetcherTest::OnURLFetchComplete
  // because this test expects the request to be cancelled.
  EXPECT_EQ(net::URLRequestStatus::CANCELED, source->GetStatus().status());
  EXPECT_EQ(net::ERR_ABORTED, source->GetStatus().error());
  EXPECT_EQ(-1, source->GetResponseCode());
  EXPECT_TRUE(source->GetCookies().empty());
  std::string data;
  EXPECT_TRUE(source->GetResponseAsString(&data));
  EXPECT_TRUE(data.empty());

  // The rest is the same as URLFetcherTest::OnURLFetchComplete.
  delete fetcher_;
  io_message_loop_proxy()->PostTask(FROM_HERE, MessageLoop::QuitClosure());
}

void URLFetcherCancelTest::CreateFetcher(const GURL& url) {
  fetcher_ = new URLFetcherImpl(url, content::URLFetcher::GET, this);
  CancelTestURLRequestContextGetter* context_getter =
      new CancelTestURLRequestContextGetter(io_message_loop_proxy());
  fetcher_->SetRequestContext(context_getter);
  fetcher_->SetMaxRetries(2);
  fetcher_->Start();
  // We need to wait for the creation of the net::URLRequestContext, since we
  // rely on it being destroyed as a signal to end the test.
  context_getter->WaitForContextCreation();
  CancelRequest();
}

void URLFetcherCancelTest::OnURLFetchComplete(
    const content::URLFetcher* source) {
  // We should have cancelled the request before completion.
  ADD_FAILURE();
  delete fetcher_;
  io_message_loop_proxy()->PostTask(FROM_HERE, MessageLoop::QuitClosure());
}

void URLFetcherCancelTest::CancelRequest() {
  delete fetcher_;
  // The URLFetcher's test context will post a Quit task once it is
  // deleted. So if this test simply hangs, it means cancellation
  // did not work.
}

void URLFetcherMultipleAttemptTest::OnURLFetchComplete(
    const content::URLFetcher* source) {
  EXPECT_TRUE(source->GetStatus().is_success());
  EXPECT_EQ(200, source->GetResponseCode());  // HTTP OK
  std::string data;
  EXPECT_TRUE(source->GetResponseAsString(&data));
  EXPECT_FALSE(data.empty());
  if (!data.empty() && data_.empty()) {
    data_ = data;
    fetcher_->StartWithRequestContextGetter(
        new TestURLRequestContextGetter(io_message_loop_proxy()));
  } else {
    EXPECT_EQ(data, data_);
    delete fetcher_;  // Have to delete this here and not in the destructor,
                      // because the destructor won't necessarily run on the
                      // same thread that CreateFetcher() did.

    io_message_loop_proxy()->PostTask(FROM_HERE, MessageLoop::QuitClosure());
    // If the current message loop is not the IO loop, it will be shut down when
    // the main loop returns and this thread subsequently goes out of scope.
  }
}

void URLFetcherTempFileTest::OnURLFetchComplete(
    const content::URLFetcher* source) {
  EXPECT_TRUE(source->GetStatus().is_success());
  EXPECT_EQ(source->GetResponseCode(), 200);

  EXPECT_TRUE(source->GetResponseAsFilePath(
      take_ownership_of_temp_file_, &temp_file_));

  EXPECT_TRUE(file_util::ContentsEqual(expected_file_, temp_file_));

  delete fetcher_;

  io_message_loop_proxy()->PostTask(FROM_HERE, MessageLoop::QuitClosure());
}

TEST_F(URLFetcherTest, SameThreadsTest) {
  net::TestServer test_server(net::TestServer::TYPE_HTTP, FilePath(kDocRoot));
  ASSERT_TRUE(test_server.Start());

  // Create the fetcher on the main thread.  Since IO will happen on the main
  // thread, this will test URLFetcher's ability to do everything on one
  // thread.
  CreateFetcher(test_server.GetURL("defaultresponse"));

  MessageLoop::current()->Run();
}

#if defined(OS_MACOSX)
// SIGSEGV on Mac: http://crbug.com/60426
TEST_F(URLFetcherTest, DISABLED_DifferentThreadsTest) {
#else
TEST_F(URLFetcherTest, DifferentThreadsTest) {
#endif
  net::TestServer test_server(net::TestServer::TYPE_HTTP, FilePath(kDocRoot));
  ASSERT_TRUE(test_server.Start());

  // Create a separate thread that will create the URLFetcher.  The current
  // (main) thread will do the IO, and when the fetch is complete it will
  // terminate the main thread's message loop; then the other thread's
  // message loop will be shut down automatically as the thread goes out of
  // scope.
  base::Thread t("URLFetcher test thread");
  ASSERT_TRUE(t.Start());
  t.message_loop()->PostTask(
      FROM_HERE,
      base::Bind(&URLFetcherTest::CreateFetcher,
                 base::Unretained(this),
                 test_server.GetURL("defaultresponse")));

  MessageLoop::current()->Run();
}

#if defined(OS_MACOSX)
// SIGSEGV on Mac: http://crbug.com/60426
TEST_F(URLFetcherPostTest, DISABLED_Basic) {
#else
TEST_F(URLFetcherPostTest, Basic) {
#endif
  net::TestServer test_server(net::TestServer::TYPE_HTTP, FilePath(kDocRoot));
  ASSERT_TRUE(test_server.Start());

  CreateFetcher(test_server.GetURL("echo"));
  MessageLoop::current()->Run();
}

TEST_F(URLFetcherHeadersTest, Headers) {
  net::TestServer test_server(net::TestServer::TYPE_HTTP,
      FilePath(FILE_PATH_LITERAL("net/data/url_request_unittest")));
  ASSERT_TRUE(test_server.Start());

  CreateFetcher(test_server.GetURL("files/with-headers.html"));
  MessageLoop::current()->Run();
  // The actual tests are in the URLFetcherHeadersTest fixture.
}

TEST_F(URLFetcherSocketAddressTest, SocketAddress) {
  net::TestServer test_server(net::TestServer::TYPE_HTTP,
      FilePath(FILE_PATH_LITERAL("net/data/url_request_unittest")));
  ASSERT_TRUE(test_server.Start());
  expected_port_ = test_server.host_port_pair().port();

  // Reusing "with-headers.html" but doesn't really matter.
  CreateFetcher(test_server.GetURL("files/with-headers.html"));
  MessageLoop::current()->Run();
  // The actual tests are in the URLFetcherSocketAddressTest fixture.
}

TEST_F(URLFetcherProtectTest, Overload) {
  net::TestServer test_server(net::TestServer::TYPE_HTTP, FilePath(kDocRoot));
  ASSERT_TRUE(test_server.Start());

  GURL url(test_server.GetURL("defaultresponse"));

  // Registers an entry for test url. It only allows 3 requests to be sent
  // in 200 milliseconds.
  net::URLRequestThrottlerManager* manager =
      net::URLRequestThrottlerManager::GetInstance();
  scoped_refptr<net::URLRequestThrottlerEntry> entry(
      new net::URLRequestThrottlerEntry(manager, "", 200, 3, 1, 2.0, 0.0, 256));
  manager->OverrideEntryForTests(url, entry);

  CreateFetcher(url);

  MessageLoop::current()->Run();

  net::URLRequestThrottlerManager::GetInstance()->EraseEntryForTests(url);
}

TEST_F(URLFetcherProtectTest, ServerUnavailable) {
  net::TestServer test_server(net::TestServer::TYPE_HTTP, FilePath(kDocRoot));
  ASSERT_TRUE(test_server.Start());

  GURL url(test_server.GetURL("files/server-unavailable.html"));

  // Registers an entry for test url. The backoff time is calculated by:
  //     new_backoff = 2.0 * old_backoff + 0
  // and maximum backoff time is 256 milliseconds.
  // Maximum retries allowed is set to 11.
  net::URLRequestThrottlerManager* manager =
      net::URLRequestThrottlerManager::GetInstance();
  scoped_refptr<net::URLRequestThrottlerEntry> entry(
      new net::URLRequestThrottlerEntry(manager, "", 200, 3, 1, 2.0, 0.0, 256));
  manager->OverrideEntryForTests(url, entry);

  CreateFetcher(url);

  MessageLoop::current()->Run();

  net::URLRequestThrottlerManager::GetInstance()->EraseEntryForTests(url);
}

TEST_F(URLFetcherProtectTestPassedThrough, ServerUnavailablePropagateResponse) {
  net::TestServer test_server(net::TestServer::TYPE_HTTP, FilePath(kDocRoot));
  ASSERT_TRUE(test_server.Start());

  GURL url(test_server.GetURL("files/server-unavailable.html"));

  // Registers an entry for test url. The backoff time is calculated by:
  //     new_backoff = 2.0 * old_backoff + 0
  // and maximum backoff time is 150000 milliseconds.
  // Maximum retries allowed is set to 11.
  net::URLRequestThrottlerManager* manager =
      net::URLRequestThrottlerManager::GetInstance();
  scoped_refptr<net::URLRequestThrottlerEntry> entry(
      new net::URLRequestThrottlerEntry(
          manager, "", 200, 3, 100, 2.0, 0.0, 150000));
  // Total time if *not* for not doing automatic backoff would be 150s.
  // In reality it should be "as soon as server responds".
  manager->OverrideEntryForTests(url, entry);

  CreateFetcher(url);

  MessageLoop::current()->Run();

  net::URLRequestThrottlerManager::GetInstance()->EraseEntryForTests(url);
}

#if defined(OS_MACOSX)
// SIGSEGV on Mac: http://crbug.com/60426
TEST_F(URLFetcherBadHTTPSTest, DISABLED_BadHTTPSTest) {
#else
TEST_F(URLFetcherBadHTTPSTest, BadHTTPSTest) {
#endif
  net::TestServer::HTTPSOptions https_options(
      net::TestServer::HTTPSOptions::CERT_EXPIRED);
  net::TestServer test_server(https_options, FilePath(kDocRoot));
  ASSERT_TRUE(test_server.Start());

  CreateFetcher(test_server.GetURL("defaultresponse"));
  MessageLoop::current()->Run();
}

#if defined(OS_MACOSX)
// SIGSEGV on Mac: http://crbug.com/60426
TEST_F(URLFetcherCancelTest, DISABLED_ReleasesContext) {
#else
TEST_F(URLFetcherCancelTest, ReleasesContext) {
#endif
  net::TestServer test_server(net::TestServer::TYPE_HTTP, FilePath(kDocRoot));
  ASSERT_TRUE(test_server.Start());

  GURL url(test_server.GetURL("files/server-unavailable.html"));

  // Registers an entry for test url. The backoff time is calculated by:
  //     new_backoff = 2.0 * old_backoff +0
  // The initial backoff is 2 seconds and maximum backoff is 4 seconds.
  // Maximum retries allowed is set to 2.
  net::URLRequestThrottlerManager* manager =
      net::URLRequestThrottlerManager::GetInstance();
  scoped_refptr<net::URLRequestThrottlerEntry> entry(
      new net::URLRequestThrottlerEntry(
          manager, "", 200, 3, 2000, 2.0, 0.0, 4000));
  manager->OverrideEntryForTests(url, entry);

  // Create a separate thread that will create the URLFetcher.  The current
  // (main) thread will do the IO, and when the fetch is complete it will
  // terminate the main thread's message loop; then the other thread's
  // message loop will be shut down automatically as the thread goes out of
  // scope.
  base::Thread t("URLFetcher test thread");
  ASSERT_TRUE(t.Start());
  t.message_loop()->PostTask(
      FROM_HERE,
      base::Bind(&URLFetcherTest::CreateFetcher, base::Unretained(this), url));

  MessageLoop::current()->Run();

  net::URLRequestThrottlerManager::GetInstance()->EraseEntryForTests(url);
}

TEST_F(URLFetcherCancelTest, CancelWhileDelayedStartTaskPending) {
  net::TestServer test_server(net::TestServer::TYPE_HTTP, FilePath(kDocRoot));
  ASSERT_TRUE(test_server.Start());

  GURL url(test_server.GetURL("files/server-unavailable.html"));

  // Register an entry for test url.
  // Using a sliding window of 4 seconds, and max of 1 request, under a fast
  // run we expect to have a 4 second delay when posting the Start task.
  net::URLRequestThrottlerManager* manager =
      net::URLRequestThrottlerManager::GetInstance();
  scoped_refptr<net::URLRequestThrottlerEntry> entry(
      new net::URLRequestThrottlerEntry(
          manager, "", 4000, 1, 2000, 2.0, 0.0, 4000));
  manager->OverrideEntryForTests(url, entry);
  // Fake that a request has just started.
  entry->ReserveSendingTimeForNextRequest(base::TimeTicks());

  // The next request we try to send will be delayed by ~4 seconds.
  // The slower the test runs, the less the delay will be (since it takes the
  // time difference from now).

  base::Thread t("URLFetcher test thread");
  ASSERT_TRUE(t.Start());
  t.message_loop()->PostTask(
      FROM_HERE,
      base::Bind(&URLFetcherTest::CreateFetcher, base::Unretained(this), url));

  MessageLoop::current()->Run();

  net::URLRequestThrottlerManager::GetInstance()->EraseEntryForTests(url);
}

TEST_F(URLFetcherMultipleAttemptTest, SameData) {
  net::TestServer test_server(net::TestServer::TYPE_HTTP, FilePath(kDocRoot));
  ASSERT_TRUE(test_server.Start());

  // Create the fetcher on the main thread.  Since IO will happen on the main
  // thread, this will test URLFetcher's ability to do everything on one
  // thread.
  CreateFetcher(test_server.GetURL("defaultresponse"));

  MessageLoop::current()->Run();
}

void CancelAllOnIO() {
  EXPECT_EQ(1, URLFetcherTest::GetNumFetcherCores());
  URLFetcherImpl::CancelAll();
  EXPECT_EQ(0, URLFetcherTest::GetNumFetcherCores());
}

// Tests to make sure CancelAll() will successfully cancel existing URLFetchers.
TEST_F(URLFetcherTest, CancelAll) {
  net::TestServer test_server(net::TestServer::TYPE_HTTP, FilePath(kDocRoot));
  ASSERT_TRUE(test_server.Start());
  EXPECT_EQ(0, GetNumFetcherCores());

  CreateFetcher(test_server.GetURL("defaultresponse"));
  io_message_loop_proxy()->PostTaskAndReply(
      FROM_HERE,
      base::Bind(&CancelAllOnIO),
      MessageLoop::QuitClosure());
  MessageLoop::current()->Run();
  EXPECT_EQ(0, GetNumFetcherCores());
  delete fetcher_;
}

}  // namespace.
