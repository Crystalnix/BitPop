// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_COMMON_GPU_GPU_COMMAND_BUFFER_STUB_H_
#define CONTENT_COMMON_GPU_GPU_COMMAND_BUFFER_STUB_H_

#include <deque>
#include <string>
#include <vector>

#include "base/id_map.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "content/common/content_export.h"
#include "content/common/gpu/gpu_memory_allocation.h"
#include "googleurl/src/gurl.h"
#include "gpu/command_buffer/common/constants.h"
#include "gpu/command_buffer/service/command_buffer_service.h"
#include "gpu/command_buffer/service/context_group.h"
#include "gpu/command_buffer/service/gpu_scheduler.h"
#include "ipc/ipc_listener.h"
#include "ipc/ipc_sender.h"
#include "media/base/video_decoder_config.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/gfx/size.h"
#include "ui/gl/gl_context.h"
#include "ui/gl/gl_surface.h"
#include "ui/gl/gpu_preference.h"
#include "ui/surface/transport_dib.h"

#if defined(OS_MACOSX)
#include "ui/surface/accelerated_surface_mac.h"
#endif

class GpuChannel;
struct GpuMemoryAllocation;
class GpuVideoDecodeAccelerator;
class GpuWatchdog;

namespace gpu {
namespace gles2 {
class MailboxManager;
}
}

// This Base class is used to expose methods of GpuCommandBufferStub used for
// testability.
class CONTENT_EXPORT GpuCommandBufferStubBase {
 public:
  struct CONTENT_EXPORT SurfaceState {
    int32 surface_id;
    bool visible;
    base::TimeTicks last_used_time;

    SurfaceState(int32 surface_id,
                 bool visible,
                 base::TimeTicks last_used_time);
  };

 public:
  virtual ~GpuCommandBufferStubBase() {}

  // Will not have surface state if this is an offscreen commandbuffer.
  virtual bool client_has_memory_allocation_changed_callback() const = 0;
  virtual bool has_surface_state() const = 0;
  virtual const SurfaceState& surface_state() const = 0;

  virtual gfx::Size GetSurfaceSize() const = 0;

  virtual bool IsInSameContextShareGroup(
      const GpuCommandBufferStubBase& other) const = 0;

  virtual void SetMemoryAllocation(
      const GpuMemoryAllocation& allocation) = 0;
};

