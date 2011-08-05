// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SOCKET_UDP_SERVER_SOCKET_H_
#define NET_SOCKET_UDP_SERVER_SOCKET_H_
#pragma once

#include "net/base/completion_callback.h"
#include "net/udp/datagram_server_socket.h"
#include "net/udp/udp_socket.h"

namespace net {

class IPEndPoint;
class BoundNetLog;

// A client socket that uses UDP as the transport layer.
class NET_API UDPServerSocket : public DatagramServerSocket {
 public:
  UDPServerSocket(net::NetLog* net_log,
                  const net::NetLog::Source& source);
  virtual ~UDPServerSocket();

  // Implement DatagramServerSocket:
  virtual int Listen(const IPEndPoint& address);
  virtual int RecvFrom(IOBuffer* buf,
                       int buf_len,
                       IPEndPoint* address,
                       CompletionCallback* callback);
  virtual int SendTo(IOBuffer* buf,
                     int buf_len,
                     const IPEndPoint& address,
                     CompletionCallback* callback);
  virtual void Close();
  virtual int GetPeerAddress(IPEndPoint* address) const;
  virtual int GetLocalAddress(IPEndPoint* address) const;

 private:
  UDPSocket socket_;
  DISALLOW_COPY_AND_ASSIGN(UDPServerSocket);
};

}  // namespace net

#endif  // NET_SOCKET_UDP_SERVER_SOCKET_H_
