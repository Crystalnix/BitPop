// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/protocol/message_reader.h"

#include "base/bind.h"
#include "base/callback.h"
#include "base/location.h"
#include "base/thread_task_runner_handle.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/socket/socket.h"
#include "remoting/base/compound_buffer.h"
#include "remoting/proto/internal.pb.h"

namespace remoting {
namespace protocol {

static const int kReadBufferSize = 4096;

MessageReader::MessageReader()
    : socket_(NULL),
      read_pending_(false),
      pending_messages_(0),
      closed_(false) {
}

void MessageReader::Init(net::Socket* socket,
                         const MessageReceivedCallback& callback) {
  message_received_callback_ = callback;
  DCHECK(socket);
  socket_ = socket;
  DoRead();
}

MessageReader::~MessageReader() {
  CHECK_EQ(pending_messages_, 0);
}

void MessageReader::DoRead() {
  // Don't try to read again if there is another read pending or we
  // have messages that we haven't finished processing yet.
  while (!closed_ && !read_pending_ && pending_messages_ == 0) {
    read_buffer_ = new net::IOBuffer(kReadBufferSize);
    int result = socket_->Read(
        read_buffer_, kReadBufferSize, base::Bind(&MessageReader::OnRead,
                                                  base::Unretained(this)));
    HandleReadResult(result);
  }
}

void MessageReader::OnRead(int result) {
  DCHECK(read_pending_);
  read_pending_ = false;

  if (!closed_) {
    HandleReadResult(result);
    DoRead();
  }
}

void MessageReader::HandleReadResult(int result) {
  if (closed_)
    return;

  if (result > 0) {
    OnDataReceived(read_buffer_, result);
  } else if (result == net::ERR_IO_PENDING) {
    read_pending_ = true;
  } else {
    if (result != net::ERR_CONNECTION_CLOSED) {
      LOG(ERROR) << "Read() returned error " << result;
    }
    // Stop reading after any error.
    closed_ = true;
  }
}

void MessageReader::OnDataReceived(net::IOBuffer* data, int data_size) {
  message_decoder_.AddData(data, data_size);

  // Get list of all new messages first, and then call the callback
  // for all of them.
  std::vector<CompoundBuffer*> new_messages;
  while (true) {
    CompoundBuffer* buffer = message_decoder_.GetNextMessage();
    if (!buffer)
      break;
    new_messages.push_back(buffer);
  }

  pending_messages_ += new_messages.size();

  for (std::vector<CompoundBuffer*>::iterator it = new_messages.begin();
       it != new_messages.end(); ++it) {
    message_received_callback_.Run(
        scoped_ptr<CompoundBuffer>(*it),
        base::Bind(&MessageReader::OnMessageDone, this,
                   base::ThreadTaskRunnerHandle::Get()));
  }
}

void MessageReader::OnMessageDone(
    scoped_refptr<base::SingleThreadTaskRunner> task_runner) {
  if (task_runner->BelongsToCurrentThread()) {
    ProcessDoneEvent();
  } else {
    task_runner->PostTask(
        FROM_HERE, base::Bind(&MessageReader::ProcessDoneEvent, this));
  }
}

void MessageReader::ProcessDoneEvent() {
  pending_messages_--;
  DCHECK_GE(pending_messages_, 0);

  if (!read_pending_)
    DoRead();  // Start next read if neccessary.
}

}  // namespace protocol
}  // namespace remoting
