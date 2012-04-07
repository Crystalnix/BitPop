// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/url_request/url_request_job_factory.h"

#include "base/bind.h"
#include "base/memory/weak_ptr.h"
#include "net/url_request/url_request_job.h"
#include "net/url_request/url_request_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {

namespace {

class MockURLRequestJob : public URLRequestJob {
 public:
  MockURLRequestJob(URLRequest* request, const URLRequestStatus& status)
      : URLRequestJob(request),
        status_(status),
        ALLOW_THIS_IN_INITIALIZER_LIST(weak_factory_(this)) {}

  virtual void Start() {
    // Start reading asynchronously so that all error reporting and data
    // callbacks happen as they would for network requests.
    MessageLoop::current()->PostTask(
        FROM_HERE,
        base::Bind(&MockURLRequestJob::StartAsync,
                   weak_factory_.GetWeakPtr()));
  }

 private:
  void StartAsync() {
    SetStatus(status_);
    NotifyHeadersComplete();
  }

  URLRequestStatus status_;
  base::WeakPtrFactory<MockURLRequestJob> weak_factory_;
};

class DummyProtocolHandler : public URLRequestJobFactory::ProtocolHandler {
 public:
  virtual URLRequestJob* MaybeCreateJob(URLRequest* request) const {
    return new MockURLRequestJob(
        request, URLRequestStatus(URLRequestStatus::SUCCESS, OK));
  }
};

class DummyInterceptor : public URLRequestJobFactory::Interceptor {
 public:
  DummyInterceptor()
      : did_intercept_(false),
        handle_all_protocols_(false) { }

  virtual URLRequestJob* MaybeIntercept(URLRequest* request) const {
    did_intercept_ = true;
    return new MockURLRequestJob(
        request,
        URLRequestStatus(URLRequestStatus::FAILED, ERR_FAILED));
  }

  virtual URLRequestJob* MaybeInterceptRedirect(
      const GURL& /* location */,
      URLRequest* /* request */) const {
    return NULL;
  }

  virtual URLRequestJob* MaybeInterceptResponse(
      URLRequest* /* request */) const {
    return NULL;
  }

  virtual bool WillHandleProtocol(
      const std::string& /* protocol */) const {
    return handle_all_protocols_;
  }

