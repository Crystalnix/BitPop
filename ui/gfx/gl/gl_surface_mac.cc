// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gfx/gl/gl_surface.h"

#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "third_party/mesa/MesaLib/include/GL/osmesa.h"
#include "ui/gfx/gl/gl_bindings.h"
#include "ui/gfx/gl/gl_implementation.h"
#include "ui/gfx/gl/gl_surface_cgl.h"
#include "ui/gfx/gl/gl_surface_osmesa.h"
#include "ui/gfx/gl/gl_surface_stub.h"

namespace gfx {

bool GLSurface::InitializeOneOff() {
  static bool initialized = false;
  if (initialized)
    return true;

  static const GLImplementation kAllowedGLImplementations[] = {
    kGLImplementationDesktopGL,
    kGLImplementationOSMesaGL
  };

  if (!InitializeRequestedGLBindings(
           kAllowedGLImplementations,
           kAllowedGLImplementations + arraysize(kAllowedGLImplementations),
           kGLImplementationDesktopGL)) {
    LOG(ERROR) << "InitializeRequestedGLBindings failed.";
    return false;
  }

  switch (GetGLImplementation()) {
    case kGLImplementationDesktopGL:
      if (!GLSurfaceCGL::InitializeOneOff()) {
        LOG(ERROR) << "GLSurfaceCGL::InitializeOneOff failed.";
        return false;
      }
      break;
    default:
      break;
  }

  initialized = true;
  return true;
}

// TODO(apatrick): support ViewGLSurface on mac.
#if 0
scoped_refptr<GLSurface> GLSurface::CreateViewGLSurface(
    gfx::PluginWindowHandle window) {
  switch (GetGLImplementation()) {
    case kGLImplementationOSMesaGL: {
      scoped_refptr<GLSurface> surface(
          new NativeViewGLSurfaceOSMesa(window));
      if (!surface->Initialize())
        return NULL;

      return surface;
    }
    case kGLImplementationDesktopGL: {
      scoped_refptr<GLSurface> surface(new NativeViewGLSurfaceCGL(
          window));
      if (!surface->Initialize())
        return NULL;

      return surface;
    }
    case kGLImplementationMockGL:
      return new GLSurfaceStub;
    default:
      NOTREACHED();
      return NULL;
  }
}
#endif

scoped_refptr<GLSurface> GLSurface::CreateOffscreenGLSurface(
    const gfx::Size& size) {
  switch (GetGLImplementation()) {
    case kGLImplementationOSMesaGL: {
      scoped_refptr<GLSurface> surface(new GLSurfaceOSMesa(OSMESA_RGBA,
                                                           size));
      if (!surface->Initialize())
        return NULL;

      return surface;
    }
    case kGLImplementationDesktopGL: {
      scoped_refptr<GLSurface> surface(new PbufferGLSurfaceCGL(size));
      if (!surface->Initialize())
        return NULL;

      return surface;
    }
    case kGLImplementationMockGL:
      return new GLSurfaceStub;
    default:
      NOTREACHED();
      return NULL;
  }
}

}  // namespace gfx
