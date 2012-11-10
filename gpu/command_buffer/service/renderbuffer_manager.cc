// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/command_buffer/service/renderbuffer_manager.h"
#include "base/logging.h"
#include "base/debug/trace_event.h"
#include "gpu/command_buffer/common/gles2_cmd_utils.h"
#include "gpu/command_buffer/service/gles2_cmd_decoder.h"
#include "gpu/command_buffer/service/memory_tracking.h"

namespace gpu {
namespace gles2 {

RenderbufferManager::RenderbufferManager(
    MemoryTracker* memory_tracker,
    GLint max_renderbuffer_size,
    GLint max_samples)
    : renderbuffer_memory_tracker_(new MemoryTypeTracker(
        memory_tracker,
        "RenderbufferManager",
        "RenderbufferMemory")),
      max_renderbuffer_size_(max_renderbuffer_size),
      max_samples_(max_samples),
      num_uncleared_renderbuffers_(0),
      mem_represented_(0),
      renderbuffer_info_count_(0),
      have_context_(true) {
  UpdateMemRepresented();
}

RenderbufferManager::~RenderbufferManager() {
  DCHECK(renderbuffer_infos_.empty());
  // If this triggers, that means something is keeping a reference to
  // a RenderbufferInfo belonging to this.
  CHECK_EQ(renderbuffer_info_count_, 0u);

  DCHECK_EQ(0, num_uncleared_renderbuffers_);
}

size_t RenderbufferManager::RenderbufferInfo::EstimatedSize() {
  return width_ * height_ * samples_ *
         GLES2Util::RenderbufferBytesPerPixel(internal_format_);
}

RenderbufferManager::RenderbufferInfo::~RenderbufferInfo() {
  if (manager_) {
    if (manager_->have_context_) {
      GLuint id = service_id();
      glDeleteRenderbuffersEXT(1, &id);
    }
    manager_->StopTracking(this);
    manager_ = NULL;
  }
}

void RenderbufferManager::UpdateMemRepresented() {
  renderbuffer_memory_tracker_->UpdateMemRepresented(mem_represented_);
}

void RenderbufferManager::Destroy(bool have_context) {
  have_context_ = have_context;
  renderbuffer_infos_.clear();
  DCHECK_EQ(0u, mem_represented_);
  UpdateMemRepresented();
}

void RenderbufferManager::StartTracking(RenderbufferInfo* /* renderbuffer */) {
  ++renderbuffer_info_count_;
}

void RenderbufferManager::StopTracking(RenderbufferInfo* renderbuffer) {
  --renderbuffer_info_count_;
  if (!renderbuffer->cleared()) {
    --num_uncleared_renderbuffers_;
  }
  mem_represented_ -= renderbuffer->EstimatedSize();
}

void RenderbufferManager::SetInfo(
    RenderbufferInfo* renderbuffer,
    GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height) {
  DCHECK(renderbuffer);
  if (!renderbuffer->cleared()) {
    --num_uncleared_renderbuffers_;
  }
  mem_represented_ -= renderbuffer->EstimatedSize();
  renderbuffer->SetInfo(samples, internalformat, width, height);
  mem_represented_ += renderbuffer->EstimatedSize();
  UpdateMemRepresented();
  if (!renderbuffer->cleared()) {
    ++num_uncleared_renderbuffers_;
  }
}

void RenderbufferManager::SetCleared(RenderbufferInfo* renderbuffer) {
  DCHECK(renderbuffer);
  if (!renderbuffer->cleared()) {
    --num_uncleared_renderbuffers_;
  }
  renderbuffer->set_cleared();
  if (!renderbuffer->cleared()) {
    ++num_uncleared_renderbuffers_;
  }
}

void RenderbufferManager::CreateRenderbufferInfo(
    GLuint client_id, GLuint service_id) {
  RenderbufferInfo::Ref info(new RenderbufferInfo(this, service_id));
  std::pair<RenderbufferInfoMap::iterator, bool> result =
      renderbuffer_infos_.insert(std::make_pair(client_id, info));
  DCHECK(result.second);
  if (!info->cleared()) {
    ++num_uncleared_renderbuffers_;
  }
}

RenderbufferManager::RenderbufferInfo* RenderbufferManager::GetRenderbufferInfo(
    GLuint client_id) {
  RenderbufferInfoMap::iterator it = renderbuffer_infos_.find(client_id);
  return it != renderbuffer_infos_.end() ? it->second : NULL;
}

void RenderbufferManager::RemoveRenderbufferInfo(GLuint client_id) {
  RenderbufferInfoMap::iterator it = renderbuffer_infos_.find(client_id);
  if (it != renderbuffer_infos_.end()) {
    RenderbufferInfo* info = it->second;
    info->MarkAsDeleted();
    renderbuffer_infos_.erase(it);
  }
}

bool RenderbufferManager::GetClientId(
    GLuint service_id, GLuint* client_id) const {
  // This doesn't need to be fast. It's only used during slow queries.
  for (RenderbufferInfoMap::const_iterator it = renderbuffer_infos_.begin();
       it != renderbuffer_infos_.end(); ++it) {
    if (it->second->service_id() == service_id) {
      *client_id = it->first;
      return true;
    }
  }
  return false;
}

}  // namespace gles2
}  // namespace gpu


