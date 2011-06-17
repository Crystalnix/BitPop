// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dlfcn.h>

#include "base/memory/singleton.h"
#include "ui/gfx/surface/io_surface_support_mac.h"

typedef CFTypeRef (*IOSurfaceCreateProcPtr)(CFDictionaryRef properties);
typedef uint32 (*IOSurfaceGetIDProcPtr)(CFTypeRef io_surface);
typedef CFTypeRef (*IOSurfaceLookupProcPtr)(uint32 io_surface_id);
typedef mach_port_t (*IOSurfaceCreateMachPortProcPtr)(CFTypeRef io_surface);
typedef CFTypeRef (*IOSurfaceLookupFromMachPortProcPtr)(mach_port_t port);
typedef size_t (*IOSurfaceGetWidthPtr)(CFTypeRef io_surface);
typedef size_t (*IOSurfaceGetHeightPtr)(CFTypeRef io_surface);
typedef CGLError (*CGLTexImageIOSurface2DProcPtr)(CGLContextObj ctx,
                                                  GLenum target,
                                                  GLenum internal_format,
                                                  GLsizei width,
                                                  GLsizei height,
                                                  GLenum format,
                                                  GLenum type,
                                                  CFTypeRef io_surface,
                                                  GLuint plane);

class IOSurfaceSupportImpl : public IOSurfaceSupport {
 public:
  static IOSurfaceSupportImpl* GetInstance();

  bool InitializedSuccessfully() {
    return initialized_successfully_;
  }

  virtual CFStringRef GetKIOSurfaceWidth();
  virtual CFStringRef GetKIOSurfaceHeight();
  virtual CFStringRef GetKIOSurfaceBytesPerElement();
  virtual CFStringRef GetKIOSurfaceIsGlobal();

  virtual CFTypeRef IOSurfaceCreate(CFDictionaryRef properties);
  virtual uint32 IOSurfaceGetID(CFTypeRef io_surface);
  virtual CFTypeRef IOSurfaceLookup(uint32 io_surface_id);
  virtual mach_port_t IOSurfaceCreateMachPort(CFTypeRef io_surface);
  virtual CFTypeRef IOSurfaceLookupFromMachPort(mach_port_t port);

  virtual size_t IOSurfaceGetWidth(CFTypeRef io_surface);
  virtual size_t IOSurfaceGetHeight(CFTypeRef io_surface);

  virtual CGLError CGLTexImageIOSurface2D(CGLContextObj ctx,
                                          GLenum target,
                                          GLenum internal_format,
                                          GLsizei width,
                                          GLsizei height,
                                          GLenum format,
                                          GLenum type,
                                          CFTypeRef io_surface,
                                          GLuint plane);

 private:
  IOSurfaceSupportImpl();
  ~IOSurfaceSupportImpl();

  void* iosurface_handle_;
  void* opengl_handle_;
  CFStringRef k_io_surface_width_;
  CFStringRef k_io_surface_height_;
  CFStringRef k_io_surface_bytes_per_element_;
  CFStringRef k_io_surface_is_global_;
  IOSurfaceCreateProcPtr io_surface_create_;
  IOSurfaceGetIDProcPtr io_surface_get_id_;
  IOSurfaceLookupProcPtr io_surface_lookup_;
  IOSurfaceCreateMachPortProcPtr io_surface_create_mach_port_;
  IOSurfaceLookupFromMachPortProcPtr io_surface_lookup_from_mach_port_;
  IOSurfaceGetWidthPtr io_surface_get_width_;
  IOSurfaceGetHeightPtr io_surface_get_height_;
  CGLTexImageIOSurface2DProcPtr cgl_tex_image_io_surface_2d_;
  bool initialized_successfully_;

  friend struct DefaultSingletonTraits<IOSurfaceSupportImpl>;
  DISALLOW_COPY_AND_ASSIGN(IOSurfaceSupportImpl);
};

