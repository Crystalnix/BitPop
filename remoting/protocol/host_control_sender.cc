// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This stub is thread safe because of the use of BufferedSocketWriter.
// BufferedSocketWriter buffers messages and send them on them right thread.

#include "remoting/protocol/host_control_sender.h"

#include "base/task.h"
#include "remoting/protocol/buffered_socket_writer.h"
#include "remoting/proto/control.pb.h"
#include "remoting/proto/internal.pb.h"
#include "remoting/protocol/util.h"

namespace remoting {
namespace protocol {

HostControlSender::HostControlSender(net::Socket* socket)
    : buffered_writer_(new BufferedSocketWriter()) {
  buffered_writer_->Init(socket, NULL);
}

HostControlSender::~HostControlSender() {
}

void HostControlSender::SuggestResolution(
    const SuggestResolutionRequest* msg, Task* done) {
  protocol::ControlMessage message;
  message.mutable_suggest_resolution()->CopyFrom(*msg);
  buffered_writer_->Write(SerializeAndFrameMessage(message), done);
}

void HostControlSender::BeginSessionRequest(const LocalLoginCredentials* msg,
                                            Task* done) {
  protocol::ControlMessage message;
  message.mutable_begin_session_request()->mutable_credentials()->CopyFrom(
      *msg);
  buffered_writer_->Write(SerializeAndFrameMessage(message), done);
}

}  // namespace protocol
}  // namespace remoting
