// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_DEVTOOLS_AGENT_FILTER_H_
#define CONTENT_RENDERER_DEVTOOLS_AGENT_FILTER_H_

#include <string>

#include "ipc/ipc_channel_proxy.h"

class MessageLoop;
struct DevToolsMessageData;

// DevToolsAgentFilter is registered as an IPC filter in order to be able to
// dispatch messages while on the IO thread. The reason for that is that while
// debugging, Render thread is being held by the v8 and hence no messages
// are being dispatched there. While holding the thread in a tight loop,
// v8 provides thread-safe Api for controlling debugger. In our case v8's Api
// is being used from this communication agent on the IO thread.
class DevToolsAgentFilter : public IPC::ChannelProxy::MessageFilter {
 public:
  // There is a single instance of this class instantiated by the RenderThread.
  DevToolsAgentFilter();

  static void SendRpcMessage(const DevToolsMessageData& data);

  // IPC::ChannelProxy::MessageFilter override. Called on IO thread.
  virtual bool OnMessageReceived(const IPC::Message& message) OVERRIDE;

 protected:
  virtual ~DevToolsAgentFilter();

 private:
  void OnDispatchOnInspectorBackend(const std::string& message);

  bool message_handled_;
  MessageLoop* render_thread_loop_;

  DISALLOW_COPY_AND_ASSIGN(DevToolsAgentFilter);
};

#endif  // CONTENT_RENDERER_DEVTOOLS_AGENT_FILTER_H_
