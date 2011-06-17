// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/command_line.h"
#include "base/logging.h"
#include "ui/gfx/gl/gl_context.h"
#include "ui/gfx/gl/gl_bindings.h"
#include "ui/gfx/gl/gl_implementation.h"
#include "ui/gfx/gl/gl_switches.h"

namespace gfx {

void GLContext::ReleaseCurrent() {
  // TODO(apatrick): Implement this in GLContext derivatives.
}

GLSurface* GLContext::GetSurface() {
  // TODO(apatrick): Remove this when surfaces are split from contexts.
  return NULL;
}

unsigned int GLContext::GetBackingFrameBufferObject() {
  return 0;
}

std::string GLContext::GetExtensions() {
  DCHECK(IsCurrent());
  const char* ext = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
  return std::string(ext ? ext : "");
}

bool GLContext::HasExtension(const char* name) {
  std::string extensions = GetExtensions();
  extensions += " ";

  std::string delimited_name(name);
  delimited_name += " ";

  return extensions.find(delimited_name) != std::string::npos;
}

bool GLContext::InitializeCommon() {
  if (!MakeCurrent()) {
    LOG(ERROR) << "MakeCurrent failed.";
    return false;
  }

  if (!IsOffscreen()) {
    if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kDisableGpuVsync))
      SetSwapInterval(0);
    else
      SetSwapInterval(1);
  }

  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
  if (glGetError() != GL_NO_ERROR) {
    LOG(ERROR) << "glClear failed.";
    return false;
  }

  return true;
}

bool GLContext::LosesAllContextsOnContextLost()
{
  switch (GetGLImplementation()) {
    case kGLImplementationDesktopGL:
      return false;
    case kGLImplementationEGLGLES2:
      return true;
    case kGLImplementationOSMesaGL:
      return false;
    case kGLImplementationMockGL:
      return false;
    default:
      NOTREACHED();
      return true;
  }
}

}  // namespace gfx
