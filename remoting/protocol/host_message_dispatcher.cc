// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/ref_counted.h"
#include "net/base/io_buffer.h"
#include "remoting/proto/control.pb.h"
#include "remoting/proto/event.pb.h"
#include "remoting/proto/internal.pb.h"
#include "remoting/protocol/connection_to_client.h"
#include "remoting/protocol/host_message_dispatcher.h"
#include "remoting/protocol/host_stub.h"
#include "remoting/protocol/input_stub.h"
#include "remoting/protocol/message_reader.h"
#include "remoting/protocol/session.h"

namespace remoting {
namespace protocol {

HostMessageDispatcher::HostMessageDispatcher() :
    host_stub_(NULL),
    input_stub_(NULL) {
}

HostMessageDispatcher::~HostMessageDispatcher() {
}

void HostMessageDispatcher::Initialize(
    ConnectionToClient* connection,
    HostStub* host_stub, InputStub* input_stub) {
  if (!connection || !host_stub || !input_stub ||
      !connection->session()->event_channel() ||
      !connection->session()->control_channel()) {
    return;
  }

  control_message_reader_.reset(new ProtobufMessageReader<ControlMessage>());
  event_message_reader_.reset(new ProtobufMessageReader<EventMessage>());
  connection_ = connection;
  host_stub_ = host_stub;
  input_stub_ = input_stub;

  // Initialize the readers on the sockets provided by channels.
  event_message_reader_->Init(
      connection->session()->event_channel(),
      NewCallback(this, &HostMessageDispatcher::OnEventMessageReceived));
  control_message_reader_->Init(
      connection->session()->control_channel(),
      NewCallback(this, &HostMessageDispatcher::OnControlMessageReceived));
}

void HostMessageDispatcher::OnControlMessageReceived(
    ControlMessage* message, Task* done_task) {
  // TODO(sergeyu): Add message validation.
  if (message->has_begin_session_request()) {
    host_stub_->BeginSessionRequest(
        &message->begin_session_request().credentials(), done_task);
    return;
  }
  if (message->has_suggest_resolution()) {
    host_stub_->SuggestResolution(&message->suggest_resolution(), done_task);
    return;
  }
  LOG(WARNING) << "Invalid control message received.";
  done_task->Run();
  delete done_task;
}

void HostMessageDispatcher::OnEventMessageReceived(
    EventMessage* message, Task* done_task) {
  // TODO(sergeyu): Add message validation.
  connection_->UpdateSequenceNumber(message->sequence_number());
  if (message->has_key_event()) {
    input_stub_->InjectKeyEvent(&message->key_event(), done_task);
    return;
  }
  if (message->has_mouse_event()) {
    input_stub_->InjectMouseEvent(&message->mouse_event(), done_task);
    return;
  }
  LOG(WARNING) << "Invalid event message received.";
  done_task->Run();
  delete done_task;
}

}  // namespace protocol
}  // namespace remoting
