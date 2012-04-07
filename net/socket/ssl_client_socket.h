// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SOCKET_SSL_CLIENT_SOCKET_H_
#define NET_SOCKET_SSL_CLIENT_SOCKET_H_
#pragma once

#include <string>

#include "net/base/completion_callback.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/socket/ssl_socket.h"
#include "net/socket/stream_socket.h"

namespace net {

class CertVerifier;
class OriginBoundCertService;
class SSLCertRequestInfo;
class SSLHostInfo;
class SSLHostInfoFactory;
class SSLInfo;
class TransportSecurityState;

// This struct groups together several fields which are used by various
// classes related to SSLClientSocket.
struct SSLClientSocketContext {
  SSLClientSocketContext()
      : cert_verifier(NULL),
        origin_bound_cert_service(NULL),
        transport_security_state(NULL),
        ssl_host_info_factory(NULL) {}

  SSLClientSocketContext(CertVerifier* cert_verifier_arg,
                         OriginBoundCertService* origin_bound_cert_service_arg,
                         TransportSecurityState* transport_security_state_arg,
                         SSLHostInfoFactory* ssl_host_info_factory_arg,
                         const std::string& ssl_session_cache_shard_arg)
      : cert_verifier(cert_verifier_arg),
        origin_bound_cert_service(origin_bound_cert_service_arg),
        transport_security_state(transport_security_state_arg),
        ssl_host_info_factory(ssl_host_info_factory_arg),
        ssl_session_cache_shard(ssl_session_cache_shard_arg) {}

  CertVerifier* cert_verifier;
  OriginBoundCertService* origin_bound_cert_service;
  TransportSecurityState* transport_security_state;
  SSLHostInfoFactory* ssl_host_info_factory;
  // ssl_session_cache_shard is an opaque string that identifies a shard of the
  // SSL session cache. SSL sockets with the same ssl_session_cache_shard may
  // resume each other's SSL sessions but we'll never sessions between shards.
  const std::string ssl_session_cache_shard;
};

// A client socket that uses SSL as the transport layer.
//
// NOTE: The SSL handshake occurs within the Connect method after a TCP
// connection is established.  If a SSL error occurs during the handshake,
// Connect will fail.
//
class NET_EXPORT SSLClientSocket : public SSLSocket {
 public:
  SSLClientSocket();

  // Next Protocol Negotiation (NPN) allows a TLS client and server to come to
  // an agreement about the application level protocol to speak over a
  // connection.
  enum NextProtoStatus {
    // WARNING: These values are serialized to disk. Don't change them.

    kNextProtoUnsupported = 0,  // The server doesn't support NPN.
    kNextProtoNegotiated = 1,   // We agreed on a protocol.
    kNextProtoNoOverlap = 2,    // No protocols in common. We requested
                                // the first protocol in our list.
  };

  // Next Protocol Negotiation (NPN), if successful, results in agreement on an
  // application-level string that specifies the application level protocol to
  // use over the TLS connection. NextProto enumerates the application level
  // protocols that we recognise.
  enum NextProto {
    kProtoUnknown = 0,
    kProtoHTTP11 = 1,
    kProtoSPDY1 = 2,
    kProtoSPDY2 = 3,
    kProtoSPDY21 = 4,
  };

  // Gets the SSL connection information of the socket.
  //
  // TODO(sergeyu): Move this method to the SSLSocket interface and
  // implemented in SSLServerSocket too.
  virtual void GetSSLInfo(SSLInfo* ssl_info) = 0;

  // Gets the SSL CertificateRequest info of the socket after Connect failed
  // with ERR_SSL_CLIENT_AUTH_CERT_NEEDED.
  virtual void GetSSLCertRequestInfo(
      SSLCertRequestInfo* cert_request_info) = 0;

  // Get the application level protocol that we negotiated with the server.
  // *proto is set to the resulting protocol (n.b. that the string may have
  // embedded NULs).
  //   kNextProtoUnsupported: *proto is cleared.
  //   kNextProtoNegotiated:  *proto is set to the negotiated protocol.
  //   kNextProtoNoOverlap:   *proto is set to the first protocol in the
  //                          supported list.
  // *server_protos is set to the server advertised protocols.
  virtual NextProtoStatus GetNextProto(std::string* proto,
                                       std::string* server_protos) = 0;

  static NextProto NextProtoFromString(const std::string& proto_string);

  static const char* NextProtoToString(SSLClientSocket::NextProto next_proto);

  static const char* NextProtoStatusToString(
      const SSLClientSocket::NextProtoStatus status);

  // Can be used with the second argument(|server_protos|) of |GetNextProto| to
  // construct a comma separated string of server advertised protocols.
  static std::string ServerProtosToString(const std::string& server_protos);

  static bool IgnoreCertError(int error, int load_flags);

  // ClearSessionCache clears the SSL session cache, used to resume SSL
  // sessions.
  static void ClearSessionCache();

  virtual bool was_npn_negotiated() const;

  virtual bool set_was_npn_negotiated(bool negotiated);

  virtual bool was_spdy_negotiated() const;

  virtual bool set_was_spdy_negotiated(bool negotiated);

  virtual SSLClientSocket::NextProto protocol_negotiated() const;

  virtual void set_protocol_negotiated(
      SSLClientSocket::NextProto protocol_negotiated);

  // Returns true if an origin bound certificate was sent on this connection.
  // This may be useful for protocols, like SPDY, which allow the same
  // connection to be shared between multiple origins, each of which need
  // an origin bound certificate.
  virtual bool was_origin_bound_cert_sent() const;

  virtual bool set_was_origin_bound_cert_sent(bool sent);

 private:
  // True if NPN was responded to, independent of selecting SPDY or HTTP.
  bool was_npn_negotiated_;
  // True if NPN successfully negotiated SPDY.
  bool was_spdy_negotiated_;
  // Protocol that we negotiated with the server.
  SSLClientSocket::NextProto protocol_negotiated_;
  // True if an origin bound certificate was sent.
  bool was_origin_bound_cert_sent_;
};

}  // namespace net

#endif  // NET_SOCKET_SSL_CLIENT_SOCKET_H_
