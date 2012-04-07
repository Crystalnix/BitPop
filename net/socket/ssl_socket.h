// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SOCKET_SSL_SOCKET_H_
#define NET_SOCKET_SSL_SOCKET_H_

#include "base/basictypes.h"
#include "base/string_piece.h"
#include "net/socket/stream_socket.h"

namespace net {

// SSLSocket interface defines method that are common between client
// and server SSL sockets.
class NET_EXPORT SSLSocket : public StreamSocket {
public:
  virtual ~SSLSocket() {}

  // Exports data derived from the SSL master-secret (see RFC 5705).
  // The call will fail with an error if the socket is not connected, or the
  // SSL implementation does not support the operation.
  virtual int ExportKeyingMaterial(const base::StringPiece& label,
                                   const base::StringPiece& context,
                                   unsigned char *out,
                                   unsigned int outlen) = 0;
};

}  // namespace net

#endif  // NET_SOCKET_SSL_SOCKET_H_
