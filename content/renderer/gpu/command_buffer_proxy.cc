// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/gpu/command_buffer_proxy.h"

#include "base/debug/trace_event.h"
#include "base/logging.h"
#include "base/process_util.h"
#include "base/shared_memory.h"
#include "base/task.h"
#include "content/common/gpu/gpu_messages.h"
#include "content/common/plugin_messages.h"
#include "content/common/view_messages.h"
#include "content/renderer/plugin_channel_host.h"
#include "content/renderer/render_thread.h"
#include "gpu/command_buffer/common/cmd_buffer_common.h"
#include "ui/gfx/size.h"

using gpu::Buffer;

CommandBufferProxy::CommandBufferProxy(
    IPC::Channel::Sender* channel,
    int route_id)
    : num_entries_(0),
      channel_(channel),
      route_id_(route_id),
      flush_count_(0) {
}

CommandBufferProxy::~CommandBufferProxy() {
  // Delete all the locally cached shared memory objects, closing the handle
  // in this process.
  for (TransferBufferMap::iterator it = transfer_buffers_.begin();
       it != transfer_buffers_.end();
       ++it) {
    delete it->second.shared_memory;
    it->second.shared_memory = NULL;
  }
}

bool CommandBufferProxy::OnMessageReceived(const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(CommandBufferProxy, message)
    IPC_MESSAGE_HANDLER(GpuCommandBufferMsg_UpdateState, OnUpdateState);
    IPC_MESSAGE_HANDLER(GpuCommandBufferMsg_SwapBuffers, OnSwapBuffers);
    IPC_MESSAGE_HANDLER(GpuCommandBufferMsg_NotifyRepaint,
                        OnNotifyRepaint);
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  DCHECK(handled);
  return handled;
}

void CommandBufferProxy::OnChannelError() {
  // Prevent any further messages from being sent.
  channel_ = NULL;

  // When the client sees that the context is lost, they should delete this
  // CommandBufferProxy and create a new one.
  last_state_.error = gpu::error::kLostContext;

  if (channel_error_callback_.get())
    channel_error_callback_->Run();
}

void CommandBufferProxy::SetChannelErrorCallback(Callback0::Type* callback) {
  channel_error_callback_.reset(callback);
}

bool CommandBufferProxy::Initialize(int32 size) {
  DCHECK(!ring_buffer_.get());

  RenderThread* render_thread = RenderThread::current();
  if (!render_thread)
    return false;

  base::SharedMemoryHandle handle;
  if (!render_thread->Send(new ViewHostMsg_AllocateSharedMemoryBuffer(
      size,
      &handle))) {
    return false;
  }

  if (!base::SharedMemory::IsHandleValid(handle))
    return false;

#if defined(OS_POSIX)
  handle.auto_close = false;
#endif

  // Take ownership of shared memory. This will close the handle if Send below
  // fails. Otherwise, callee takes ownership before this variable
  // goes out of scope.
  base::SharedMemory shared_memory(handle, false);

  return Initialize(&shared_memory, size);
}

bool CommandBufferProxy::Initialize(base::SharedMemory* buffer, int32 size) {
  bool result;
  if (!Send(new GpuCommandBufferMsg_Initialize(route_id_,
                                               buffer->handle(),
                                               size,
                                               &result))) {
    LOG(ERROR) << "Could not send GpuCommandBufferMsg_Initialize.";
    return false;
  }

  if (!result) {
    LOG(ERROR) << "Failed to initialize command buffer service.";
    return false;
  }

  base::SharedMemoryHandle handle;
  if (!buffer->GiveToProcess(base::GetCurrentProcessHandle(), &handle)) {
    LOG(ERROR) << "Failed to duplicate command buffer handle.";
    return false;
  }

  ring_buffer_.reset(new base::SharedMemory(handle, false));
  if (!ring_buffer_->Map(size)) {
    LOG(ERROR) << "Failed to map shared memory for command buffer.";
    ring_buffer_.reset();
    return false;
  }

  num_entries_ = size / sizeof(gpu::CommandBufferEntry);
  return true;
}

