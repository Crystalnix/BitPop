// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_COMMAND_BUFFER_SERVICE_BUFFER_MANAGER_H_
#define GPU_COMMAND_BUFFER_SERVICE_BUFFER_MANAGER_H_

#include <map>
#include "base/basictypes.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "gpu/command_buffer/service/gl_utils.h"

namespace gpu {
namespace gles2 {

// This class keeps track of the buffers and their sizes so we can do
// bounds checking.
//
// NOTE: To support shared resources an instance of this class will need to be
// shared by multiple GLES2Decoders.
class BufferManager {
 public:
  // Info about Buffers currently in the system.
  class BufferInfo : public base::RefCounted<BufferInfo> {
   public:
    typedef scoped_refptr<BufferInfo> Ref;

    explicit BufferInfo(GLuint service_id);

    GLuint service_id() const {
      return service_id_;
    }

    GLsizeiptr size() const {
      return size_;
    }

    // Sets a range of data for this buffer. Returns false if the offset or size
    // is out of range.
    bool SetRange(
      GLintptr offset, GLsizeiptr size, const GLvoid * data);

    // Gets the maximum value in the buffer for the given range interpreted as
    // the given type. Returns false if offset and count are out of range.
    // offset is in bytes.
    // count is in elements of type.
    bool GetMaxValueForRange(GLuint offset, GLsizei count, GLenum type,
                             GLuint* max_value);

    // Returns a pointer to shadowed data.
    const void* GetRange(GLintptr offset, GLsizeiptr size) const;

    bool IsDeleted() const {
      return service_id_ == 0;
    }

    bool IsValid() const {
      return target() && !IsDeleted();
    }

   private:
    friend class BufferManager;
    friend class BufferManagerTest;
    friend class base::RefCounted<BufferInfo>;

    // Represents a range in a buffer.
    class Range {
     public:
      Range(GLuint offset, GLsizei count, GLenum type)
          : offset_(offset),
            count_(count),
            type_(type) {
      }

      // A less functor provided for std::map so it can find ranges.
      struct Less {
        bool operator() (const Range& lhs, const Range& rhs) const {
          if (lhs.offset_ != rhs.offset_) {
            return lhs.offset_ < rhs.offset_;
          }
          if (lhs.count_ != rhs.count_) {
            return lhs.count_ < rhs.count_;
          }
          return lhs.type_ < rhs.type_;
        }
      };

     private:
      GLuint offset_;
      GLsizei count_;
      GLenum type_;
    };

    ~BufferInfo();

    GLenum target() const {
      return target_;
    }

    void set_target(GLenum target) {
      DCHECK_EQ(target_, 0u);  // you can only set this once.
      target_ = target;
    }

    bool shadowed() const {
      return shadowed_;
    }

    void MarkAsDeleted() {
      service_id_ = 0;
      shadow_.reset();
      ClearCache();
    }

    void SetSize(GLsizeiptr size, bool shadow);

    // Clears any cache of index ranges.
    void ClearCache();

    // Service side buffer id.
    GLuint service_id_;

    // The type of buffer. 0 = unset, GL_BUFFER_ARRAY = vertex data,
    // GL_ELEMENT_BUFFER_ARRAY = index data.
    // Once set a buffer can not be used for something else.
    GLenum target_;

    // Size of buffer.
    GLsizeiptr size_;

    // Whether or not the data is shadowed.
    bool shadowed_;

    // A copy of the data in the buffer. This data is only kept if the target
    // is backed_ = true.
    scoped_array<int8> shadow_;

    // A map of ranges to the highest value in that range of a certain type.
    typedef std::map<Range, GLuint, Range::Less> RangeToMaxValueMap;
    RangeToMaxValueMap range_set_;
  };

  BufferManager();
  ~BufferManager();

  // Must call before destruction.
  void Destroy(bool have_context);

  // Creates a BufferInfo for the given buffer.
  void CreateBufferInfo(GLuint client_id, GLuint service_id);

  // Gets the buffer info for the given buffer.
  BufferInfo* GetBufferInfo(GLuint client_id);

  // Removes a buffer info for the given buffer.
  void RemoveBufferInfo(GLuint client_id);

  // Gets a client id for a given service id.
  bool GetClientId(GLuint service_id, GLuint* client_id) const;

  // Sets the size of a buffer.
  void SetSize(BufferInfo* info, GLsizeiptr size);

  // Sets the target of a buffer. Returns false if the target can not be set.
  bool SetTarget(BufferInfo* info, GLenum target);

  void set_allow_buffers_on_multiple_targets(bool allow) {
    allow_buffers_on_multiple_targets_ = allow;
  }

 private:
  // Info for each buffer in the system.
  // TODO(gman): Choose a faster container.
  typedef std::map<GLuint, BufferInfo::Ref> BufferInfoMap;
  BufferInfoMap buffer_infos_;

  // Whether or not buffers can be bound to multiple targets.
  bool allow_buffers_on_multiple_targets_;

  DISALLOW_COPY_AND_ASSIGN(BufferManager);
};

}  // namespace gles2
}  // namespace gpu

#endif  // GPU_COMMAND_BUFFER_SERVICE_BUFFER_MANAGER_H_
