// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/command_line.h"
#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/threading/thread_local.h"
#include "ui/gl/gl_bindings.h"
#include "ui/gl/gl_context.h"
#include "ui/gl/gl_implementation.h"
#include "ui/gl/gl_surface.h"
#include "ui/gl/gl_switches.h"

namespace gfx {

namespace {
base::LazyInstance<base::ThreadLocalPointer<GLContext> >::Leaky
    current_context_ = LAZY_INSTANCE_INITIALIZER;
}  // namespace

GLContext::GLContext(GLShareGroup* share_group) : share_group_(share_group) {
  if (!share_group_.get())
    share_group_ = new GLShareGroup;

  share_group_->AddContext(this);
}

GLContext::~GLContext() {
  share_group_->RemoveContext(this);
  if (GetCurrent() == this) {
    SetCurrent(NULL, NULL);
  }
}

std::string GLContext::GetExtensions() {
  DCHECK(IsCurrent(NULL));
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

GLShareGroup* GLContext::share_group() {
  return share_group_.get();
}

bool GLContext::LosesAllContextsOnContextLost() {
  switch (GetGLImplementation()) {
    case kGLImplementationDesktopGL:
      return false;
    case kGLImplementationEGLGLES2:
      return true;
    case kGLImplementationOSMesaGL:
    case kGLImplementationAppleGL:
      return false;
    case kGLImplementationMockGL:
      return false;
    default:
      NOTREACHED();
      return true;
  }
}

GLContext* GLContext::GetCurrent() {
  return current_context_.Pointer()->Get();
}

void GLContext::SetCurrent(GLContext* context, GLSurface* surface) {
  current_context_.Pointer()->Set(context);
  GLSurface::SetCurrent(surface);
}

bool GLContext::WasAllocatedUsingRobustnessExtension() {
  return false;
}

bool GLContext::InitializeExtensionBindings() {
  DCHECK(IsCurrent(NULL));
  static bool initialized = false;
  if (initialized)
    return initialized;
  initialized = InitializeGLExtensionBindings(GetGLImplementation(), this);
  if (!initialized)
    LOG(ERROR) << "Could not initialize extension bindings.";
  return initialized;
}

}  // namespace gfx