Buffer CommandBufferProxy::GetRingBuffer() {
  DCHECK(ring_buffer_.get());
  // Return locally cached ring buffer.
  Buffer buffer;
  buffer.ptr = ring_buffer_->memory();
  buffer.size = num_entries_ * sizeof(gpu::CommandBufferEntry);
  buffer.shared_memory = ring_buffer_.get();
  return buffer;
}

gpu::CommandBuffer::State CommandBufferProxy::GetState() {
  // Send will flag state with lost context if IPC fails.
  if (last_state_.error == gpu::error::kNoError) {
    gpu::CommandBuffer::State state;
    if (Send(new GpuCommandBufferMsg_GetState(route_id_, &state)))
      OnUpdateState(state);
  }

  return last_state_;
}

void CommandBufferProxy::Flush(int32 put_offset) {
  if (last_state_.error != gpu::error::kNoError)
    return;

  Send(new GpuCommandBufferMsg_AsyncFlush(route_id_,
                                          put_offset,
                                          ++flush_count_));
}

gpu::CommandBuffer::State CommandBufferProxy::FlushSync(int32 put_offset,
                                                        int32 last_known_get) {
  TRACE_EVENT0("gpu", "CommandBufferProxy::FlushSync");
  if (last_known_get == last_state_.get_offset) {
    // Send will flag state with lost context if IPC fails.
    if (last_state_.error == gpu::error::kNoError) {
      gpu::CommandBuffer::State state;
      if (Send(new GpuCommandBufferMsg_Flush(route_id_,
                                             put_offset,
                                             last_known_get,
                                             ++flush_count_,
                                             &state)))
        OnUpdateState(state);
    }
  } else {
    Flush(put_offset);
  }

  return last_state_;
}

void CommandBufferProxy::SetGetOffset(int32 get_offset) {
  // Not implemented in proxy.
  NOTREACHED();
}

int32 CommandBufferProxy::CreateTransferBuffer(size_t size, int32 id_request) {
  if (last_state_.error != gpu::error::kNoError)
    return -1;

  RenderThread* render_thread = RenderThread::current();
  if (!render_thread)
    return -1;

  base::SharedMemoryHandle handle;
  if (!render_thread->Send(new ViewHostMsg_AllocateSharedMemoryBuffer(
      size,
      &handle))) {
    return -1;
  }

  if (!base::SharedMemory::IsHandleValid(handle))
    return -1;

  // Handle is closed by the SharedMemory object below. This stops
  // base::FileDescriptor from closing it as well.
#if defined(OS_POSIX)
  handle.auto_close = false;
#endif

  // Take ownership of shared memory. This will close the handle if Send below
  // fails. Otherwise, callee takes ownership before this variable
  // goes out of scope by duping the handle.
  base::SharedMemory shared_memory(handle, false);

  int32 id;
  if (!Send(new GpuCommandBufferMsg_RegisterTransferBuffer(route_id_,
                                                           handle,
                                                           size,
                                                           id_request,
                                                           &id))) {
    return -1;
  }

  return id;
}

int32 CommandBufferProxy::RegisterTransferBuffer(
    base::SharedMemory* shared_memory,
    size_t size,
    int32 id_request) {
  if (last_state_.error != gpu::error::kNoError)
    return -1;

  int32 id;
  if (!Send(new GpuCommandBufferMsg_RegisterTransferBuffer(
      route_id_,
      shared_memory->handle(),  // Returns FileDescriptor with auto_close off.
      size,
      id_request,
      &id))) {
    return -1;
  }

  return id;
}

