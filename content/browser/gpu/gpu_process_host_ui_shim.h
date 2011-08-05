// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_GPU_GPU_PROCESS_HOST_UI_SHIM_H_
#define CONTENT_BROWSER_GPU_GPU_PROCESS_HOST_UI_SHIM_H_
#pragma once

// This class lives on the UI thread and supports classes like the
// BackingStoreProxy, which must live on the UI thread. The IO thread
// portion of this class, the GpuProcessHost, is responsible for
// shuttling messages between the browser and GPU processes.

#include <queue>

#include "base/callback.h"
#include "base/task.h"
#include "base/memory/linked_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/threading/non_thread_safe.h"
#include "content/common/message_router.h"

namespace gfx {
class Size;
}

struct GPUCreateCommandBufferConfig;
struct GpuHostMsg_AcceleratedSurfaceSetIOSurface_Params;
struct GpuHostMsg_AcceleratedSurfaceBuffersSwapped_Params;

namespace IPC {
struct ChannelHandle;
class Message;
}

// A task that will forward an IPC message to the UI shim.
class RouteToGpuProcessHostUIShimTask : public Task {
 public:
  RouteToGpuProcessHostUIShimTask(int host_id, const IPC::Message& msg);
  virtual ~RouteToGpuProcessHostUIShimTask();

 private:
  virtual void Run();

  int host_id_;
  IPC::Message msg_;
};

class GpuProcessHostUIShim
    : public IPC::Channel::Listener,
      public IPC::Channel::Sender,
      public base::NonThreadSafe {
 public:
  // Create a GpuProcessHostUIShim with the given ID.  The object can be found
  // using FromID with the same id.
  static GpuProcessHostUIShim* Create(int host_id);

  // Destroy the GpuProcessHostUIShim with the given host ID. This can only
  // be called on the UI thread. Only the GpuProcessHost should destroy the
  // UI shim.
  static void Destroy(int host_id);

  // Destroy all remaining GpuProcessHostUIShims.
  static void DestroyAll();

  static GpuProcessHostUIShim* FromID(int host_id);

  // IPC::Channel::Sender implementation.
  virtual bool Send(IPC::Message* msg);

  // IPC::Channel::Listener implementation.
  // The GpuProcessHost causes this to be called on the UI thread to
  // dispatch the incoming messages from the GPU process, which are
  // actually received on the IO thread.
  virtual bool OnMessageReceived(const IPC::Message& message);

#if defined(OS_MACOSX)
  // Notify the GPU process that an accelerated surface was destroyed.
  void DidDestroyAcceleratedSurface(int renderer_id, int32 render_view_id);

  // TODO(apatrick): Remove this when mac does not use AcceleratedSurfaces for
  // when running the GPU thread in the browser process.
  static void SendToGpuHost(int host_id, IPC::Message* msg);
#endif

 private:
  explicit GpuProcessHostUIShim(int host_id);
  virtual ~GpuProcessHostUIShim();

  // Message handlers.
  bool OnControlMessageReceived(const IPC::Message& message);

  void OnLogMessage(int level, const std::string& header,
      const std::string& message);
#if defined(TOOLKIT_USES_GTK) && !defined(TOUCH_UI) || defined(OS_WIN)
  void OnResizeView(int32 renderer_id,
                    int32 render_view_id,
                    int32 command_buffer_route_id,
                    gfx::Size size);
#elif defined(OS_MACOSX)
  void OnAcceleratedSurfaceSetIOSurface(
      const GpuHostMsg_AcceleratedSurfaceSetIOSurface_Params& params);
  void OnAcceleratedSurfaceBuffersSwapped(
      const GpuHostMsg_AcceleratedSurfaceBuffersSwapped_Params& params);
#endif

  // The serial number of the GpuProcessHost / GpuProcessHostUIShim pair.
  int host_id_;
};

#endif  // CONTENT_BROWSER_GPU_GPU_PROCESS_HOST_UI_SHIM_H_
