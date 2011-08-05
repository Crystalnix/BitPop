// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if defined(OS_WIN)
#include <windows.h>
#endif

#include "content/browser/renderer_host/gpu_message_filter.h"

#include "base/callback.h"
#include "content/browser/gpu/gpu_process_host.h"
#include "content/common/gpu/gpu_messages.h"

GpuMessageFilter::GpuMessageFilter(int render_process_id)
    : gpu_host_id_(0),
      render_process_id_(render_process_id) {
}

// WeakPtrs to a GpuMessageFilter need to be Invalidated from
// the same thread from which they were created.
GpuMessageFilter::~GpuMessageFilter() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
}

bool GpuMessageFilter::OnMessageReceived(
    const IPC::Message& message,
    bool* message_was_ok) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP_EX(GpuMessageFilter, message, *message_was_ok)
    IPC_MESSAGE_HANDLER(GpuHostMsg_EstablishGpuChannel,
                        OnEstablishGpuChannel)
    IPC_MESSAGE_HANDLER_DELAY_REPLY(GpuHostMsg_SynchronizeGpu,
                                    OnSynchronizeGpu)
    IPC_MESSAGE_HANDLER_DELAY_REPLY(GpuHostMsg_CreateViewCommandBuffer,
                                    OnCreateViewCommandBuffer)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP_EX()
  return handled;
}

void GpuMessageFilter::OnDestruct() const {
  BrowserThread::DeleteOnUIThread::Destruct(this);
}

// Callbacks used in this file.
namespace {

class EstablishChannelCallback
    : public CallbackRunner<Tuple3<const IPC::ChannelHandle&,
                                   base::ProcessHandle,
                                   const GPUInfo&> > {
 public:
  explicit EstablishChannelCallback(GpuMessageFilter* filter):
      filter_(filter->AsWeakPtr()) {
  }

  virtual void RunWithParams(const TupleType& params) {
    DispatchToMethod(this, &EstablishChannelCallback::Send, params);
  }

  void Send(const IPC::ChannelHandle& channel,
            base::ProcessHandle gpu_process_for_browser,
            const GPUInfo& gpu_info) {
    if (!filter_)
      return;

    base::ProcessHandle renderer_process_for_gpu;
    if (gpu_process_for_browser != 0) {
#if defined(OS_WIN)
      // Create a process handle that the renderer process can give to the GPU
      // process to give it access to its handles.
      DuplicateHandle(base::GetCurrentProcessHandle(),
                      filter_->peer_handle(),
                      gpu_process_for_browser,
                      &renderer_process_for_gpu,
                      PROCESS_DUP_HANDLE,
                      FALSE,
                      0);
#else
      renderer_process_for_gpu = filter_->peer_handle();
#endif
    } else {
      renderer_process_for_gpu = 0;
    }

    IPC::Message* reply = new GpuMsg_GpuChannelEstablished(
        channel, renderer_process_for_gpu, gpu_info);

    // If the renderer process is performing synchronous initialization,
    // it needs to handle this message before receiving the reply for
    // the synchronous GpuHostMsg_SynchronizeGpu message.
    reply->set_unblock(true);

    filter_->Send(reply);
  }

 private:
  base::WeakPtr<GpuMessageFilter> filter_;
};

class SynchronizeCallback : public CallbackRunner<Tuple0> {
 public:
  SynchronizeCallback(GpuMessageFilter* filter, IPC::Message* reply):
      filter_(filter->AsWeakPtr()),
      reply_(reply) {
  }

  virtual void RunWithParams(const TupleType& params) {
    DispatchToMethod(this, &SynchronizeCallback::Send, params);
  }

  void Send() {
    if (filter_)
      filter_->Send(reply_);
  }

 private:
  base::WeakPtr<GpuMessageFilter> filter_;
  IPC::Message* reply_;
};

class CreateCommandBufferCallback : public CallbackRunner<Tuple1<int32> > {
 public:
  CreateCommandBufferCallback(GpuMessageFilter* filter,
                              IPC::Message* reply) :
      filter_(filter->AsWeakPtr()),
      reply_(reply) {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  }

  virtual void RunWithParams(const TupleType& params) {
    DispatchToMethod(this, &CreateCommandBufferCallback::Send, params);
  }

  void Send(int32 route_id) {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
    GpuHostMsg_CreateViewCommandBuffer::WriteReplyParams(reply_, route_id);
    if (filter_)
      filter_->Send(reply_);
  }

 private:
  base::WeakPtr<GpuMessageFilter> filter_;
  IPC::Message* reply_;
};

}  // namespace

void GpuMessageFilter::OnEstablishGpuChannel(
    content::CauseForGpuLaunch cause_for_gpu_launch) {
  scoped_ptr<EstablishChannelCallback> callback(
      new EstablishChannelCallback(this));

  // TODO(apatrick): Eventually, this will return the route ID of a
  // GpuProcessStub, from which the renderer process will create a
  // GpuProcessProxy. The renderer will use the proxy for all subsequent
  // communication with the GPU process. This means if the GPU process
  // terminates, the renderer process will not find itself unknowingly sending
  // IPCs to a newly launched GPU process. Also, I will rename this function
  // to something like OnCreateGpuProcess.
  GpuProcessHost* host = GpuProcessHost::FromID(gpu_host_id_);
  if (!host) {
    host = GpuProcessHost::GetForRenderer(
        render_process_id_, cause_for_gpu_launch);
    if (!host) {
      callback->Run(IPC::ChannelHandle(),
                    static_cast<base::ProcessHandle>(NULL),
                    GPUInfo());
      return;
    }

    gpu_host_id_ = host->host_id();
  }

  host->EstablishGpuChannel(render_process_id_, callback.release());
}

void GpuMessageFilter::OnSynchronizeGpu(IPC::Message* reply) {
  GpuProcessHost* host = GpuProcessHost::FromID(gpu_host_id_);
  if (!host) {
    // TODO(apatrick): Eventually, this IPC message will be routed to a
    // GpuProcessStub with a particular routing ID. The error will be set if
    // the GpuProcessStub with that routing ID is not in the MessageRouter.
    reply->set_reply_error();
    Send(reply);
    return;
  }

  host->Synchronize(new SynchronizeCallback(this, reply));
}

void GpuMessageFilter::OnCreateViewCommandBuffer(
    gfx::PluginWindowHandle compositing_surface,
    int32 render_view_id,
    const GPUCreateCommandBufferConfig& init_params,
    IPC::Message* reply) {
  GpuProcessHost* host = GpuProcessHost::FromID(gpu_host_id_);
  if (!host) {
    // TODO(apatrick): Eventually, this IPC message will be routed to a
    // GpuProcessStub with a particular routing ID. The error will be set if
    // the GpuProcessStub with that routing ID is not in the MessageRouter.
    reply->set_reply_error();
    Send(reply);
    return;
  }

  host->CreateViewCommandBuffer(
      compositing_surface,
      render_view_id,
      render_process_id_,
      init_params,
      new CreateCommandBufferCallback(this, reply));
}
