// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_proxy_client_socket_pool.h"

#include "base/callback.h"
#include "base/compiler_specific.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "net/base/mock_cert_verifier.h"
#include "net/base/mock_host_resolver.h"
#include "net/base/net_errors.h"
#include "net/base/ssl_config_service_defaults.h"
#include "net/base/test_completion_callback.h"
#include "net/http/http_auth_handler_factory.h"
#include "net/http/http_network_session.h"
#include "net/http/http_proxy_client_socket.h"
#include "net/http/http_server_properties_impl.h"
#include "net/proxy/proxy_service.h"
#include "net/socket/client_socket_handle.h"
#include "net/socket/client_socket_pool_histograms.h"
#include "net/socket/socket_test_util.h"
#include "net/spdy/spdy_protocol.h"
#include "net/spdy/spdy_test_util_spdy2.h"
#include "testing/gtest/include/gtest/gtest.h"

using namespace net::test_spdy2;

namespace net {

namespace {

const int kMaxSockets = 32;
const int kMaxSocketsPerGroup = 6;
const char * const kAuthHeaders[] = {
  "proxy-authorization", "Basic Zm9vOmJhcg=="
};
const int kAuthHeadersSize = arraysize(kAuthHeaders) / 2;

enum HttpProxyType {
  HTTP,
  HTTPS,
  SPDY
};

typedef ::testing::TestWithParam<HttpProxyType> TestWithHttpParam;

}  // namespace

class HttpProxyClientSocketPoolSpdy2Test : public TestWithHttpParam {
 protected:
  HttpProxyClientSocketPoolSpdy2Test()
      : ssl_config_(),
        ignored_transport_socket_params_(new TransportSocketParams(
            HostPortPair("proxy", 80), LOWEST, false, false,
            OnHostResolutionCallback())),
        ignored_ssl_socket_params_(new SSLSocketParams(
            ignored_transport_socket_params_, NULL, NULL,
            ProxyServer::SCHEME_DIRECT, HostPortPair("www.google.com", 443),
            ssl_config_, 0, false, false)),
        tcp_histograms_("MockTCP"),
        transport_socket_pool_(
            kMaxSockets, kMaxSocketsPerGroup,
            &tcp_histograms_,
            &socket_factory_),
        ssl_histograms_("MockSSL"),
        cert_verifier_(new MockCertVerifier),
        proxy_service_(ProxyService::CreateDirect()),
        ssl_config_service_(new SSLConfigServiceDefaults),
        ssl_socket_pool_(kMaxSockets, kMaxSocketsPerGroup,
                         &ssl_histograms_,
                         &host_resolver_,
                         cert_verifier_.get(),
                         NULL /* server_bound_cert_store */,
                         NULL /* transport_security_state */,
                         ""   /* ssl_session_cache_shard */,
                         &socket_factory_,
                         &transport_socket_pool_,
                         NULL,
                         NULL,
                         ssl_config_service_.get(),
                         BoundNetLog().net_log()),
        http_auth_handler_factory_(
            HttpAuthHandlerFactory::CreateDefault(&host_resolver_)),
        session_(CreateNetworkSession()),
        http_proxy_histograms_("HttpProxyUnitTest"),
        ssl_data_(NULL),
        data_(NULL),
        pool_(kMaxSockets, kMaxSocketsPerGroup,
              &http_proxy_histograms_,
              NULL,
              &transport_socket_pool_,
              &ssl_socket_pool_,
              NULL) {
  }

  virtual ~HttpProxyClientSocketPoolSpdy2Test() {
  }

  void AddAuthToCache() {
    const string16 kFoo(ASCIIToUTF16("foo"));
    const string16 kBar(ASCIIToUTF16("bar"));
    GURL proxy_url(GetParam() == HTTP ? "http://proxy" : "https://proxy:80");
    session_->http_auth_cache()->Add(proxy_url,
                                     "MyRealm1",
                                     HttpAuth::AUTH_SCHEME_BASIC,
                                     "Basic realm=MyRealm1",
                                     AuthCredentials(kFoo, kBar),
                                     "/");
  }

