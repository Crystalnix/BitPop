// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/chromedriver/net/sync_websocket_impl.h"

#include "base/bind.h"
#include "base/callback.h"
#include "base/location.h"
#include "base/single_thread_task_runner.h"
#include "base/synchronization/waitable_event.h"
#include "googleurl/src/gurl.h"
#include "net/base/net_errors.h"
#include "net/url_request/url_request_context_getter.h"

SyncWebSocketImpl::SyncWebSocketImpl(
    net::URLRequestContextGetter* context_getter)
    : core_(new Core(context_getter)) {}

SyncWebSocketImpl::~SyncWebSocketImpl() {}

bool SyncWebSocketImpl::Connect(const GURL& url) {
  return core_->Connect(url);
}

bool SyncWebSocketImpl::Send(const std::string& message) {
  return core_->Send(message);
}

bool SyncWebSocketImpl::ReceiveNextMessage(std::string* message) {
  return core_->ReceiveNextMessage(message);
}

SyncWebSocketImpl::Core::Core(net::URLRequestContextGetter* context_getter)
    : context_getter_(context_getter),
      closed_(false),
      on_update_event_(&lock_) {}

bool SyncWebSocketImpl::Core::Connect(const GURL& url) {
  bool success = false;
  base::WaitableEvent event(false, false);
  context_getter_->GetNetworkTaskRunner()->PostTask(
      FROM_HERE,
      base::Bind(&SyncWebSocketImpl::Core::ConnectOnIO,
                 this, url, &success, &event));
  event.Wait();
  return success;
}

bool SyncWebSocketImpl::Core::Send(const std::string& message) {
  bool success = false;
  base::WaitableEvent event(false, false);
  context_getter_->GetNetworkTaskRunner()->PostTask(
      FROM_HERE,
      base::Bind(&SyncWebSocketImpl::Core::SendOnIO,
                 this, message, &success, &event));
  event.Wait();
  return success;
}

bool SyncWebSocketImpl::Core::ReceiveNextMessage(std::string* message) {
  base::AutoLock lock(lock_);
  while (received_queue_.empty() && !closed_) on_update_event_.Wait();
  if (closed_)
    return false;
  *message = received_queue_.front();
  received_queue_.pop_front();
  return true;
}

void SyncWebSocketImpl::Core::OnMessageReceived(const std::string& message) {
  base::AutoLock lock(lock_);
  received_queue_.push_back(message);
  on_update_event_.Signal();
}

void SyncWebSocketImpl::Core::OnClose() {
  base::AutoLock lock(lock_);
  closed_ = true;
  on_update_event_.Signal();
}

SyncWebSocketImpl::Core::~Core() { }

void SyncWebSocketImpl::Core::ConnectOnIO(
    const GURL& url,
    bool* success,
    base::WaitableEvent* event) {
  socket_.reset(new WebSocket(context_getter_, url, this));
  socket_->Connect(base::Bind(
      &SyncWebSocketImpl::Core::OnConnectCompletedOnIO,
      this, success, event));
}

void SyncWebSocketImpl::Core::OnConnectCompletedOnIO(
    bool* success,
    base::WaitableEvent* event,
    int error) {
  *success = (error == net::OK);
  event->Signal();
}

void SyncWebSocketImpl::Core::SendOnIO(
    const std::string& message,
    bool* success,
    base::WaitableEvent* event) {
  *success = socket_->Send(message);
  event->Signal();
}

void SyncWebSocketImpl::Core::OnDestruct() const {
  scoped_refptr<base::SingleThreadTaskRunner> network_task_runner =
      context_getter_->GetNetworkTaskRunner();
  if (network_task_runner->BelongsToCurrentThread())
    delete this;
  else
    network_task_runner->DeleteSoon(FROM_HERE, this);
}
