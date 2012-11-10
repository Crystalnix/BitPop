// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_GPU_GPU_PROCESS_HOST_H_
#define CONTENT_BROWSER_GPU_GPU_PROCESS_HOST_H_

#include <map>
#include <queue>
#include <string>

#include "base/callback.h"
#include "base/memory/linked_ptr.h"
#include "base/process.h"
#include "base/threading/non_thread_safe.h"
#include "base/time.h"
#include "content/common/content_export.h"
#include "content/common/gpu/gpu_process_launch_causes.h"
#include "content/public/browser/browser_child_process_host_delegate.h"
#include "content/public/common/gpu_info.h"
#include "ipc/ipc_sender.h"
#include "ui/gfx/native_widget_types.h"

class GpuMainThread;
struct GPUCreateCommandBufferConfig;
struct GpuHostMsg_AcceleratedSurfaceBuffersSwapped_Params;
struct GpuHostMsg_AcceleratedSurfacePostSubBuffer_Params;
struct GpuHostMsg_AcceleratedSurfaceRelease_Params;

class BrowserChildProcessHostImpl;

namespace IPC {
struct ChannelHandle;
}

class GpuProcessHost : public content::BrowserChildProcessHostDelegate,
                       public IPC::Sender,
                       public base::NonThreadSafe {
 public:
  enum GpuProcessKind {
    GPU_PROCESS_KIND_UNSANDBOXED,
    GPU_PROCESS_KIND_SANDBOXED,
    GPU_PROCESS_KIND_COUNT
  };

  typedef base::Callback<void(const IPC::ChannelHandle&,
                              const content::GPUInfo&)>
      EstablishChannelCallback;

  typedef base::Callback<void(int32)> CreateCommandBufferCallback;

  static bool gpu_enabled() { return gpu_enabled_; }

  // Creates a new GpuProcessHost or gets an existing one, resulting in the
  // launching of a GPU process if required.  Returns null on failure. It
  // is not safe to store the pointer once control has returned to the message
  // loop as it can be destroyed. Instead store the associated GPU host ID.
  // This could return NULL if GPU access is not allowed (blacklisted).
  static GpuProcessHost* Get(GpuProcessKind kind,
                             content::CauseForGpuLaunch cause);

  // Helper function to send the given message to the GPU process on the IO
  // thread.  Calls Get and if a host is returned, sends it.  Can be called from
  // any thread.
  CONTENT_EXPORT static void SendOnIO(GpuProcessKind kind,
                                      content::CauseForGpuLaunch cause,
                                      IPC::Message* message);

  // Get the GPU process host for the GPU process with the given ID. Returns
  // null if the process no longer exists.
  static GpuProcessHost* FromID(int host_id);
  int host_id() const { return host_id_; }

  // IPC::Sender implementation.
  virtual bool Send(IPC::Message* msg) OVERRIDE;

  // Tells the GPU process to create a new channel for communication with a
  // client. Once the GPU process responds asynchronously with the IPC handle
  // and GPUInfo, we call the callback.
  void EstablishGpuChannel(int client_id,
                           bool share_context,
                           const EstablishChannelCallback& callback);

  // Tells the GPU process to create a new command buffer that draws into the
  // given surface.
  void CreateViewCommandBuffer(
      const gfx::GLSurfaceHandle& compositing_surface,
      int surface_id,
      int client_id,
      const GPUCreateCommandBufferConfig& init_params,
      const CreateCommandBufferCallback& callback);

  // Whether this GPU process is set up to use software rendering.
  bool software_rendering();

  // What kind of GPU process, e.g. sandboxed or unsandboxed.
  GpuProcessKind kind();

  void ForceShutdown();

 private:
  static bool HostIsValid(GpuProcessHost* host);

  GpuProcessHost(int host_id, GpuProcessKind kind);
  virtual ~GpuProcessHost();

  bool Init();

  // Post an IPC message to the UI shim's message handler on the UI thread.
  void RouteOnUIThread(const IPC::Message& message);

  // BrowserChildProcessHostDelegate implementation.
  virtual bool OnMessageReceived(const IPC::Message& message) OVERRIDE;
  virtual void OnChannelConnected(int32 peer_pid) OVERRIDE;
  virtual void OnProcessLaunched() OVERRIDE;
  virtual void OnProcessCrashed(int exit_code) OVERRIDE;

  // Message handlers.
  void OnChannelEstablished(const IPC::ChannelHandle& channel_handle);
  void OnCommandBufferCreated(const int32 route_id);
  void OnDestroyCommandBuffer(int32 surface_id);

#if defined(OS_MACOSX)
  void OnAcceleratedSurfaceBuffersSwapped(
      const GpuHostMsg_AcceleratedSurfaceBuffersSwapped_Params& params);
#endif
#if defined(OS_WIN) && !defined(USE_AURA)
  void OnAcceleratedSurfaceBuffersSwapped(
      const GpuHostMsg_AcceleratedSurfaceBuffersSwapped_Params& params);
  void OnAcceleratedSurfacePostSubBuffer(
      const GpuHostMsg_AcceleratedSurfacePostSubBuffer_Params& params);
  void OnAcceleratedSurfaceSuspend(int32 surface_id);
  void OnAcceleratedSurfaceRelease(
    const GpuHostMsg_AcceleratedSurfaceRelease_Params& params);
#endif

  bool LaunchGpuProcess(const std::string& channel_id);

  void SendOutstandingReplies();
  void EstablishChannelError(
      const EstablishChannelCallback& callback,
      const IPC::ChannelHandle& channel_handle,
      base::ProcessHandle client_process_for_gpu,
      const content::GPUInfo& gpu_info);
  void CreateCommandBufferError(const CreateCommandBufferCallback& callback,
                                int32 route_id);

  // The serial number of the GpuProcessHost / GpuProcessHostUIShim pair.
  int host_id_;

  // These are the channel requests that we have already sent to
  // the GPU process, but haven't heard back about yet.
  std::queue<EstablishChannelCallback> channel_requests_;

  // The pending create command buffer requests we need to reply to.
  std::queue<CreateCommandBufferCallback> create_command_buffer_requests_;

#if defined(TOOLKIT_GTK)
  // Encapsulates surfaces that we lock when creating view command buffers.
  // We release this lock once the command buffer (or associated GPU process)
  // is destroyed. This prevents the browser from destroying the surface
  // while the GPU process is drawing to it.

  // Multimap is used to simulate reference counting, see comment in
  // GpuProcessHostUIShim::CreateViewCommandBuffer.
  class SurfaceRef;
  typedef std::multimap<int, linked_ptr<SurfaceRef> > SurfaceRefMap;
  SurfaceRefMap surface_refs_;
#endif

  // Qeueud messages to send when the process launches.
  std::queue<IPC::Message*> queued_messages_;

  // Whether the GPU process is valid, set to false after Send() failed.
  bool valid_;

  // Whether we are running a GPU thread inside the browser process instead
  // of a separate GPU process.
  bool in_process_;

  bool software_rendering_;
  GpuProcessKind kind_;

  scoped_ptr<GpuMainThread> in_process_gpu_thread_;

  // Whether we actually launched a GPU process.
  bool process_launched_;

  // Time Init started.  Used to log total GPU process startup time to UMA.
  base::TimeTicks init_start_time_;

  // Master switch for enabling/disabling GPU acceleration for the current
  // browser session. It does not change the acceleration settings for
  // existing tabs, just the future ones.
  static bool gpu_enabled_;

  static bool hardware_gpu_enabled_;

  scoped_ptr<BrowserChildProcessHostImpl> process_;

  DISALLOW_COPY_AND_ASSIGN(GpuProcessHost);
};

#endif  // CONTENT_BROWSER_GPU_GPU_PROCESS_HOST_H_
