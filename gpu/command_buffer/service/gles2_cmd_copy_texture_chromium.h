// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_COMMAND_BUFFER_SERVICE_GLES2_CMD_COPY_TEXTURE_CHROMIUM_H_
#define GPU_COMMAND_BUFFER_SERVICE_GLES2_CMD_COPY_TEXTURE_CHROMIUM_H_

#include "gpu/command_buffer/service/gl_utils.h"

// This class encapsulates the resources required to implement the
// GL_CHROMIUM_copy_texture extension.  The copy operation is performed
// via a blit to a framebuffer object.
class CopyTextureCHROMIUMResourceManager {
 public:
  CopyTextureCHROMIUMResourceManager() : initialized_(false) {}

  void Initialize();
  void Destroy();

  void DoCopyTexture(GLenum target, GLuint source_id, GLuint dest_id,
                     GLint level, bool flip_y, bool premultiply_alpha,
                     bool unpremultiply_alpha);

  // The attributes used during invocation of the extension.
  static const GLuint kVertexPositionAttrib = 0;
  static const GLuint kVertexTextureAttrib = 1;

 private:
  bool initialized_;

  static const int kNumPrograms = 6;
  GLuint programs_[kNumPrograms];
  GLuint buffer_ids_[2];
  GLuint framebuffer_;
  GLuint sampler_locations_[kNumPrograms];

  DISALLOW_COPY_AND_ASSIGN(CopyTextureCHROMIUMResourceManager);
};

#endif  // GPU_COMMAND_BUFFER_SERVICE_GLES2_CMD_COPY_TEXTURE_CHROMIUM_H_


