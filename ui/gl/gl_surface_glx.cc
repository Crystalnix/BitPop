// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern "C" {
#include <X11/Xlib.h>
}

#include "ui/gl/gl_surface_glx.h"

#include "base/basictypes.h"
#include "base/debug/trace_event.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "base/process_util.h"
#include "third_party/mesa/MesaLib/include/GL/osmesa.h"
#include "ui/base/x/x11_util.h"
#include "ui/gl/gl_bindings.h"
#include "ui/gl/gl_implementation.h"

namespace gfx {

namespace {

// scoped_ptr functor for XFree(). Use as follows:
//   scoped_ptr_malloc<XVisualInfo, ScopedPtrXFree> foo(...);
// where "XVisualInfo" is any X type that is freed with XFree.
class ScopedPtrXFree {
 public:
  void operator()(void* x) const {
    ::XFree(x);
  }
};

Display* g_display;
const char* g_glx_extensions = NULL;
bool g_glx_create_context_robustness_supported = false;

}  // namespace

GLSurfaceGLX::GLSurfaceGLX() {}

bool GLSurfaceGLX::InitializeOneOff() {
  static bool initialized = false;
  if (initialized)
    return true;

  g_display = base::MessagePumpForUI::GetDefaultXDisplay();
  if (!g_display) {
    LOG(ERROR) << "XOpenDisplay failed.";
    return false;
  }

  int major, minor;
  if (!glXQueryVersion(g_display, &major, &minor)) {
    LOG(ERROR) << "glxQueryVersion failed";
    return false;
  }

  if (major == 1 && minor < 3) {
    LOG(ERROR) << "GLX 1.3 or later is required.";
    return false;
  }

  g_glx_extensions = glXQueryExtensionsString(g_display, 0);
  g_glx_create_context_robustness_supported =
      HasGLXExtension("GLX_ARB_create_context_robustness");

  initialized = true;
  return true;
}

// static
const char* GLSurfaceGLX::GetGLXExtensions() {
  return g_glx_extensions;
}

// static
bool GLSurfaceGLX::HasGLXExtension(const char* name) {
  return ExtensionsContain(GetGLXExtensions(), name);
}

// static
bool GLSurfaceGLX::IsCreateContextRobustnessSupported() {
  return g_glx_create_context_robustness_supported;
}

void* GLSurfaceGLX::GetDisplay() {
  return g_display;
}

GLSurfaceGLX::~GLSurfaceGLX() {}

NativeViewGLSurfaceGLX::NativeViewGLSurfaceGLX(gfx::AcceleratedWidget window)
  : window_(window),
    config_(NULL) {
}

bool NativeViewGLSurfaceGLX::Initialize() {
  XWindowAttributes attributes;
  if (!XGetWindowAttributes(g_display, window_, &attributes)) {
    LOG(ERROR) << "XGetWindowAttributes failed for window " << window_ << ".";
    return false;
  }
  size_ = gfx::Size(attributes.width, attributes.height);
  return true;
}

void NativeViewGLSurfaceGLX::Destroy() {
}

bool NativeViewGLSurfaceGLX::Resize(const gfx::Size& size) {
  // On Intel drivers, the frame buffer won't be resize until the next swap. If
  // we only do PostSubBuffer, then we're stuck in the old size. Force a swap
  // now.
  if (gfx::g_GLX_MESA_copy_sub_buffer && size_ != size)
    SwapBuffers();
  size_ = size;
  return true;
}

bool NativeViewGLSurfaceGLX::IsOffscreen() {
  return false;
}

bool NativeViewGLSurfaceGLX::SwapBuffers() {
  glXSwapBuffers(g_display, window_);
  // For latency_tests.cc:
  UNSHIPPED_TRACE_EVENT_INSTANT0("test_gpu", "CompositorSwapBuffersComplete");
  return true;
}

gfx::Size NativeViewGLSurfaceGLX::GetSize() {
  return size_;
}

void* NativeViewGLSurfaceGLX::GetHandle() {
  return reinterpret_cast<void*>(window_);
}

std::string NativeViewGLSurfaceGLX::GetExtensions() {
  std::string extensions = GLSurface::GetExtensions();
  if (g_GLX_MESA_copy_sub_buffer) {
    extensions += extensions.empty() ? "" : " ";
    extensions += "GL_CHROMIUM_post_sub_buffer";
  }
  return extensions;
}

void* NativeViewGLSurfaceGLX::GetConfig() {
  if (!config_) {
    // This code path is expensive, but we only take it when
    // attempting to use GLX_ARB_create_context_robustness, in which
    // case we need a GLXFBConfig for the window in order to create a
    // context for it.
    //
    // TODO(kbr): this is not a reliable code path. On platforms which
    // support it, we should use glXChooseFBConfig in the browser
    // process to choose the FBConfig and from there the X Visual to
    // use when creating the window in the first place. Then we can
    // pass that FBConfig down rather than attempting to reconstitute
    // it.

    XWindowAttributes attributes;
    if (!XGetWindowAttributes(
        g_display,
        reinterpret_cast<GLXDrawable>(GetHandle()),
        &attributes)) {
      LOG(ERROR) << "XGetWindowAttributes failed for window " <<
          reinterpret_cast<GLXDrawable>(GetHandle()) << ".";
      return NULL;
    }

    int visual_id = XVisualIDFromVisual(attributes.visual);

    int num_elements = 0;
    scoped_ptr_malloc<GLXFBConfig, ScopedPtrXFree> configs(
        glXGetFBConfigs(g_display,
                        DefaultScreen(g_display),
                        &num_elements));
    if (!configs.get()) {
      LOG(ERROR) << "glXGetFBConfigs failed.";
      return NULL;
    }
    if (!num_elements) {
      LOG(ERROR) << "glXGetFBConfigs returned 0 elements.";
      return NULL;
    }
    bool found = false;
    int i;
    for (i = 0; i < num_elements; ++i) {
      int value;
      if (glXGetFBConfigAttrib(
              g_display, configs.get()[i], GLX_VISUAL_ID, &value)) {
        LOG(ERROR) << "glXGetFBConfigAttrib failed.";
        return NULL;
      }
      if (value == visual_id) {
        found = true;
        break;
      }
    }
    if (found) {
      config_ = configs.get()[i];
    }
  }

  return config_;
}

bool NativeViewGLSurfaceGLX::PostSubBuffer(
    int x, int y, int width, int height) {
  DCHECK(g_GLX_MESA_copy_sub_buffer);
  glXCopySubBufferMESA(g_display, window_, x, y, width, height);
  return true;
}

NativeViewGLSurfaceGLX::NativeViewGLSurfaceGLX()
  : window_(0),
    config_(NULL) {
}

NativeViewGLSurfaceGLX::~NativeViewGLSurfaceGLX() {
  Destroy();
}

PbufferGLSurfaceGLX::PbufferGLSurfaceGLX(const gfx::Size& size)
  : size_(size),
    config_(NULL),
    pbuffer_(0) {
}

bool PbufferGLSurfaceGLX::Initialize() {
  DCHECK(!pbuffer_);

  static const int config_attributes[] = {
    GLX_BUFFER_SIZE, 32,
    GLX_ALPHA_SIZE, 8,
    GLX_BLUE_SIZE, 8,
    GLX_GREEN_SIZE, 8,
    GLX_RED_SIZE, 8,
    GLX_RENDER_TYPE, GLX_RGBA_BIT,
    GLX_DRAWABLE_TYPE, GLX_PBUFFER_BIT,
    GLX_DOUBLEBUFFER, False,
    0
  };

  int num_elements = 0;
  scoped_ptr_malloc<GLXFBConfig, ScopedPtrXFree> configs(
      glXChooseFBConfig(g_display,
                        DefaultScreen(g_display),
                        config_attributes,
                        &num_elements));
  if (!configs.get()) {
    LOG(ERROR) << "glXChooseFBConfig failed.";
    return false;
  }
  if (!num_elements) {
    LOG(ERROR) << "glXChooseFBConfig returned 0 elements.";
    return false;
  }

  config_ = configs.get()[0];

  const int pbuffer_attributes[] = {
    GLX_PBUFFER_WIDTH, size_.width(),
    GLX_PBUFFER_HEIGHT, size_.height(),
    0
  };
  pbuffer_ = glXCreatePbuffer(g_display,
                              static_cast<GLXFBConfig>(config_),
                              pbuffer_attributes);
  if (!pbuffer_) {
    Destroy();
    LOG(ERROR) << "glXCreatePbuffer failed.";
    return false;
  }

  return true;
}

void PbufferGLSurfaceGLX::Destroy() {
  if (pbuffer_) {
    glXDestroyPbuffer(g_display, pbuffer_);
    pbuffer_ = 0;
  }

  config_ = NULL;
}

bool PbufferGLSurfaceGLX::IsOffscreen() {
  return true;
}

bool PbufferGLSurfaceGLX::SwapBuffers() {
  NOTREACHED() << "Attempted to call SwapBuffers on a pbuffer.";
  return false;
}

gfx::Size PbufferGLSurfaceGLX::GetSize() {
  return size_;
}

void* PbufferGLSurfaceGLX::GetHandle() {
  return reinterpret_cast<void*>(pbuffer_);
}

void* PbufferGLSurfaceGLX::GetConfig() {
  return config_;
}

PbufferGLSurfaceGLX::~PbufferGLSurfaceGLX() {
  Destroy();
}

}  // namespace gfx
