// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/pepper_platform_context_3d_impl.h"

#include "content/renderer/command_buffer_proxy.h"
#include "content/renderer/render_thread.h"
#include "content/renderer/renderer_gl_context.h"
#include "content/renderer/gpu_channel_host.h"
#include "googleurl/src/gurl.h"
#include "gpu/command_buffer/client/gles2_cmd_helper.h"
#include "gpu/command_buffer/client/gles2_implementation.h"

#ifdef ENABLE_GPU

PlatformContext3DImpl::PlatformContext3DImpl(RendererGLContext* parent_context)
      : parent_context_(parent_context->AsWeakPtr()),
        parent_texture_id_(0),
        command_buffer_(NULL),
        callback_factory_(ALLOW_THIS_IN_INITIALIZER_LIST(this)) {
}

PlatformContext3DImpl::~PlatformContext3DImpl() {
  if (command_buffer_) {
    DCHECK(channel_.get());
    channel_->DestroyCommandBuffer(command_buffer_);
    command_buffer_ = NULL;
  }

  channel_ = NULL;

  if (parent_context_.get() && parent_texture_id_ != 0) {
    parent_context_->GetImplementation()->FreeTextureId(parent_texture_id_);
  }

}

bool PlatformContext3DImpl::Init() {
  // Ignore initializing more than once.
  if (command_buffer_)
    return true;

  // Parent may already have been deleted.
  if (!parent_context_.get())
    return false;

  RenderThread* render_thread = RenderThread::current();
  if (!render_thread)
    return false;

  channel_ = render_thread->GetGpuChannel();
  if (!channel_.get())
    return false;

  DCHECK(channel_->state() == GpuChannelHost::kConnected);

  // Flush any remaining commands in the parent context to make sure the
  // texture id accounting stays consistent.
  gpu::gles2::GLES2Implementation* parent_gles2 =
      parent_context_->GetImplementation();
  parent_gles2->helper()->CommandBufferHelper::Finish();
  parent_texture_id_ = parent_gles2->MakeTextureId();

  // TODO(apatrick): Let Pepper plugins configure their back buffer surface.
  static const int32 kAttribs[] = {
    RendererGLContext::ALPHA_SIZE, 8,
    RendererGLContext::DEPTH_SIZE, 24,
    RendererGLContext::STENCIL_SIZE, 8,
    RendererGLContext::SAMPLES, 0,
    RendererGLContext::SAMPLE_BUFFERS, 0,
    RendererGLContext::NONE,
  };
  std::vector<int32> attribs(kAttribs, kAttribs + ARRAYSIZE_UNSAFE(kAttribs));
  CommandBufferProxy* parent_command_buffer =
      parent_context_->GetCommandBufferProxy();
  command_buffer_ = channel_->CreateOffscreenCommandBuffer(
      parent_command_buffer,
      gfx::Size(1, 1),
      "*",
      attribs,
      parent_texture_id_,
      GURL::EmptyGURL());
  if (!command_buffer_)
    return false;
  command_buffer_->SetChannelErrorCallback(callback_factory_.NewCallback(
      &PlatformContext3DImpl::OnContextLost));

  return true;
}

void PlatformContext3DImpl::SetSwapBuffersCallback(Callback0::Type* callback) {
  DCHECK(command_buffer_);
  command_buffer_->SetSwapBuffersCallback(callback);
}

unsigned PlatformContext3DImpl::GetBackingTextureId() {
  DCHECK(command_buffer_);
  return parent_texture_id_;
}

gpu::CommandBuffer* PlatformContext3DImpl::GetCommandBuffer() {
  return command_buffer_;
}

void PlatformContext3DImpl::SetContextLostCallback(Callback0::Type* callback) {
    context_lost_callback_.reset(callback);
}

void PlatformContext3DImpl::OnContextLost() {
  DCHECK(command_buffer_);

  if (context_lost_callback_.get())
    context_lost_callback_->Run();
}

#endif  // ENABLE_GPU
