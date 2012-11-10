// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/command_buffer/service/gles2_cmd_copy_texture_chromium.h"

#include "base/basictypes.h"
#include "gpu/command_buffer/common/types.h"
#include "gpu/command_buffer/service/gl_utils.h"

#define SHADER0(Src) \
    "#ifdef GL_ES\n"\
    "precision mediump float;\n"\
    "#endif\n"\
    #Src
#define SHADER(Src) SHADER0(Src)

namespace {

const GLfloat kQuadVertices[] = { -1.0f, -1.0f, 0.0f, 1.0f,
                                   1.0f, -1.0f, 0.0f, 1.0f,
                                   1.0f,  1.0f, 0.0f, 1.0f,
                                  -1.0f,  1.0f, 0.0f, 1.0f };

const GLfloat kTextureCoords[] = { 0.0f, 0.0f,
                                   1.0f, 0.0f,
                                   1.0f, 1.0f,
                                   0.0f, 1.0f };

const int kNumShaders = 7;
enum ShaderId {
  VERTEX_SHADER_POS_TEX,
  FRAGMENT_SHADER_TEX,
  FRAGMENT_SHADER_TEX_FLIP_Y,
  FRAGMENT_SHADER_TEX_PREMULTIPLY_ALPHA,
  FRAGMENT_SHADER_TEX_UNPREMULTIPLY_ALPHA,
  FRAGMENT_SHADER_TEX_PREMULTIPLY_ALPHA_FLIP_Y,
  FRAGMENT_SHADER_TEX_UNPREMULTIPLY_ALPHA_FLIP_Y
};

enum ProgramId {
  PROGRAM_COPY_TEXTURE,
  PROGRAM_COPY_TEXTURE_FLIP_Y,
  PROGRAM_COPY_TEXTURE_PREMULTIPLY_ALPHA,
  PROGRAM_COPY_TEXTURE_UNPREMULTIPLY_ALPHA,
  PROGRAM_COPY_TEXTURE_PREMULTIPLY_ALPHA_FLIPY,
  PROGRAM_COPY_TEXTURE_UNPREMULTIPLY_ALPHA_FLIPY
};

// Returns the correct program to evaluate the copy operation for
// the CHROMIUM_flipy and premultiply alpha pixel store settings.
ProgramId GetProgram(bool flip_y, bool premultiply_alpha,
                     bool unpremultiply_alpha) {
  // If both pre-multiply and unpremultiply are requested, then perform no
  // alpha manipulation.
  if (premultiply_alpha && unpremultiply_alpha) {
    premultiply_alpha = false;
    unpremultiply_alpha = false;
  }

  if (flip_y && premultiply_alpha)
    return PROGRAM_COPY_TEXTURE_PREMULTIPLY_ALPHA_FLIPY;

  if (flip_y && unpremultiply_alpha)
    return PROGRAM_COPY_TEXTURE_UNPREMULTIPLY_ALPHA_FLIPY;

  if (flip_y)
    return PROGRAM_COPY_TEXTURE_FLIP_Y;

  if (premultiply_alpha)
    return PROGRAM_COPY_TEXTURE_PREMULTIPLY_ALPHA;

  if (unpremultiply_alpha)
    return PROGRAM_COPY_TEXTURE_UNPREMULTIPLY_ALPHA;

  return PROGRAM_COPY_TEXTURE;
}

const char* GetShaderSource(ShaderId shader) {
  switch (shader) {
    case VERTEX_SHADER_POS_TEX:
      return SHADER(
        attribute vec4 a_position;
        attribute vec2 a_texCoord;
        varying vec2 v_uv;
        void main(void) {
          gl_Position = a_position;
          v_uv = a_texCoord;
        });
    case FRAGMENT_SHADER_TEX:
      return SHADER(
        uniform sampler2D u_texSampler;
        varying vec2 v_uv;
        void main(void) {
          gl_FragColor = texture2D(u_texSampler, v_uv.st);
        });
    case FRAGMENT_SHADER_TEX_FLIP_Y:
      return SHADER(
        uniform sampler2D u_texSampler;
        varying vec2 v_uv;
        void main(void) {
          gl_FragColor = texture2D(u_texSampler, vec2(v_uv.s, 1.0 - v_uv.t));
        });
    case FRAGMENT_SHADER_TEX_PREMULTIPLY_ALPHA:
      return SHADER(
        uniform sampler2D u_texSampler;
        varying vec2 v_uv;
        void main(void) {
          gl_FragColor = texture2D(u_texSampler, v_uv.st);
          gl_FragColor.rgb *= gl_FragColor.a;
        });
    case FRAGMENT_SHADER_TEX_UNPREMULTIPLY_ALPHA:
      return SHADER(
        uniform sampler2D u_texSampler;
        varying vec2 v_uv;
        void main(void) {
          gl_FragColor = texture2D(u_texSampler, v_uv.st);
          if (gl_FragColor.a > 0.0)
            gl_FragColor.rgb /= gl_FragColor.a;
        });
    case FRAGMENT_SHADER_TEX_PREMULTIPLY_ALPHA_FLIP_Y:
      return SHADER(
        uniform sampler2D u_texSampler;
        varying vec2 v_uv;
        void main(void) {
          gl_FragColor = texture2D(u_texSampler, vec2(v_uv.s, 1.0 - v_uv.t));
          gl_FragColor.rgb *= gl_FragColor.a;
        });
    case FRAGMENT_SHADER_TEX_UNPREMULTIPLY_ALPHA_FLIP_Y:
      return SHADER(
        uniform sampler2D u_texSampler;
        varying vec2 v_uv;
        void main(void) {
          gl_FragColor = texture2D(u_texSampler, vec2(v_uv.s, 1.0 - v_uv.t));
          if (gl_FragColor.a > 0.0)
            gl_FragColor.rgb /= gl_FragColor.a;
        });
    default:
      return 0;
  }
}

}  // namespace