  scoped_refptr<TransportSocketParams> GetTcpParams() {
    if (GetParam() != HTTP)
      return scoped_refptr<TransportSocketParams>();
    return ignored_transport_socket_params_;
  }

  scoped_refptr<SSLSocketParams> GetSslParams() {
    if (GetParam() == HTTP)
      return scoped_refptr<SSLSocketParams>();
    return ignored_ssl_socket_params_;
  }

  // Returns the a correctly constructed HttpProxyParms
  // for the HTTP or HTTPS proxy.
  scoped_refptr<HttpProxySocketParams> GetParams(bool tunnel) {
    return scoped_refptr<HttpProxySocketParams>(
        new HttpProxySocketParams(
            GetTcpParams(),
            GetSslParams(),
            GURL(tunnel ? "https://www.google.com/" : "http://www.google.com"),
            "",
            HostPortPair("www.google.com", tunnel ? 443 : 80),
            session_->http_auth_cache(),
            session_->http_auth_handler_factory(),
            session_->spdy_session_pool(),
            tunnel));
  }

  scoped_refptr<HttpProxySocketParams> GetTunnelParams() {
    return GetParams(true);
  }

  scoped_refptr<HttpProxySocketParams> GetNoTunnelParams() {
    return GetParams(false);
  }

  DeterministicMockClientSocketFactory& socket_factory() {
    return socket_factory_;
  }

  void Initialize(MockRead* reads, size_t reads_count,
                  MockWrite* writes, size_t writes_count,
                  MockRead* spdy_reads, size_t spdy_reads_count,
                  MockWrite* spdy_writes, size_t spdy_writes_count) {
    if (GetParam() == SPDY) {
      data_.reset(new DeterministicSocketData(spdy_reads, spdy_reads_count,
                                              spdy_writes, spdy_writes_count));
    } else {
      data_.reset(new DeterministicSocketData(reads, reads_count, writes,
                                              writes_count));
    }

    data_->set_connect_data(MockConnect(SYNCHRONOUS, OK));
    data_->StopAfter(2);  // Request / Response

    socket_factory_.AddSocketDataProvider(data_.get());

    if (GetParam() != HTTP) {
      ssl_data_.reset(new SSLSocketDataProvider(SYNCHRONOUS, OK));
      if (GetParam() == SPDY) {
        InitializeSpdySsl();
      }
      socket_factory_.AddSSLSocketDataProvider(ssl_data_.get());
    }
  }

  void InitializeSpdySsl() {
    ssl_data_->SetNextProto(kProtoSPDY2);
  }

  HttpNetworkSession* CreateNetworkSession() {
    HttpNetworkSession::Params params;
    params.host_resolver = &host_resolver_;
    params.cert_verifier = cert_verifier_.get();
    params.proxy_service = proxy_service_.get();
    params.client_socket_factory = &socket_factory_;
    params.ssl_config_service = ssl_config_service_;
    params.http_auth_handler_factory = http_auth_handler_factory_.get();
    params.http_server_properties = &http_server_properties_;
    HttpNetworkSession* session = new HttpNetworkSession(params);
    SpdySessionPoolPeer pool_peer(session->spdy_session_pool());
    pool_peer.EnableSendingInitialSettings(false);
    return session;
  }

 private:
  SSLConfig ssl_config_;

  scoped_refptr<TransportSocketParams> ignored_transport_socket_params_;
  scoped_refptr<SSLSocketParams> ignored_ssl_socket_params_;
  ClientSocketPoolHistograms tcp_histograms_;
  DeterministicMockClientSocketFactory socket_factory_;
  MockTransportClientSocketPool transport_socket_pool_;
  ClientSocketPoolHistograms ssl_histograms_;
  MockHostResolver host_resolver_;
  scoped_ptr<CertVerifier> cert_verifier_;
  const scoped_ptr<ProxyService> proxy_service_;
  const scoped_refptr<SSLConfigService> ssl_config_service_;
  SSLClientSocketPool ssl_socket_pool_;

