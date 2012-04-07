// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/client_socket_factory.h"

#include "base/lazy_instance.h"
#include "build/build_config.h"
#include "net/base/cert_database.h"
#include "net/socket/client_socket_handle.h"
#if defined(OS_WIN)
#include "net/socket/ssl_client_socket_nss.h"
#include "net/socket/ssl_client_socket_win.h"
#elif defined(USE_OPENSSL)
#include "net/socket/ssl_client_socket_openssl.h"
#elif defined(USE_NSS)
#include "net/socket/ssl_client_socket_nss.h"
#elif defined(OS_MACOSX)
#include "net/socket/ssl_client_socket_mac.h"
#include "net/socket/ssl_client_socket_nss.h"
#endif
#include "net/socket/ssl_host_info.h"
#include "net/socket/tcp_client_socket.h"
#include "net/udp/udp_client_socket.h"

namespace net {

class X509Certificate;

namespace {

bool g_use_system_ssl = false;

class DefaultClientSocketFactory : public ClientSocketFactory,
                                   public CertDatabase::Observer {
 public:
  DefaultClientSocketFactory() {
    CertDatabase::AddObserver(this);
  }

  virtual ~DefaultClientSocketFactory() {
    CertDatabase::RemoveObserver(this);
  }

  virtual void OnUserCertAdded(const X509Certificate* cert) {
    ClearSSLSessionCache();
  }

  virtual void OnCertTrustChanged(const X509Certificate* cert) {
    // Per wtc, we actually only need to flush when trust is reduced.
    // Always flush now because OnCertTrustChanged does not tell us this.
    // See comments in ClientSocketPoolManager::OnCertTrustChanged.
    ClearSSLSessionCache();
  }

  virtual DatagramClientSocket* CreateDatagramClientSocket(
      DatagramSocket::BindType bind_type,
      const RandIntCallback& rand_int_cb,
      NetLog* net_log,
      const NetLog::Source& source) {
    return new UDPClientSocket(bind_type, rand_int_cb, net_log, source);
  }

  virtual StreamSocket* CreateTransportClientSocket(
      const AddressList& addresses,
      NetLog* net_log,
      const NetLog::Source& source) {
    return new TCPClientSocket(addresses, net_log, source);
  }

  virtual SSLClientSocket* CreateSSLClientSocket(
      ClientSocketHandle* transport_socket,
      const HostPortPair& host_and_port,
      const SSLConfig& ssl_config,
      SSLHostInfo* ssl_host_info,
      const SSLClientSocketContext& context) {
    scoped_ptr<SSLHostInfo> shi(ssl_host_info);

#if defined(OS_WIN)
    if (g_use_system_ssl) {
      return new SSLClientSocketWin(transport_socket, host_and_port,
                                    ssl_config, context);
    }
    return new SSLClientSocketNSS(transport_socket, host_and_port, ssl_config,
                                  shi.release(), context);
#elif defined(USE_OPENSSL)
    return new SSLClientSocketOpenSSL(transport_socket, host_and_port,
                                      ssl_config, context);
#elif defined(USE_NSS)
    return new SSLClientSocketNSS(transport_socket, host_and_port, ssl_config,
                                  shi.release(), context);
#elif defined(OS_MACOSX)
    if (g_use_system_ssl) {
      return new SSLClientSocketMac(transport_socket, host_and_port,
                                    ssl_config, context);
    }
    return new SSLClientSocketNSS(transport_socket, host_and_port, ssl_config,
                                  shi.release(), context);
#else
    NOTIMPLEMENTED();
    return NULL;
#endif
  }

  void ClearSSLSessionCache() {
    SSLClientSocket::ClearSessionCache();
  }

};

static base::LazyInstance<DefaultClientSocketFactory>
    g_default_client_socket_factory = LAZY_INSTANCE_INITIALIZER;

}  // namespace

// Deprecated function (http://crbug.com/37810) that takes a StreamSocket.
SSLClientSocket* ClientSocketFactory::CreateSSLClientSocket(
    StreamSocket* transport_socket,
    const HostPortPair& host_and_port,
    const SSLConfig& ssl_config,
    SSLHostInfo* ssl_host_info,
    const SSLClientSocketContext& context) {
  ClientSocketHandle* socket_handle = new ClientSocketHandle();
  socket_handle->set_socket(transport_socket);
  return CreateSSLClientSocket(socket_handle, host_and_port, ssl_config,
                               ssl_host_info, context);
}

// static
ClientSocketFactory* ClientSocketFactory::GetDefaultFactory() {
  return g_default_client_socket_factory.Pointer();
}

// static
void ClientSocketFactory::UseSystemSSL() {
  g_use_system_ssl = true;
}

}  // namespace net
