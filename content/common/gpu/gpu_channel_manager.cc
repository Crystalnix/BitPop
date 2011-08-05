// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/gpu/gpu_channel_manager.h"

#include <string>
#include <vector>

#include "app/win/scoped_com_initializer.h"
#include "base/command_line.h"
#include "base/threading/worker_pool.h"
#include "build/build_config.h"
#include "content/common/child_process.h"
#include "content/common/gpu/gpu_messages.h"
#include "ipc/ipc_channel_handle.h"
#include "ui/gfx/gl/gl_context.h"
#include "ui/gfx/gl/gl_implementation.h"

GpuChannelManager::GpuChannelManager(IPC::Message::Sender* browser_channel,
                                     GpuWatchdog* watchdog,
                                     base::MessageLoopProxy* io_message_loop,
                                     base::WaitableEvent* shutdown_event)
    : ALLOW_THIS_IN_INITIALIZER_LIST(method_factory_(this)),
      io_message_loop_(io_message_loop),
      shutdown_event_(shutdown_event),
      browser_channel_(browser_channel),
      watchdog_(watchdog) {
  DCHECK(browser_channel);
  DCHECK(io_message_loop);
  DCHECK(shutdown_event);
}

GpuChannelManager::~GpuChannelManager() {
  gpu_channels_.clear();
}

void GpuChannelManager::RemoveChannel(int renderer_id) {
  gpu_channels_.erase(renderer_id);
}

bool GpuChannelManager::OnMessageReceived(const IPC::Message& msg) {
  bool msg_is_ok = true;
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP_EX(GpuChannelManager, msg, msg_is_ok)
    IPC_MESSAGE_HANDLER(GpuMsg_EstablishChannel, OnEstablishChannel)
    IPC_MESSAGE_HANDLER(GpuMsg_CloseChannel, OnCloseChannel)
    IPC_MESSAGE_HANDLER(GpuMsg_CreateViewCommandBuffer,
                        OnCreateViewCommandBuffer)
    IPC_MESSAGE_HANDLER(GpuMsg_Synchronize, OnSynchronize)
    IPC_MESSAGE_HANDLER(GpuMsg_VisibilityChanged, OnVisibilityChanged)
#if defined(TOOLKIT_USES_GTK) && !defined(TOUCH_UI) || defined(OS_WIN)
    IPC_MESSAGE_HANDLER(GpuMsg_ResizeViewACK, OnResizeViewACK);
#elif defined(OS_MACOSX)
    IPC_MESSAGE_HANDLER(GpuMsg_AcceleratedSurfaceBuffersSwappedACK,
                        OnAcceleratedSurfaceBuffersSwappedACK)
    IPC_MESSAGE_HANDLER(GpuMsg_DestroyCommandBuffer,
                        OnDestroyCommandBuffer)
#endif
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP_EX()
  return handled;
}

bool GpuChannelManager::Send(IPC::Message* msg) {
  return browser_channel_->Send(msg);
}

void GpuChannelManager::OnEstablishChannel(int renderer_id) {
  scoped_refptr<GpuChannel> channel;
  IPC::ChannelHandle channel_handle;
  GPUInfo gpu_info;

  GpuChannelMap::const_iterator iter = gpu_channels_.find(renderer_id);
  if (iter == gpu_channels_.end())
    channel = new GpuChannel(this, watchdog_, renderer_id);
  else
    channel = iter->second;

  DCHECK(channel != NULL);

  if (channel->Init(io_message_loop_, shutdown_event_))
    gpu_channels_[renderer_id] = channel;
  else
    channel = NULL;

  if (channel.get()) {
    channel_handle.name = channel->GetChannelName();
#if defined(OS_POSIX)
    // On POSIX, pass the renderer-side FD. Also mark it as auto-close so
    // that it gets closed after it has been sent.
    int renderer_fd = channel->GetRendererFileDescriptor();
    channel_handle.socket = base::FileDescriptor(dup(renderer_fd), true);
#endif
  }

  Send(new GpuHostMsg_ChannelEstablished(channel_handle));
}

void GpuChannelManager::OnCloseChannel(
    const IPC::ChannelHandle& channel_handle) {
  for (GpuChannelMap::iterator iter = gpu_channels_.begin();
       iter != gpu_channels_.end(); ++iter) {
    if (iter->second->GetChannelName() == channel_handle.name) {
      gpu_channels_.erase(iter);
      return;
    }
  }
}

void GpuChannelManager::OnSynchronize() {
  Send(new GpuHostMsg_SynchronizeReply());
}

void GpuChannelManager::OnVisibilityChanged(
    int32 render_view_id, int32 renderer_id, bool visible) {
  // TODO(amarinichev): this will be used for context eviction
}

void GpuChannelManager::OnCreateViewCommandBuffer(
    gfx::PluginWindowHandle window,
    int32 render_view_id,
    int32 renderer_id,
    const GPUCreateCommandBufferConfig& init_params) {
  int32 route_id = MSG_ROUTING_NONE;

  GpuChannelMap::const_iterator iter = gpu_channels_.find(renderer_id);
  if (iter != gpu_channels_.end()) {
    iter->second->CreateViewCommandBuffer(
        window, render_view_id, init_params, &route_id);
  }

  Send(new GpuHostMsg_CommandBufferCreated(route_id));
}

void GpuChannelManager::OnResizeViewACK(int32 renderer_id,
                                        int32 command_buffer_route_id) {
  GpuChannelMap::const_iterator iter = gpu_channels_.find(renderer_id);
  if (iter == gpu_channels_.end())
    return;
  scoped_refptr<GpuChannel> channel = iter->second;

  channel->ViewResized(command_buffer_route_id);
}

#if defined(OS_MACOSX)
void GpuChannelManager::OnAcceleratedSurfaceBuffersSwappedACK(
    int renderer_id, int32 route_id, uint64 swap_buffers_count) {
  GpuChannelMap::const_iterator iter = gpu_channels_.find(renderer_id);
  if (iter == gpu_channels_.end())
    return;
  scoped_refptr<GpuChannel> channel = iter->second;
  channel->AcceleratedSurfaceBuffersSwapped(route_id, swap_buffers_count);
}

void GpuChannelManager::OnDestroyCommandBuffer(
    int renderer_id, int32 renderer_view_id) {
  GpuChannelMap::const_iterator iter = gpu_channels_.find(renderer_id);
  if (iter == gpu_channels_.end())
    return;
  scoped_refptr<GpuChannel> channel = iter->second;
  channel->DestroyCommandBufferByViewId(renderer_view_id);
}
#endif

void GpuChannelManager::LoseAllContexts() {
  MessageLoop::current()->PostTask(
      FROM_HERE, method_factory_.NewRunnableMethod(
          &GpuChannelManager::OnLoseAllContexts));
}

void GpuChannelManager::OnLoseAllContexts() {
  gpu_channels_.clear();
}
