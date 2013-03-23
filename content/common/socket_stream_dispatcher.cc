// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/socket_stream_dispatcher.h"

#include <vector>

#include "base/bind.h"
#include "base/id_map.h"
#include "base/lazy_instance.h"
#include "base/memory/ref_counted.h"
#include "base/message_loop.h"
#include "content/common/child_thread.h"
#include "content/common/socket_stream.h"
#include "content/common/socket_stream_handle_data.h"
#include "content/common/socket_stream_messages.h"
#include "googleurl/src/gurl.h"
#include "webkit/glue/websocketstreamhandle_bridge.h"
#include "webkit/glue/websocketstreamhandle_delegate.h"

namespace content {

// IPCWebSocketStreamHandleBridge is owned by each SocketStreamHandle.
// It communicates with the main browser process via SocketStreamDispatcher.
class IPCWebSocketStreamHandleBridge
    : public webkit_glue::WebSocketStreamHandleBridge {
 public:
  IPCWebSocketStreamHandleBridge(
      ChildThread* child_thread,
      WebKit::WebSocketStreamHandle* handle,
      webkit_glue::WebSocketStreamHandleDelegate* delegate)
      : socket_id_(content::kNoSocketId),
        child_thread_(child_thread),
        handle_(handle),
        delegate_(delegate) {}

  // Returns the handle having given id or NULL if there is no such handle.
  static IPCWebSocketStreamHandleBridge* FromSocketId(int id);

  // webkit_glue::WebSocketStreamHandleBridge methods.
  virtual void Connect(const GURL& url);
  virtual bool Send(const std::vector<char>& data);
  virtual void Close();

  // Called by SocketStreamDispatcher.
  void OnConnected(int max_amount_send_allowed);
  void OnSentData(int amount_sent);
  void OnReceivedData(const std::vector<char>& data);
  void OnClosed();

 private:
  virtual ~IPCWebSocketStreamHandleBridge();

  void DoConnect(const GURL& url);
  void DoClose();
  int socket_id_;

  ChildThread* child_thread_;
  WebKit::WebSocketStreamHandle* handle_;
  webkit_glue::WebSocketStreamHandleDelegate* delegate_;

  static base::LazyInstance<IDMap<IPCWebSocketStreamHandleBridge> >::Leaky
      all_bridges;
};

// static
base::LazyInstance<IDMap<IPCWebSocketStreamHandleBridge> >::Leaky
    IPCWebSocketStreamHandleBridge::all_bridges = LAZY_INSTANCE_INITIALIZER;

/* static */
IPCWebSocketStreamHandleBridge* IPCWebSocketStreamHandleBridge::FromSocketId(
    int id) {
  return all_bridges.Get().Lookup(id);
}

IPCWebSocketStreamHandleBridge::~IPCWebSocketStreamHandleBridge() {
  DVLOG(1) << "IPCWebSocketStreamHandleBridge destructor socket_id="
           << socket_id_;
  if (socket_id_ != content::kNoSocketId) {
    child_thread_->Send(new SocketStreamHostMsg_Close(socket_id_));
    socket_id_ = content::kNoSocketId;
  }
}

void IPCWebSocketStreamHandleBridge::Connect(const GURL& url) {
  DCHECK(child_thread_);
  DVLOG(1) << "Connect url=" << url;
  child_thread_->message_loop()->PostTask(
      FROM_HERE,
      base::Bind(&IPCWebSocketStreamHandleBridge::DoConnect, this, url));
}

bool IPCWebSocketStreamHandleBridge::Send(
    const std::vector<char>& data) {
  DVLOG(1) << "Send data.size=" << data.size();
  if (child_thread_->Send(
      new SocketStreamHostMsg_SendData(socket_id_, data))) {
    if (delegate_)
      delegate_->WillSendData(handle_, &data[0], data.size());
    return true;
  }
  return false;
}

void IPCWebSocketStreamHandleBridge::Close() {
  DVLOG(1) << "Close socket_id" << socket_id_;
  AddRef();  // Released in DoClose().
  child_thread_->message_loop()->PostTask(
      FROM_HERE,
      base::Bind(&IPCWebSocketStreamHandleBridge::DoClose, this));
}

void IPCWebSocketStreamHandleBridge::OnConnected(int max_pending_send_allowed) {
  DVLOG(1) << "IPCWebSocketStreamHandleBridge::OnConnected socket_id="
           << socket_id_;
  if (delegate_)
    delegate_->DidOpenStream(handle_, max_pending_send_allowed);
}

void IPCWebSocketStreamHandleBridge::OnSentData(int amount_sent) {
  if (delegate_)
    delegate_->DidSendData(handle_, amount_sent);
}

void IPCWebSocketStreamHandleBridge::OnReceivedData(
    const std::vector<char>& data) {
  if (delegate_)
    delegate_->DidReceiveData(handle_, &data[0], data.size());
}

void IPCWebSocketStreamHandleBridge::OnClosed() {
  DVLOG(1) << "IPCWebSocketStreamHandleBridge::OnClosed";
  if (socket_id_ != content::kNoSocketId) {
    all_bridges.Get().Remove(socket_id_);
    socket_id_ = content::kNoSocketId;
  }
  if (delegate_)
    delegate_->DidClose(handle_);
  delegate_ = NULL;
  Release();
}

void IPCWebSocketStreamHandleBridge::DoConnect(const GURL& url) {
  DCHECK(child_thread_);
  DCHECK_EQ(socket_id_, content::kNoSocketId);
  if (delegate_)
    delegate_->WillOpenStream(handle_, url);

  socket_id_ = all_bridges.Get().Add(this);
  DCHECK_NE(socket_id_, content::kNoSocketId);
  int render_view_id = MSG_ROUTING_NONE;
  const SocketStreamHandleData* data =
      SocketStreamHandleData::ForHandle(handle_);
  if (data)
    render_view_id = data->render_view_id();
  AddRef();  // Released in OnClosed().
  if (child_thread_->Send(
      new SocketStreamHostMsg_Connect(render_view_id, url, socket_id_))) {
    DVLOG(1) << "Connect socket_id=" << socket_id_;
    // TODO(ukai): timeout to OnConnected.
  } else {
    DLOG(ERROR) << "IPC SocketStream_Connect failed.";
    OnClosed();
  }
}

void IPCWebSocketStreamHandleBridge::DoClose() {
  child_thread_->Send(new SocketStreamHostMsg_Close(socket_id_));
  Release();
}

SocketStreamDispatcher::SocketStreamDispatcher() {
}

/* static */
webkit_glue::WebSocketStreamHandleBridge*
SocketStreamDispatcher::CreateBridge(
    WebKit::WebSocketStreamHandle* handle,
    webkit_glue::WebSocketStreamHandleDelegate* delegate) {
  return new IPCWebSocketStreamHandleBridge(
      ChildThread::current(), handle, delegate);
}

bool SocketStreamDispatcher::OnMessageReceived(const IPC::Message& msg) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(SocketStreamDispatcher, msg)
    IPC_MESSAGE_HANDLER(SocketStreamMsg_Connected, OnConnected)
    IPC_MESSAGE_HANDLER(SocketStreamMsg_SentData, OnSentData)
    IPC_MESSAGE_HANDLER(SocketStreamMsg_ReceivedData, OnReceivedData)
    IPC_MESSAGE_HANDLER(SocketStreamMsg_Closed, OnClosed)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void SocketStreamDispatcher::OnConnected(int socket_id,
                                         int max_pending_send_allowed) {
  DVLOG(1) << "SocketStreamDispatcher::OnConnected socket_id=" << socket_id
           << " max_pending_send_allowed=" << max_pending_send_allowed;
  IPCWebSocketStreamHandleBridge* bridge =
      IPCWebSocketStreamHandleBridge::FromSocketId(socket_id);
  if (bridge)
    bridge->OnConnected(max_pending_send_allowed);
  else
    DLOG(ERROR) << "No SocketStreamHandleBridge for socket_id=" << socket_id;
}

void SocketStreamDispatcher::OnSentData(int socket_id, int amount_sent) {
  IPCWebSocketStreamHandleBridge* bridge =
      IPCWebSocketStreamHandleBridge::FromSocketId(socket_id);
  if (bridge)
    bridge->OnSentData(amount_sent);
  else
    DLOG(ERROR) << "No SocketStreamHandleBridge for socket_id=" << socket_id;
}

void SocketStreamDispatcher::OnReceivedData(
    int socket_id, const std::vector<char>& data) {
  IPCWebSocketStreamHandleBridge* bridge =
      IPCWebSocketStreamHandleBridge::FromSocketId(socket_id);
  if (bridge)
    bridge->OnReceivedData(data);
  else
    DLOG(ERROR) << "No SocketStreamHandleBridge for socket_id=" << socket_id;
}

void SocketStreamDispatcher::OnClosed(int socket_id) {
  IPCWebSocketStreamHandleBridge* bridge =
      IPCWebSocketStreamHandleBridge::FromSocketId(socket_id);
  if (bridge)
    bridge->OnClosed();
  else
    DLOG(ERROR) << "No SocketStreamHandleBridge for socket_id=" << socket_id;
}

}  // namespace content
