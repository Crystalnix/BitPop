// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/command_buffer/service/texture_definition.h"

namespace gpu {
namespace gles2 {

TextureDefinition::LevelInfo::LevelInfo(GLenum target,
                                        GLenum internal_format,
                                        GLsizei width,
                                        GLsizei height,
                                        GLsizei depth,
                                        GLint border,
                                        GLenum format,
                                        GLenum type,
                                        bool cleared)
    : target(target),
      internal_format(internal_format),
      width(width),
      height(height),
      depth(depth),
      border(border),
      format(format),
      type(type),
      cleared(cleared) {
}

TextureDefinition::TextureDefinition(GLenum target,
                                     GLuint service_id,
                                     const LevelInfos& level_infos)
    : target_(target),
      service_id_(service_id),
      level_infos_(level_infos) {
}

TextureDefinition::~TextureDefinition() {
  DCHECK_EQ(0U, service_id_) << "TextureDefinition leaked texture.";
}

GLuint TextureDefinition::ReleaseServiceId() {
  GLuint service_id = service_id_;
  service_id_ = 0;
  return service_id;
}

}  // namespace gles2
}  // namespace gpu
