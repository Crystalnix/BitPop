// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/gpu/gpu_info_collector.h"

#include <string>
#include <vector>

#include "base/logging.h"
#include "base/string_number_conversions.h"
#include "base/string_piece.h"
#include "base/string_split.h"
#include "ui/gfx/gl/gl_bindings.h"
#include "ui/gfx/gl/gl_context.h"

namespace {

// This creates an offscreen GL context for gl queries.  Returned GLContext
// should be deleted in FinalizeGLContext.
gfx::GLContext* InitializeGLContext() {
  if (!gfx::GLContext::InitializeOneOff()) {
    LOG(ERROR) << "gfx::GLContext::InitializeOneOff() failed";
    return NULL;
  }
  gfx::GLContext* context = gfx::GLContext::CreateOffscreenGLContext(NULL);
  if (context == NULL) {
    LOG(ERROR) << "gfx::GLContext::CreateOffscreenGLContext(NULL) failed";
    return NULL;
  }
  if (!context->MakeCurrent()) {
    LOG(ERROR) << "gfx::GLContext::MakeCurrent() failed";
    context->Destroy();
    delete context;
    return NULL;
  }
  return context;
}

// This destroy and delete the GL context.
void FinalizeGLContext(gfx::GLContext** context) {
  DCHECK(context);
  if (*context) {
    (*context)->Destroy();
    delete *context;
    *context = NULL;
  }
}

std::string GetGLString(unsigned int pname) {
  const char* gl_string =
      reinterpret_cast<const char*>(glGetString(pname));
  if (gl_string)
    return std::string(gl_string);
  return "";
}

// Return a version string in the format of "major.minor".
std::string GetVersionFromString(const std::string& version_string) {
  size_t begin = version_string.find_first_of("0123456789");
  if (begin != std::string::npos) {
    size_t end = version_string.find_first_not_of("01234567890.", begin);
    std::string sub_string;
    if (end != std::string::npos)
      sub_string = version_string.substr(begin, end - begin);
    else
      sub_string = version_string.substr(begin);
    std::vector<std::string> pieces;
    base::SplitString(sub_string, '.', &pieces);
    if (pieces.size() >= 2)
      return pieces[0] + "." + pieces[1];
  }
  return "";
}

}  // namespace anonymous

namespace gpu_info_collector {

bool CollectGraphicsInfoGL(GPUInfo* gpu_info) {
  DCHECK(gpu_info);

  gfx::GLContext* context = InitializeGLContext();
  if (context == NULL)
    return false;

  gpu_info->gl_renderer = GetGLString(GL_RENDERER);
  gpu_info->gl_vendor = GetGLString(GL_VENDOR);
  gpu_info->gl_version_string =GetGLString(GL_VERSION);
  gpu_info->gl_extensions =GetGLString(GL_EXTENSIONS);

  bool validGLVersionInfo = CollectGLVersionInfo(gpu_info);
  bool validVideoCardInfo = CollectVideoCardInfo(gpu_info);
  bool validDriverInfo = CollectDriverInfoGL(gpu_info);

  FinalizeGLContext(&context);

  return (validGLVersionInfo && validVideoCardInfo && validDriverInfo);
}

bool CollectGLVersionInfo(GPUInfo* gpu_info) {
  DCHECK(gpu_info);

  std::string gl_version_string = gpu_info->gl_version_string;
  std::string glsl_version_string =
      GetGLString(GL_SHADING_LANGUAGE_VERSION);

  gpu_info->gl_version = GetVersionFromString(gl_version_string);

  std::string glsl_version = GetVersionFromString(glsl_version_string);
  gpu_info->pixel_shader_version = glsl_version;
  gpu_info->vertex_shader_version = glsl_version;

  return true;
}

}  // namespace gpu_info_collector

