// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_UDP_UDP_SOCKET_LIBEVENT_H_
#define NET_UDP_UDP_SOCKET_LIBEVENT_H_
#pragma once

#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "base/threading/non_thread_safe.h"
#include "net/base/address_list_net_log_param.h"
#include "net/base/completion_callback.h"
#include "net/base/rand_callback.h"
#include "net/base/io_buffer.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_log.h"
#include "net/udp/datagram_socket.h"

namespace net {

class UDPSocketLibevent : public base::NonThreadSafe {
 public:
  UDPSocketLibevent(DatagramSocket::BindType bind_type,
                    const RandIntCallback& rand_int_cb,
                    net::NetLog* net_log,
                    const net::NetLog::Source& source);
  virtual ~UDPSocketLibevent();

  // Connect the socket to connect with a certain |address|.
  // Returns a net error code.
  int Connect(const IPEndPoint& address);

  // Bind the address/port for this socket to |address|.  This is generally
  // only used on a server.
  // Returns a net error code.
  int Bind(const IPEndPoint& address);

  // Close the socket.
  void Close();

  // Copy the remote udp address into |address| and return a network error code.
  int GetPeerAddress(IPEndPoint* address) const;

  // Copy the local udp address into |address| and return a network error code.
  // (similar to getsockname)
  int GetLocalAddress(IPEndPoint* address) const;

  // IO:
  // Multiple outstanding read requests are not supported.
  // Full duplex mode (reading and writing at the same time) is supported

  // Read from the socket.
  // Only usable from the client-side of a UDP socket, after the socket
  // has been connected.
  int Read(IOBuffer* buf, int buf_len, const CompletionCallback& callback);

  // Write to the socket.
  // Only usable from the client-side of a UDP socket, after the socket
  // has been connected.
  int Write(IOBuffer* buf, int buf_len, const CompletionCallback& callback);

  // Read from a socket and receive sender address information.
  // |buf| is the buffer to read data into.
  // |buf_len| is the maximum amount of data to read.
  // |address| is a buffer provided by the caller for receiving the sender
  //   address information about the received data.  This buffer must be kept
  //   alive by the caller until the callback is placed.
  // |address_length| is a ptr to the length of the |address| buffer.  This
  //   is an input parameter containing the maximum size |address| can hold
  //   and also an output parameter for the size of |address| upon completion.
  // |callback| the callback on completion of the Recv.
  // Returns a net error code, or ERR_IO_PENDING if the IO is in progress.
  // If ERR_IO_PENDING is returned, the caller must keep |buf|, |address|,
  // and |address_length| alive until the callback is called.
  int RecvFrom(IOBuffer* buf,
               int buf_len,
               IPEndPoint* address,
               const CompletionCallback& callback);

  // Send to a socket with a particular destination.
  // |buf| is the buffer to send
  // |buf_len| is the number of bytes to send
  // |address| is the recipient address.
  // |address_length| is the size of the recipient address
  // |callback| is the user callback function to call on complete.
  // Returns a net error code, or ERR_IO_PENDING if the IO is in progress.
  // If ERR_IO_PENDING is returned, the caller must keep |buf| and |address|
  // alive until the callback is called.
  int SendTo(IOBuffer* buf,
             int buf_len,
             const IPEndPoint& address,
             const CompletionCallback& callback);

  // Set the receive buffer size (in bytes) for the socket.
  bool SetReceiveBufferSize(int32 size);

  // Set the send buffer size (in bytes) for the socket.
  bool SetSendBufferSize(int32 size);

  // Returns true if the socket is already connected or bound.
  bool is_connected() const { return socket_ != kInvalidSocket; }

  const BoundNetLog& NetLog() const { return net_log_; }

 private:
  static const int kInvalidSocket = -1;

  class ReadWatcher : public MessageLoopForIO::Watcher {
   public:
    explicit ReadWatcher(UDPSocketLibevent* socket) : socket_(socket) {}

    // MessageLoopForIO::Watcher methods

