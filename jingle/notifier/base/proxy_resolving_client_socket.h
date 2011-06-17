// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This ClientSocket implementation wraps a ClientSocketHandle that is created
// from the client socket pool after resolving proxies.

#ifndef JINGLE_NOTIFIER_BASE_PROXY_RESOLVING_CLIENT_SOCKET_H_
#define JINGLE_NOTIFIER_BASE_PROXY_RESOLVING_CLIENT_SOCKET_H_
#pragma once

#include "base/basictypes.h"
#include "base/memory/ref_counted.h"
#include "net/base/completion_callback.h"
#include "net/base/host_port_pair.h"
#include "net/base/net_errors.h"
#include "net/base/net_log.h"
#include "net/base/ssl_config_service.h"
#include "net/proxy/proxy_info.h"
#include "net/proxy/proxy_service.h"
#include "net/socket/client_socket.h"

namespace net {
class ClientSocketHandle;
class HttpNetworkSession;
class URLRequestContextGetter;
}  // namespace net

// TODO(sanjeevr): Move this to net/
namespace notifier {

class ProxyResolvingClientSocket : public net::ClientSocket {
 public:
  ProxyResolvingClientSocket(
    const scoped_refptr<net::URLRequestContextGetter>&
        request_context_getter,
    const net::SSLConfig& ssl_config,
    const net::HostPortPair& dest_host_port_pair,
    net::NetLog* net_log);
  virtual ~ProxyResolvingClientSocket();

  // net::ClientSocket implementation.
  virtual int Read(net::IOBuffer* buf, int buf_len,
                   net::CompletionCallback* callback) OVERRIDE;
  virtual int Write(net::IOBuffer* buf, int buf_len,
                    net::CompletionCallback* callback) OVERRIDE;
  virtual bool SetReceiveBufferSize(int32 size) OVERRIDE;
  virtual bool SetSendBufferSize(int32 size) OVERRIDE;
  virtual int Connect(net::CompletionCallback* callback) OVERRIDE;
  virtual void Disconnect() OVERRIDE;
  virtual bool IsConnected() const OVERRIDE;
  virtual bool IsConnectedAndIdle() const OVERRIDE;
  virtual int GetPeerAddress(net::AddressList* address) const OVERRIDE;
  virtual int GetLocalAddress(net::IPEndPoint* address) const OVERRIDE;
  virtual const net::BoundNetLog& NetLog() const OVERRIDE;
  virtual void SetSubresourceSpeculation() OVERRIDE;
  virtual void SetOmniboxSpeculation() OVERRIDE;
  virtual bool WasEverUsed() const OVERRIDE;
  virtual bool UsingTCPFastOpen() const OVERRIDE;

 private:
  // Proxy resolution and connection functions.
  void ProcessProxyResolveDone(int status);
  void ProcessConnectDone(int status);

  void CloseTransportSocket();
  void RunUserConnectCallback(int status);
  int ReconsiderProxyAfterError(int error);

  // Callbacks passed to net APIs.
  net::CompletionCallbackImpl<ProxyResolvingClientSocket>
      proxy_resolve_callback_;
  net::CompletionCallbackImpl<ProxyResolvingClientSocket> connect_callback_;

  scoped_refptr<net::HttpNetworkSession> network_session_;

  // The transport socket.
  scoped_ptr<net::ClientSocketHandle> transport_;

  const net::SSLConfig ssl_config_;
  net::ProxyService::PacRequest* pac_request_;
  net::ProxyInfo proxy_info_;
  net::HostPortPair dest_host_port_pair_;
  bool tried_direct_connect_fallback_;
  net::BoundNetLog bound_net_log_;
  ScopedRunnableMethodFactory<ProxyResolvingClientSocket>
      scoped_runnable_method_factory_;

  // The callback passed to Connect().
  net::CompletionCallback* user_connect_callback_;
};

}  // namespace notifier

#endif  // JINGLE_NOTIFIER_BASE_PROXY_RESOLVING_CLIENT_SOCKET_H_
