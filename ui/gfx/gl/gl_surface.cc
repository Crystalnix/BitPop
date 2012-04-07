// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gfx/gl/gl_surface.h"

#include <algorithm>
#include <vector>

#include "base/command_line.h"
#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/threading/thread_local.h"
#include "ui/gfx/gl/gl_context.h"
#include "ui/gfx/gl/gl_implementation.h"

namespace gfx {

namespace {
base::LazyInstance<base::ThreadLocalPointer<GLSurface> >::Leaky
    current_surface_ = LAZY_INSTANCE_INITIALIZER;
}  // namespace

// static
bool GLSurface::InitializeOneOff() {
  static bool initialized = false;
  if (initialized)
    return true;

  std::vector<GLImplementation> allowed_impls;
  GetAllowedGLImplementations(&allowed_impls);
  DCHECK(!allowed_impls.empty());

  // The default implementation is always the first one in list.
  GLImplementation impl = allowed_impls[0];
  bool fallback_to_osmesa = false;
  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kUseGL)) {
    std::string requested_implementation_name =
        CommandLine::ForCurrentProcess()->GetSwitchValueASCII(switches::kUseGL);
    if (requested_implementation_name == "any") {
      fallback_to_osmesa = true;
    } else if (requested_implementation_name == "swiftshader") {
      impl = kGLImplementationEGLGLES2;
    } else {
      impl = GetNamedGLImplementation(requested_implementation_name);
      if (std::find(allowed_impls.begin(),
                    allowed_impls.end(),
                    impl) == allowed_impls.end()) {
        LOG(ERROR) << "Requested GL implementation is not available.";
        return false;
      }
    }
  }

  initialized = InitializeGLBindings(impl) && InitializeOneOffInternal();
  if (!initialized && fallback_to_osmesa) {
    ClearGLBindings();
    initialized = InitializeGLBindings(kGLImplementationOSMesaGL) &&
                  InitializeOneOffInternal();
  }

  if (initialized) {
    DVLOG(1) << "Using "
             << GetGLImplementationName(GetGLImplementation())
             << " GL implementation.";
    if (CommandLine::ForCurrentProcess()->HasSwitch(
        switches::kEnableGPUServiceLogging))
      InitializeDebugGLBindings();
  }
  return initialized;
}

GLSurface::GLSurface() {
}

GLSurface::~GLSurface() {
  if (GetCurrent() == this)
    SetCurrent(NULL);
}

bool GLSurface::Initialize()
{
  return true;
}

bool GLSurface::Resize(const gfx::Size& size) {
  NOTIMPLEMENTED();
  return false;
}

std::string GLSurface::GetExtensions() {
  // Use of GLSurfaceAdapter class means that we can't compare
  // GetCurrent() and this directly.
  DCHECK_EQ(GetCurrent()->GetHandle(), GetHandle());
  return std::string("");
}

unsigned int GLSurface::GetBackingFrameBufferObject() {
  return 0;
}

bool GLSurface::PostSubBuffer(int x, int y, int width, int height) {
  return false;
}

bool GLSurface::OnMakeCurrent(GLContext* context) {
  return true;
}

void GLSurface::SetVisible(bool visible) {
}

void* GLSurface::GetShareHandle() {
  NOTIMPLEMENTED();
  return NULL;
}

void* GLSurface::GetDisplay() {
  NOTIMPLEMENTED();
  return NULL;
}

void* GLSurface::GetConfig() {
  NOTIMPLEMENTED();
  return NULL;
}

unsigned GLSurface::GetFormat() {
  NOTIMPLEMENTED();
  return 0;
}

GLSurface* GLSurface::GetCurrent() {
  return current_surface_.Pointer()->Get();
}

void GLSurface::SetCurrent(GLSurface* surface) {
  current_surface_.Pointer()->Set(surface);
}

GLSurfaceAdapter::GLSurfaceAdapter(GLSurface* surface) : surface_(surface) {
}

GLSurfaceAdapter::~GLSurfaceAdapter() {
}

bool GLSurfaceAdapter::Initialize() {
  return surface_->Initialize();
}

void GLSurfaceAdapter::Destroy() {
  surface_->Destroy();
}

bool GLSurfaceAdapter::Resize(const gfx::Size& size) {
  return surface_->Resize(size);
}

bool GLSurfaceAdapter::IsOffscreen() {
  return surface_->IsOffscreen();
}

bool GLSurfaceAdapter::SwapBuffers() {
  return surface_->SwapBuffers();
}

bool GLSurfaceAdapter::PostSubBuffer(int x, int y, int width, int height) {
  return surface_->PostSubBuffer(x, y, width, height);
}

std::string GLSurfaceAdapter::GetExtensions() {
  return surface_->GetExtensions();
}

gfx::Size GLSurfaceAdapter::GetSize() {
  return surface_->GetSize();
}

void* GLSurfaceAdapter::GetHandle() {
  return surface_->GetHandle();
}

unsigned int GLSurfaceAdapter::GetBackingFrameBufferObject() {
  return surface_->GetBackingFrameBufferObject();
}

bool GLSurfaceAdapter::OnMakeCurrent(GLContext* context) {
  return surface_->OnMakeCurrent(context);
}

void GLSurfaceAdapter::SetVisible(bool visible) {
  surface_->SetVisible(visible);
}

void* GLSurfaceAdapter::GetShareHandle() {
  return surface_->GetShareHandle();
}

void* GLSurfaceAdapter::GetDisplay() {
  return surface_->GetDisplay();
}

void* GLSurfaceAdapter::GetConfig() {
  return surface_->GetConfig();
}

unsigned GLSurfaceAdapter::GetFormat() {
  return surface_->GetFormat();
}

}  // namespace gfx