IOSurfaceSupportImpl* IOSurfaceSupportImpl::GetInstance() {
  IOSurfaceSupportImpl* impl = Singleton<IOSurfaceSupportImpl>::get();
  if (impl->InitializedSuccessfully())
    return impl;
  return NULL;
}

CFStringRef IOSurfaceSupportImpl::GetKIOSurfaceWidth() {
  return k_io_surface_width_;
}

CFStringRef IOSurfaceSupportImpl::GetKIOSurfaceHeight() {
  return k_io_surface_height_;
}

CFStringRef IOSurfaceSupportImpl::GetKIOSurfaceBytesPerElement() {
  return k_io_surface_bytes_per_element_;
}

CFStringRef IOSurfaceSupportImpl::GetKIOSurfaceIsGlobal() {
  return k_io_surface_is_global_;
}

CFTypeRef IOSurfaceSupportImpl::IOSurfaceCreate(CFDictionaryRef properties) {
  return io_surface_create_(properties);
}

uint32 IOSurfaceSupportImpl::IOSurfaceGetID(
    CFTypeRef io_surface) {
  return io_surface_get_id_(io_surface);
}

CFTypeRef IOSurfaceSupportImpl::IOSurfaceLookup(uint32 io_surface_id) {
  return io_surface_lookup_(io_surface_id);
}

mach_port_t IOSurfaceSupportImpl::IOSurfaceCreateMachPort(
    CFTypeRef io_surface) {
  return io_surface_create_mach_port_(io_surface);
}

CFTypeRef IOSurfaceSupportImpl::IOSurfaceLookupFromMachPort(mach_port_t port) {
  return io_surface_lookup_from_mach_port_(port);
}

size_t IOSurfaceSupportImpl::IOSurfaceGetWidth(CFTypeRef io_surface) {
  return io_surface_get_width_(io_surface);
}

size_t IOSurfaceSupportImpl::IOSurfaceGetHeight(CFTypeRef io_surface) {
  return io_surface_get_height_(io_surface);
}


CGLError IOSurfaceSupportImpl::CGLTexImageIOSurface2D(CGLContextObj ctx,
                                                      GLenum target,
                                                      GLenum internal_format,
                                                      GLsizei width,
                                                      GLsizei height,
                                                      GLenum format,
                                                      GLenum type,
                                                      CFTypeRef io_surface,
                                                      GLuint plane) {
  return cgl_tex_image_io_surface_2d_(ctx,
                                      target,
                                      internal_format,
                                      width,
                                      height,
                                      format,
                                      type,
                                      io_surface,
                                      plane);
}

