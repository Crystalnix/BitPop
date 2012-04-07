// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if defined(ENABLE_GPU)

#include "content/common/gpu/image_transport_surface.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/win/windows_version.h"
#include "content/common/gpu/gpu_messages.h"
#include "content/public/common/content_switches.h"
#include "third_party/angle/include/EGL/egl.h"
#include "ui/gfx/gl/gl_bindings.h"
#include "ui/gfx/gl/gl_context.h"
#include "ui/gfx/gl/gl_implementation.h"
#include "ui/gfx/gl/gl_surface_egl.h"
#include "ui/gfx/native_widget_types.h"

namespace {

// We are backed by an Pbuffer offscreen surface through which ANGLE provides
// a handle to the corresponding render target texture through an extension.
class PbufferImageTransportSurface
    : public gfx::GLSurfaceAdapter,
      public ImageTransportSurface,
      public base::SupportsWeakPtr<PbufferImageTransportSurface> {
 public:
  PbufferImageTransportSurface(GpuChannelManager* manager,
                               GpuCommandBufferStub* stub);

  // gfx::GLSurface implementation
  virtual bool Initialize() OVERRIDE;
  virtual void Destroy() OVERRIDE;
  virtual bool IsOffscreen() OVERRIDE;
  virtual bool SwapBuffers() OVERRIDE;
  virtual bool PostSubBuffer(int x, int y, int width, int height) OVERRIDE;
  virtual std::string GetExtensions() OVERRIDE;
  virtual void SetVisible(bool visible) OVERRIDE;

 protected:
  // ImageTransportSurface implementation
  virtual void OnNewSurfaceACK(uint64 surface_handle,
                               TransportDIB::Handle shm_handle) OVERRIDE;
  virtual void OnBuffersSwappedACK() OVERRIDE;
  virtual void OnPostSubBufferACK() OVERRIDE;
  virtual void OnResizeViewACK() OVERRIDE;
  virtual void OnResize(gfx::Size size) OVERRIDE;

 private:
  virtual ~PbufferImageTransportSurface();
  void SendBuffersSwapped();

  // Whether the surface is currently visible.
  bool is_visible_;

  // Size to resize to when the surface becomes visible.
  gfx::Size visible_size_;

  scoped_ptr<ImageTransportHelper> helper_;

  DISALLOW_COPY_AND_ASSIGN(PbufferImageTransportSurface);
};

PbufferImageTransportSurface::PbufferImageTransportSurface(
    GpuChannelManager* manager,
    GpuCommandBufferStub* stub)
    : GLSurfaceAdapter(new gfx::PbufferGLSurfaceEGL(false, gfx::Size(1, 1))),
      is_visible_(true) {
  helper_.reset(new ImageTransportHelper(this,
                                         manager,
                                         stub,
                                         gfx::kNullPluginWindow));
}

PbufferImageTransportSurface::~PbufferImageTransportSurface() {
  Destroy();
}

bool PbufferImageTransportSurface::Initialize() {
  // Only support this path if the GL implementation is ANGLE.
  // IO surfaces will not work with, for example, OSMesa software renderer
  // GL contexts.
  if (gfx::GetGLImplementation() != gfx::kGLImplementationEGLGLES2)
    return false;

  if (!helper_->Initialize())
    return false;

  return GLSurfaceAdapter::Initialize();
}

void PbufferImageTransportSurface::Destroy() {
  helper_->Destroy();
  GLSurfaceAdapter::Destroy();
}

bool PbufferImageTransportSurface::IsOffscreen() {
  return false;
}

bool PbufferImageTransportSurface::SwapBuffers() {
  HANDLE surface_handle = GetShareHandle();
  if (!surface_handle)
    return false;

  helper_->DeferToFence(base::Bind(
      &PbufferImageTransportSurface::SendBuffersSwapped,
      AsWeakPtr()));

  return true;
}

bool PbufferImageTransportSurface::PostSubBuffer(
    int x, int y, int width, int height) {
  NOTREACHED();
  return false;
}

void PbufferImageTransportSurface::SetVisible(bool visible) {
  if (visible == is_visible_)
    return;

  is_visible_ = visible;

  if (visible)
    Resize(visible_size_);
  else
    Resize(gfx::Size(1, 1));
}

std::string PbufferImageTransportSurface::GetExtensions() {
  std::string extensions = gfx::GLSurface::GetExtensions();
  extensions += extensions.empty() ? "" : " ";
  extensions += "GL_CHROMIUM_front_buffer_cached";
  return extensions;
}

void PbufferImageTransportSurface::SendBuffersSwapped() {
  GpuHostMsg_AcceleratedSurfaceBuffersSwapped_Params params;
  params.surface_handle = reinterpret_cast<int64>(GetShareHandle());
  params.size = GetSize();
  helper_->SendAcceleratedSurfaceBuffersSwapped(params);

  helper_->SetScheduled(false);
}

void PbufferImageTransportSurface::OnBuffersSwappedACK() {
  helper_->SetScheduled(true);
}

void PbufferImageTransportSurface::OnPostSubBufferACK() {
  NOTREACHED();
}

void PbufferImageTransportSurface::OnNewSurfaceACK(
    uint64 surface_handle,
    TransportDIB::Handle shm_handle) {
  NOTREACHED();
}

void PbufferImageTransportSurface::OnResizeViewACK() {
  NOTREACHED();
}

void PbufferImageTransportSurface::OnResize(gfx::Size size) {
  if (is_visible_)
    Resize(size);

  visible_size_ = size;
}

}  // namespace anonymous

// static
scoped_refptr<gfx::GLSurface> ImageTransportSurface::CreateSurface(
    GpuChannelManager* manager,
    GpuCommandBufferStub* stub,
    gfx::PluginWindowHandle handle) {
  scoped_refptr<gfx::GLSurface> surface;

  if (gfx::GetGLImplementation() == gfx::kGLImplementationEGLGLES2 &&
      !CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kDisableImageTransportSurface)) {
    const char* extensions = eglQueryString(eglGetDisplay(EGL_DEFAULT_DISPLAY),
                                            EGL_EXTENSIONS);
    if (strstr(extensions, "EGL_ANGLE_query_surface_pointer") &&
        strstr(extensions, "EGL_ANGLE_surface_d3d_texture_2d_share_handle")) {
      surface = new PbufferImageTransportSurface(manager, stub);
    }
  }

  if (!surface.get()) {
    surface = gfx::GLSurface::CreateViewGLSurface(false, handle);
    if (!surface.get())
      return NULL;

    surface = new PassThroughImageTransportSurface(manager,
                                                   stub,
                                                   surface.get());
  }

  if (surface->Initialize())
    return surface;
  else
    return NULL;
}

#endif  // ENABLE_GPU
