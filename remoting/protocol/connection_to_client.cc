// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/protocol/connection_to_client.h"

#include "base/bind.h"
#include "base/location.h"
#include "base/message_loop_proxy.h"
#include "google/protobuf/message.h"
#include "net/base/io_buffer.h"
#include "remoting/protocol/host_control_dispatcher.h"
#include "remoting/protocol/host_event_dispatcher.h"
#include "remoting/protocol/host_stub.h"
#include "remoting/protocol/input_stub.h"

namespace remoting {
namespace protocol {

ConnectionToClient::ConnectionToClient(protocol::Session* session)
    : handler_(NULL),
      host_stub_(NULL),
      input_stub_(NULL),
      session_(session) {
  session_->SetStateChangeCallback(
      base::Bind(&ConnectionToClient::OnSessionStateChange,
                 base::Unretained(this)));
  session_->SetRouteChangeCallback(
      base::Bind(&ConnectionToClient::OnSessionRouteChange,
                 base::Unretained(this)));
}

ConnectionToClient::~ConnectionToClient() {
  if (session_.get()) {
    base::MessageLoopProxy::current()->DeleteSoon(
        FROM_HERE, session_.release());
  }
}

void ConnectionToClient::SetEventHandler(EventHandler* event_handler) {
  DCHECK(CalledOnValidThread());
  handler_ = event_handler;
}

protocol::Session* ConnectionToClient::session() {
  DCHECK(CalledOnValidThread());
  return session_.get();
}

void ConnectionToClient::Disconnect() {
  DCHECK(CalledOnValidThread());

  CloseChannels();

  DCHECK(session_.get());
  Session* session = session_.release();

  // It may not be safe to delete |session_| here becase this method
  // may be invoked in resonse to a libjingle event and libjingle's
  // sigslot doesn't handle it properly, so postpone the deletion.
  base::MessageLoopProxy::current()->DeleteSoon(FROM_HERE, session);

  // This should trigger OnConnectionClosed() event and this object
  // may be destroyed as the result.
  session->Close();
}

void ConnectionToClient::UpdateSequenceNumber(int64 sequence_number) {
  DCHECK(CalledOnValidThread());
  handler_->OnSequenceNumberUpdated(this, sequence_number);
}

VideoStub* ConnectionToClient::video_stub() {
  DCHECK(CalledOnValidThread());
  return video_writer_.get();
}

// Return pointer to ClientStub.
ClientStub* ConnectionToClient::client_stub() {
  DCHECK(CalledOnValidThread());
  return control_dispatcher_.get();
}

void ConnectionToClient::set_host_stub(protocol::HostStub* host_stub) {
  DCHECK(CalledOnValidThread());
  host_stub_ = host_stub;
}

void ConnectionToClient::set_input_stub(protocol::InputStub* input_stub) {
  DCHECK(CalledOnValidThread());
  input_stub_ = input_stub;
}

void ConnectionToClient::OnSessionStateChange(Session::State state) {
  DCHECK(CalledOnValidThread());

  DCHECK(handler_);
  switch(state) {
    case Session::INITIALIZING:
    case Session::CONNECTING:
    case Session::CONNECTED:
      // Don't care about these events.
      break;

    case Session::AUTHENTICATED:
      // Initialize channels.
      control_dispatcher_.reset(new HostControlDispatcher());
      control_dispatcher_->Init(session_.get(), base::Bind(
          &ConnectionToClient::OnChannelInitialized, base::Unretained(this)));
      control_dispatcher_->set_host_stub(host_stub_);

      event_dispatcher_.reset(new HostEventDispatcher());
      event_dispatcher_->Init(session_.get(), base::Bind(
          &ConnectionToClient::OnChannelInitialized, base::Unretained(this)));
      event_dispatcher_->set_input_stub(input_stub_);
      event_dispatcher_->set_sequence_number_callback(base::Bind(
          &ConnectionToClient::UpdateSequenceNumber, base::Unretained(this)));

      video_writer_.reset(VideoWriter::Create(
          base::MessageLoopProxy::current(), session_->config()));
      video_writer_->Init(session_.get(), base::Bind(
          &ConnectionToClient::OnChannelInitialized, base::Unretained(this)));

      break;

    case Session::CLOSED:
      CloseChannels();
      handler_->OnConnectionClosed(this);
      break;

    case Session::FAILED:
      CloseOnError();
      break;
  }
}

void ConnectionToClient::OnSessionRouteChange(
    const std::string& channel_name, const net::IPEndPoint& end_point) {
  handler_->OnClientIpAddress(this, channel_name, end_point);
}

void ConnectionToClient::OnChannelInitialized(bool successful) {
  DCHECK(CalledOnValidThread());

  if (!successful) {
    LOG(ERROR) << "Failed to connect a channel";
    CloseOnError();
    return;
  }

  NotifyIfChannelsReady();
}

void ConnectionToClient::NotifyIfChannelsReady() {
  DCHECK(CalledOnValidThread());

  if (control_dispatcher_.get() && control_dispatcher_->is_connected() &&
      event_dispatcher_.get() && event_dispatcher_->is_connected() &&
      video_writer_.get() && video_writer_->is_connected()) {
    handler_->OnConnectionOpened(this);
  }
}

void ConnectionToClient::CloseOnError() {
  CloseChannels();
  handler_->OnConnectionFailed(this, session_->error());
}

void ConnectionToClient::CloseChannels() {
  control_dispatcher_.reset();
  event_dispatcher_.reset();
  video_writer_.reset();
}

}  // namespace protocol
}  // namespace remoting
