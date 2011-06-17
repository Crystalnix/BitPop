// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef JINGLE_GLUE_STREAM_SOCKET_ADAPTER_H_
#define JINGLE_GLUE_STREAM_SOCKET_ADAPTER_H_

#include "base/memory/scoped_ptr.h"
#include "net/base/net_log.h"
#include "net/socket/client_socket.h"
#include "third_party/libjingle/source/talk/base/sigslot.h"

class MessageLoop;

namespace talk_base {
class StreamInterface;
}  // namespace talk_base

namespace jingle_glue {

// StreamSocketAdapter implements net::Socket interface on top of
// libjingle's StreamInterface. It is used by JingleChromotocolConnection
// to provide net::Socket interface for channels.
class StreamSocketAdapter : public net::ClientSocket,
                            public sigslot::has_slots<> {
 public:
  // Ownership of the stream is passed to the adapter.
  explicit StreamSocketAdapter(talk_base::StreamInterface* stream);
  virtual ~StreamSocketAdapter();

  // ClientSocket interface.
  virtual int Connect(net::CompletionCallback* callback);
  virtual void Disconnect();
  virtual bool IsConnected() const;
  virtual bool IsConnectedAndIdle() const;
  virtual int GetPeerAddress(net::AddressList* address) const;
  virtual int GetLocalAddress(net::IPEndPoint* address) const;
  virtual const net::BoundNetLog& NetLog() const;
  virtual void SetSubresourceSpeculation();
  virtual void SetOmniboxSpeculation();
  virtual bool WasEverUsed() const;
  virtual bool UsingTCPFastOpen() const;

  // Closes the stream. |error_code| specifies error code that will
  // be returned by Read() and Write() after the stream is closed.
  void Close(int error_code);

  // Socket interface.
  virtual int Read(net::IOBuffer* buffer, int buffer_size,
                   net::CompletionCallback* callback);
  virtual int Write(net::IOBuffer* buffer, int buffer_size,
                    net::CompletionCallback* callback);

  virtual bool SetReceiveBufferSize(int32 size);
  virtual bool SetSendBufferSize(int32 size);

 private:
  void OnStreamEvent(talk_base::StreamInterface* stream,
                     int events, int error);

  void DoWrite();
  void DoRead();

  int ReadStream(net::IOBuffer* buffer, int buffer_size);
  int WriteStream(net::IOBuffer* buffer, int buffer_size);

  MessageLoop* message_loop_;

  scoped_ptr<talk_base::StreamInterface> stream_;

  bool read_pending_;
  net::CompletionCallback* read_callback_;
  scoped_refptr<net::IOBuffer> read_buffer_;
  int read_buffer_size_;

  bool write_pending_;
  net::CompletionCallback* write_callback_;
  scoped_refptr<net::IOBuffer> write_buffer_;
  int write_buffer_size_;

  int closed_error_code_;

  net::BoundNetLog net_log_;

  DISALLOW_COPY_AND_ASSIGN(StreamSocketAdapter);
};

}  // namespace jingle_glue

#endif  // JINGLE_GLUE_STREAM_SOCKET_ADAPTER_H_
