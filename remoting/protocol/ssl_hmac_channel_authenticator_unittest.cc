// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/protocol/ssl_hmac_channel_authenticator.h"

#include "base/bind.h"
#include "base/file_path.h"
#include "base/file_util.h"
#include "base/message_loop.h"
#include "base/path_service.h"
#include "crypto/rsa_private_key.h"
#include "net/base/net_errors.h"
#include "remoting/protocol/connection_tester.h"
#include "remoting/protocol/fake_session.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/libjingle/source/talk/xmllite/xmlelement.h"

using testing::_;
using testing::SaveArg;

namespace remoting {
namespace protocol {

namespace {

const char kTestSharedSecret[] = "1234-1234-5678";
const char kTestSharedSecretBad[] = "0000-0000-0001";

class MockChannelDoneCallback {
 public:
  MOCK_METHOD2(OnDone, void(net::Error error, net::StreamSocket* socket));
};

}  // namespace

class SslHmacChannelAuthenticatorTest : public testing::Test {
 public:
  SslHmacChannelAuthenticatorTest() {
  }
  virtual ~SslHmacChannelAuthenticatorTest() {
  }

 protected:
  virtual void SetUp() OVERRIDE {
    FilePath certs_dir;
    PathService::Get(base::DIR_SOURCE_ROOT, &certs_dir);
    certs_dir = certs_dir.AppendASCII("net");
    certs_dir = certs_dir.AppendASCII("data");
    certs_dir = certs_dir.AppendASCII("ssl");
    certs_dir = certs_dir.AppendASCII("certificates");

    FilePath cert_path = certs_dir.AppendASCII("unittest.selfsigned.der");
    ASSERT_TRUE(file_util::ReadFileToString(cert_path, &host_cert_));

    FilePath key_path = certs_dir.AppendASCII("unittest.key.bin");
    std::string key_string;
    ASSERT_TRUE(file_util::ReadFileToString(key_path, &key_string));
    std::vector<uint8> key_vector(
        reinterpret_cast<const uint8*>(key_string.data()),
        reinterpret_cast<const uint8*>(key_string.data() +
                                       key_string.length()));
    private_key_.reset(
        crypto::RSAPrivateKey::CreateFromPrivateKeyInfo(key_vector));
  }

  void RunChannelAuth(bool expected_fail) {
    client_fake_socket_.reset(new FakeSocket());
    host_fake_socket_.reset(new FakeSocket());
    client_fake_socket_->PairWith(host_fake_socket_.get());

    client_auth_->SecureAndAuthenticate(
        client_fake_socket_.release(),
        base::Bind(&MockChannelDoneCallback::OnDone,
                   base::Unretained(&client_callback_)));

    host_auth_->SecureAndAuthenticate(
        host_fake_socket_.release(),
        base::Bind(&MockChannelDoneCallback::OnDone,
                   base::Unretained(&host_callback_)));

    net::StreamSocket* client_socket = NULL;
    net::StreamSocket* host_socket = NULL;

    if (expected_fail) {
      EXPECT_CALL(client_callback_, OnDone(net::ERR_FAILED, NULL));
      EXPECT_CALL(host_callback_, OnDone(net::ERR_FAILED, NULL));
    } else {
      EXPECT_CALL(client_callback_, OnDone(net::OK, _))
          .WillOnce(SaveArg<1>(&client_socket));
      EXPECT_CALL(host_callback_, OnDone(net::OK, _))
          .WillOnce(SaveArg<1>(&host_socket));
    }

    message_loop_.RunAllPending();

    client_socket_.reset(client_socket);
    host_socket_.reset(host_socket);
  }

  MessageLoop message_loop_;

  scoped_ptr<crypto::RSAPrivateKey> private_key_;
  std::string host_cert_;
  scoped_ptr<FakeSocket> client_fake_socket_;
  scoped_ptr<FakeSocket> host_fake_socket_;
  scoped_ptr<ChannelAuthenticator> client_auth_;
  scoped_ptr<ChannelAuthenticator> host_auth_;
  MockChannelDoneCallback client_callback_;
  MockChannelDoneCallback host_callback_;
  scoped_ptr<net::StreamSocket> client_socket_;
  scoped_ptr<net::StreamSocket> host_socket_;

  DISALLOW_COPY_AND_ASSIGN(SslHmacChannelAuthenticatorTest);
};

// Verify that a channel can be connected using a valid shared secret.
TEST_F(SslHmacChannelAuthenticatorTest, SuccessfulAuth) {
  client_auth_ = SslHmacChannelAuthenticator::CreateForClient(
      host_cert_, kTestSharedSecret);
  host_auth_ = SslHmacChannelAuthenticator::CreateForHost(
      host_cert_, private_key_.get(), kTestSharedSecret);

  RunChannelAuth(false);

  EXPECT_TRUE(client_socket_.get() != NULL);
  EXPECT_TRUE(host_socket_.get() != NULL);

  StreamConnectionTester tester(host_socket_.get(), client_socket_.get(),
                                100, 2);

  tester.Start();
  message_loop_.Run();
  tester.CheckResults();
}

// Verify that channels cannot be using invalid shared secret.
TEST_F(SslHmacChannelAuthenticatorTest, InvalidChannelSecret) {
  client_auth_ = SslHmacChannelAuthenticator::CreateForClient(
      host_cert_, kTestSharedSecretBad);
  host_auth_ = SslHmacChannelAuthenticator::CreateForHost(
      host_cert_, private_key_.get(), kTestSharedSecret);

  RunChannelAuth(true);

  EXPECT_TRUE(host_socket_.get() == NULL);
}

}  // namespace protocol
}  // namespace remoting
