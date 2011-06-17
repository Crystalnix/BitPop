// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "jingle/glue/stream_socket_adapter.h"

#include "base/logging.h"
#include "base/message_loop.h"
#include "net/base/address_list.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "third_party/libjingle/source/talk/base/stream.h"

namespace jingle_glue {

StreamSocketAdapter::StreamSocketAdapter(talk_base::StreamInterface* stream)
    : message_loop_(MessageLoop::current()),
      stream_(stream),
      read_pending_(false),
      write_pending_(false),
      closed_error_code_(net::OK) {
  DCHECK(stream);
  stream_->SignalEvent.connect(this, &StreamSocketAdapter::OnStreamEvent);
}

StreamSocketAdapter::~StreamSocketAdapter() {
}

int StreamSocketAdapter::Connect(net::CompletionCallback* callback) {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  return net::OK;
}

void StreamSocketAdapter::Disconnect() {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
}

bool StreamSocketAdapter::IsConnected() const {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  return true;
}

bool StreamSocketAdapter::IsConnectedAndIdle() const {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  return true;
}

int StreamSocketAdapter::GetPeerAddress(net::AddressList* address) const {
  DCHECK_EQ(MessageLoop::current(), message_loop_);

  // We actually don't know the peer address. Returning so the upper layers
  // won't complain.
  net::IPAddressNumber ip_address(4);
  *address = net::AddressList(ip_address, 0, false);
  return net::OK;
}

int StreamSocketAdapter::GetLocalAddress(net::IPEndPoint* address) const {
  return net::ERR_UNEXPECTED;
}

const net::BoundNetLog& StreamSocketAdapter::NetLog() const {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  return net_log_;
}

void StreamSocketAdapter::SetSubresourceSpeculation() {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
}

void StreamSocketAdapter::SetOmniboxSpeculation() {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
}

bool StreamSocketAdapter::WasEverUsed() const {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  return true;
}

bool StreamSocketAdapter::UsingTCPFastOpen() const {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  return false;
}

int StreamSocketAdapter::Read(
    net::IOBuffer* buffer, int buffer_size, net::CompletionCallback* callback) {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  DCHECK(buffer);
  CHECK(!read_pending_);

  if (!stream_.get()) {
    DCHECK(closed_error_code_ != net::OK);
    return closed_error_code_;
  }

  int result = ReadStream(buffer, buffer_size);
  if (result == net::ERR_SOCKET_NOT_CONNECTED &&
      stream_->GetState() == talk_base::SS_OPENING)
    result = net::ERR_IO_PENDING;
  if (result == net::ERR_IO_PENDING) {
    read_pending_ = true;
    read_callback_ = callback;
    read_buffer_ = buffer;
    read_buffer_size_ = buffer_size;
  }
  return result;
}

int StreamSocketAdapter::Write(
    net::IOBuffer* buffer, int buffer_size, net::CompletionCallback* callback) {
  DCHECK_EQ(MessageLoop::current(), message_loop_);
  DCHECK(buffer);
  CHECK(!write_pending_);

  if (!stream_.get()) {
    DCHECK(closed_error_code_ != net::OK);
    return closed_error_code_;
  }

  int result = WriteStream(buffer, buffer_size);
  if (result == net::ERR_SOCKET_NOT_CONNECTED &&
      stream_->GetState() == talk_base::SS_OPENING)
    result = net::ERR_IO_PENDING;

  if (result == net::ERR_IO_PENDING) {
    write_pending_ = true;
    write_callback_ = callback;
    write_buffer_ = buffer;
    write_buffer_size_ = buffer_size;
  }
  return result;
}

bool StreamSocketAdapter::SetReceiveBufferSize(int32 size) {
  NOTIMPLEMENTED();
  return false;
}

bool StreamSocketAdapter::SetSendBufferSize(int32 size) {
  NOTIMPLEMENTED();
  return false;
}

void StreamSocketAdapter::Close(int error_code) {
  DCHECK_EQ(MessageLoop::current(), message_loop_);

  if (!stream_.get())  // Already closed.
    return;

  DCHECK(error_code != net::OK);
  closed_error_code_ = error_code;
  stream_->SignalEvent.disconnect(this);
  stream_->Close();
  stream_.reset(NULL);

  if (read_pending_) {
    net::CompletionCallback* callback = read_callback_;
    read_pending_ = false;
    read_buffer_ = NULL;
    callback->Run(error_code);
  }

  if (write_pending_) {
    net::CompletionCallback* callback = write_callback_;
    write_pending_ = false;
    write_buffer_ = NULL;
    callback->Run(error_code);
  }
}

void StreamSocketAdapter::OnStreamEvent(
    talk_base::StreamInterface* stream, int events, int error) {
  DCHECK_EQ(MessageLoop::current(), message_loop_);

  if (events & talk_base::SE_WRITE)
    DoWrite();

  if (events & talk_base::SE_READ)
    DoRead();
}

void StreamSocketAdapter::DoWrite() {
  DCHECK_EQ(MessageLoop::current(), message_loop_);

  // Write if there is a pending read.
  if (write_buffer_) {
    int result = WriteStream(write_buffer_, write_buffer_size_);
    if (result != net::ERR_IO_PENDING) {
      net::CompletionCallback* callback = write_callback_;
      write_pending_ = false;
      write_buffer_ = NULL;
      callback->Run(result);
    }
  }
}

void StreamSocketAdapter::DoRead() {
  DCHECK_EQ(MessageLoop::current(), message_loop_);

  // Read if there is a pending read.
  if (read_pending_) {
    int result = ReadStream(read_buffer_, read_buffer_size_);
    if (result != net::ERR_IO_PENDING) {
      net::CompletionCallback* callback = read_callback_;\
      read_pending_ = false;
      read_buffer_ = NULL;
      callback->Run(result);
    }
  }
}

int StreamSocketAdapter::ReadStream(net::IOBuffer* buffer, int buffer_size) {
  DCHECK_EQ(MessageLoop::current(), message_loop_);

  size_t bytes_read;
  int error;
  talk_base::StreamResult result = stream_->Read(
      buffer->data(), buffer_size, &bytes_read, &error);
  switch (result) {
    case talk_base::SR_SUCCESS:
      return bytes_read;

    case talk_base::SR_BLOCK:
      return net::ERR_IO_PENDING;

    case talk_base::SR_EOS:
      return net::ERR_CONNECTION_CLOSED;

    case talk_base::SR_ERROR:
      return net::MapSystemError(error);
  }
  NOTREACHED();
  return net::ERR_FAILED;
}

int StreamSocketAdapter::WriteStream(net::IOBuffer* buffer, int buffer_size) {
  DCHECK_EQ(MessageLoop::current(), message_loop_);

  size_t bytes_written;
  int error;
  talk_base::StreamResult result = stream_->Write(
      buffer->data(), buffer_size, &bytes_written, &error);
  switch (result) {
    case talk_base::SR_SUCCESS:
      return bytes_written;

    case talk_base::SR_BLOCK:
      return net::ERR_IO_PENDING;

    case talk_base::SR_EOS:
      return net::ERR_CONNECTION_CLOSED;

    case talk_base::SR_ERROR:
      return net::MapSystemError(error);
  }
  NOTREACHED();
  return net::ERR_FAILED;
}

}  // namespace jingle_glue
