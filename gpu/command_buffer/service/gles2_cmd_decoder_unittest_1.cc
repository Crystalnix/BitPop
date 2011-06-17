// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/command_buffer/service/gles2_cmd_decoder.h"

#include "gpu/command_buffer/common/gl_mock.h"
#include "gpu/command_buffer/common/gles2_cmd_format.h"
#include "gpu/command_buffer/common/gles2_cmd_utils.h"
#include "gpu/command_buffer/service/gles2_cmd_decoder_unittest_base.h"
#include "gpu/command_buffer/service/cmd_buffer_engine.h"
#include "gpu/command_buffer/service/context_group.h"
#include "gpu/command_buffer/service/program_manager.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::gfx::MockGLInterface;
using ::testing::_;
using ::testing::DoAll;
using ::testing::InSequence;
using ::testing::MatcherCast;
using ::testing::Pointee;
using ::testing::Return;
using ::testing::SetArrayArgument;
using ::testing::SetArgumentPointee;
using ::testing::StrEq;

namespace gpu {
namespace gles2 {

class GLES2DecoderTest1 : public GLES2DecoderTestBase {
 public:
  GLES2DecoderTest1() { }
};

template <>
void GLES2DecoderTestBase::SpecializedSetup<GenerateMipmap, 0>(
    bool valid) {
  DoBindTexture(GL_TEXTURE_2D, client_texture_id_, kServiceTextureId);
  DoTexImage2D(
      GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE,
      0, 0);
  if (valid) {
    EXPECT_CALL(*gl_, TexParameteri(
        GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR))
        .Times(1)
        .RetiresOnSaturation();
    EXPECT_CALL(*gl_, TexParameteri(
        GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR))
        .Times(1)
        .RetiresOnSaturation();
  }
};

template <>
void GLES2DecoderTestBase::SpecializedSetup<CheckFramebufferStatus, 0>(
    bool /* valid */) {
  DoBindFramebuffer(GL_FRAMEBUFFER, client_framebuffer_id_,
                    kServiceFramebufferId);
};

template <>
void GLES2DecoderTestBase::SpecializedSetup<CopyTexImage2D, 0>(
    bool valid) {
  if (valid) {
    EXPECT_CALL(*gl_, GetError())
        .WillOnce(Return(GL_NO_ERROR))
        .WillOnce(Return(GL_NO_ERROR))
        .RetiresOnSaturation();
  }
};

template <>
void GLES2DecoderTestBase::SpecializedSetup<CopyTexSubImage2D, 0>(bool valid) {
  if (valid) {
    DoBindTexture(GL_TEXTURE_2D, client_texture_id_, kServiceTextureId);
    DoTexImage2D(
        GL_TEXTURE_2D, 2, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE,
        0, 0);
  }
};

template <>
void GLES2DecoderTestBase::SpecializedSetup<DetachShader, 0>(bool valid) {
  if (valid) {
    EXPECT_CALL(*gl_,
                AttachShader(kServiceProgramId, kServiceShaderId))
        .Times(1)
        .RetiresOnSaturation();
    AttachShader attach_cmd;
    attach_cmd.Init(client_program_id_, client_shader_id_);
    EXPECT_EQ(error::kNoError, ExecuteCmd(attach_cmd));
  }
};

template <>
void GLES2DecoderTestBase::SpecializedSetup<FramebufferRenderbuffer, 0>(
    bool valid) {
  DoBindFramebuffer(GL_FRAMEBUFFER, client_framebuffer_id_,
                    kServiceFramebufferId);
  if (valid) {
    EXPECT_CALL(*gl_, GetError())
        .WillOnce(Return(GL_NO_ERROR))
        .RetiresOnSaturation();
    // Return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT so the code
    // doesn't try to clear the buffer. That is tested else where.
    EXPECT_CALL(*gl_, CheckFramebufferStatusEXT(GL_FRAMEBUFFER))
        .WillOnce(Return(GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT))
        .RetiresOnSaturation();
    EXPECT_CALL(*gl_, GetError())
        .WillOnce(Return(GL_NO_ERROR))
        .RetiresOnSaturation();
  }
};

template <>
void GLES2DecoderTestBase::SpecializedSetup<FramebufferTexture2D, 0>(
    bool valid) {
  DoBindFramebuffer(GL_FRAMEBUFFER, client_framebuffer_id_,
                    kServiceFramebufferId);
  if (valid) {
    EXPECT_CALL(*gl_, GetError())
        .WillOnce(Return(GL_NO_ERROR))
        .RetiresOnSaturation();
    // Return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT so the code
    // doesn't try to clear the buffer. That is tested else where.
    EXPECT_CALL(*gl_, CheckFramebufferStatusEXT(GL_FRAMEBUFFER))
        .WillOnce(Return(GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT))
        .RetiresOnSaturation();
    EXPECT_CALL(*gl_, GetError())
        .WillOnce(Return(GL_NO_ERROR))
        .RetiresOnSaturation();
  }
};

template <>
void GLES2DecoderTestBase::SpecializedSetup<GetFramebufferAttachmentParameteriv,
                                            0>(bool /* valid */) {
  DoBindFramebuffer(GL_FRAMEBUFFER, client_framebuffer_id_,
                    kServiceFramebufferId);
};

template <>
void GLES2DecoderTestBase::SpecializedSetup<GetRenderbufferParameteriv, 0>(
    bool /* valid */) {
  DoBindRenderbuffer(GL_RENDERBUFFER, client_renderbuffer_id_,
                    kServiceRenderbufferId);
};

template <>
void GLES2DecoderTestBase::SpecializedSetup<GetProgramInfoLog, 0>(
    bool /* valid */) {
  ProgramManager::ProgramInfo* info = GetProgramInfo(client_program_id_);
  info->set_log_info("hello");
};

template <>
void GLES2DecoderTestBase::SpecializedSetup<GetVertexAttribfv, 0>(bool valid) {
  DoBindBuffer(GL_ARRAY_BUFFER, client_buffer_id_, kServiceBufferId);
  DoVertexAttribPointer(1, 1, GL_FLOAT, 0, 0);
  if (valid) {
    EXPECT_CALL(*gl_, GetError())
        .WillOnce(Return(GL_NO_ERROR))
        .WillOnce(Return(GL_NO_ERROR))
        .RetiresOnSaturation();
  }
};

#include "gpu/command_buffer/service/gles2_cmd_decoder_unittest_1_autogen.h"

}  // namespace gles2
}  // namespace gpu

