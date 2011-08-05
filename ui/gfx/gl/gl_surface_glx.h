// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_GFX_GL_GL_SURFACE_GLX_H_
#define UI_GFX_GL_GL_SURFACE_GLX_H_

#include "ui/gfx/gl/gl_surface.h"

#include "ui/base/x/x11_util.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/gfx/size.h"

namespace gfx {

// Base class for GLX surfaces.
class GLSurfaceGLX : public GLSurface {
 public:
  GLSurfaceGLX();
  virtual ~GLSurfaceGLX();

  static bool InitializeOneOff();
  static Display* GetDisplay();

  // Get the FB config that the surface was created with.
  virtual void* GetConfig() = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(GLSurfaceGLX);
};

// A surface used to render to a view.
class NativeViewGLSurfaceGLX : public GLSurfaceGLX {
 public:
  explicit NativeViewGLSurfaceGLX(gfx::PluginWindowHandle window);
  virtual ~NativeViewGLSurfaceGLX();

  // Implement GLSurfaceGLX.
  virtual bool Initialize();
  virtual void Destroy();
  virtual bool IsOffscreen();
  virtual bool SwapBuffers();
  virtual gfx::Size GetSize();
  virtual void* GetHandle();
  virtual void* GetConfig();

 private:
  gfx::PluginWindowHandle window_;
  void* config_;
  XID glx_window_;
  DISALLOW_COPY_AND_ASSIGN(NativeViewGLSurfaceGLX);
};

// A surface used to render to an offscreen pbuffer.
class PbufferGLSurfaceGLX : public GLSurfaceGLX {
 public:
  explicit PbufferGLSurfaceGLX(const gfx::Size& size);
  virtual ~PbufferGLSurfaceGLX();

  // Implement GLSurfaceGLX.
  virtual bool Initialize();
  virtual void Destroy();
  virtual bool IsOffscreen();
  virtual bool SwapBuffers();
  virtual gfx::Size GetSize();
  virtual void* GetHandle();
  virtual void* GetConfig();

 private:
  gfx::Size size_;
  void* config_;
  XID pbuffer_;
  DISALLOW_COPY_AND_ASSIGN(PbufferGLSurfaceGLX);
};

}  // namespace gfx

#endif  // UI_GFX_GL_GL_SURFACE_GLX_H_