void CommandBufferProxy::DestroyTransferBuffer(int32 id) {
  if (last_state_.error != gpu::error::kNoError)
    return;

  // Remove the transfer buffer from the client side cache.
  TransferBufferMap::iterator it = transfer_buffers_.find(id);
  if (it != transfer_buffers_.end()) {
    delete it->second.shared_memory;
    transfer_buffers_.erase(it);
  }

  Send(new GpuCommandBufferMsg_DestroyTransferBuffer(route_id_, id));
}

Buffer CommandBufferProxy::GetTransferBuffer(int32 id) {
  if (last_state_.error != gpu::error::kNoError)
    return Buffer();

  // Check local cache to see if there is already a client side shared memory
  // object for this id.
  TransferBufferMap::iterator it = transfer_buffers_.find(id);
  if (it != transfer_buffers_.end()) {
    return it->second;
  }

  // Assuming we are in the renderer process, the service is responsible for
  // duplicating the handle. This might not be true for NaCl.
  base::SharedMemoryHandle handle;
  uint32 size;
  if (!Send(new GpuCommandBufferMsg_GetTransferBuffer(route_id_,
                                                      id,
                                                      &handle,
                                                      &size))) {
    return Buffer();
  }

  // Cache the transfer buffer shared memory object client side.
  base::SharedMemory* shared_memory = new base::SharedMemory(handle, false);

  // Map the shared memory on demand.
  if (!shared_memory->memory()) {
    if (!shared_memory->Map(size)) {
      delete shared_memory;
      return Buffer();
    }
  }

  Buffer buffer;
  buffer.ptr = shared_memory->memory();
  buffer.size = size;
  buffer.shared_memory = shared_memory;
  transfer_buffers_[id] = buffer;

  return buffer;
}

void CommandBufferProxy::SetToken(int32 token) {
  // Not implemented in proxy.
  NOTREACHED();
}

void CommandBufferProxy::OnNotifyRepaint() {
  if (notify_repaint_task_.get())
    MessageLoop::current()->PostNonNestableTask(
        FROM_HERE, notify_repaint_task_.release());
}

void CommandBufferProxy::SetParseError(
    gpu::error::Error error) {
  // Not implemented in proxy.
  NOTREACHED();
}

void CommandBufferProxy::OnSwapBuffers() {
  if (swap_buffers_callback_.get())
    swap_buffers_callback_->Run();
}

void CommandBufferProxy::SetSwapBuffersCallback(Callback0::Type* callback) {
  swap_buffers_callback_.reset(callback);
}

void CommandBufferProxy::ResizeOffscreenFrameBuffer(const gfx::Size& size) {
  if (last_state_.error != gpu::error::kNoError)
    return;

  Send(new GpuCommandBufferMsg_ResizeOffscreenFrameBuffer(route_id_, size));
}

void CommandBufferProxy::SetNotifyRepaintTask(Task* task) {
  notify_repaint_task_.reset(task);
}

#if defined(OS_MACOSX)
void CommandBufferProxy::SetWindowSize(const gfx::Size& size) {
  if (last_state_.error != gpu::error::kNoError)
    return;

  Send(new GpuCommandBufferMsg_SetWindowSize(route_id_, size));
}
#endif

bool CommandBufferProxy::Send(IPC::Message* msg) {
  // Caller should not intentionally send a message if the context is lost.
  DCHECK(last_state_.error == gpu::error::kNoError);

  if (channel_) {
    if (channel_->Send(msg)) {
      return true;
    } else {
      // Flag the command buffer as lost. Defer deleting the channel until
      // OnChannelError is called after returning to the message loop in case
      // it is referenced elsewhere.
      last_state_.error = gpu::error::kLostContext;
      return false;
    }
  }

  // Callee takes ownership of message, regardless of whether Send is
  // successful. See IPC::Message::Sender.
  delete msg;
  return false;
}

void CommandBufferProxy::OnUpdateState(const gpu::CommandBuffer::State& state) {
  // Handle wraparound. It works as long as we don't have more than 2B state
  // updates in flight across which reordering occurs.
  if (state.generation - last_state_.generation < 0x80000000U)
    last_state_ = state;
}