  mutable bool did_intercept_;
  mutable bool handle_all_protocols_;
};

TEST(URLRequestJobFactoryTest, NoProtocolHandler) {
  TestDelegate delegate;
  scoped_refptr<URLRequestContext> request_context(new TestURLRequestContext);
  TestURLRequest request(GURL("foo://bar"), &delegate);
  request.set_context(request_context);
  request.Start();

  MessageLoop::current()->Run();
  EXPECT_EQ(URLRequestStatus::FAILED, request.status().status());
  EXPECT_EQ(ERR_UNKNOWN_URL_SCHEME, request.status().error());
}

TEST(URLRequestJobFactoryTest, BasicProtocolHandler) {
  TestDelegate delegate;
  scoped_refptr<URLRequestContext> request_context(new TestURLRequestContext);
  URLRequestJobFactory job_factory;
  request_context->set_job_factory(&job_factory);
  job_factory.SetProtocolHandler("foo", new DummyProtocolHandler);
  TestURLRequest request(GURL("foo://bar"), &delegate);
  request.set_context(request_context);
  request.Start();

  MessageLoop::current()->Run();
  EXPECT_EQ(URLRequestStatus::SUCCESS, request.status().status());
  EXPECT_EQ(OK, request.status().error());
}

TEST(URLRequestJobFactoryTest, DeleteProtocolHandler) {
  scoped_refptr<URLRequestContext> request_context(new TestURLRequestContext);
  URLRequestJobFactory job_factory;
  request_context->set_job_factory(&job_factory);
  job_factory.SetProtocolHandler("foo", new DummyProtocolHandler);
  job_factory.SetProtocolHandler("foo", NULL);
}

TEST(URLRequestJobFactoryTest, BasicInterceptor) {
  TestDelegate delegate;
  scoped_refptr<URLRequestContext> request_context(new TestURLRequestContext);
  URLRequestJobFactory job_factory;
  request_context->set_job_factory(&job_factory);
  job_factory.AddInterceptor(new DummyInterceptor);
  TestURLRequest request(GURL("http://bar"), &delegate);
  request.set_context(request_context);
  request.Start();

  MessageLoop::current()->Run();
  EXPECT_EQ(URLRequestStatus::FAILED, request.status().status());
  EXPECT_EQ(ERR_FAILED, request.status().error());
}

TEST(URLRequestJobFactoryTest, InterceptorNeedsValidSchemeStill) {
  TestDelegate delegate;
  scoped_refptr<URLRequestContext> request_context(new TestURLRequestContext);
  URLRequestJobFactory job_factory;
  request_context->set_job_factory(&job_factory);
  job_factory.AddInterceptor(new DummyInterceptor);
  TestURLRequest request(GURL("foo://bar"), &delegate);
  request.set_context(request_context);
  request.Start();

  MessageLoop::current()->Run();
  EXPECT_EQ(URLRequestStatus::FAILED, request.status().status());
  EXPECT_EQ(ERR_UNKNOWN_URL_SCHEME, request.status().error());
}

TEST(URLRequestJobFactoryTest, InterceptorOverridesProtocolHandler) {
  TestDelegate delegate;
  scoped_refptr<URLRequestContext> request_context(new TestURLRequestContext);
  URLRequestJobFactory job_factory;
  request_context->set_job_factory(&job_factory);
  job_factory.SetProtocolHandler("foo", new DummyProtocolHandler);
  job_factory.AddInterceptor(new DummyInterceptor);
  TestURLRequest request(GURL("foo://bar"), &delegate);
  request.set_context(request_context);
  request.Start();

  MessageLoop::current()->Run();
  EXPECT_EQ(URLRequestStatus::FAILED, request.status().status());
  EXPECT_EQ(ERR_FAILED, request.status().error());
}

TEST(URLRequestJobFactoryTest, InterceptorDoesntInterceptUnknownProtocols) {
  TestDelegate delegate;
  scoped_refptr<URLRequestContext> request_context(new TestURLRequestContext);
  URLRequestJobFactory job_factory;
  request_context->set_job_factory(&job_factory);
  DummyInterceptor* interceptor = new DummyInterceptor;
  job_factory.AddInterceptor(interceptor);
  TestURLRequest request(GURL("foo://bar"), &delegate);
  request.set_context(request_context);
  request.Start();

  MessageLoop::current()->Run();
  EXPECT_FALSE(interceptor->did_intercept_);
}

TEST(URLRequestJobFactoryTest, InterceptorInterceptsHandledUnknownProtocols) {
  TestDelegate delegate;
  scoped_refptr<URLRequestContext> request_context(new TestURLRequestContext);
  URLRequestJobFactory job_factory;
  request_context->set_job_factory(&job_factory);
  DummyInterceptor* interceptor = new DummyInterceptor;
  interceptor->handle_all_protocols_ = true;
  job_factory.AddInterceptor(interceptor);
  TestURLRequest request(GURL("foo://bar"), &delegate);
  request.set_context(request_context);
  request.Start();

  MessageLoop::current()->Run();
  EXPECT_TRUE(interceptor->did_intercept_);
  EXPECT_EQ(URLRequestStatus::FAILED, request.status().status());
  EXPECT_EQ(ERR_FAILED, request.status().error());
}

TEST(URLRequestJobFactoryTest, InterceptorAffectsIsHandledProtocol) {
  DummyInterceptor* interceptor = new DummyInterceptor;
  URLRequestJobFactory job_factory;
  job_factory.AddInterceptor(interceptor);
  EXPECT_FALSE(interceptor->WillHandleProtocol("anything"));
  EXPECT_FALSE(job_factory.IsHandledProtocol("anything"));
  interceptor->handle_all_protocols_ = true;
  EXPECT_TRUE(interceptor->WillHandleProtocol("anything"));
  EXPECT_TRUE(job_factory.IsHandledProtocol("anything"));
}

}  // namespace

}  // namespace net
