// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/bind.h"
#include "base/debug/trace_event.h"
#include "base/location.h"
#include "base/message_loop_proxy.h"
#include "content/common/view_messages.h"
#include "content/renderer/gpu/input_event_filter.h"

using WebKit::WebInputEvent;

InputEventFilter::InputEventFilter(IPC::Listener* main_listener,
                                   base::MessageLoopProxy* target_loop,
                                   const Handler& handler)
    : main_loop_(base::MessageLoopProxy::current()),
      main_listener_(main_listener),
      sender_(NULL),
      target_loop_(target_loop),
      handler_(handler) {
  DCHECK(target_loop_);
  DCHECK(!handler_.is_null());
}

void InputEventFilter::AddRoute(int routing_id) {
  base::AutoLock locked(routes_lock_);
  routes_.insert(routing_id);
}

void InputEventFilter::RemoveRoute(int routing_id) {
  base::AutoLock locked(routes_lock_);
  routes_.erase(routing_id);
}

void InputEventFilter::DidHandleInputEvent() {
  DCHECK(target_loop_->BelongsToCurrentThread());

  bool processed = true;
  SendACK(messages_.front(), processed);
  messages_.pop();
}

void InputEventFilter::DidNotHandleInputEvent(bool send_to_widget) {
  DCHECK(target_loop_->BelongsToCurrentThread());

  if (send_to_widget) {
    // Forward to the renderer thread, and dispatch the message there.
    TRACE_EVENT0("InputEventFilter::DidNotHandleInputEvent",
                 "ForwardToRenderThread");
    main_loop_->PostTask(
        FROM_HERE,
        base::Bind(&InputEventFilter::ForwardToMainListener,
                   this, messages_.front()));
  } else {
    TRACE_EVENT0("InputEventFilter::DidNotHandleInputEvent", "LeaveUnhandled");
    bool processed = false;
    SendACK(messages_.front(), processed);
  }
  messages_.pop();
}

void InputEventFilter::OnFilterAdded(IPC::Channel* channel) {
  io_loop_ = base::MessageLoopProxy::current();
  sender_ = channel;
}

void InputEventFilter::OnFilterRemoved() {
  sender_ = NULL;
}

void InputEventFilter::OnChannelClosing() {
  sender_ = NULL;
}

bool InputEventFilter::OnMessageReceived(const IPC::Message& message) {
  if (message.type() != ViewMsg_HandleInputEvent::ID)
    return false;

  {
    base::AutoLock locked(routes_lock_);
    if (routes_.find(message.routing_id()) == routes_.end())
      return false;
  }

  const WebInputEvent* event = CrackMessage(message);
  if (event->type == WebInputEvent::Undefined)
    return false;

  target_loop_->PostTask(
      FROM_HERE,
      base::Bind(&InputEventFilter::ForwardToHandler, this, message));
  return true;
}

// static
const WebInputEvent* InputEventFilter::CrackMessage(
    const IPC::Message& message) {
  DCHECK(message.type() == ViewMsg_HandleInputEvent::ID);

  PickleIterator iter(message);
  const char* data;
  int data_length;
  if (!message.ReadData(&iter, &data, &data_length))
    return NULL;

  return reinterpret_cast<const WebInputEvent*>(data);
}

InputEventFilter::~InputEventFilter() {
}

void InputEventFilter::ForwardToMainListener(const IPC::Message& message) {
  main_listener_->OnMessageReceived(message);
}

void InputEventFilter::ForwardToHandler(const IPC::Message& message) {
  DCHECK(target_loop_->BelongsToCurrentThread());

  // Save this message for later, in case we need to bounce it back up to the
  // main listener.
  //
  // TODO(darin): Change RenderWidgetHost to always require an ACK before
  // sending the next input event.  This way we can nuke this queue.
  //
  messages_.push(message);

  handler_.Run(message.routing_id(), CrackMessage(message));
}

void InputEventFilter::SendACK(const IPC::Message& message, bool processed) {
  DCHECK(target_loop_->BelongsToCurrentThread());

  io_loop_->PostTask(
      FROM_HERE,
      base::Bind(&InputEventFilter::SendACKOnIOThread, this,
                 message.routing_id(), CrackMessage(message)->type, processed));
}

void InputEventFilter::SendACKOnIOThread(int routing_id,
                                         WebInputEvent::Type event_type,
                                         bool processed) {
  DCHECK(io_loop_->BelongsToCurrentThread());

  if (!sender_)
    return;  // Filter was removed.

  sender_->Send(
      new ViewHostMsg_HandleInputEvent_ACK(routing_id, event_type, processed));
}
