// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/command_buffer/service/renderbuffer_manager.h"

#include "gpu/command_buffer/common/gl_mock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace gpu {
namespace gles2 {

class RenderbufferManagerTest : public testing::Test {
 public:
  static const GLint kMaxSize = 128;
  static const GLint kMaxSamples = 4;

  RenderbufferManagerTest()
      : manager_(NULL, kMaxSize, kMaxSamples) {
  }
  ~RenderbufferManagerTest() {
    manager_.Destroy(false);
  }

 protected:
  virtual void SetUp() {
    gl_.reset(new ::testing::StrictMock<gfx::MockGLInterface>());
    ::gfx::GLInterface::SetGLInterface(gl_.get());
  }

  virtual void TearDown() {
    ::gfx::GLInterface::SetGLInterface(NULL);
    gl_.reset();
  }

  // Use StrictMock to make 100% sure we know how GL will be called.
  scoped_ptr< ::testing::StrictMock< ::gfx::MockGLInterface> > gl_;
  RenderbufferManager manager_;
};

// GCC requires these declarations, but MSVC requires they not be present
#ifndef COMPILER_MSVC
const GLint RenderbufferManagerTest::kMaxSize;
const GLint RenderbufferManagerTest::kMaxSamples;
#endif

TEST_F(RenderbufferManagerTest, Basic) {
  const GLuint kClient1Id = 1;
  const GLuint kService1Id = 11;
  const GLuint kClient2Id = 2;
  EXPECT_EQ(kMaxSize, manager_.max_renderbuffer_size());
  EXPECT_EQ(kMaxSamples, manager_.max_samples());
  EXPECT_FALSE(manager_.HaveUnclearedRenderbuffers());
  // Check we can create renderbuffer.
  manager_.CreateRenderbufferInfo(kClient1Id, kService1Id);
  // Check renderbuffer got created.
  RenderbufferManager::RenderbufferInfo* info1 =
      manager_.GetRenderbufferInfo(kClient1Id);
  ASSERT_TRUE(info1 != NULL);
  EXPECT_FALSE(manager_.HaveUnclearedRenderbuffers());
  GLuint client_id = 0;
  EXPECT_TRUE(manager_.GetClientId(info1->service_id(), &client_id));
  EXPECT_EQ(kClient1Id, client_id);
  // Check we get nothing for a non-existent renderbuffer.
  EXPECT_TRUE(manager_.GetRenderbufferInfo(kClient2Id) == NULL);
  // Check trying to a remove non-existent renderbuffers does not crash.
  manager_.RemoveRenderbufferInfo(kClient2Id);
  // Check that the renderbuffer is deleted when the last ref is released.
  EXPECT_CALL(*gl_, DeleteRenderbuffersEXT(1, ::testing::Pointee(kService1Id)))
      .Times(1)
      .RetiresOnSaturation();
  // Check we can't get the renderbuffer after we remove it.
  manager_.RemoveRenderbufferInfo(kClient1Id);
  EXPECT_TRUE(manager_.GetRenderbufferInfo(kClient1Id) == NULL);
  EXPECT_FALSE(manager_.HaveUnclearedRenderbuffers());
}

TEST_F(RenderbufferManagerTest, Destroy) {
  const GLuint kClient1Id = 1;
  const GLuint kService1Id = 11;
  // Check we can create renderbuffer.
  manager_.CreateRenderbufferInfo(kClient1Id, kService1Id);
  // Check renderbuffer got created.
  RenderbufferManager::RenderbufferInfo* info1 =
      manager_.GetRenderbufferInfo(kClient1Id);
  ASSERT_TRUE(info1 != NULL);
  EXPECT_CALL(*gl_, DeleteRenderbuffersEXT(1, ::testing::Pointee(kService1Id)))
      .Times(1)
      .RetiresOnSaturation();
  manager_.Destroy(true);
  info1 = manager_.GetRenderbufferInfo(kClient1Id);
  ASSERT_TRUE(info1 == NULL);
}

TEST_F(RenderbufferManagerTest, RenderbufferInfo) {
  const GLuint kClient1Id = 1;
  const GLuint kService1Id = 11;
  // Check we can create renderbuffer.
  manager_.CreateRenderbufferInfo(kClient1Id, kService1Id);
  // Check renderbuffer got created.
  RenderbufferManager::RenderbufferInfo* info1 =
      manager_.GetRenderbufferInfo(kClient1Id);
  ASSERT_TRUE(info1 != NULL);
  EXPECT_EQ(kService1Id, info1->service_id());
  EXPECT_EQ(0, info1->samples());
  EXPECT_EQ(static_cast<GLenum>(GL_RGBA4), info1->internal_format());
  EXPECT_EQ(0, info1->width());
  EXPECT_EQ(0, info1->height());
  EXPECT_TRUE(info1->cleared());
  EXPECT_EQ(0u, info1->EstimatedSize());

  // Check if we set the info it gets marked as not cleared.
  const GLsizei kSamples = 4;
  const GLenum kFormat = GL_RGBA4;
  const GLsizei kWidth = 128;
  const GLsizei kHeight = 64;
  manager_.SetInfo(info1, kSamples, kFormat, kWidth, kHeight);
  EXPECT_EQ(kSamples, info1->samples());
  EXPECT_EQ(kFormat, info1->internal_format());
  EXPECT_EQ(kWidth, info1->width());
  EXPECT_EQ(kHeight, info1->height());
  EXPECT_FALSE(info1->cleared());
  EXPECT_FALSE(info1->IsDeleted());
  EXPECT_TRUE(manager_.HaveUnclearedRenderbuffers());
  EXPECT_EQ(kWidth * kHeight * 4u * 2u, info1->EstimatedSize());

  manager_.SetCleared(info1);
  EXPECT_TRUE(info1->cleared());
  EXPECT_FALSE(manager_.HaveUnclearedRenderbuffers());

  manager_.SetInfo(info1, kSamples, kFormat, kWidth, kHeight);
  EXPECT_TRUE(manager_.HaveUnclearedRenderbuffers());

  // Check that the renderbuffer is deleted when the last ref is released.
  EXPECT_CALL(*gl_, DeleteRenderbuffersEXT(1, ::testing::Pointee(kService1Id)))
      .Times(1)
      .RetiresOnSaturation();
  manager_.RemoveRenderbufferInfo(kClient1Id);
  EXPECT_FALSE(manager_.HaveUnclearedRenderbuffers());
}

TEST_F(RenderbufferManagerTest, UseDeletedRenderbufferInfo) {
  const GLuint kClient1Id = 1;
  const GLuint kService1Id = 11;
  manager_.CreateRenderbufferInfo(kClient1Id, kService1Id);
  RenderbufferManager::RenderbufferInfo::Ref info1(
      manager_.GetRenderbufferInfo(kClient1Id));
  ASSERT_TRUE(info1 != NULL);
  // Remove it.
  manager_.RemoveRenderbufferInfo(kClient1Id);
  // Use after removing.
  const GLsizei kSamples = 4;
  const GLenum kFormat = GL_RGBA4;
  const GLsizei kWidth = 128;
  const GLsizei kHeight = 64;
  manager_.SetInfo(info1, kSamples, kFormat, kWidth, kHeight);
  // See that it still affects manager.
  EXPECT_TRUE(manager_.HaveUnclearedRenderbuffers());
  manager_.SetCleared(info1);
  EXPECT_FALSE(manager_.HaveUnclearedRenderbuffers());
  // Check that the renderbuffer is deleted when the last ref is released.
  EXPECT_CALL(*gl_, DeleteRenderbuffersEXT(1, ::testing::Pointee(kService1Id)))
      .Times(1)
      .RetiresOnSaturation();
  info1 = NULL;
}

}  // namespace gles2
}  // namespace gpu


