// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/protocol/buffered_socket_writer.h"

#include "base/bind.h"
#include "base/location.h"
#include "base/message_loop_proxy.h"
#include "base/stl_util.h"
#include "net/base/net_errors.h"

namespace remoting {
namespace protocol {

class BufferedSocketWriterBase::PendingPacket {
 public:
  PendingPacket(scoped_refptr<net::IOBufferWithSize> data,
                const base::Closure& done_task)
      : data_(data),
        done_task_(done_task) {
  }
  ~PendingPacket() {
    if (!done_task_.is_null())
      done_task_.Run();
  }

  net::IOBufferWithSize* data() {
    return data_;
  }

 private:
  scoped_refptr<net::IOBufferWithSize> data_;
  base::Closure done_task_;

  DISALLOW_COPY_AND_ASSIGN(PendingPacket);
};

BufferedSocketWriterBase::BufferedSocketWriterBase(
    base::MessageLoopProxy* message_loop)
    : buffer_size_(0),
      socket_(NULL),
      message_loop_(message_loop),
      write_pending_(false),
      closed_(false) {
}

BufferedSocketWriterBase::~BufferedSocketWriterBase() { }

void BufferedSocketWriterBase::Init(net::Socket* socket,
                                    const WriteFailedCallback& callback) {
  DCHECK(message_loop_->BelongsToCurrentThread());
  DCHECK(socket);
  socket_ = socket;
  write_failed_callback_ = callback;
}

bool BufferedSocketWriterBase::Write(
    scoped_refptr<net::IOBufferWithSize> data, const base::Closure& done_task) {
  {
    base::AutoLock auto_lock(lock_);
    queue_.push_back(new PendingPacket(data, done_task));
    buffer_size_ += data->size();
  }
  message_loop_->PostTask(
      FROM_HERE, base::Bind(&BufferedSocketWriterBase::DoWrite, this));
  return true;
}

void BufferedSocketWriterBase::DoWrite() {
  DCHECK(message_loop_->BelongsToCurrentThread());
  DCHECK(socket_);

  // Don't try to write if there is another write pending.
  if (write_pending_)
    return;

  // Don't write after Close().
  if (closed_)
    return;

  while (true) {
    net::IOBuffer* current_packet;
    int current_packet_size;
    {
      base::AutoLock auto_lock(lock_);
      GetNextPacket_Locked(&current_packet, &current_packet_size);
    }

    // Return if the queue is empty.
    if (!current_packet)
      return;

    int result = socket_->Write(
        current_packet, current_packet_size,
        base::Bind(&BufferedSocketWriterBase::OnWritten,
                   base::Unretained(this)));
    if (result >= 0) {
      base::AutoLock auto_lock(lock_);
      AdvanceBufferPosition_Locked(result);
    } else {
      if (result == net::ERR_IO_PENDING) {
        write_pending_ = true;
      } else {
        HandleError(result);
        if (!write_failed_callback_.is_null())
          write_failed_callback_.Run(result);
      }
      return;
    }
  }
}

void BufferedSocketWriterBase::OnWritten(int result) {
  DCHECK(message_loop_->BelongsToCurrentThread());
  write_pending_ = false;

  if (result < 0) {
    HandleError(result);
    if (!write_failed_callback_.is_null())
      write_failed_callback_.Run(result);
    return;
  }

  {
    base::AutoLock auto_lock(lock_);
    AdvanceBufferPosition_Locked(result);
  }

  // Schedule next write.
  message_loop_->PostTask(
      FROM_HERE, base::Bind(&BufferedSocketWriterBase::DoWrite, this));
}

void BufferedSocketWriterBase::HandleError(int result) {
  DCHECK(message_loop_->BelongsToCurrentThread());

  closed_ = true;

  base::AutoLock auto_lock(lock_);
  STLDeleteElements(&queue_);

  // Notify subclass that an error is received.
  OnError_Locked(result);
}

int BufferedSocketWriterBase::GetBufferSize() {
  base::AutoLock auto_lock(lock_);
  return buffer_size_;
}

int BufferedSocketWriterBase::GetBufferChunks() {
  base::AutoLock auto_lock(lock_);
  return queue_.size();
}

void BufferedSocketWriterBase::Close() {
  DCHECK(message_loop_->BelongsToCurrentThread());
  closed_ = true;
}

void BufferedSocketWriterBase::PopQueue() {
  // This also calls |done_task|.
  delete queue_.front();
  queue_.pop_front();
}

BufferedSocketWriter::BufferedSocketWriter(
    base::MessageLoopProxy* message_loop)
  : BufferedSocketWriterBase(message_loop) {
}

BufferedSocketWriter::~BufferedSocketWriter() {
  STLDeleteElements(&queue_);
}

void BufferedSocketWriter::GetNextPacket_Locked(
    net::IOBuffer** buffer, int* size) {
  if (!current_buf_) {
    if (queue_.empty()) {
      *buffer = NULL;
      return;  // Nothing to write.
    }
    current_buf_ = new net::DrainableIOBuffer(
        queue_.front()->data(), queue_.front()->data()->size());
  }

  *buffer = current_buf_;
  *size = current_buf_->BytesRemaining();
}

void BufferedSocketWriter::AdvanceBufferPosition_Locked(int written) {
  buffer_size_ -= written;
  current_buf_->DidConsume(written);

  if (current_buf_->BytesRemaining() == 0) {
    PopQueue();
    current_buf_ = NULL;
  }
}

void BufferedSocketWriter::OnError_Locked(int result) {
  current_buf_ = NULL;
}

BufferedDatagramWriter::BufferedDatagramWriter(
    base::MessageLoopProxy* message_loop)
    : BufferedSocketWriterBase(message_loop) {
}
BufferedDatagramWriter::~BufferedDatagramWriter() { }

void BufferedDatagramWriter::GetNextPacket_Locked(
    net::IOBuffer** buffer, int* size) {
  if (queue_.empty()) {
    *buffer = NULL;
    return;  // Nothing to write.
  }
  *buffer = queue_.front()->data();
  *size = queue_.front()->data()->size();
}

void BufferedDatagramWriter::AdvanceBufferPosition_Locked(int written) {
  DCHECK_EQ(written, queue_.front()->data()->size());
  buffer_size_ -= queue_.front()->data()->size();
  PopQueue();
}

void BufferedDatagramWriter::OnError_Locked(int result) {
  // Nothing to do here.
}

}  // namespace protocol
}  // namespace remoting
