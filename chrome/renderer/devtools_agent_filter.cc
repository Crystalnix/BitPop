// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/devtools_agent_filter.h"

#include "base/message_loop.h"
#include "chrome/common/render_messages.h"
#include "chrome/renderer/devtools_agent.h"
#include "content/common/devtools_messages.h"
#include "content/renderer/plugin_channel_host.h"
#include "content/renderer/render_view.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebDevToolsAgent.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebString.h"

using WebKit::WebDevToolsAgent;
using WebKit::WebString;

namespace {

class MessageImpl : public WebDevToolsAgent::MessageDescriptor {
 public:
  MessageImpl(const std::string& message, int host_id)
      : msg(message),
        host_id(host_id) {}
  virtual ~MessageImpl() {}
  virtual WebDevToolsAgent* agent() {
    DevToolsAgent* agent = DevToolsAgent::FromHostId(host_id);
    if (!agent)
      return 0;
    return agent->GetWebAgent();
  }
  virtual WebString message() { return WebString::fromUTF8(msg); }
 private:
  std::string msg;
  int host_id;
};

}

// static
void DevToolsAgentFilter::DispatchMessageLoop() {
  MessageLoop* current = MessageLoop::current();
  bool old_state = current->NestableTasksAllowed();
  current->SetNestableTasksAllowed(true);
  current->RunAllPending();
  current->SetNestableTasksAllowed(old_state);
}

// static
IPC::Channel* DevToolsAgentFilter::channel_ = NULL;
// static
int DevToolsAgentFilter::current_routing_id_ = 0;

DevToolsAgentFilter::DevToolsAgentFilter()
    : message_handled_(false),
      render_thread_loop_(MessageLoop::current()) {
  WebDevToolsAgent::setMessageLoopDispatchHandler(
      &DevToolsAgentFilter::DispatchMessageLoop);
}

DevToolsAgentFilter::~DevToolsAgentFilter() {
}

bool DevToolsAgentFilter::OnMessageReceived(const IPC::Message& message) {
  // Dispatch debugger commands directly from IO.
  message_handled_ = true;
  current_routing_id_ = message.routing_id();
  IPC_BEGIN_MESSAGE_MAP(DevToolsAgentFilter, message)
    IPC_MESSAGE_HANDLER(DevToolsAgentMsg_DebuggerCommand, OnDebuggerCommand)
    IPC_MESSAGE_HANDLER(DevToolsAgentMsg_DispatchOnInspectorBackend,
                        OnDispatchOnInspectorBackend)
    IPC_MESSAGE_UNHANDLED(message_handled_ = false)
  IPC_END_MESSAGE_MAP()
  return message_handled_;
}

void DevToolsAgentFilter::OnFilterAdded(IPC::Channel* channel) {
  channel_ = channel;
}

void DevToolsAgentFilter::OnDebuggerCommand(const std::string& command) {
  WebDevToolsAgent::executeDebuggerCommand(
      WebString::fromUTF8(command), current_routing_id_);
}

void DevToolsAgentFilter::OnDispatchOnInspectorBackend(
    const std::string& message) {
  if (!WebDevToolsAgent::shouldInterruptForMessage(
          WebString::fromUTF8(message))) {
      message_handled_ = false;
      return;
  }
  WebDevToolsAgent::interruptAndDispatch(
      new MessageImpl(message, current_routing_id_));

  render_thread_loop_->PostTask(
      FROM_HERE,
      NewRunnableFunction(&WebDevToolsAgent::processPendingMessages));
}
