// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_UDP_UDP_SOCKET_WIN_H_
#define NET_UDP_UDP_SOCKET_WIN_H_

#include <winsock2.h>

#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/threading/non_thread_safe.h"
#include "base/win/object_watcher.h"
#include "net/base/completion_callback.h"
#include "net/base/net_export.h"
#include "net/base/rand_callback.h"
#include "net/base/ip_endpoint.h"
#include "net/base/io_buffer.h"
#include "net/base/net_log.h"
#include "net/udp/datagram_socket.h"

namespace net {

class NET_EXPORT UDPSocketWin : NON_EXPORTED_BASE(public base::NonThreadSafe) {
 public:
  UDPSocketWin(DatagramSocket::BindType bind_type,
               const RandIntCallback& rand_int_cb,
               net::NetLog* net_log,
               const net::NetLog::Source& source);
  virtual ~UDPSocketWin();

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
  bool is_connected() const { return socket_ != INVALID_SOCKET; }

  const BoundNetLog& NetLog() const { return net_log_; }

  // Sets corresponding flags in |socket_options_| to allow the socket
  // to share the local address to which socket will be bound with
  // other processes. Should be called before Bind().
  void AllowAddressReuse();

  // Sets corresponding flags in |socket_options_| to allow sending
  // and receiving packets sent to and from broadcast
  // addresses. Should be called before Bind().
  void AllowBroadcast();

 private:
  enum SocketOptions {
    SOCKET_OPTION_REUSE_ADDRESS = 1 << 0,
    SOCKET_OPTION_BROADCAST     = 1 << 1
  };

  class ReadDelegate : public base::win::ObjectWatcher::Delegate {
   public:
    explicit ReadDelegate(UDPSocketWin* socket) : socket_(socket) {}
    virtual ~ReadDelegate() {}

    // base::ObjectWatcher::Delegate methods:
    virtual void OnObjectSignaled(HANDLE object);

   private:
    UDPSocketWin* const socket_;
  };

  class WriteDelegate : public base::win::ObjectWatcher::Delegate {
   public:
    explicit WriteDelegate(UDPSocketWin* socket) : socket_(socket) {}
    virtual ~WriteDelegate() {}

    // base::ObjectWatcher::Delegate methods:
    virtual void OnObjectSignaled(HANDLE object);

   private:
    UDPSocketWin* const socket_;
  };

  void DoReadCallback(int rv);
  void DoWriteCallback(int rv);
  void DidCompleteRead();
  void DidCompleteWrite();

  // Handles stats and logging. |result| is the number of bytes transferred, on
  // success, or the net error code on failure. LogRead retrieves the address
  // from |recv_addr_storage_|, while LogWrite takes it as an optional argument.
  void LogRead(int result, const char* bytes) const;
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

  // Applies |socket_options_| to |socket_|. Should be called before
  // Bind().
  int SetSocketOptions();
  int DoBind(const IPEndPoint& address);
  int RandomBind(const IPEndPoint& address);

  // Attempts to convert the data in |recv_addr_storage_| and |recv_addr_len_|
  // to an IPEndPoint and writes it to |address|. Returns true on success.
  bool ReceiveAddressToIPEndpoint(IPEndPoint* address) const;

  SOCKET socket_;

  // Bitwise-or'd combination of SocketOptions. Specifies set of
  // options that should be applied to |socket_| before bind.
  int socket_options_;

  // How to do source port binding, used only when UDPSocket is part of
  // UDPClientSocket, since UDPServerSocket provides Bind.
  DatagramSocket::BindType bind_type_;

  // PRNG function for generating port numbers.
  RandIntCallback rand_int_cb_;

  // These are mutable since they're just cached copies to make
  // GetPeerAddress/GetLocalAddress smarter.
  mutable scoped_ptr<IPEndPoint> local_address_;
  mutable scoped_ptr<IPEndPoint> remote_address_;

  // The socket's win wrappers
  ReadDelegate read_delegate_;
  WriteDelegate write_delegate_;

  // Watchers to watch for events from Read() and Write().
  base::win::ObjectWatcher read_watcher_;
  base::win::ObjectWatcher write_watcher_;

  // OVERLAPPED for pending read and write operations.
  OVERLAPPED read_overlapped_;
  OVERLAPPED write_overlapped_;

  // The buffer used by InternalRead() to retry Read requests
  scoped_refptr<IOBuffer> read_iobuffer_;
  struct sockaddr_storage recv_addr_storage_;
  socklen_t recv_addr_len_;
  IPEndPoint* recv_from_address_;

  // Cached copy of the current address we're sending to, if any.  Used for
  // logging.
  scoped_ptr<IPEndPoint> send_to_address_;

  // The buffer used by InternalWrite() to retry Write requests
  scoped_refptr<IOBuffer> write_iobuffer_;

  // External callback; called when read is complete.
  CompletionCallback read_callback_;

  // External callback; called when write is complete.
  CompletionCallback write_callback_;

  BoundNetLog net_log_;

  DISALLOW_COPY_AND_ASSIGN(UDPSocketWin);
};

}  // namespace net

#endif  // NET_UDP_UDP_SOCKET_WIN_H_
