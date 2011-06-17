// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_CLIENT_CLIENT_CONTEXT_H_
#define REMOTING_CLIENT_CLIENT_CONTEXT_H_

#include <string>

#include "base/threading/thread.h"
#include "remoting/jingle_glue/jingle_thread.h"

namespace remoting {

// A class that manages threads and running context for the chromoting client
// process.
class ClientContext {
 public:
  ClientContext();
  virtual ~ClientContext();

  void Start();
  void Stop();

  JingleThread* jingle_thread();
  MessageLoop* jingle_message_loop();

  MessageLoop* main_message_loop();
  MessageLoop* decode_message_loop();

 private:
  // A thread that handles Jingle network operations (used in
  // JingleHostConnection).
  JingleThread jingle_thread_;

  // A thread that handles capture rate control and sending data to the
  // HostConnection.
  base::Thread main_thread_;

  // A thread that handles all decode operations.
  base::Thread decode_thread_;

  DISALLOW_COPY_AND_ASSIGN(ClientContext);
};

}  // namespace remoting

#endif  // REMOTING_CLIENT_CLIENT_CONTEXT_H_
