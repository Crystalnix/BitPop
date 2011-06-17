// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/command_buffer/service/buffer_manager.h"
#include "base/logging.h"
#include "gpu/command_buffer/common/gles2_cmd_utils.h"
#include "gpu/command_buffer/service/gles2_cmd_decoder.h"

namespace gpu {
namespace gles2 {

BufferManager::BufferManager()
    : allow_buffers_on_multiple_targets_(false) {
}

BufferManager::~BufferManager() {
  DCHECK(buffer_infos_.empty());
}

void BufferManager::Destroy(bool have_context) {
  while (!buffer_infos_.empty()) {
    if (have_context) {
      BufferInfo* info = buffer_infos_.begin()->second;
      if (!info->IsDeleted()) {
        GLuint service_id = info->service_id();
        glDeleteBuffersARB(1, &service_id);
        info->MarkAsDeleted();
      }
    }
    buffer_infos_.erase(buffer_infos_.begin());
  }
}

void BufferManager::CreateBufferInfo(GLuint client_id, GLuint service_id) {
  std::pair<BufferInfoMap::iterator, bool> result =
      buffer_infos_.insert(
          std::make_pair(client_id,
                         BufferInfo::Ref(new BufferInfo(service_id))));
  DCHECK(result.second);
}

BufferManager::BufferInfo* BufferManager::GetBufferInfo(
    GLuint client_id) {
  BufferInfoMap::iterator it = buffer_infos_.find(client_id);
  return it != buffer_infos_.end() ? it->second : NULL;
}

void BufferManager::RemoveBufferInfo(GLuint client_id) {
  BufferInfoMap::iterator it = buffer_infos_.find(client_id);
  if (it != buffer_infos_.end()) {
    it->second->MarkAsDeleted();
    buffer_infos_.erase(it);
  }
}

BufferManager::BufferInfo::BufferInfo(GLuint service_id)
    : service_id_(service_id),
      target_(0),
      size_(0),
      shadowed_(false) {
}

BufferManager::BufferInfo::~BufferInfo() { }

void BufferManager::BufferInfo::SetSize(GLsizeiptr size, bool shadow) {
  DCHECK(!IsDeleted());
  if (size != size_ || shadow != shadowed_) {
    shadowed_ = shadow;
    size_ = size;
    ClearCache();
    if (shadowed_) {
      shadow_.reset(new int8[size]);
      memset(shadow_.get(), 0, size);
    }
  }
}

bool BufferManager::BufferInfo::SetRange(
    GLintptr offset, GLsizeiptr size, const GLvoid * data) {
  DCHECK(!IsDeleted());
  if (offset < 0 || offset + size < offset || offset + size > size_) {
    return false;
  }
  if (shadowed_) {
    memcpy(shadow_.get() + offset, data, size);
    ClearCache();
  }
  return true;
}

const void* BufferManager::BufferInfo::GetRange(
    GLintptr offset, GLsizeiptr size) const {
  if (!shadowed_) {
    return NULL;
  }
  if (offset < 0 || offset + size < offset || offset + size > size_) {
    return NULL;
  }
  return shadow_.get() + offset;
}

void BufferManager::BufferInfo::ClearCache() {
  range_set_.clear();
}

template <typename T>
GLuint GetMaxValue(const void* data, GLuint offset, GLsizei count) {
  GLuint max_value = 0;
  const T* element = reinterpret_cast<const T*>(
      static_cast<const int8*>(data) + offset);
  const T* end = element + count;
  for (; element < end; ++element) {
    if (*element > max_value) {
      max_value = *element;
    }
  }
  return max_value;
}

bool BufferManager::BufferInfo::GetMaxValueForRange(
    GLuint offset, GLsizei count, GLenum type, GLuint* max_value) {
  DCHECK(!IsDeleted());
  Range range(offset, count, type);
  RangeToMaxValueMap::iterator it = range_set_.find(range);
  if (it != range_set_.end()) {
    *max_value = it->second;
    return true;
  }

  uint32 size;
  if (!SafeMultiplyUint32(
      count, GLES2Util::GetGLTypeSizeForTexturesAndBuffers(type), &size)) {
    return false;
  }

  if (!SafeAddUint32(offset, size, &size)) {
    return false;
  }

  if (size > static_cast<uint32>(size_)) {
    return false;
  }

  if (!shadowed_) {
    return false;
  }

  // Scan the range for the max value and store
  GLuint max_v = 0;
  switch (type) {
    case GL_UNSIGNED_BYTE:
      max_v = GetMaxValue<uint8>(shadow_.get(), offset, count);
      break;
    case GL_UNSIGNED_SHORT:
      // Check we are not accessing an odd byte for a 2 byte value.
      if ((offset & 1) != 0) {
        return false;
      }
      max_v = GetMaxValue<uint16>(shadow_.get(), offset, count);
      break;
    case GL_UNSIGNED_INT:
      // Check we are not accessing a non aligned address for a 4 byte value.
      if ((offset & 3) != 0) {
        return false;
      }
      max_v = GetMaxValue<uint32>(shadow_.get(), offset, count);
      break;
    default:
      NOTREACHED();  // should never get here by validation.
      break;
  }
  std::pair<RangeToMaxValueMap::iterator, bool> result =
      range_set_.insert(std::make_pair(range, max_v));
  *max_value = max_v;
  return true;
}

bool BufferManager::GetClientId(GLuint service_id, GLuint* client_id) const {
  // This doesn't need to be fast. It's only used during slow queries.
  for (BufferInfoMap::const_iterator it = buffer_infos_.begin();
       it != buffer_infos_.end(); ++it) {
    if (it->second->service_id() == service_id) {
      *client_id = it->first;
      return true;
    }
  }
  return false;
}

void BufferManager::SetSize(BufferManager::BufferInfo* info, GLsizeiptr size) {
  DCHECK(info);
  info->SetSize(size,
                info->target() == GL_ELEMENT_ARRAY_BUFFER ||
                allow_buffers_on_multiple_targets_);
}

bool BufferManager::SetTarget(BufferManager::BufferInfo* info, GLenum target) {
  // Check that we are not trying to bind it to a different target.
  if (info->target() != 0 && info->target() != target &&
      !allow_buffers_on_multiple_targets_) {
    return false;
  }
  if (info->target() == 0) {
    info->set_target(target);
  }
  return true;
}

}  // namespace gles2
}  // namespace gpu