    virtual void OnFileCanReadWithoutBlocking(int /* fd */) OVERRIDE {
      if (!socket_->read_callback_.is_null())
        socket_->DidCompleteRead();
    }

    virtual void OnFileCanWriteWithoutBlocking(int /* fd */) OVERRIDE {}

   private:
    UDPSocketLibevent* const socket_;

    DISALLOW_COPY_AND_ASSIGN(ReadWatcher);
  };

  class WriteWatcher : public MessageLoopForIO::Watcher {
   public:
    explicit WriteWatcher(UDPSocketLibevent* socket) : socket_(socket) {}

    // MessageLoopForIO::Watcher methods

    virtual void OnFileCanReadWithoutBlocking(int /* fd */) OVERRIDE {}

    virtual void OnFileCanWriteWithoutBlocking(int /* fd */) OVERRIDE {
      if (!socket_->write_callback_.is_null())
        socket_->DidCompleteWrite();
    }

   private:
    UDPSocketLibevent* const socket_;

    DISALLOW_COPY_AND_ASSIGN(WriteWatcher);
  };

  void DoReadCallback(int rv);
  void DoWriteCallback(int rv);
  void DidCompleteRead();
  void DidCompleteWrite();

  // Handles stats and logging. |result| is the number of bytes transferred, on
  // success, or the net error code on failure. On success, LogRead takes in a
  // sockaddr and its length, which are mandatory, while LogWrite takes in an
  // optional IPEndPoint.
  void LogRead(int result, const char* bytes, socklen_t addr_len,
               const sockaddr* addr) const;
  void LogWrite(int result, const char* bytes, const IPEndPoint* address) const;

  // Returns the OS error code (or 0 on success).
  int CreateSocket(const IPEndPoint& address);

  // Same as SendTo(), except that address is passed by pointer
  // instead of by reference. It is called from Write() with |address|
  // set to NULL.
  int SendToOrWrite(IOBuffer* buf,
                    int buf_len,
                    const IPEndPoint* address,
                    const CompletionCallback& callback);

  int InternalConnect(const IPEndPoint& address);
  int InternalRecvFrom(IOBuffer* buf, int buf_len, IPEndPoint* address);
  int InternalSendTo(IOBuffer* buf, int buf_len, const IPEndPoint* address);

  int DoBind(const IPEndPoint& address);
  int RandomBind(const IPEndPoint& address);

  int socket_;

  // How to do source port binding, used only when UDPSocket is part of
  // UDPClientSocket, since UDPServerSocket provides Bind.
  DatagramSocket::BindType bind_type_;

  // PRNG function for generating port numbers.
  RandIntCallback rand_int_cb_;

  // These are mutable since they're just cached copies to make
  // GetPeerAddress/GetLocalAddress smarter.
  mutable scoped_ptr<IPEndPoint> local_address_;
  mutable scoped_ptr<IPEndPoint> remote_address_;

  // The socket's libevent wrappers
  MessageLoopForIO::FileDescriptorWatcher read_socket_watcher_;
  MessageLoopForIO::FileDescriptorWatcher write_socket_watcher_;

  // The corresponding watchers for reads and writes.
  ReadWatcher read_watcher_;
  WriteWatcher write_watcher_;

  // The buffer used by InternalRead() to retry Read requests
  scoped_refptr<IOBuffer> read_buf_;
  int read_buf_len_;
  IPEndPoint* recv_from_address_;

  // The buffer used by InternalWrite() to retry Write requests
  scoped_refptr<IOBuffer> write_buf_;
  int write_buf_len_;
  scoped_ptr<IPEndPoint> send_to_address_;

  // External callback; called when read is complete.
  CompletionCallback read_callback_;

  // External callback; called when write is complete.
  CompletionCallback write_callback_;

  BoundNetLog net_log_;

  DISALLOW_COPY_AND_ASSIGN(UDPSocketLibevent);
};

}  // namespace net

#endif  // NET_UDP_UDP_SOCKET_LIBEVENT_H_
