// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/cert_verifier.h"

#include "base/bind.h"
#include "base/file_path.h"
#include "base/stringprintf.h"
#include "net/base/cert_test_util.h"
#include "net/base/net_errors.h"
#include "net/base/net_log.h"
#include "net/base/test_completion_callback.h"
#include "net/base/x509_certificate.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {

namespace {

class TestTimeService : public CertVerifier::TimeService {
 public:
  // CertVerifier::TimeService methods:
  virtual base::Time Now() { return current_time_; }

  void set_current_time(base::Time now) { current_time_ = now; }

 private:
  base::Time current_time_;
};

void FailTest(int /* result */) {
  FAIL();
}

// Tests a cache hit, which should results in synchronous completion.
TEST(CertVerifierTest, CacheHit) {
  TestTimeService* time_service = new TestTimeService;
  base::Time current_time = base::Time::Now();
  time_service->set_current_time(current_time);
  CertVerifier verifier(time_service);

  FilePath certs_dir = GetTestCertsDirectory();
  scoped_refptr<X509Certificate> test_cert(
      ImportCertFromFile(certs_dir, "ok_cert.pem"));
  ASSERT_NE(static_cast<X509Certificate*>(NULL), test_cert);

  int error;
  CertVerifyResult verify_result;
  TestCompletionCallback callback;
  CertVerifier::RequestHandle request_handle;

  error = verifier.Verify(test_cert, "www.example.com", 0, NULL, &verify_result,
                          callback.callback(), &request_handle, BoundNetLog());
  ASSERT_EQ(ERR_IO_PENDING, error);
  ASSERT_TRUE(request_handle != NULL);
  error = callback.WaitForResult();
  ASSERT_TRUE(IsCertificateError(error));
  ASSERT_EQ(1u, verifier.requests());
  ASSERT_EQ(0u, verifier.cache_hits());
  ASSERT_EQ(0u, verifier.inflight_joins());
  ASSERT_EQ(1u, verifier.GetCacheSize());

  error = verifier.Verify(test_cert, "www.example.com", 0, NULL, &verify_result,
                          callback.callback(), &request_handle, BoundNetLog());
  // Synchronous completion.
  ASSERT_NE(ERR_IO_PENDING, error);
  ASSERT_TRUE(IsCertificateError(error));
  ASSERT_TRUE(request_handle == NULL);
  ASSERT_EQ(2u, verifier.requests());
  ASSERT_EQ(1u, verifier.cache_hits());
  ASSERT_EQ(0u, verifier.inflight_joins());
  ASSERT_EQ(1u, verifier.GetCacheSize());
}

// Tests the same server certificate with different intermediate CA
// certificates.  These should be treated as different certificate chains even
// though the two X509Certificate objects contain the same server certificate.
TEST(CertVerifierTest, DifferentCACerts) {
  TestTimeService* time_service = new TestTimeService;
  base::Time current_time = base::Time::Now();
  time_service->set_current_time(current_time);
  CertVerifier verifier(time_service);

  FilePath certs_dir = GetTestCertsDirectory();

  scoped_refptr<X509Certificate> server_cert =
      ImportCertFromFile(certs_dir, "salesforce_com_test.pem");
  ASSERT_NE(static_cast<X509Certificate*>(NULL), server_cert);

  scoped_refptr<X509Certificate> intermediate_cert1 =
      ImportCertFromFile(certs_dir, "verisign_intermediate_ca_2011.pem");
  ASSERT_NE(static_cast<X509Certificate*>(NULL), intermediate_cert1);

  scoped_refptr<X509Certificate> intermediate_cert2 =
      ImportCertFromFile(certs_dir, "verisign_intermediate_ca_2016.pem");
  ASSERT_NE(static_cast<X509Certificate*>(NULL), intermediate_cert2);

  X509Certificate::OSCertHandles intermediates;
  intermediates.push_back(intermediate_cert1->os_cert_handle());
  scoped_refptr<X509Certificate> cert_chain1 =
      X509Certificate::CreateFromHandle(server_cert->os_cert_handle(),
                                        intermediates);

  intermediates.clear();
  intermediates.push_back(intermediate_cert2->os_cert_handle());
  scoped_refptr<X509Certificate> cert_chain2 =
      X509Certificate::CreateFromHandle(server_cert->os_cert_handle(),
                                        intermediates);

  int error;
  CertVerifyResult verify_result;
  TestCompletionCallback callback;
  CertVerifier::RequestHandle request_handle;

  error = verifier.Verify(cert_chain1, "www.example.com", 0, NULL,
                          &verify_result, callback.callback(),
                          &request_handle, BoundNetLog());
  ASSERT_EQ(ERR_IO_PENDING, error);
  ASSERT_TRUE(request_handle != NULL);
  error = callback.WaitForResult();
  ASSERT_TRUE(IsCertificateError(error));
  ASSERT_EQ(1u, verifier.requests());
  ASSERT_EQ(0u, verifier.cache_hits());
  ASSERT_EQ(0u, verifier.inflight_joins());
  ASSERT_EQ(1u, verifier.GetCacheSize());

  error = verifier.Verify(cert_chain2, "www.example.com", 0, NULL,
                          &verify_result, callback.callback(),
                          &request_handle, BoundNetLog());
  ASSERT_EQ(ERR_IO_PENDING, error);
  ASSERT_TRUE(request_handle != NULL);
  error = callback.WaitForResult();
  ASSERT_TRUE(IsCertificateError(error));
  ASSERT_EQ(2u, verifier.requests());
  ASSERT_EQ(0u, verifier.cache_hits());
  ASSERT_EQ(0u, verifier.inflight_joins());
  ASSERT_EQ(2u, verifier.GetCacheSize());
}

// Tests an inflight join.
TEST(CertVerifierTest, InflightJoin) {
  TestTimeService* time_service = new TestTimeService;
  base::Time current_time = base::Time::Now();
  time_service->set_current_time(current_time);
  CertVerifier verifier(time_service);

  FilePath certs_dir = GetTestCertsDirectory();
  scoped_refptr<X509Certificate> test_cert(
      ImportCertFromFile(certs_dir, "ok_cert.pem"));
  ASSERT_NE(static_cast<X509Certificate*>(NULL), test_cert);

  int error;
  CertVerifyResult verify_result;
  TestCompletionCallback callback;
  CertVerifier::RequestHandle request_handle;
  CertVerifyResult verify_result2;
  TestCompletionCallback callback2;
  CertVerifier::RequestHandle request_handle2;

  error = verifier.Verify(test_cert, "www.example.com", 0, NULL, &verify_result,
                          callback.callback(), &request_handle, BoundNetLog());
  ASSERT_EQ(ERR_IO_PENDING, error);
  ASSERT_TRUE(request_handle != NULL);
  error = verifier.Verify(
      test_cert, "www.example.com", 0, NULL, &verify_result2,
      callback2.callback(), &request_handle2, BoundNetLog());
  ASSERT_EQ(ERR_IO_PENDING, error);
  ASSERT_TRUE(request_handle2 != NULL);
  error = callback.WaitForResult();
  ASSERT_TRUE(IsCertificateError(error));
  error = callback2.WaitForResult();
  ASSERT_TRUE(IsCertificateError(error));
  ASSERT_EQ(2u, verifier.requests());
  ASSERT_EQ(0u, verifier.cache_hits());
  ASSERT_EQ(1u, verifier.inflight_joins());
}

// Tests cache entry expiration.
TEST(CertVerifierTest, ExpiredCacheEntry) {
  TestTimeService* time_service = new TestTimeService;
  base::Time current_time = base::Time::Now();
  time_service->set_current_time(current_time);
  CertVerifier verifier(time_service);

  FilePath certs_dir = GetTestCertsDirectory();
  scoped_refptr<X509Certificate> test_cert(
      ImportCertFromFile(certs_dir, "ok_cert.pem"));
  ASSERT_NE(static_cast<X509Certificate*>(NULL), test_cert);

  int error;
  CertVerifyResult verify_result;
  TestCompletionCallback callback;
  CertVerifier::RequestHandle request_handle;

  error = verifier.Verify(
      test_cert, "www.example.com", 0, NULL, &verify_result,
      callback.callback(), &request_handle, BoundNetLog());
  ASSERT_EQ(ERR_IO_PENDING, error);
  ASSERT_TRUE(request_handle != NULL);
  error = callback.WaitForResult();
  ASSERT_TRUE(IsCertificateError(error));
  ASSERT_EQ(1u, verifier.requests());
  ASSERT_EQ(0u, verifier.cache_hits());
  ASSERT_EQ(0u, verifier.inflight_joins());

  // Before expiration, should have a cache hit.
  error = verifier.Verify(
      test_cert, "www.example.com", 0, NULL, &verify_result,
      callback.callback(), &request_handle, BoundNetLog());
  // Synchronous completion.
  ASSERT_NE(ERR_IO_PENDING, error);
  ASSERT_TRUE(IsCertificateError(error));
  ASSERT_TRUE(request_handle == NULL);
  ASSERT_EQ(2u, verifier.requests());
  ASSERT_EQ(1u, verifier.cache_hits());
  ASSERT_EQ(0u, verifier.inflight_joins());

  // After expiration, should not have a cache hit.
  ASSERT_EQ(1u, verifier.GetCacheSize());
  current_time += base::TimeDelta::FromMinutes(60);
  time_service->set_current_time(current_time);
  error = verifier.Verify(
      test_cert, "www.example.com", 0, NULL, &verify_result,
      callback.callback(), &request_handle, BoundNetLog());
  ASSERT_EQ(ERR_IO_PENDING, error);
  ASSERT_TRUE(request_handle != NULL);
  ASSERT_EQ(0u, verifier.GetCacheSize());
  error = callback.WaitForResult();
  ASSERT_TRUE(IsCertificateError(error));
  ASSERT_EQ(3u, verifier.requests());
  ASSERT_EQ(1u, verifier.cache_hits());
  ASSERT_EQ(0u, verifier.inflight_joins());
}

// Tests a full cache.
TEST(CertVerifierTest, FullCache) {
  TestTimeService* time_service = new TestTimeService;
  base::Time current_time = base::Time::Now();
  time_service->set_current_time(current_time);
  CertVerifier verifier(time_service);

  // Reduce the maximum cache size in this test so that we can fill up the
  // cache quickly.
  const unsigned kCacheSize = 5;
  verifier.set_max_cache_entries(kCacheSize);

  FilePath certs_dir = GetTestCertsDirectory();
  scoped_refptr<X509Certificate> test_cert(
      ImportCertFromFile(certs_dir, "ok_cert.pem"));
  ASSERT_NE(static_cast<X509Certificate*>(NULL), test_cert);

  int error;
  CertVerifyResult verify_result;
  TestCompletionCallback callback;
  CertVerifier::RequestHandle request_handle;

  error = verifier.Verify(
      test_cert, "www.example.com", 0, NULL, &verify_result,
      callback.callback(), &request_handle, BoundNetLog());
  ASSERT_EQ(ERR_IO_PENDING, error);
  ASSERT_TRUE(request_handle != NULL);
  error = callback.WaitForResult();
  ASSERT_TRUE(IsCertificateError(error));
  ASSERT_EQ(1u, verifier.requests());
  ASSERT_EQ(0u, verifier.cache_hits());
  ASSERT_EQ(0u, verifier.inflight_joins());

  for (unsigned i = 0; i < kCacheSize; i++) {
    std::string hostname = base::StringPrintf("www%d.example.com", i + 1);
    error = verifier.Verify(
        test_cert, hostname, 0, NULL, &verify_result,
        callback.callback(), &request_handle, BoundNetLog());
    ASSERT_EQ(ERR_IO_PENDING, error);
    ASSERT_TRUE(request_handle != NULL);
    error = callback.WaitForResult();
    ASSERT_TRUE(IsCertificateError(error));
  }
  ASSERT_EQ(kCacheSize + 1, verifier.requests());
  ASSERT_EQ(0u, verifier.cache_hits());
  ASSERT_EQ(0u, verifier.inflight_joins());

  ASSERT_EQ(kCacheSize, verifier.GetCacheSize());
  current_time += base::TimeDelta::FromMinutes(60);
  time_service->set_current_time(current_time);
  error = verifier.Verify(
      test_cert, "www999.example.com", 0, NULL, &verify_result,
      callback.callback(), &request_handle, BoundNetLog());
  ASSERT_EQ(ERR_IO_PENDING, error);
  ASSERT_TRUE(request_handle != NULL);
  ASSERT_EQ(kCacheSize, verifier.GetCacheSize());
  error = callback.WaitForResult();
  ASSERT_EQ(1u, verifier.GetCacheSize());
  ASSERT_TRUE(IsCertificateError(error));
  ASSERT_EQ(kCacheSize + 2, verifier.requests());
  ASSERT_EQ(0u, verifier.cache_hits());
  ASSERT_EQ(0u, verifier.inflight_joins());
}

// Tests that the callback of a canceled request is never made.
TEST(CertVerifierTest, CancelRequest) {
  CertVerifier verifier;

  FilePath certs_dir = GetTestCertsDirectory();
  scoped_refptr<X509Certificate> test_cert(
      ImportCertFromFile(certs_dir, "ok_cert.pem"));
  ASSERT_NE(static_cast<X509Certificate*>(NULL), test_cert);

  int error;
  CertVerifyResult verify_result;
  CertVerifier::RequestHandle request_handle;

  error = verifier.Verify(
      test_cert, "www.example.com", 0, NULL, &verify_result,
      base::Bind(&FailTest), &request_handle, BoundNetLog());
  ASSERT_EQ(ERR_IO_PENDING, error);
  ASSERT_TRUE(request_handle != NULL);
  verifier.CancelRequest(request_handle);

  // Issue a few more requests to the worker pool and wait for their
  // completion, so that the task of the canceled request (which runs on a
  // worker thread) is likely to complete by the end of this test.
  TestCompletionCallback callback;
  for (int i = 0; i < 5; ++i) {
    error = verifier.Verify(
        test_cert, "www2.example.com", 0, NULL, &verify_result,
        callback.callback(), &request_handle, BoundNetLog());
    ASSERT_EQ(ERR_IO_PENDING, error);
    ASSERT_TRUE(request_handle != NULL);
    error = callback.WaitForResult();
    verifier.ClearCache();
  }
}

// Tests that a canceled request is not leaked.
TEST(CertVerifierTest, CancelRequestThenQuit) {
  CertVerifier verifier;

  FilePath certs_dir = GetTestCertsDirectory();
  scoped_refptr<X509Certificate> test_cert(
      ImportCertFromFile(certs_dir, "ok_cert.pem"));
  ASSERT_NE(static_cast<X509Certificate*>(NULL), test_cert);

  int error;
  CertVerifyResult verify_result;
  TestCompletionCallback callback;
  CertVerifier::RequestHandle request_handle;

  error = verifier.Verify(test_cert, "www.example.com", 0, NULL, &verify_result,
                          callback.callback(), &request_handle, BoundNetLog());
  ASSERT_EQ(ERR_IO_PENDING, error);
  ASSERT_TRUE(request_handle != NULL);
  verifier.CancelRequest(request_handle);
  // Destroy |verifier| by going out of scope.
}

}  // namespace

}  // namespace net
