// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_CURVECP_CLIENT_PACKETIZER_H_
#define NET_CURVECP_CLIENT_PACKETIZER_H_

#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time.h"
#include "net/base/address_list.h"
#include "net/base/completion_callback.h"
#include "net/curvecp/packetizer.h"
#include "net/curvecp/protocol.h"

namespace net {

class AddressList;
class IOBuffer;
class IPEndPoint;
class UDPClientSocket;

class ClientPacketizer : public Packetizer {
 public:
  ClientPacketizer();
  virtual ~ClientPacketizer();

  int Connect(const AddressList& server,
              Packetizer::Listener* listener,
              const CompletionCallback& callback);

  // Packetizer implementation.
  virtual int SendMessage(ConnectionKey key,
                          const char* data,
                          size_t length,
                          const CompletionCallback& callback) OVERRIDE;
  virtual void Close(ConnectionKey key) OVERRIDE;
  virtual int GetPeerAddress(IPEndPoint* endpoint) const OVERRIDE;
  virtual int max_message_payload() const OVERRIDE;

 private:
  enum StateType {
    NONE,                      // The initial state, before connect.
    LOOKUP_COOKIE,             // Looking up a cookie in the disk cache.
    LOOKUP_COOKIE_COMPLETE,    // The disk cache lookup is complete.
    SENDING_HELLO,             // Sending a Hello packet.
    SENDING_HELLO_COMPLETE,    // Hello packet has been sent.
    WAITING_COOKIE,            // Waiting for a Cookie packet.
    WAITING_COOKIE_COMPLETE,   // The Cookie packet has arrived.
    CONNECTED,                 // Connected
  };

  int DoLoop(int result);
  int DoLookupCookie();
  int DoLookupCookieComplete(int result);
  int DoSendingHello();
  int DoSendingHelloComplete(int result);
  int DoWaitingCookie();
  int DoWaitingCookieComplete(int result);
  int DoConnected(int result);

  void DoCallback(int result);

  // Connect to the next address in our list.
  int ConnectNextAddress();

  // We set a timeout for responses to the Hello message.
  void StartHelloTimer(base::TimeDelta delay);
  void RevokeHelloTimer();
  void OnHelloTimeout();   // Called when the Hello Timer fires.

  // Process the result of a Read operation.
  void ProcessRead(int bytes_read);

  // Read packets until an error occurs.
  int ReadPackets();

  // Callback when an internal IO is completed.
  void OnIOComplete(int result);

  StateType next_state_;
  scoped_ptr<UDPClientSocket> socket_;
  Packetizer::Listener* listener_;
  CompletionCallback user_callback_;
  AddressList addresses_;
  int current_address_index_;
  int hello_attempts_;  // Number of attempts to send a Hello Packet.
  bool initiate_sent_;  // Indicates whether the Initiate Packet was sent.

  scoped_refptr<IOBuffer> read_buffer_;  // Buffer for internal reads.

  uchar shortterm_public_key_[32];

  CompletionCallback io_callback_;
  base::WeakPtrFactory<ClientPacketizer> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(ClientPacketizer);
};

}  // namespace net

#endif  // NET_CURVECP_CLIENT_PACKETIZER_H_