IOSurfaceSupportImpl::IOSurfaceSupportImpl()
    : iosurface_handle_(NULL),
      opengl_handle_(NULL),
      k_io_surface_width_(NULL),
      k_io_surface_height_(NULL),
      k_io_surface_bytes_per_element_(NULL),
      k_io_surface_is_global_(NULL),
      io_surface_create_(NULL),
      io_surface_get_id_(NULL),
      io_surface_lookup_(NULL),
      io_surface_create_mach_port_(NULL),
      io_surface_lookup_from_mach_port_(NULL),
      io_surface_get_width_(NULL),
      io_surface_get_height_(NULL),
      cgl_tex_image_io_surface_2d_(NULL),
      initialized_successfully_(false) {
  iosurface_handle_ = dlopen(
      "/System/Library/Frameworks/IOSurface.framework/IOSurface",
      RTLD_LAZY | RTLD_LOCAL);
  if (!iosurface_handle_)
    return;
  opengl_handle_ = dlopen(
      "/System/Library/Frameworks/OpenGL.framework/OpenGL",
      RTLD_LAZY | RTLD_LOCAL);
  if (!opengl_handle_) {
    dlclose(iosurface_handle_);
    iosurface_handle_ = NULL;
    return;
  }

  void* surface_width_ptr = dlsym(iosurface_handle_, "kIOSurfaceWidth");
  void* surface_height_ptr = dlsym(iosurface_handle_, "kIOSurfaceHeight");
  void* surface_bytes_per_element_ptr =
      dlsym(iosurface_handle_, "kIOSurfaceBytesPerElement");
  void* surface_is_global_ptr =
      dlsym(iosurface_handle_, "kIOSurfaceIsGlobal");
  void* surface_create_ptr = dlsym(iosurface_handle_, "IOSurfaceCreate");
  void* surface_get_id_ptr = dlsym(iosurface_handle_, "IOSurfaceGetID");
  void* surface_lookup_ptr = dlsym(iosurface_handle_, "IOSurfaceLookup");
  void* surface_create_mach_port_ptr =
      dlsym(iosurface_handle_, "IOSurfaceCreateMachPort");
  void* surface_lookup_from_mach_port_ptr =
      dlsym(iosurface_handle_, "IOSurfaceLookupFromMachPort");
  void* io_surface_get_width_ptr =
      dlsym(iosurface_handle_, "IOSurfaceGetWidth");
  void* io_surface_get_height_ptr =
      dlsym(iosurface_handle_, "IOSurfaceGetHeight");
  void* tex_image_io_surface_2d_ptr =
      dlsym(opengl_handle_, "CGLTexImageIOSurface2D");
  if (!surface_width_ptr ||
      !surface_height_ptr ||
      !surface_bytes_per_element_ptr ||
      !surface_is_global_ptr ||
      !surface_create_ptr ||
      !surface_get_id_ptr ||
      !surface_lookup_ptr ||
      !surface_create_mach_port_ptr ||
      !surface_lookup_from_mach_port_ptr ||
      !io_surface_get_width_ptr ||
      !io_surface_get_height_ptr ||
      !tex_image_io_surface_2d_ptr) {
    dlclose(iosurface_handle_);
    iosurface_handle_ = NULL;
    dlclose(opengl_handle_);
    opengl_handle_ = NULL;
    return;
  }

  k_io_surface_width_ = *static_cast<CFStringRef*>(surface_width_ptr);
  k_io_surface_height_ = *static_cast<CFStringRef*>(surface_height_ptr);
  k_io_surface_bytes_per_element_ =
      *static_cast<CFStringRef*>(surface_bytes_per_element_ptr);
  k_io_surface_is_global_ = *static_cast<CFStringRef*>(surface_is_global_ptr);
  io_surface_create_ = reinterpret_cast<IOSurfaceCreateProcPtr>(
      surface_create_ptr);
  io_surface_get_id_ =
      reinterpret_cast<IOSurfaceGetIDProcPtr>(surface_get_id_ptr);
  io_surface_lookup_ =
      reinterpret_cast<IOSurfaceLookupProcPtr>(surface_lookup_ptr);
  io_surface_create_mach_port_ =
      reinterpret_cast<IOSurfaceCreateMachPortProcPtr>(
          surface_create_mach_port_ptr);
  io_surface_lookup_from_mach_port_ =
      reinterpret_cast<IOSurfaceLookupFromMachPortProcPtr>(
          surface_lookup_from_mach_port_ptr);
  io_surface_get_width_ =
      reinterpret_cast<IOSurfaceGetWidthPtr>(
          io_surface_get_width_ptr);
  io_surface_get_height_ =
      reinterpret_cast<IOSurfaceGetHeightPtr>(
          io_surface_get_height_ptr);
  cgl_tex_image_io_surface_2d_ =
      reinterpret_cast<CGLTexImageIOSurface2DProcPtr>(
          tex_image_io_surface_2d_ptr);
  initialized_successfully_ = true;
}

IOSurfaceSupportImpl::~IOSurfaceSupportImpl() {
  if (iosurface_handle_)
    dlclose(iosurface_handle_);
  if (opengl_handle_)
    dlclose(opengl_handle_);
}

IOSurfaceSupport* IOSurfaceSupport::Initialize() {
  return IOSurfaceSupportImpl::GetInstance();
}

IOSurfaceSupport::IOSurfaceSupport() {
}

IOSurfaceSupport::~IOSurfaceSupport() {
}

