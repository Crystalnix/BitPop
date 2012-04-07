// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/p2p/p2p_transport_impl.h"

#include "content/renderer/p2p/ipc_network_manager.h"
#include "content/renderer/p2p/ipc_socket_factory.h"
#include "content/renderer/p2p/port_allocator.h"
#include "jingle/glue/channel_socket_adapter.h"
#include "jingle/glue/pseudotcp_adapter.h"
#include "jingle/glue/thread_wrapper.h"
#include "jingle/glue/utils.h"
#include "net/base/net_errors.h"
#include "third_party/libjingle/source/talk/p2p/base/p2ptransportchannel.h"
#include "third_party/libjingle/source/talk/p2p/client/basicportallocator.h"

namespace content {

P2PTransportImpl::P2PTransportImpl(
    talk_base::NetworkManager* network_manager,
    talk_base::PacketSocketFactory* socket_factory)
    : socket_dispatcher_(NULL),
      event_handler_(NULL),
      state_(STATE_NONE),
      network_manager_(network_manager),
      socket_factory_(socket_factory) {
}

P2PTransportImpl::P2PTransportImpl(P2PSocketDispatcher* socket_dispatcher)
    : socket_dispatcher_(socket_dispatcher),
      event_handler_(NULL),
      state_(STATE_NONE),
      network_manager_(new IpcNetworkManager(socket_dispatcher)),
      socket_factory_(new IpcPacketSocketFactory(socket_dispatcher)) {
  DCHECK(socket_dispatcher);
}

P2PTransportImpl::~P2PTransportImpl() {
  MessageLoop* message_loop = MessageLoop::current();

  // Because libjingle's sigslot doesn't handle deletion from a signal
  // handler we have to postpone deletion of libjingle objects.
  message_loop->DeleteSoon(FROM_HERE, channel_.release());
  message_loop->DeleteSoon(FROM_HERE, allocator_.release());
  message_loop->DeleteSoon(FROM_HERE, socket_factory_.release());
  message_loop->DeleteSoon(FROM_HERE, network_manager_.release());
}

bool P2PTransportImpl::Init(WebKit::WebFrame* web_frame,
                            const std::string& name,
                            Protocol protocol,
                            const Config& config,
                            EventHandler* event_handler) {
  DCHECK(event_handler);

  // Before proceeding, ensure we have libjingle thread wrapper for
  // the current thread.
  jingle_glue::JingleThreadWrapper::EnsureForCurrentThread();

  name_ = name;
  event_handler_ = event_handler;

  if (socket_dispatcher_) {
    DCHECK(web_frame);
    allocator_.reset(new P2PPortAllocator(
        web_frame, socket_dispatcher_, network_manager_.get(),
        socket_factory_.get(), config));
  } else {
    // Use BasicPortAllocator if we don't have P2PSocketDispatcher
    // (for unittests).
    allocator_.reset(new cricket::BasicPortAllocator(
        network_manager_.get(), socket_factory_.get()));
  }

  DCHECK(!channel_.get());
  channel_.reset(new cricket::P2PTransportChannel(
      name, "", NULL, allocator_.get()));
  channel_->SignalRequestSignaling.connect(
      this, &P2PTransportImpl::OnRequestSignaling);
  channel_->SignalCandidateReady.connect(
      this, &P2PTransportImpl::OnCandidateReady);

  if (protocol == PROTOCOL_UDP) {
    channel_->SignalReadableState.connect(
        this, &P2PTransportImpl::OnReadableState);
    channel_->SignalWritableState.connect(
        this, &P2PTransportImpl::OnWriteableState);
  }

  channel_adapter_.reset(new jingle_glue::TransportChannelSocketAdapter(
      channel_.get()));

  channel_->Connect();

  if (protocol == PROTOCOL_TCP) {
    pseudo_tcp_adapter_.reset(new jingle_glue::PseudoTcpAdapter(
        channel_adapter_.release()));

    if (config.tcp_receive_window > 0)
      pseudo_tcp_adapter_->SetReceiveBufferSize(config.tcp_receive_window);
    if (config.tcp_send_window > 0)
      pseudo_tcp_adapter_->SetReceiveBufferSize(config.tcp_receive_window);
    pseudo_tcp_adapter_->SetNoDelay(config.tcp_no_delay);
    if (config.tcp_ack_delay_ms > 0)
      pseudo_tcp_adapter_->SetAckDelay(config.tcp_ack_delay_ms);

    int result = pseudo_tcp_adapter_->Connect(
        base::Bind(&P2PTransportImpl::OnTcpConnected, base::Unretained(this)));
    if (result != net::ERR_IO_PENDING)
      OnTcpConnected(result);
  }

  return true;
}

bool P2PTransportImpl::AddRemoteCandidate(const std::string& address) {
  cricket::Candidate candidate;
  if (!jingle_glue::DeserializeP2PCandidate(address, &candidate)) {
    LOG(ERROR) << "Failed to parse candidate " << address;
    return false;
  }

  channel_->OnCandidate(candidate);
  return true;
}

void P2PTransportImpl::OnRequestSignaling() {
  channel_->OnSignalingReady();
}

void P2PTransportImpl::OnCandidateReady(
    cricket::TransportChannelImpl* channel,
    const cricket::Candidate& candidate) {
  event_handler_->OnCandidateReady(
      jingle_glue::SerializeP2PCandidate(candidate));
}

void P2PTransportImpl::OnReadableState(cricket::TransportChannel* channel) {
  state_ = static_cast<State>(state_ | STATE_READABLE);
  event_handler_->OnStateChange(state_);
}

void P2PTransportImpl::OnWriteableState(cricket::TransportChannel* channel) {
  state_ = static_cast<State>(state_ | STATE_WRITABLE);
  event_handler_->OnStateChange(state_);
}

net::Socket* P2PTransportImpl::GetChannel() {
  if (pseudo_tcp_adapter_.get()) {
    DCHECK(!channel_adapter_.get());
    return pseudo_tcp_adapter_.get();
  } else {
    DCHECK(channel_adapter_.get());
    return channel_adapter_.get();
  }
}

void P2PTransportImpl::OnTcpConnected(int result) {
  if (result < 0) {
    event_handler_->OnError(result);
    return;
  }
  state_ = static_cast<State>(STATE_READABLE | STATE_WRITABLE);
  event_handler_->OnStateChange(state_);
}

}  // namespace content
