// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/protocol/connection_to_client.h"

#include "google/protobuf/message.h"
#include "net/base/io_buffer.h"
#include "remoting/protocol/client_control_sender.h"
#include "remoting/protocol/host_message_dispatcher.h"
#include "remoting/protocol/host_stub.h"
#include "remoting/protocol/input_stub.h"

// TODO(hclam): Remove this header once MessageDispatcher is used.
#include "remoting/base/compound_buffer.h"

namespace remoting {
namespace protocol {

// Determine how many update streams we should count to find the size of
// average update stream.
static const size_t kAverageUpdateStream = 10;

ConnectionToClient::ConnectionToClient(MessageLoop* message_loop,
                                       EventHandler* handler)
    : loop_(message_loop),
      handler_(handler),
      host_stub_(NULL),
      input_stub_(NULL) {
  DCHECK(loop_);
  DCHECK(handler_);
}

ConnectionToClient::~ConnectionToClient() {
  // TODO(hclam): When we shut down the viewer we may have to close the
  // connection.
}

void ConnectionToClient::Init(protocol::Session* session) {
  DCHECK_EQ(session->message_loop(), MessageLoop::current());

  session_ = session;
  session_->SetStateChangeCallback(
      NewCallback(this, &ConnectionToClient::OnSessionStateChange));
}

protocol::Session* ConnectionToClient::session() {
  return session_;
}

void ConnectionToClient::Disconnect() {
  // This method can be called from main thread so perform threading switching.
  if (MessageLoop::current() != loop_) {
    loop_->PostTask(
        FROM_HERE,
        NewRunnableMethod(this, &ConnectionToClient::Disconnect));
    return;
  }

  // If there is a channel then close it and release the reference.
  if (session_) {
    session_->Close(NewRunnableMethod(this, &ConnectionToClient::OnClosed));
    session_ = NULL;
  }
}

void ConnectionToClient::UpdateSequenceNumber(int64 sequence_number) {
  handler_->OnSequenceNumberUpdated(this, sequence_number);
}

VideoStub* ConnectionToClient::video_stub() {
  return video_writer_.get();
}

// Return pointer to ClientStub.
ClientStub* ConnectionToClient::client_stub() {
  return client_stub_.get();
}

void ConnectionToClient::set_host_stub(protocol::HostStub* host_stub) {
  host_stub_ = host_stub;
}

void ConnectionToClient::set_input_stub(protocol::InputStub* input_stub) {
  input_stub_ = input_stub;
}

void ConnectionToClient::OnSessionStateChange(protocol::Session::State state) {
  if (state == protocol::Session::CONNECTED) {
    client_stub_.reset(new ClientControlSender(session_->control_channel()));
    video_writer_.reset(VideoWriter::Create(session_->config()));
    video_writer_->Init(session_);

    dispatcher_.reset(new HostMessageDispatcher());
    dispatcher_->Initialize(this, host_stub_, input_stub_);
  }

  // This method can be called from main thread so perform threading switching.
  if (MessageLoop::current() != loop_) {
    loop_->PostTask(
        FROM_HERE,
        NewRunnableMethod(this, &ConnectionToClient::StateChangeTask, state));
  } else {
    StateChangeTask(state);
  }
}

void ConnectionToClient::StateChangeTask(protocol::Session::State state) {
  DCHECK_EQ(loop_, MessageLoop::current());

  DCHECK(handler_);
  switch(state) {
    case protocol::Session::CONNECTING:
      break;
    // Don't care about this message.
    case protocol::Session::CONNECTED:
      handler_->OnConnectionOpened(this);
      break;
    case protocol::Session::CLOSED:
      handler_->OnConnectionClosed(this);
      break;
    case protocol::Session::FAILED:
      handler_->OnConnectionFailed(this);
      break;
    default:
      // We shouldn't receive other states.
      NOTREACHED();
  }
}

// OnClosed() is used as a callback for protocol::Session::Close().
void ConnectionToClient::OnClosed() {
}

}  // namespace protocol
}  // namespace remoting