  const scoped_ptr<HttpAuthHandlerFactory> http_auth_handler_factory_;
  HttpServerPropertiesImpl http_server_properties_;
  const scoped_refptr<HttpNetworkSession> session_;
  ClientSocketPoolHistograms http_proxy_histograms_;
  SpdyTestStateHelper spdy_state_;

 protected:
  scoped_ptr<SSLSocketDataProvider> ssl_data_;
  scoped_ptr<DeterministicSocketData> data_;
  HttpProxyClientSocketPool pool_;
  ClientSocketHandle handle_;
  TestCompletionCallback callback_;
};

//-----------------------------------------------------------------------------
// All tests are run with three different proxy types: HTTP, HTTPS (non-SPDY)
// and SPDY.
INSTANTIATE_TEST_CASE_P(HttpProxyClientSocketPoolSpdy2Tests,
                        HttpProxyClientSocketPoolSpdy2Test,
                        ::testing::Values(HTTP, HTTPS, SPDY));

TEST_P(HttpProxyClientSocketPoolSpdy2Test, NoTunnel) {
  Initialize(NULL, 0, NULL, 0, NULL, 0, NULL, 0);

  int rv = handle_.Init("a", GetNoTunnelParams(), LOW, CompletionCallback(),
                        &pool_, BoundNetLog());
  EXPECT_EQ(OK, rv);
  EXPECT_TRUE(handle_.is_initialized());
  ASSERT_TRUE(handle_.socket());
  HttpProxyClientSocket* tunnel_socket =
          static_cast<HttpProxyClientSocket*>(handle_.socket());
  EXPECT_TRUE(tunnel_socket->IsConnected());
}

TEST_P(HttpProxyClientSocketPoolSpdy2Test, NeedAuth) {
  MockWrite writes[] = {
    MockWrite(ASYNC, 0, "CONNECT www.google.com:443 HTTP/1.1\r\n"
              "Host: www.google.com\r\n"
              "Proxy-Connection: keep-alive\r\n\r\n"),
  };
  MockRead reads[] = {
    // No credentials.
    MockRead(ASYNC, 1, "HTTP/1.1 407 Proxy Authentication Required\r\n"),
    MockRead(ASYNC, 2, "Proxy-Authenticate: Basic realm=\"MyRealm1\"\r\n"),
    MockRead(ASYNC, 3, "Content-Length: 10\r\n\r\n"),
    MockRead(ASYNC, 4, "0123456789"),
  };
  scoped_ptr<SpdyFrame> req(ConstructSpdyConnect(NULL, 0, 1));
  scoped_ptr<SpdyFrame> rst(ConstructSpdyRstStream(1, CANCEL));
  MockWrite spdy_writes[] = {
    CreateMockWrite(*req, 0, ASYNC),
    CreateMockWrite(*rst, 2, ASYNC),
  };
  static const char* const kAuthChallenge[] = {
    "status", "407 Proxy Authentication Required",
    "version", "HTTP/1.1",
    "proxy-authenticate", "Basic realm=\"MyRealm1\"",
  };
  scoped_ptr<SpdyFrame> resp(

      ConstructSpdyControlFrame(NULL,
                                0,
                                false,
                                1,
                                LOWEST,
                                SYN_REPLY,
                                CONTROL_FLAG_NONE,
                                kAuthChallenge,
                                arraysize(kAuthChallenge)));
  MockRead spdy_reads[] = {
    CreateMockRead(*resp, 1, ASYNC),
    MockRead(ASYNC, 0, 3)
  };

  Initialize(reads, arraysize(reads), writes, arraysize(writes),
             spdy_reads, arraysize(spdy_reads), spdy_writes,
             arraysize(spdy_writes));

  data_->StopAfter(4);
  int rv = handle_.Init("a", GetTunnelParams(), LOW, callback_.callback(),
                        &pool_, BoundNetLog());
  EXPECT_EQ(ERR_IO_PENDING, rv);
  EXPECT_FALSE(handle_.is_initialized());
  EXPECT_FALSE(handle_.socket());

  data_->RunFor(GetParam() == SPDY ? 2 : 4);
  rv = callback_.WaitForResult();
  EXPECT_EQ(ERR_PROXY_AUTH_REQUESTED, rv);
  EXPECT_TRUE(handle_.is_initialized());
  ASSERT_TRUE(handle_.socket());
  ProxyClientSocket* tunnel_socket =
      static_cast<ProxyClientSocket*>(handle_.socket());
  if (GetParam() == SPDY) {
    EXPECT_TRUE(tunnel_socket->IsConnected());
    EXPECT_TRUE(tunnel_socket->IsUsingSpdy());
  } else {
    EXPECT_FALSE(tunnel_socket->IsConnected());
    EXPECT_FALSE(tunnel_socket->IsUsingSpdy());
    EXPECT_FALSE(tunnel_socket->IsUsingSpdy());
  }
}

TEST_P(HttpProxyClientSocketPoolSpdy2Test, HaveAuth) {
  // It's pretty much impossible to make the SPDY case behave synchronously
  // so we skip this test for SPDY
  if (GetParam() == SPDY)
    return;
  MockWrite writes[] = {
    MockWrite(SYNCHRONOUS, 0,
              "CONNECT www.google.com:443 HTTP/1.1\r\n"
              "Host: www.google.com\r\n"
              "Proxy-Connection: keep-alive\r\n"
              "Proxy-Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),
  };
  MockRead reads[] = {
    MockRead(SYNCHRONOUS, 1, "HTTP/1.1 200 Connection Established\r\n\r\n"),
  };

  Initialize(reads, arraysize(reads), writes, arraysize(writes), NULL, 0,
             NULL, 0);
  AddAuthToCache();

  int rv = handle_.Init("a", GetTunnelParams(), LOW, callback_.callback(),
                        &pool_, BoundNetLog());
  EXPECT_EQ(OK, rv);
  EXPECT_TRUE(handle_.is_initialized());
  ASSERT_TRUE(handle_.socket());
  HttpProxyClientSocket* tunnel_socket =
          static_cast<HttpProxyClientSocket*>(handle_.socket());
  EXPECT_TRUE(tunnel_socket->IsConnected());
}

TEST_P(HttpProxyClientSocketPoolSpdy2Test, AsyncHaveAuth) {
  MockWrite writes[] = {
    MockWrite(ASYNC, 0, "CONNECT www.google.com:443 HTTP/1.1\r\n"
              "Host: www.google.com\r\n"
              "Proxy-Connection: keep-alive\r\n"
              "Proxy-Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),
  };
  MockRead reads[] = {
    MockRead(ASYNC, 1, "HTTP/1.1 200 Connection Established\r\n\r\n"),
  };

  scoped_ptr<SpdyFrame> req(ConstructSpdyConnect(kAuthHeaders,
                                                       kAuthHeadersSize, 1));
  MockWrite spdy_writes[] = {
    CreateMockWrite(*req, 0, ASYNC)
  };
  scoped_ptr<SpdyFrame> resp(ConstructSpdyGetSynReply(NULL, 0, 1));
  MockRead spdy_reads[] = {
    CreateMockRead(*resp, 1, ASYNC),
    MockRead(ASYNC, 0, 2)
  };

  Initialize(reads, arraysize(reads), writes, arraysize(writes),
             spdy_reads, arraysize(spdy_reads), spdy_writes,
             arraysize(spdy_writes));
  AddAuthToCache();

  int rv = handle_.Init("a", GetTunnelParams(), LOW, callback_.callback(),
                        &pool_, BoundNetLog());
  EXPECT_EQ(ERR_IO_PENDING, rv);
  EXPECT_FALSE(handle_.is_initialized());
  EXPECT_FALSE(handle_.socket());

  data_->RunFor(2);
  EXPECT_EQ(OK, callback_.WaitForResult());
  EXPECT_TRUE(handle_.is_initialized());
  ASSERT_TRUE(handle_.socket());
  HttpProxyClientSocket* tunnel_socket =
          static_cast<HttpProxyClientSocket*>(handle_.socket());
  EXPECT_TRUE(tunnel_socket->IsConnected());
}

TEST_P(HttpProxyClientSocketPoolSpdy2Test, TCPError) {
  if (GetParam() == SPDY) return;
  data_.reset(new DeterministicSocketData(NULL, 0, NULL, 0));
  data_->set_connect_data(MockConnect(ASYNC, ERR_CONNECTION_CLOSED));

  socket_factory().AddSocketDataProvider(data_.get());

  int rv = handle_.Init("a", GetTunnelParams(), LOW, callback_.callback(),
                        &pool_, BoundNetLog());
  EXPECT_EQ(ERR_IO_PENDING, rv);
  EXPECT_FALSE(handle_.is_initialized());
  EXPECT_FALSE(handle_.socket());

  EXPECT_EQ(ERR_PROXY_CONNECTION_FAILED, callback_.WaitForResult());

  EXPECT_FALSE(handle_.is_initialized());
  EXPECT_FALSE(handle_.socket());
}

TEST_P(HttpProxyClientSocketPoolSpdy2Test, SSLError) {
  if (GetParam() == HTTP) return;
  data_.reset(new DeterministicSocketData(NULL, 0, NULL, 0));
  data_->set_connect_data(MockConnect(ASYNC, OK));
  socket_factory().AddSocketDataProvider(data_.get());

  ssl_data_.reset(new SSLSocketDataProvider(ASYNC,
                                            ERR_CERT_AUTHORITY_INVALID));
  if (GetParam() == SPDY) {
    InitializeSpdySsl();
  }
  socket_factory().AddSSLSocketDataProvider(ssl_data_.get());

  int rv = handle_.Init("a", GetTunnelParams(), LOW, callback_.callback(),
                        &pool_, BoundNetLog());
  EXPECT_EQ(ERR_IO_PENDING, rv);
  EXPECT_FALSE(handle_.is_initialized());
  EXPECT_FALSE(handle_.socket());

  EXPECT_EQ(ERR_PROXY_CERTIFICATE_INVALID, callback_.WaitForResult());

  EXPECT_FALSE(handle_.is_initialized());
  EXPECT_FALSE(handle_.socket());
}

TEST_P(HttpProxyClientSocketPoolSpdy2Test, SslClientAuth) {
  if (GetParam() == HTTP) return;
  data_.reset(new DeterministicSocketData(NULL, 0, NULL, 0));
  data_->set_connect_data(MockConnect(ASYNC, OK));
  socket_factory().AddSocketDataProvider(data_.get());

  ssl_data_.reset(new SSLSocketDataProvider(ASYNC,
                                            ERR_SSL_CLIENT_AUTH_CERT_NEEDED));
  if (GetParam() == SPDY) {
    InitializeSpdySsl();
  }
  socket_factory().AddSSLSocketDataProvider(ssl_data_.get());

  int rv = handle_.Init("a", GetTunnelParams(), LOW, callback_.callback(),
                        &pool_, BoundNetLog());
  EXPECT_EQ(ERR_IO_PENDING, rv);
  EXPECT_FALSE(handle_.is_initialized());
  EXPECT_FALSE(handle_.socket());

  EXPECT_EQ(ERR_SSL_CLIENT_AUTH_CERT_NEEDED, callback_.WaitForResult());

  EXPECT_FALSE(handle_.is_initialized());
  EXPECT_FALSE(handle_.socket());
}

TEST_P(HttpProxyClientSocketPoolSpdy2Test, TunnelUnexpectedClose) {
  MockWrite writes[] = {
    MockWrite(ASYNC, 0,
              "CONNECT www.google.com:443 HTTP/1.1\r\n"
              "Host: www.google.com\r\n"
              "Proxy-Connection: keep-alive\r\n"
              "Proxy-Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),
  };
  MockRead reads[] = {
    MockRead(ASYNC, 1, "HTTP/1.1 200 Conn"),
    MockRead(ASYNC, ERR_CONNECTION_CLOSED, 2),
  };
  scoped_ptr<SpdyFrame> req(ConstructSpdyConnect(kAuthHeaders,
                                                       kAuthHeadersSize, 1));
  MockWrite spdy_writes[] = {
    CreateMockWrite(*req, 0, ASYNC)
  };
  MockRead spdy_reads[] = {
    MockRead(ASYNC, ERR_CONNECTION_CLOSED, 1),
  };

  Initialize(reads, arraysize(reads), writes, arraysize(writes),
             spdy_reads, arraysize(spdy_reads), spdy_writes,
             arraysize(spdy_writes));
  AddAuthToCache();

  int rv = handle_.Init("a", GetTunnelParams(), LOW, callback_.callback(),
                        &pool_, BoundNetLog());
  EXPECT_EQ(ERR_IO_PENDING, rv);
  EXPECT_FALSE(handle_.is_initialized());
  EXPECT_FALSE(handle_.socket());

  data_->RunFor(3);
  EXPECT_EQ(ERR_CONNECTION_CLOSED, callback_.WaitForResult());
  EXPECT_FALSE(handle_.is_initialized());
  EXPECT_FALSE(handle_.socket());
}

TEST_P(HttpProxyClientSocketPoolSpdy2Test, TunnelSetupError) {
  MockWrite writes[] = {
    MockWrite(ASYNC, 0,
              "CONNECT www.google.com:443 HTTP/1.1\r\n"
              "Host: www.google.com\r\n"
              "Proxy-Connection: keep-alive\r\n"
              "Proxy-Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),
  };
  MockRead reads[] = {
    MockRead(ASYNC, 1, "HTTP/1.1 304 Not Modified\r\n\r\n"),
  };
  scoped_ptr<SpdyFrame> req(ConstructSpdyConnect(kAuthHeaders,
                                                       kAuthHeadersSize, 1));
  scoped_ptr<SpdyFrame> rst(ConstructSpdyRstStream(1, CANCEL));
  MockWrite spdy_writes[] = {
    CreateMockWrite(*req, 0, ASYNC),
    CreateMockWrite(*rst, 2, ASYNC),
  };
  scoped_ptr<SpdyFrame> resp(ConstructSpdySynReplyError(1));
  MockRead spdy_reads[] = {
    CreateMockRead(*resp, 1, ASYNC),
    MockRead(ASYNC, 0, 3),
  };

  Initialize(reads, arraysize(reads), writes, arraysize(writes),
             spdy_reads, arraysize(spdy_reads), spdy_writes,
             arraysize(spdy_writes));
  AddAuthToCache();

  int rv = handle_.Init("a", GetTunnelParams(), LOW, callback_.callback(),
                        &pool_, BoundNetLog());
  EXPECT_EQ(ERR_IO_PENDING, rv);
  EXPECT_FALSE(handle_.is_initialized());
  EXPECT_FALSE(handle_.socket());

  data_->RunFor(2);

  rv = callback_.WaitForResult();
  if (GetParam() == HTTP) {
    // HTTP Proxy CONNECT responses are not trustworthy
    EXPECT_EQ(ERR_TUNNEL_CONNECTION_FAILED, rv);
    EXPECT_FALSE(handle_.is_initialized());
    EXPECT_FALSE(handle_.socket());
  } else {
    // HTTPS or SPDY Proxy CONNECT responses are trustworthy
    EXPECT_EQ(ERR_HTTPS_PROXY_TUNNEL_RESPONSE, rv);
    EXPECT_TRUE(handle_.is_initialized());
    EXPECT_TRUE(handle_.socket());
  }
}

// It would be nice to also test the timeouts in HttpProxyClientSocketPool.

}  // namespace net
