// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gfx/compositor/compositor.h"

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "ui/gfx/transform.h"
#include "ui/gfx/gl/gl_context.h"
#include "ui/gfx/gl/gl_bindings.h"
#include "ui/gfx/gl/gl_implementation.h"

namespace ui {

class CompositorGL : public Compositor {
 public:
  explicit CompositorGL(gfx::AcceleratedWidget widget);

 private:
  // Overridden from Compositor.
  void NotifyStart() OVERRIDE;
  void NotifyEnd() OVERRIDE;
  void DrawTextureWithTransform(TextureID txt,
                                const ui::Transform& transform) OVERRIDE;
  void SaveTransform() OVERRIDE;
  void RestoreTransform() OVERRIDE;

  // The GL context used for compositing.
  scoped_ptr<gfx::GLContext> gl_context_;

  // Keep track of whether compositing has started or not.
  bool started_;

  DISALLOW_COPY_AND_ASSIGN(CompositorGL);
};

CompositorGL::CompositorGL(gfx::AcceleratedWidget widget)
    : gl_context_(gfx::GLContext::CreateViewGLContext(widget, false)),
      started_(false) {
}

void CompositorGL::NotifyStart() {
  started_ = true;
  gl_context_->MakeCurrent();
}

void CompositorGL::NotifyEnd() {
  DCHECK(started_);
  gl_context_->SwapBuffers();
  started_ = false;
}

void CompositorGL::DrawTextureWithTransform(TextureID txt,
                                            const ui::Transform& transform) {
  DCHECK(started_);

  // TODO(wjmaclean):
  NOTIMPLEMENTED();
}

void CompositorGL::SaveTransform() {
  // TODO(sadrul):
  NOTIMPLEMENTED();
}

void CompositorGL::RestoreTransform() {
  // TODO(sadrul):
  NOTIMPLEMENTED();
}

// static
Compositor* Compositor::Create(gfx::AcceleratedWidget widget) {
  if (gfx::GetGLImplementation() != gfx::kGLImplementationNone)
    return new CompositorGL(widget);
  return NULL;
}

}  // namespace ui
