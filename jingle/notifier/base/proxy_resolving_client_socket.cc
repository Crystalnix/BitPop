// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "jingle/notifier/base/proxy_resolving_client_socket.h"

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/logging.h"
#include "googleurl/src/gurl.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/http/http_network_session.h"
#include "net/socket/client_socket_handle.h"
#include "net/socket/client_socket_pool_manager.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_getter.h"

namespace notifier {

ProxyResolvingClientSocket::ProxyResolvingClientSocket(
    const scoped_refptr<net::URLRequestContextGetter>& request_context_getter,
    const net::SSLConfig& ssl_config,
    const net::HostPortPair& dest_host_port_pair,
    net::NetLog* net_log)
        : proxy_resolve_callback_(ALLOW_THIS_IN_INITIALIZER_LIST(this),
                        &ProxyResolvingClientSocket::ProcessProxyResolveDone),
          connect_callback_(ALLOW_THIS_IN_INITIALIZER_LIST(this),
                            &ProxyResolvingClientSocket::ProcessConnectDone),
          ssl_config_(ssl_config),
          pac_request_(NULL),
          dest_host_port_pair_(dest_host_port_pair),
          tried_direct_connect_fallback_(false),
          bound_net_log_(
              net::BoundNetLog::Make(net_log, net::NetLog::SOURCE_SOCKET)),
          scoped_runnable_method_factory_(
              ALLOW_THIS_IN_INITIALIZER_LIST(this)),
          user_connect_callback_(NULL) {
  DCHECK(request_context_getter);
  net::URLRequestContext* request_context =
      request_context_getter->GetURLRequestContext();
  DCHECK(request_context);
  net::HttpNetworkSession::Params session_params;
  session_params.host_resolver = request_context->host_resolver();
  session_params.cert_verifier = request_context->cert_verifier();
  session_params.dnsrr_resolver = request_context->dnsrr_resolver();
  session_params.proxy_service = request_context->proxy_service();
  session_params.ssl_config_service = request_context->ssl_config_service();
  session_params.http_auth_handler_factory =
      request_context->http_auth_handler_factory();
  network_session_ = new net::HttpNetworkSession(session_params);
}

ProxyResolvingClientSocket::~ProxyResolvingClientSocket() {
  Disconnect();
}

int ProxyResolvingClientSocket::Read(net::IOBuffer* buf, int buf_len,
                                     net::CompletionCallback* callback) {
  if (transport_.get() && transport_->socket())
    return transport_->socket()->Read(buf, buf_len, callback);
  NOTREACHED();
  return net::ERR_SOCKET_NOT_CONNECTED;
}

int ProxyResolvingClientSocket::Write(net::IOBuffer* buf, int buf_len,
                                      net::CompletionCallback* callback) {
  if (transport_.get() && transport_->socket())
    return transport_->socket()->Write(buf, buf_len, callback);
  NOTREACHED();
  return net::ERR_SOCKET_NOT_CONNECTED;
}

bool ProxyResolvingClientSocket::SetReceiveBufferSize(int32 size) {
  if (transport_.get() && transport_->socket())
    return transport_->socket()->SetReceiveBufferSize(size);
  NOTREACHED();
  return false;
}

bool ProxyResolvingClientSocket::SetSendBufferSize(int32 size) {
  if (transport_.get() && transport_->socket())
    return transport_->socket()->SetSendBufferSize(size);
  NOTREACHED();
  return false;
}

int ProxyResolvingClientSocket::Connect(net::CompletionCallback* callback) {
  DCHECK(!user_connect_callback_);

  tried_direct_connect_fallback_ = false;

  // First we try and resolve the proxy.
  GURL url("http://" + dest_host_port_pair_.ToString());
  int status = network_session_->proxy_service()->ResolveProxy(
      url,
      &proxy_info_,
      &proxy_resolve_callback_,
      &pac_request_,
      bound_net_log_);
  if (status != net::ERR_IO_PENDING) {
    // We defer execution of ProcessProxyResolveDone instead of calling it
    // directly here for simplicity. From the caller's point of view,
    // the connect always happens asynchronously.
    MessageLoop* message_loop = MessageLoop::current();
    CHECK(message_loop);
    message_loop->PostTask(
        FROM_HERE,
        scoped_runnable_method_factory_.NewRunnableMethod(
            &ProxyResolvingClientSocket::ProcessProxyResolveDone, status));
  }
  user_connect_callback_ = callback;
  return net::ERR_IO_PENDING;
}

void ProxyResolvingClientSocket::RunUserConnectCallback(int status) {
  DCHECK_LE(status, net::OK);
  net::CompletionCallback* user_connect_callback = user_connect_callback_;
  user_connect_callback_ = NULL;
  user_connect_callback->Run(status);
}

// Always runs asynchronously.
void ProxyResolvingClientSocket::ProcessProxyResolveDone(int status) {
  pac_request_ = NULL;

  DCHECK_NE(status, net::ERR_IO_PENDING);
  if (status == net::OK) {
    // Remove unsupported proxies from the list.
    proxy_info_.RemoveProxiesWithoutScheme(
        net::ProxyServer::SCHEME_DIRECT |
        net::ProxyServer::SCHEME_HTTP | net::ProxyServer::SCHEME_HTTPS |
        net::ProxyServer::SCHEME_SOCKS4 | net::ProxyServer::SCHEME_SOCKS5);

    if (proxy_info_.is_empty()) {
      // No proxies/direct to choose from. This happens when we don't support
      // any of the proxies in the returned list.
      status = net::ERR_NO_SUPPORTED_PROXIES;
    }
  }

  // Since we are faking the URL, it is possible that no proxies match our URL.
  // Try falling back to a direct connection if we have not tried that before.
  if (status != net::OK) {
    if (!tried_direct_connect_fallback_) {
      tried_direct_connect_fallback_ = true;
      proxy_info_.UseDirect();
    } else {
      CloseTransportSocket();
      RunUserConnectCallback(status);
      return;
    }
  }

  transport_.reset(new net::ClientSocketHandle);
  // Now that we have resolved the proxy, we need to connect.
  status = net::ClientSocketPoolManager::InitSocketHandleForRawConnect(
      dest_host_port_pair_,
      network_session_.get(),
      proxy_info_,
      ssl_config_,
      ssl_config_,
      bound_net_log_,
      transport_.get(),
      &connect_callback_);
  if (status != net::ERR_IO_PENDING) {
    // Since this method is always called asynchronously. it is OK to call
    // ProcessConnectDone synchronously.
    ProcessConnectDone(status);
  }
}

void ProxyResolvingClientSocket::ProcessConnectDone(int status) {
  if (status != net::OK) {
    // If the connection fails, try another proxy.
    status = ReconsiderProxyAfterError(status);
    // ReconsiderProxyAfterError either returns an error (in which case it is
    // not reconsidering a proxy) or returns ERR_IO_PENDING if it is considering
    // another proxy.
    DCHECK_NE(status, net::OK);
    if (status == net::ERR_IO_PENDING)
      // Proxy reconsideration pending. Return.
      return;
    CloseTransportSocket();
  }
  RunUserConnectCallback(status);
}

// TODO(sanjeevr): This has largely been copied from
// HttpStreamFactoryImpl::Job::ReconsiderProxyAfterError. This should be
// refactored into some common place.
// This method reconsiders the proxy on certain errors. If it does reconsider
// a proxy it always returns ERR_IO_PENDING and posts a call to
// ProcessProxyResolveDone with the result of the reconsideration.
int ProxyResolvingClientSocket::ReconsiderProxyAfterError(int error) {
  DCHECK(!pac_request_);
  DCHECK_NE(error, net::OK);
  DCHECK_NE(error, net::ERR_IO_PENDING);
  // A failure to resolve the hostname or any error related to establishing a
  // TCP connection could be grounds for trying a new proxy configuration.
  //
  // Why do this when a hostname cannot be resolved?  Some URLs only make sense
  // to proxy servers.  The hostname in those URLs might fail to resolve if we
  // are still using a non-proxy config.  We need to check if a proxy config
  // now exists that corresponds to a proxy server that could load the URL.
  //
  switch (error) {
    case net::ERR_PROXY_CONNECTION_FAILED:
    case net::ERR_NAME_NOT_RESOLVED:
    case net::ERR_INTERNET_DISCONNECTED:
    case net::ERR_ADDRESS_UNREACHABLE:
    case net::ERR_CONNECTION_CLOSED:
    case net::ERR_CONNECTION_RESET:
    case net::ERR_CONNECTION_REFUSED:
    case net::ERR_CONNECTION_ABORTED:
    case net::ERR_TIMED_OUT:
    case net::ERR_TUNNEL_CONNECTION_FAILED:
    case net::ERR_SOCKS_CONNECTION_FAILED:
      break;
    case net::ERR_SOCKS_CONNECTION_HOST_UNREACHABLE:
      // Remap the SOCKS-specific "host unreachable" error to a more
      // generic error code (this way consumers like the link doctor
      // know to substitute their error page).
      //
      // Note that if the host resolving was done by the SOCSK5 proxy, we can't
      // differentiate between a proxy-side "host not found" versus a proxy-side
      // "address unreachable" error, and will report both of these failures as
      // ERR_ADDRESS_UNREACHABLE.
      return net::ERR_ADDRESS_UNREACHABLE;
    default:
      return error;
  }

  if (proxy_info_.is_https() && ssl_config_.send_client_cert) {
    network_session_->ssl_client_auth_cache()->Remove(
        proxy_info_.proxy_server().host_port_pair().ToString());
  }

  GURL url("http://" + dest_host_port_pair_.ToString());
  int rv = network_session_->proxy_service()->ReconsiderProxyAfterError(
      url, &proxy_info_, &proxy_resolve_callback_, &pac_request_,
      bound_net_log_);
  if (rv == net::OK || rv == net::ERR_IO_PENDING) {
    CloseTransportSocket();
  } else {
    // If ReconsiderProxyAfterError() failed synchronously, it means
    // there was nothing left to fall-back to, so fail the transaction
    // with the last connection error we got.
    rv = error;
  }

  // We either have new proxy info or there was an error in falling back.
  // In both cases we want to post ProcessProxyResolveDone (in the error case
  // we might still want to fall back a direct connection).
  if (rv != net::ERR_IO_PENDING) {
    MessageLoop* message_loop = MessageLoop::current();
    CHECK(message_loop);
    message_loop->PostTask(
        FROM_HERE,
        scoped_runnable_method_factory_.NewRunnableMethod(
            &ProxyResolvingClientSocket::ProcessProxyResolveDone, rv));
    // Since we potentially have another try to go (trying the direct connect)
    // set the return code code to ERR_IO_PENDING.
    rv = net::ERR_IO_PENDING;
  }
  return rv;
}

void ProxyResolvingClientSocket::Disconnect() {
  CloseTransportSocket();
  if (pac_request_)
    network_session_->proxy_service()->CancelPacRequest(pac_request_);
  user_connect_callback_ = NULL;
}

bool ProxyResolvingClientSocket::IsConnected() const {
  if (!transport_.get() || !transport_->socket())
    return false;
  return transport_->socket()->IsConnected();
}

bool ProxyResolvingClientSocket::IsConnectedAndIdle() const {
  if (!transport_.get() || !transport_->socket())
    return false;
  return transport_->socket()->IsConnectedAndIdle();
}

int ProxyResolvingClientSocket::GetPeerAddress(
    net::AddressList* address) const {
  if (transport_.get() && transport_->socket())
    return transport_->socket()->GetPeerAddress(address);
  NOTREACHED();
  return net::ERR_SOCKET_NOT_CONNECTED;
}

int ProxyResolvingClientSocket::GetLocalAddress(
    net::IPEndPoint* address) const {
  if (transport_.get() && transport_->socket())
    return transport_->socket()->GetLocalAddress(address);
  NOTREACHED();
  return net::ERR_SOCKET_NOT_CONNECTED;
}

const net::BoundNetLog& ProxyResolvingClientSocket::NetLog() const {
  if (transport_.get() && transport_->socket())
    return transport_->socket()->NetLog();
  NOTREACHED();
  return bound_net_log_;
}

void ProxyResolvingClientSocket::SetSubresourceSpeculation() {
  if (transport_.get() && transport_->socket())
    transport_->socket()->SetSubresourceSpeculation();
  else
    NOTREACHED();
}

void ProxyResolvingClientSocket::SetOmniboxSpeculation() {
  if (transport_.get() && transport_->socket())
    transport_->socket()->SetOmniboxSpeculation();
  else
    NOTREACHED();
}

bool ProxyResolvingClientSocket::WasEverUsed() const {
  if (transport_.get() && transport_->socket())
    return transport_->socket()->WasEverUsed();
  NOTREACHED();
  return false;
}

bool ProxyResolvingClientSocket::UsingTCPFastOpen() const {
  if (transport_.get() && transport_->socket())
    return transport_->socket()->UsingTCPFastOpen();
  NOTREACHED();
  return false;
}

void ProxyResolvingClientSocket::CloseTransportSocket() {
  if (transport_.get() && transport_->socket())
    transport_->socket()->Disconnect();
  transport_.reset();
}

}  // namespace notifier
