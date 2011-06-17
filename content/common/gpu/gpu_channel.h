// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_COMMON_GPU_GPU_CHANNEL_H_
#define CONTENT_COMMON_GPU_GPU_CHANNEL_H_
#pragma once

#include <set>
#include <string>
#include <vector>

#include "base/id_map.h"
#include "base/memory/scoped_ptr.h"
#include "base/process.h"
#include "build/build_config.h"
#include "content/common/gpu/gpu_command_buffer_stub.h"
#include "content/common/message_router.h"
#include "ipc/ipc_sync_channel.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/gfx/size.h"

class GpuChannelManager;
struct GPUCreateCommandBufferConfig;
class GpuWatchdog;
class MessageLoop;
class TransportTexture;
	 
namespace base {
class WaitableEvent;
}

// Encapsulates an IPC channel between the GPU process and one renderer
// process. On the renderer side there's a corresponding GpuChannelHost.
class GpuChannel : public IPC::Channel::Listener,
                   public IPC::Message::Sender,
                   public base::RefCountedThreadSafe<GpuChannel> {
 public:
  // Takes ownership of the renderer process handle.
  GpuChannel(GpuChannelManager* gpu_channel_manager,
             GpuWatchdog* watchdog,
             int renderer_id);
  virtual ~GpuChannel();

  bool Init(MessageLoop* io_message_loop, base::WaitableEvent* shutdown_event);

  // Get the GpuChannelManager that owns this channel.
  GpuChannelManager* gpu_channel_manager() const {
    return gpu_channel_manager_;
  }

  // Returns the name of the associated IPC channel.
  std::string GetChannelName();

#if defined(OS_POSIX)
  int GetRendererFileDescriptor();
#endif  // defined(OS_POSIX)

  base::ProcessHandle renderer_process() const {
    return renderer_process_;
  }

  // IPC::Channel::Listener implementation:
  virtual bool OnMessageReceived(const IPC::Message& msg);
  virtual void OnChannelError();
  virtual void OnChannelConnected(int32 peer_pid);

  // IPC::Message::Sender implementation:
  virtual bool Send(IPC::Message* msg);

  void CreateViewCommandBuffer(
      gfx::PluginWindowHandle window,
      int32 render_view_id,
      const GPUCreateCommandBufferConfig& init_params,
      int32* route_id);

  void ViewResized(int32 command_buffer_route_id);

#if defined(OS_MACOSX)
  virtual void AcceleratedSurfaceBuffersSwapped(
      int32 route_id, uint64 swap_buffers_count);
  void DestroyCommandBufferByViewId(int32 render_view_id);
#endif

  void LoseAllContexts();

  // Get the TransportTexture by ID.
  TransportTexture* GetTransportTexture(int32 route_id);

  // Destroy the TransportTexture by ID. This method is only called by
  // TransportTexture to delete and detach itself.
  void DestroyTransportTexture(int32 route_id);

  // A callback which is called after a Set/WaitLatch command is processed.
  // The bool parameter will be true for SetLatch, and false for a WaitLatch
  // that is blocked. An unblocked WaitLatch will not trigger a callback.
  void OnLatchCallback(int route_id, bool is_set_latch);

 private:
  bool OnControlMessageReceived(const IPC::Message& msg);

  int GenerateRouteID();

  // Message handlers.
  void OnInitialize(base::ProcessHandle renderer_process);
  void OnCreateOffscreenCommandBuffer(
      int32 parent_route_id,
      const gfx::Size& size,
      const GPUCreateCommandBufferConfig& init_params,
      uint32 parent_texture_id,
      int32* route_id);
  void OnDestroyCommandBuffer(int32 route_id);

  void OnCreateVideoDecoder(int32 context_route_id,
                            int32 decoder_host_id);
  void OnDestroyVideoDecoder(int32 decoder_id);
  void OnCreateTransportTexture(int32 context_route_id, int32 host_id);
 
  // The lifetime of objects of this class is managed by a GpuChannelManager.
  // The GpuChannelManager destroy all the GpuChannels that they own when they
  // are destroyed. So a raw pointer is safe.
  GpuChannelManager* gpu_channel_manager_;

  scoped_ptr<IPC::SyncChannel> channel_;

  // The id of the renderer who is on the other side of the channel.
  int renderer_id_;

  // Handle to the renderer process that is on the other side of the channel.
  base::ProcessHandle renderer_process_;

  // The process id of the renderer process.
  base::ProcessId renderer_pid_;

  // Used to implement message routing functionality to CommandBuffer objects
  MessageRouter router_;

#if defined(ENABLE_GPU)
  typedef IDMap<GpuCommandBufferStub, IDMapOwnPointer> StubMap;
  StubMap stubs_;
  std::set<int32> latched_routes_;
#endif  // defined (ENABLE_GPU)

  // A collection of transport textures created.
  typedef IDMap<TransportTexture, IDMapOwnPointer> TransportTextureMap;
  TransportTextureMap transport_textures_;

  bool log_messages_;  // True if we should log sent and received messages.
  gpu::gles2::DisallowedExtensions disallowed_extensions_;
  GpuWatchdog* watchdog_;

  DISALLOW_COPY_AND_ASSIGN(GpuChannel);
};

#endif  // CONTENT_COMMON_GPU_GPU_CHANNEL_H_
