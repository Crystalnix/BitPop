// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RENDERER_HOST_P2P_SOCKET_HOST_TCP_SERVER_H_
#define CONTENT_BROWSER_RENDERER_HOST_P2P_SOCKET_HOST_TCP_SERVER_H_

#include <map>

#include "base/compiler_specific.h"
#include "base/message_loop.h"
#include "base/scoped_ptr.h"
#include "content/browser/renderer_host/p2p/socket_host.h"
#include "content/common/p2p_sockets.h"
#include "net/socket/tcp_server_socket.h"

namespace net {
class ClientSocket;
}  // namespace net

class P2PSocketHostTcpServer : public P2PSocketHost {
 public:
  P2PSocketHostTcpServer(IPC::Message::Sender* message_sender,
                   int routing_id, int id);
  virtual ~P2PSocketHostTcpServer();

  // P2PSocketHost overrides.
  virtual bool Init(const net::IPEndPoint& local_address,
                    const net::IPEndPoint& remote_address) OVERRIDE;
  virtual void Send(const net::IPEndPoint& to,
                    const std::vector<char>& data) OVERRIDE;
  virtual P2PSocketHost* AcceptIncomingTcpConnection(
      const net::IPEndPoint& remote_address, int id) OVERRIDE;

 private:
  typedef std::map<net::IPEndPoint, net::ClientSocket*> AcceptedSocketsMap;

  void OnError();

  void DoAccept();
  void HandleAcceptResult(int result);

  // Callback for Accept().
  void OnAccepted(int result);

  scoped_ptr<net::TCPServerSocket> socket_;
  net::IPEndPoint local_address_;

  scoped_ptr<net::ClientSocket> accept_socket_;
  AcceptedSocketsMap accepted_sockets_;

  net::CompletionCallbackImpl<P2PSocketHostTcpServer> accept_callback_;

  DISALLOW_COPY_AND_ASSIGN(P2PSocketHostTcpServer);
};

#endif  // CONTENT_BROWSER_RENDERER_HOST_P2P_SOCKET_HOST_TCP_SERVER_H_
