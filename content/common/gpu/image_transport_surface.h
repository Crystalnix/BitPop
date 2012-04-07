// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_COMMON_GPU_IMAGE_TRANSPORT_SURFACE_H_
#define CONTENT_COMMON_GPU_IMAGE_TRANSPORT_SURFACE_H_
#pragma once

#if defined(ENABLE_GPU)

#include "base/callback.h"
#include "base/compiler_specific.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ipc/ipc_channel.h"
#include "ipc/ipc_message.h"
#include "ui/gfx/gl/gl_surface.h"
#include "ui/gfx/size.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/gfx/surface/transport_dib.h"

class GpuChannelManager;
class GpuCommandBufferStub;

struct GpuHostMsg_AcceleratedSurfaceNew_Params;
struct GpuHostMsg_AcceleratedSurfaceBuffersSwapped_Params;
struct GpuHostMsg_AcceleratedSurfacePostSubBuffer_Params;
struct GpuHostMsg_AcceleratedSurfaceRelease_Params;

namespace gfx {
class GLSurface;
}

namespace gpu {
class GpuScheduler;

namespace gles2 {
class GLES2Decoder;
}
}

// The GPU process is agnostic as to how it displays results. On some platforms
// it renders directly to window. On others it renders offscreen and transports
// the results to the browser process to display. This file provides a simple
// framework for making the offscreen path seem more like the onscreen path.
//
// The ImageTransportSurface class defines an simple interface for events that
// should be responded to. The factory returns an offscreen surface that looks
// a lot like an onscreen surface to the GPU process.
//
// The ImageTransportSurfaceHelper provides some glue to the outside world:
// making sure outside events reach the ImageTransportSurface and
// allowing the ImageTransportSurface to send events to the outside world.

class ImageTransportSurface {
 public:
  ImageTransportSurface();
  virtual ~ImageTransportSurface();

  virtual void OnNewSurfaceACK(
      uint64 surface_id, TransportDIB::Handle surface_handle) = 0;
  virtual void OnBuffersSwappedACK() = 0;
  virtual void OnPostSubBufferACK() = 0;
  virtual void OnResizeViewACK() = 0;
  virtual void OnResize(gfx::Size size) = 0;

  // Creates the appropriate surface depending on the GL implementation.
  static scoped_refptr<gfx::GLSurface>
      CreateSurface(GpuChannelManager* manager,
                    GpuCommandBufferStub* stub,
                    gfx::PluginWindowHandle handle);
 private:
  DISALLOW_COPY_AND_ASSIGN(ImageTransportSurface);
};

class ImageTransportHelper : public IPC::Channel::Listener {
 public:
  // Takes weak pointers to objects that outlive the helper.
  ImageTransportHelper(ImageTransportSurface* surface,
                       GpuChannelManager* manager,
                       GpuCommandBufferStub* stub,
                       gfx::PluginWindowHandle handle);
  virtual ~ImageTransportHelper();

  bool Initialize();
  void Destroy();

  // IPC::Channel::Listener implementation:
  virtual bool OnMessageReceived(const IPC::Message& message) OVERRIDE;

  // Helper send functions. Caller fills in the surface specific params
  // like size and surface id. The helper fills in the rest.
  void SendAcceleratedSurfaceNew(
      GpuHostMsg_AcceleratedSurfaceNew_Params params);
  void SendAcceleratedSurfaceBuffersSwapped(
      GpuHostMsg_AcceleratedSurfaceBuffersSwapped_Params params);
  void SendAcceleratedSurfacePostSubBuffer(
      GpuHostMsg_AcceleratedSurfacePostSubBuffer_Params params);
  void SendAcceleratedSurfaceRelease(
      GpuHostMsg_AcceleratedSurfaceRelease_Params params);
  void SendResizeView(const gfx::Size& size);

  // Whether or not we should execute more commands.
  void SetScheduled(bool is_scheduled);

  void DeferToFence(base::Closure task);

  // Make the surface's context current.
  bool MakeCurrent();

 private:
  gpu::GpuScheduler* Scheduler();
  gpu::gles2::GLES2Decoder* Decoder();

  // IPC::Message handlers.
  void OnNewSurfaceACK(uint64 surface_handle, TransportDIB::Handle shm_handle);
  void OnBuffersSwappedACK();
  void OnPostSubBufferACK();
  void OnResizeViewACK();

  // Backbuffer resize callback.
  void Resize(gfx::Size size);

  // Set the default swap interval on the surface.
  void SetSwapInterval();

  // Weak pointers that point to objects that outlive this helper.
  ImageTransportSurface* surface_;
  GpuChannelManager* manager_;

  base::WeakPtr<GpuCommandBufferStub> stub_;
  int32 route_id_;
  gfx::PluginWindowHandle handle_;

  DISALLOW_COPY_AND_ASSIGN(ImageTransportHelper);
};

// An implementation of ImageTransportSurface that implements GLSurface through
// GLSurfaceAdapter, thereby forwarding GLSurface methods through to it.
class PassThroughImageTransportSurface
    : public gfx::GLSurfaceAdapter,
      public ImageTransportSurface {
 public:
  PassThroughImageTransportSurface(GpuChannelManager* manager,
                                   GpuCommandBufferStub* stub,
                                   gfx::GLSurface* surface);
  virtual ~PassThroughImageTransportSurface();

  // GLSurface implementation.
  virtual bool Initialize() OVERRIDE;
  virtual void Destroy() OVERRIDE;
  virtual bool SwapBuffers() OVERRIDE;
  virtual bool PostSubBuffer(int x, int y, int width, int height) OVERRIDE;

  // ImageTransportSurface implementation.
  virtual void OnNewSurfaceACK(
      uint64 surface_handle, TransportDIB::Handle shm_handle) OVERRIDE;
  virtual void OnBuffersSwappedACK() OVERRIDE;
  virtual void OnPostSubBufferACK() OVERRIDE;
  virtual void OnResizeViewACK() OVERRIDE;
  virtual void OnResize(gfx::Size size) OVERRIDE;

 private:
  scoped_ptr<ImageTransportHelper> helper_;
  gfx::Size new_size_;

  DISALLOW_COPY_AND_ASSIGN(PassThroughImageTransportSurface);
};

#endif  // defined(ENABLE_GPU)

#endif  // CONTENT_COMMON_GPU_IMAGE_TRANSPORT_SURFACE_H_
