// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/command_buffer/service/buffer_manager.h"
#include "gpu/command_buffer/common/gl_mock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace gpu {
namespace gles2 {

class BufferManagerTest : public testing::Test {
 public:
  BufferManagerTest() : manager_(NULL) {
  }
  ~BufferManagerTest() {
    manager_.Destroy(false);
  }

 protected:
  virtual void SetUp() {
    gl_.reset(new ::testing::StrictMock< ::gfx::MockGLInterface>());
    ::gfx::GLInterface::SetGLInterface(gl_.get());
  }

  virtual void TearDown() {
    ::gfx::GLInterface::SetGLInterface(NULL);
    gl_.reset();
  }

  GLenum GetTarget(const BufferManager::BufferInfo* info) const {
    return info->target();
  }

  // Use StrictMock to make 100% sure we know how GL will be called.
  scoped_ptr< ::testing::StrictMock< ::gfx::MockGLInterface> > gl_;
  BufferManager manager_;
};

TEST_F(BufferManagerTest, Basic) {
  const GLuint kClientBuffer1Id = 1;
  const GLuint kServiceBuffer1Id = 11;
  const GLsizeiptr kBuffer1Size = 123;
  const GLuint kClientBuffer2Id = 2;
  // Check we can create buffer.
  manager_.CreateBufferInfo(kClientBuffer1Id, kServiceBuffer1Id);
  // Check buffer got created.
  BufferManager::BufferInfo* info1 = manager_.GetBufferInfo(kClientBuffer1Id);
  ASSERT_TRUE(info1 != NULL);
  EXPECT_EQ(0u, GetTarget(info1));
  EXPECT_EQ(0, info1->size());
  EXPECT_EQ(static_cast<GLenum>(GL_STATIC_DRAW), info1->usage());
  EXPECT_FALSE(info1->IsDeleted());
  EXPECT_EQ(kServiceBuffer1Id, info1->service_id());
  GLuint client_id = 0;
  EXPECT_TRUE(manager_.GetClientId(info1->service_id(), &client_id));
  EXPECT_EQ(kClientBuffer1Id, client_id);
  manager_.SetTarget(info1, GL_ELEMENT_ARRAY_BUFFER);
  EXPECT_EQ(static_cast<GLenum>(GL_ELEMENT_ARRAY_BUFFER), GetTarget(info1));
  // Check we and set its size.
  manager_.SetInfo(info1, kBuffer1Size, GL_DYNAMIC_DRAW);
  EXPECT_EQ(kBuffer1Size, info1->size());
  EXPECT_EQ(static_cast<GLenum>(GL_DYNAMIC_DRAW), info1->usage());
  // Check we get nothing for a non-existent buffer.
  EXPECT_TRUE(manager_.GetBufferInfo(kClientBuffer2Id) == NULL);
  // Check trying to a remove non-existent buffers does not crash.
  manager_.RemoveBufferInfo(kClientBuffer2Id);
  // Check that it gets deleted when the last reference is released.
  EXPECT_CALL(*gl_, DeleteBuffersARB(1, ::testing::Pointee(kServiceBuffer1Id)))
      .Times(1)
      .RetiresOnSaturation();
  // Check we can't get the buffer after we remove it.
  manager_.RemoveBufferInfo(kClientBuffer1Id);
  EXPECT_TRUE(manager_.GetBufferInfo(kClientBuffer1Id) == NULL);
}

TEST_F(BufferManagerTest, Destroy) {
  const GLuint kClient1Id = 1;
  const GLuint kService1Id = 11;
  // Check we can create buffer.
  manager_.CreateBufferInfo(kClient1Id, kService1Id);
  // Check buffer got created.
  BufferManager::BufferInfo* info1 =
      manager_.GetBufferInfo(kClient1Id);
  ASSERT_TRUE(info1 != NULL);
  EXPECT_CALL(*gl_, DeleteBuffersARB(1, ::testing::Pointee(kService1Id)))
      .Times(1)
      .RetiresOnSaturation();
  manager_.Destroy(true);
  // Check the resources were released.
  info1 = manager_.GetBufferInfo(kClient1Id);
  ASSERT_TRUE(info1 == NULL);
}

TEST_F(BufferManagerTest, SetRange) {
  const GLuint kClientBufferId = 1;
  const GLuint kServiceBufferId = 11;
  const uint8 data[] = {10, 9, 8, 7, 6, 5, 4, 3, 2, 1};
  manager_.CreateBufferInfo(kClientBufferId, kServiceBufferId);
  BufferManager::BufferInfo* info = manager_.GetBufferInfo(kClientBufferId);
  ASSERT_TRUE(info != NULL);
  manager_.SetTarget(info, GL_ELEMENT_ARRAY_BUFFER);
  manager_.SetInfo(info, sizeof(data), GL_STATIC_DRAW);
  EXPECT_TRUE(info->SetRange(0, sizeof(data), data));
  EXPECT_TRUE(info->SetRange(sizeof(data), 0, data));
  EXPECT_FALSE(info->SetRange(sizeof(data), 1, data));
  EXPECT_FALSE(info->SetRange(0, sizeof(data) + 1, data));
  EXPECT_FALSE(info->SetRange(-1, sizeof(data), data));
  EXPECT_FALSE(info->SetRange(0, -1, data));
  manager_.SetInfo(info, 1, GL_STATIC_DRAW);
  const int size = 0x20000;
  scoped_array<uint8> temp(new uint8[size]);
  EXPECT_FALSE(info->SetRange(0 - size, size, temp.get()));
  EXPECT_FALSE(info->SetRange(1, size / 2, temp.get()));
}

TEST_F(BufferManagerTest, GetRange) {
  const GLuint kClientBufferId = 1;
  const GLuint kServiceBufferId = 11;
  const uint8 data[] = {10, 9, 8, 7, 6, 5, 4, 3, 2, 1};
  manager_.CreateBufferInfo(kClientBufferId, kServiceBufferId);
  BufferManager::BufferInfo* info = manager_.GetBufferInfo(kClientBufferId);
  ASSERT_TRUE(info != NULL);
  manager_.SetTarget(info, GL_ELEMENT_ARRAY_BUFFER);
  manager_.SetInfo(info, sizeof(data), GL_STATIC_DRAW);
  const char* buf = static_cast<const char*>(info->GetRange(0, sizeof(data)));
  ASSERT_TRUE(buf != NULL);
  const char* buf1 =
      static_cast<const char*>(info->GetRange(1, sizeof(data) - 1));
  EXPECT_EQ(buf + 1, buf1);
  EXPECT_TRUE(info->GetRange(sizeof(data), 1) == NULL);
  EXPECT_TRUE(info->GetRange(0, sizeof(data) + 1) == NULL);
  EXPECT_TRUE(info->GetRange(-1, sizeof(data)) == NULL);
  EXPECT_TRUE(info->GetRange(-0, -1) == NULL);
  const int size = 0x20000;
  manager_.SetInfo(info, size / 2, GL_STATIC_DRAW);
  EXPECT_TRUE(info->GetRange(0 - size, size) == NULL);
  EXPECT_TRUE(info->GetRange(1, size / 2) == NULL);
}

TEST_F(BufferManagerTest, GetMaxValueForRangeUint8) {
  const GLuint kClientBufferId = 1;
  const GLuint kServiceBufferId = 11;
  const uint8 data[] = {10, 9, 8, 7, 6, 5, 4, 3, 2, 1};
  const uint8 new_data[] = {100, 120, 110};
  manager_.CreateBufferInfo(kClientBufferId, kServiceBufferId);
  BufferManager::BufferInfo* info = manager_.GetBufferInfo(kClientBufferId);
  ASSERT_TRUE(info != NULL);
  manager_.SetTarget(info, GL_ELEMENT_ARRAY_BUFFER);
  manager_.SetInfo(info, sizeof(data), GL_STATIC_DRAW);
  EXPECT_TRUE(info->SetRange(0, sizeof(data), data));
  GLuint max_value;
  // Check entire range succeeds.
  EXPECT_TRUE(info->GetMaxValueForRange(0, 10, GL_UNSIGNED_BYTE, &max_value));
  EXPECT_EQ(10u, max_value);
  // Check sub range succeeds.
  EXPECT_TRUE(info->GetMaxValueForRange(4, 3, GL_UNSIGNED_BYTE, &max_value));
  EXPECT_EQ(6u, max_value);
  // Check changing sub range succeeds.
  EXPECT_TRUE(info->SetRange(4, sizeof(new_data), new_data));
  EXPECT_TRUE(info->GetMaxValueForRange(4, 3, GL_UNSIGNED_BYTE, &max_value));
  EXPECT_EQ(120u, max_value);
  max_value = 0;
  EXPECT_TRUE(info->GetMaxValueForRange(0, 10, GL_UNSIGNED_BYTE, &max_value));
  EXPECT_EQ(120u, max_value);
  // Check out of range fails.
  EXPECT_FALSE(info->GetMaxValueForRange(0, 11, GL_UNSIGNED_BYTE, &max_value));
  EXPECT_FALSE(info->GetMaxValueForRange(10, 1, GL_UNSIGNED_BYTE, &max_value));
}

TEST_F(BufferManagerTest, GetMaxValueForRangeUint16) {
  const GLuint kClientBufferId = 1;
  const GLuint kServiceBufferId = 11;
  const uint16 data[] = {10, 9, 8, 7, 6, 5, 4, 3, 2, 1};
  const uint16 new_data[] = {100, 120, 110};
  manager_.CreateBufferInfo(kClientBufferId, kServiceBufferId);
  BufferManager::BufferInfo* info = manager_.GetBufferInfo(kClientBufferId);
  ASSERT_TRUE(info != NULL);
  manager_.SetTarget(info, GL_ELEMENT_ARRAY_BUFFER);
  manager_.SetInfo(info, sizeof(data), GL_STATIC_DRAW);
  EXPECT_TRUE(info->SetRange(0, sizeof(data), data));
  GLuint max_value;
  // Check entire range succeeds.
  EXPECT_TRUE(info->GetMaxValueForRange(0, 10, GL_UNSIGNED_SHORT, &max_value));
  EXPECT_EQ(10u, max_value);
  // Check odd offset fails for GL_UNSIGNED_SHORT.
  EXPECT_FALSE(info->GetMaxValueForRange(1, 10, GL_UNSIGNED_SHORT, &max_value));
  // Check sub range succeeds.
  EXPECT_TRUE(info->GetMaxValueForRange(8, 3, GL_UNSIGNED_SHORT, &max_value));
  EXPECT_EQ(6u, max_value);
  // Check changing sub range succeeds.
  EXPECT_TRUE(info->SetRange(8, sizeof(new_data), new_data));
  EXPECT_TRUE(info->GetMaxValueForRange(8, 3, GL_UNSIGNED_SHORT, &max_value));
  EXPECT_EQ(120u, max_value);
  max_value = 0;
  EXPECT_TRUE(info->GetMaxValueForRange(0, 10, GL_UNSIGNED_SHORT, &max_value));
  EXPECT_EQ(120u, max_value);
  // Check out of range fails.
  EXPECT_FALSE(info->GetMaxValueForRange(0, 11, GL_UNSIGNED_SHORT, &max_value));
  EXPECT_FALSE(info->GetMaxValueForRange(20, 1, GL_UNSIGNED_SHORT, &max_value));
}

TEST_F(BufferManagerTest, GetMaxValueForRangeUint32) {
  const GLuint kClientBufferId = 1;
  const GLuint kServiceBufferId = 11;
  const uint32 data[] = {10, 9, 8, 7, 6, 5, 4, 3, 2, 1};
  const uint32 new_data[] = {100, 120, 110};
  manager_.CreateBufferInfo(kClientBufferId, kServiceBufferId);
  BufferManager::BufferInfo* info = manager_.GetBufferInfo(kClientBufferId);
  ASSERT_TRUE(info != NULL);
  manager_.SetTarget(info, GL_ELEMENT_ARRAY_BUFFER);
  manager_.SetInfo(info, sizeof(data), GL_STATIC_DRAW);
  EXPECT_TRUE(info->SetRange(0, sizeof(data), data));
  GLuint max_value;
  // Check entire range succeeds.
  EXPECT_TRUE(info->GetMaxValueForRange(0, 10, GL_UNSIGNED_INT, &max_value));
  EXPECT_EQ(10u, max_value);
  // Check non aligned offsets fails for GL_UNSIGNED_INT.
  EXPECT_FALSE(info->GetMaxValueForRange(1, 10, GL_UNSIGNED_INT, &max_value));
  EXPECT_FALSE(info->GetMaxValueForRange(2, 10, GL_UNSIGNED_INT, &max_value));
  EXPECT_FALSE(info->GetMaxValueForRange(3, 10, GL_UNSIGNED_INT, &max_value));
  // Check sub range succeeds.
  EXPECT_TRUE(info->GetMaxValueForRange(16, 3, GL_UNSIGNED_INT, &max_value));
  EXPECT_EQ(6u, max_value);
  // Check changing sub range succeeds.
  EXPECT_TRUE(info->SetRange(16, sizeof(new_data), new_data));
  EXPECT_TRUE(info->GetMaxValueForRange(16, 3, GL_UNSIGNED_INT, &max_value));
  EXPECT_EQ(120u, max_value);
  max_value = 0;
  EXPECT_TRUE(info->GetMaxValueForRange(0, 10, GL_UNSIGNED_INT, &max_value));
  EXPECT_EQ(120u, max_value);
  // Check out of range fails.
  EXPECT_FALSE(info->GetMaxValueForRange(0, 11, GL_UNSIGNED_INT, &max_value));
  EXPECT_FALSE(info->GetMaxValueForRange(40, 1, GL_UNSIGNED_INT, &max_value));
}

TEST_F(BufferManagerTest, UseDeletedBuffer) {
  const GLuint kClientBufferId = 1;
  const GLuint kServiceBufferId = 11;
  const uint32 data[] = {10, 9, 8, 7, 6, 5, 4, 3, 2, 1};
  manager_.CreateBufferInfo(kClientBufferId, kServiceBufferId);
  BufferManager::BufferInfo::Ref info = manager_.GetBufferInfo(kClientBufferId);
  ASSERT_TRUE(info != NULL);
  manager_.SetTarget(info, GL_ARRAY_BUFFER);
  // Remove buffer
  manager_.RemoveBufferInfo(kClientBufferId);
  // Use it after removing
  manager_.SetInfo(info, sizeof(data), GL_STATIC_DRAW);
  // Check that it gets deleted when the last reference is released.
  EXPECT_CALL(*gl_, DeleteBuffersARB(1, ::testing::Pointee(kServiceBufferId)))
      .Times(1)
      .RetiresOnSaturation();
  info = NULL;
}

}  // namespace gles2
}  // namespace gpu


