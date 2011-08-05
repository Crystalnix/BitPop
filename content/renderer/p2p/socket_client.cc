// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/p2p/socket_client.h"

#include "base/message_loop_proxy.h"
#include "content/common/p2p_messages.h"
#include "content/renderer/p2p/socket_dispatcher.h"

P2PSocketClient::P2PSocketClient(P2PSocketDispatcher* dispatcher)
    : dispatcher_(dispatcher),
      ipc_message_loop_(dispatcher->message_loop()),
      delegate_message_loop_(NULL),
      socket_id_(0), delegate_(NULL),
      state_(STATE_UNINITIALIZED) {
}

P2PSocketClient::~P2PSocketClient() {
  DCHECK(state_ == STATE_CLOSED || state_ == STATE_UNINITIALIZED);
}

void P2PSocketClient::Init(
    P2PSocketType type, const net::IPEndPoint& local_address,
    const net::IPEndPoint& remote_address, P2PSocketClient::Delegate* delegate,
    scoped_refptr<base::MessageLoopProxy> delegate_loop) {
  if (!ipc_message_loop_->BelongsToCurrentThread()) {
    ipc_message_loop_->PostTask(
        FROM_HERE, NewRunnableMethod(
            this, &P2PSocketClient::Init, type, local_address, remote_address,
            delegate, delegate_loop));
    return;
  }

  DCHECK_EQ(state_, STATE_UNINITIALIZED);
  state_ = STATE_OPENING;
  delegate_ = delegate;
  delegate_message_loop_ = delegate_loop;
  socket_id_ = dispatcher_->RegisterClient(this);
  dispatcher_->SendP2PMessage(new P2PHostMsg_CreateSocket(
      0, type, socket_id_, local_address, remote_address));
}

void P2PSocketClient::Send(const net::IPEndPoint& address,
                           const std::vector<char>& data) {
  if (!ipc_message_loop_->BelongsToCurrentThread()) {
    ipc_message_loop_->PostTask(
        FROM_HERE, NewRunnableMethod(this, &P2PSocketClient::Send, address,
                                     data));
    return;
  }

  // Can send data only when the socket is open.
  DCHECK(state_ == STATE_OPEN || state_ == STATE_ERROR);
  if (state_ == STATE_OPEN) {
    dispatcher_->SendP2PMessage(
        new P2PHostMsg_Send(0, socket_id_, address, data));
  }
}

void P2PSocketClient::Close() {
  DCHECK(delegate_message_loop_->BelongsToCurrentThread());

  delegate_ = NULL;

  ipc_message_loop_->PostTask(
      FROM_HERE, NewRunnableMethod(this, &P2PSocketClient::DoClose));
}

void P2PSocketClient::DoClose() {
  if (dispatcher_) {
    if (state_ == STATE_OPEN || state_ == STATE_OPENING ||
        state_ == STATE_ERROR) {
      dispatcher_->SendP2PMessage(new P2PHostMsg_DestroySocket(0, socket_id_));
    }
    dispatcher_->UnregisterClient(socket_id_);
  }

  state_ = STATE_CLOSED;
}

void P2PSocketClient::set_delegate(Delegate* delegate) {
  DCHECK(delegate_message_loop_->BelongsToCurrentThread());
  delegate_ = delegate;
}

void P2PSocketClient::OnSocketCreated(const net::IPEndPoint& address) {
  DCHECK(ipc_message_loop_->BelongsToCurrentThread());
  DCHECK_EQ(state_, STATE_OPENING);
  state_ = STATE_OPEN;

  delegate_message_loop_->PostTask(FROM_HERE, NewRunnableMethod(
      this, &P2PSocketClient::DeliverOnSocketCreated, address));
}

void P2PSocketClient::DeliverOnSocketCreated(const net::IPEndPoint& address) {
  if (delegate_)
    delegate_->OnOpen(address);
}

void P2PSocketClient::OnIncomingTcpConnection(const net::IPEndPoint& address) {
  DCHECK(ipc_message_loop_->BelongsToCurrentThread());
  DCHECK_EQ(state_, STATE_OPEN);

  scoped_refptr<P2PSocketClient> new_client = new P2PSocketClient(dispatcher_);
  new_client->socket_id_ = dispatcher_->RegisterClient(new_client);
  new_client->state_ = STATE_OPEN;
  new_client->delegate_message_loop_ = delegate_message_loop_;

  dispatcher_->SendP2PMessage(new P2PHostMsg_AcceptIncomingTcpConnection(
      0, socket_id_, address, new_client->socket_id_));

  delegate_message_loop_->PostTask(FROM_HERE, NewRunnableMethod(
      this, &P2PSocketClient::DeliverOnIncomingTcpConnection,
      address, new_client));
}

void P2PSocketClient::DeliverOnIncomingTcpConnection(
    const net::IPEndPoint& address, scoped_refptr<P2PSocketClient> new_client) {
  if (delegate_)
    delegate_->OnIncomingTcpConnection(address, new_client);
}

void P2PSocketClient::OnError() {
  DCHECK(ipc_message_loop_->BelongsToCurrentThread());
  state_ = STATE_ERROR;

  delegate_message_loop_->PostTask(FROM_HERE, NewRunnableMethod(
      this, &P2PSocketClient::DeliverOnError));
}

void P2PSocketClient::DeliverOnError() {
  if (delegate_)
    delegate_->OnError();
}

void P2PSocketClient::OnDataReceived(const net::IPEndPoint& address,
                                     const std::vector<char>& data) {
  DCHECK(ipc_message_loop_->BelongsToCurrentThread());
  DCHECK_EQ(STATE_OPEN, state_);
  delegate_message_loop_->PostTask(FROM_HERE, NewRunnableMethod(
      this, &P2PSocketClient::DeliverOnDataReceived, address, data));
}

void P2PSocketClient::DeliverOnDataReceived(const net::IPEndPoint& address,
                                            const std::vector<char>& data) {
  if (delegate_)
    delegate_->OnDataReceived(address, data);
}

void P2PSocketClient::Detach() {
  DCHECK(ipc_message_loop_->BelongsToCurrentThread());
  dispatcher_ = NULL;
  OnError();
}