class GpuCommandBufferStub
    : public GpuCommandBufferStubBase,
      public IPC::Listener,
      public IPC::Sender,
      public base::SupportsWeakPtr<GpuCommandBufferStub> {
 public:
  class DestructionObserver {
   public:
    // Called in Destroy(), before the context/surface are released.
    virtual void OnWillDestroyStub(GpuCommandBufferStub* stub) = 0;

   protected:
    virtual ~DestructionObserver() {}
  };

  GpuCommandBufferStub(
      GpuChannel* channel,
      GpuCommandBufferStub* share_group,
      const gfx::GLSurfaceHandle& handle,
      gpu::gles2::MailboxManager* mailbox_manager,
      const gfx::Size& size,
      const gpu::gles2::DisallowedFeatures& disallowed_features,
      const std::string& allowed_extensions,
      const std::vector<int32>& attribs,
      gfx::GpuPreference gpu_preference,
      int32 route_id,
      int32 surface_id,
      GpuWatchdog* watchdog,
      bool software,
      const GURL& active_url);

  virtual ~GpuCommandBufferStub();

  // IPC::Listener implementation:
  virtual bool OnMessageReceived(const IPC::Message& message) OVERRIDE;

  // IPC::Sender implementation:
  virtual bool Send(IPC::Message* msg) OVERRIDE;

  // GpuCommandBufferStubBase implementation:
  virtual bool client_has_memory_allocation_changed_callback() const OVERRIDE;
  virtual bool has_surface_state() const OVERRIDE;
  virtual const GpuCommandBufferStubBase::SurfaceState& surface_state() const
      OVERRIDE;

  // Returns surface size.
  virtual gfx::Size GetSurfaceSize() const OVERRIDE;

  // Returns true iff |other| is in the same context share group as this stub.
  virtual bool IsInSameContextShareGroup(
      const GpuCommandBufferStubBase& other) const OVERRIDE;

  // Sets buffer usage depending on Memory Allocation
  virtual void SetMemoryAllocation(
      const GpuMemoryAllocation& allocation) OVERRIDE;

  // Whether this command buffer can currently handle IPC messages.
  bool IsScheduled();

  // Whether there are commands in the buffer that haven't been processed.
  bool HasUnprocessedCommands();

  // Delay an echo message until the command buffer has been rescheduled.
  void DelayEcho(IPC::Message*);

  gpu::gles2::GLES2Decoder* decoder() const { return decoder_.get(); }
  gpu::GpuScheduler* scheduler() const { return scheduler_.get(); }
  GpuChannel* channel() const { return channel_; }

  // Identifies the target surface.
  int32 surface_id() const {
    return (surface_state_.get()) ? surface_state_->surface_id : 0;
  }

  // Identifies the various GpuCommandBufferStubs in the GPU process belonging
  // to the same renderer process.
  int32 route_id() const { return route_id_; }

  gfx::GpuPreference gpu_preference() { return gpu_preference_; }

  // Sends a message to the console.
  void SendConsoleMessage(int32 id, const std::string& message);

  gfx::GLSurface* surface() const { return surface_; }

  void AddDestructionObserver(DestructionObserver* observer);
  void RemoveDestructionObserver(DestructionObserver* observer);

  // Associates a sync point to this stub. When the stub is destroyed, it will
  // retire all sync points that haven't been previously retired.
  void AddSyncPoint(uint32 sync_point);

  void SetPreemptByCounter(scoped_refptr<gpu::RefCountedCounter> counter);

 private:
  bool MakeCurrent();
  void Destroy();

  // Cleans up and sends reply if OnInitialize failed.
  void OnInitializeFailed(IPC::Message* reply_message);

  // Message handlers:
  void OnInitialize(IPC::Message* reply_message);
  void OnSetGetBuffer(int32 shm_id, IPC::Message* reply_message);
  void OnSetSharedStateBuffer(int32 shm_id, IPC::Message* reply_message);
  void OnSetParent(int32 parent_route_id,
                   uint32 parent_texture_id,
                   IPC::Message* reply_message);
  void OnGetState(IPC::Message* reply_message);
  void OnGetStateFast(IPC::Message* reply_message);
  void OnAsyncFlush(int32 put_offset, uint32 flush_count);
  void OnEcho(const IPC::Message& message);
  void OnRescheduled();
  void OnCreateTransferBuffer(int32 size,
                              int32 id_request,
                              IPC::Message* reply_message);
  void OnRegisterTransferBuffer(base::SharedMemoryHandle transfer_buffer,
                                size_t size,
                                int32 id_request,
                                IPC::Message* reply_message);
  void OnDestroyTransferBuffer(int32 id, IPC::Message* reply_message);
  void OnGetTransferBuffer(int32 id, IPC::Message* reply_message);

  void OnCreateVideoDecoder(
      media::VideoCodecProfile profile,
      IPC::Message* reply_message);
  void OnDestroyVideoDecoder(int32 decoder_route_id);

  void OnSetSurfaceVisible(bool visible);

  void OnDiscardBackbuffer();
  void OnEnsureBackbuffer();

  void OnRetireSyncPoint(uint32 sync_point);
  void OnWaitSyncPoint(uint32 sync_point);
  void OnSyncPointRetired();
  void OnSignalSyncPoint(uint32 sync_point, uint32 id);
  void OnSignalSyncPointAck(uint32 id);

  void OnSetClientHasMemoryAllocationChangedCallback(bool);

  void OnReschedule();

  void OnCommandProcessed();
  void OnParseError();

  void ReportState();

  // Wrapper for GpuScheduler::PutChanged that sets the crash report URL.
  void PutChanged();

  // Poll the command buffer to execute work.
  void PollWork();

  // Whether this command buffer needs to be polled again in the future.
  bool HasMoreWork();

  void ScheduleDelayedWork(int64 delay);

  // The lifetime of objects of this class is managed by a GpuChannel. The
  // GpuChannels destroy all the GpuCommandBufferStubs that they own when they
  // are destroyed. So a raw pointer is safe.
  GpuChannel* channel_;

  // The group of contexts that share namespaces with this context.
  scoped_refptr<gpu::gles2::ContextGroup> context_group_;

  gfx::GLSurfaceHandle handle_;
  gfx::Size initial_size_;
  gpu::gles2::DisallowedFeatures disallowed_features_;
  std::string allowed_extensions_;
  std::vector<int32> requested_attribs_;
  gfx::GpuPreference gpu_preference_;
  int32 route_id_;
  bool software_;
  bool client_has_memory_allocation_changed_callback_;
  uint32 last_flush_count_;
  scoped_ptr<GpuCommandBufferStubBase::SurfaceState> surface_state_;

  scoped_ptr<gpu::CommandBufferService> command_buffer_;
  scoped_ptr<gpu::gles2::GLES2Decoder> decoder_;
  scoped_ptr<gpu::GpuScheduler> scheduler_;
  scoped_refptr<gfx::GLContext> context_;
  scoped_refptr<gfx::GLSurface> surface_;

  // SetParent may be called before Initialize, in which case we need to keep
  // around the parent stub, so that Initialize can set the parent correctly.
  base::WeakPtr<GpuCommandBufferStub> parent_stub_for_initialization_;
  uint32 parent_texture_for_initialization_;

  GpuWatchdog* watchdog_;

  std::deque<IPC::Message*> delayed_echos_;

  // Zero or more video decoders owned by this stub, keyed by their
  // decoder_route_id.
  IDMap<GpuVideoDecodeAccelerator, IDMapOwnPointer> video_decoders_;

  ObserverList<DestructionObserver> destruction_observers_;

  // A queue of sync points associated with this stub.
  std::deque<uint32> sync_points_;
  int sync_point_wait_count_;

  bool delayed_work_scheduled_;

  scoped_refptr<gpu::RefCountedCounter> preempt_by_counter_;

  GURL active_url_;
  size_t active_url_hash_;

  DISALLOW_COPY_AND_ASSIGN(GpuCommandBufferStub);
};

#endif  // CONTENT_COMMON_GPU_GPU_COMMAND_BUFFER_STUB_H_