void CopyTextureCHROMIUMResourceManager::Initialize() {
  COMPILE_ASSERT(
      kVertexPositionAttrib == 0u || kVertexTextureAttrib == 0u,
      CopyTexture_One_of_these_attribs_must_be_0);

  // Initialize all of the GPU resources required to perform the copy.
  glGenBuffersARB(2, buffer_ids_);
  glBindBuffer(GL_ARRAY_BUFFER, buffer_ids_[0]);
  glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVertices), kQuadVertices,
               GL_STATIC_DRAW);

  glBindBuffer(GL_ARRAY_BUFFER, buffer_ids_[1]);
  glBufferData(GL_ARRAY_BUFFER, sizeof(kTextureCoords), kTextureCoords,
               GL_STATIC_DRAW);

  glGenFramebuffersEXT(1, &framebuffer_);

  GLuint shaders[kNumShaders];
  for (int shader = 0; shader < kNumShaders; ++shader) {
    shaders[shader] = glCreateShader(
        shader == 0 ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER);
    const char* shader_source = GetShaderSource(static_cast<ShaderId>(shader));
    glShaderSource(shaders[shader], 1, &shader_source, 0);
    glCompileShader(shaders[shader]);
#ifndef NDEBUG
    GLint compile_status;
    glGetShaderiv(shaders[shader], GL_COMPILE_STATUS, &compile_status);
    if (GL_TRUE != compile_status)
      DLOG(ERROR) << "CopyTextureCHROMIUM: shader compilation failure.";
#endif
  }

  for (int program = 0; program < kNumPrograms; ++program) {
    programs_[program] = glCreateProgram();
    glAttachShader(programs_[program], shaders[0]);
    glAttachShader(programs_[program], shaders[program + 1]);

    glBindAttribLocation(programs_[program], kVertexPositionAttrib,
                         "a_position");
    glBindAttribLocation(programs_[program], kVertexTextureAttrib,
                         "a_texCoord");

    glLinkProgram(programs_[program]);
#ifndef NDEBUG
    GLint linked;
    glGetProgramiv(programs_[program], GL_LINK_STATUS, &linked);
    if (!linked)
      DLOG(ERROR) << "CopyTextureCHROMIUM: program link failure.";
#endif

    sampler_locations_[program] = glGetUniformLocation(programs_[program],
                                                      "u_texSampler");
  }

  for (int shader = 0; shader < kNumShaders; ++shader)
    glDeleteShader(shaders[shader]);

  initialized_ = true;
}

void CopyTextureCHROMIUMResourceManager::Destroy() {
  if (!initialized_)
    return;

  glDeleteFramebuffersEXT(1, &framebuffer_);

  for (int program = 0; program < kNumPrograms; ++program)
    glDeleteProgram(programs_[program]);

  glDeleteBuffersARB(2, buffer_ids_);
}

void CopyTextureCHROMIUMResourceManager::DoCopyTexture(
    GLenum target,
    GLuint source_id,
    GLuint dest_id,
    GLint level,
    bool flip_y,
    bool premultiply_alpha,
    bool unpremultiply_alpha) {
  if (!initialized_) {
    DLOG(ERROR) << "CopyTextureCHROMIUM: Uninitialized manager.";
    return;
  }

  GLuint program = GetProgram(flip_y, premultiply_alpha, unpremultiply_alpha);
  glUseProgram(programs_[program]);

#ifndef NDEBUG
  glValidateProgram(programs_[program]);
  GLint validation_status;
  glGetProgramiv(programs_[program], GL_VALIDATE_STATUS, &validation_status);
  if (GL_TRUE != validation_status) {
    DLOG(ERROR) << "CopyTextureCHROMIUM: Invalid shader.";
    return;
  }
#endif

  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, framebuffer_);
  glFramebufferTexture2DEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, target,
                            dest_id, level);

#ifndef NDEBUG
  GLenum fb_status = glCheckFramebufferStatusEXT(GL_FRAMEBUFFER);
  if (GL_FRAMEBUFFER_COMPLETE != fb_status) {
    DLOG(ERROR) << "CopyTextureCHROMIUM: Incomplete framebuffer.";
    return;
  }
#endif

  glEnableVertexAttribArray(kVertexPositionAttrib);
  glEnableVertexAttribArray(kVertexTextureAttrib);

  glBindBuffer(GL_ARRAY_BUFFER, buffer_ids_[0]);
  glVertexAttribPointer(kVertexPositionAttrib, 4, GL_FLOAT, GL_FALSE,
                        4 * sizeof(GLfloat), 0);

  glBindBuffer(GL_ARRAY_BUFFER, buffer_ids_[1]);
  glVertexAttribPointer(kVertexTextureAttrib,  2, GL_FLOAT, GL_FALSE,
                        2 * sizeof(GLfloat), 0);

  glActiveTexture(GL_TEXTURE0);
  glUniform1i(sampler_locations_[program], 0);

  glBindTexture(GL_TEXTURE_2D, source_id);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_STENCIL_TEST);
  glDisable(GL_CULL_FACE);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glDepthMask(GL_FALSE);
  glDisable(GL_BLEND);

  glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}

