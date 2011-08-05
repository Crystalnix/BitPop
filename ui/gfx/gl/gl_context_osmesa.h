// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_GFX_GL_GL_CONTEXT_OSMESA_H_
#define UI_GFX_GL_GL_CONTEXT_OSMESA_H_
#pragma once

#include "ui/gfx/gl/gl_context.h"

typedef struct osmesa_context *OSMesaContext;

namespace gfx {

class GLSurface;

// Encapsulates an OSMesa OpenGL context that uses software rendering.
class GLContextOSMesa : public GLContext {
 public:
  GLContextOSMesa();
  virtual ~GLContextOSMesa();

  // Implement GLContext.
  virtual bool Initialize(GLContext* shared_context,
                          GLSurface* compatible_surface);
  virtual void Destroy();
  virtual bool MakeCurrent(GLSurface* surface);
  virtual void ReleaseCurrent(GLSurface* surface);
  virtual bool IsCurrent(GLSurface* surface);
  virtual void* GetHandle();
  virtual void SetSwapInterval(int interval);

 private:
  OSMesaContext context_;

  DISALLOW_COPY_AND_ASSIGN(GLContextOSMesa);
};

}  // namespace gfx

#endif  // UI_GFX_GL_GL_CONTEXT_OSMESA_H_
