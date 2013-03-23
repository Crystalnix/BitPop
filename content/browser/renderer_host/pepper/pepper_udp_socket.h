// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RENDERER_HOST_PEPPER_PEPPER_UDP_SOCKET_H_
#define CONTENT_BROWSER_RENDERER_HOST_PEPPER_PEPPER_UDP_SOCKET_H_

#include <string>

#include "base/basictypes.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "net/base/completion_callback.h"
#include "net/base/ip_endpoint.h"
#include "ppapi/c/pp_stdint.h"

struct PP_NetAddress_Private;

namespace net {
class IOBuffer;
class UDPServerSocket;
}

namespace content {
class PepperMessageFilter;

// PepperUDPSocket is used by PepperMessageFilter to handle requests from
// the Pepper UDP socket API (PPB_UDPSocket_Private).
class PepperUDPSocket {
 public:
  PepperUDPSocket(PepperMessageFilter* manager,
                  int32 routing_id,
                  uint32 plugin_dispatcher_id,
                  uint32 socket_id);
  ~PepperUDPSocket();

  int routing_id() { return routing_id_; }

  void AllowAddressReuse(bool value);
  void AllowBroadcast(bool value);
  void Bind(const PP_NetAddress_Private& addr);
  void RecvFrom(int32_t num_bytes);
  void SendTo(const std::string& data, const PP_NetAddress_Private& addr);
  void SendBindACKError();
  void SendSendToACKError();

 private:
  void SendRecvFromACKError();

  void OnBindCompleted(int result);
  void OnRecvFromCompleted(int result);
  void OnSendToCompleted(int result);

  PepperMessageFilter* manager_;
  int32 routing_id_;
  uint32 plugin_dispatcher_id_;
  uint32 socket_id_;
  bool allow_address_reuse_;
  bool allow_broadcast_;

  scoped_ptr<net::UDPServerSocket> socket_;

  scoped_refptr<net::IOBuffer> recvfrom_buffer_;
  scoped_refptr<net::IOBuffer> sendto_buffer_;

  net::IPEndPoint recvfrom_address_;
  net::IPEndPoint bound_address_;

  DISALLOW_COPY_AND_ASSIGN(PepperUDPSocket);
};

}  // namespace content

#endif  // CONTENT_BROWSER_RENDERER_HOST_PEPPER_PEPPER_UDP_SOCKET_H_
