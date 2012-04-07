// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RENDERER_HOST_RENDER_WIDGET_HELPER_H_
#define CONTENT_BROWSER_RENDERER_HOST_RENDER_WIDGET_HELPER_H_
#pragma once

#include <deque>
#include <map>

#include "base/atomic_sequence_num.h"
#include "base/hash_tables.h"
#include "base/memory/ref_counted.h"
#include "base/process.h"
#include "base/synchronization/lock.h"
#include "base/synchronization/waitable_event.h"
#include "content/public/common/window_container_type.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebPopupType.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/gfx/surface/transport_dib.h"

namespace IPC {
class Message;
}

namespace base {
class TimeDelta;
}

class ResourceDispatcherHost;
struct ViewHostMsg_CreateWindow_Params;
struct ViewMsg_SwapOut_Params;


// Instantiated per RenderProcessHost to provide various optimizations on
// behalf of a RenderWidgetHost.  This class bridges between the IO thread
// where the RenderProcessHost's MessageFilter lives and the UI thread where
// the RenderWidgetHost lives.
//
//
// OPTIMIZED RESIZE
//
//   RenderWidgetHelper is used to implement optimized resize.  When the
//   RenderWidgetHost is resized, it sends a Resize message to its RenderWidget
//   counterpart in the renderer process.  The RenderWidget generates a
//   UpdateRect message in response to the Resize message, and it sets the
//   IS_RESIZE_ACK flag in the UpdateRect message to true.
//
//   Back in the browser process, when the RenderProcessHost's MessageFilter
//   sees a UpdateRect message, it directs it to the RenderWidgetHelper by
//   calling the DidReceiveUpdateMsg method.  That method stores the data for
//   the UpdateRect message in a map, where it can be directly accessed by the
//   RenderWidgetHost on the UI thread during a call to RenderWidgetHost's
//   GetBackingStore method.
//
//   When the RenderWidgetHost's GetBackingStore method is called, it first
//   checks to see if it is waiting for a resize ack.  If it is, then it calls
//   the RenderWidgetHelper's WaitForUpdateMsg to check if there is already a
//   resulting UpdateRect message (or to wait a short amount of time for one to
//   arrive).  The main goal of this mechanism is to short-cut the usual way in
//   which IPC messages are proxied over to the UI thread via InvokeLater.
//   This approach is necessary since window resize is followed up immediately
//   by a request to repaint the window.
//
//
// OPTIMIZED TAB SWITCHING
//
//   When a RenderWidgetHost is in a background tab, it is flagged as hidden.
//   This causes the corresponding RenderWidget to stop sending UpdateRect
//   messages. The RenderWidgetHost also discards its backingstore when it is
//   hidden, which helps free up memory.  As a result, when a RenderWidgetHost
//   is restored, it can be momentarily without a backingstore.  (Restoring a
//   RenderWidgetHost results in a WasRestored message being sent to the
//   RenderWidget, which triggers a full UpdateRect message.)  This can lead to
//   an observed rendering glitch as the TabContents will just have to fill
//   white overtop the RenderWidgetHost until the RenderWidgetHost receives a
//   UpdateRect message to refresh its backingstore.
//
//   To avoid this 'white flash', the RenderWidgetHost again makes use of the
//   RenderWidgetHelper's WaitForUpdateMsg method.  When the RenderWidgetHost's
//   GetBackingStore method is called, it will call WaitForUpdateMsg if it has
//   no backingstore.
//
// TRANSPORT DIB CREATION
//
//   On some platforms (currently the Mac) the renderer cannot create transport
//   DIBs because of sandbox limitations. Thus, it has to make synchronous IPCs
//   to the browser for them. Since these requests are synchronous, they cannot
//   terminate on the UI thread. Thus, in this case, this object performs the
//   allocation and maintains the set of allocated transport DIBs which the
//   renderers can refer to.
//
class RenderWidgetHelper
    : public base::RefCountedThreadSafe<RenderWidgetHelper> {
 public:
  RenderWidgetHelper();

  void Init(int render_process_id,
            ResourceDispatcherHost* resource_dispatcher_host);

  // Gets the next available routing id.  This is thread safe.
  int GetNextRoutingID();


  // UI THREAD ONLY -----------------------------------------------------------

  // These three functions provide the backend implementation of the
  // corresponding functions in RenderProcessHost. See those declarations
  // for documentation.
  void CancelResourceRequests(int render_widget_id);
  void CrossSiteSwapOutACK(const ViewMsg_SwapOut_Params& params);
  bool WaitForUpdateMsg(int render_widget_id,
                        const base::TimeDelta& max_delay,
                        IPC::Message* msg);

#if defined(OS_MACOSX)
  // Given the id of a transport DIB, return a mapping to it or NULL on error.
  TransportDIB* MapTransportDIB(TransportDIB::Id dib_id);
#endif

  // IO THREAD ONLY -----------------------------------------------------------

  // Called on the IO thread when a UpdateRect message is received.
  void DidReceiveUpdateMsg(const IPC::Message& msg);

  void CreateNewWindow(const ViewHostMsg_CreateWindow_Params& params,
                       base::ProcessHandle render_process,
                       int* route_id,
                       int* surface_id);
  void CreateNewWidget(int opener_id,
                       WebKit::WebPopupType popup_type,
                       int* route_id,
                       int* surface_id);
  void CreateNewFullscreenWidget(int opener_id, int* route_id, int* surface_id);

#if defined(OS_MACOSX)
  // Called on the IO thread to handle the allocation of a TransportDIB.  If
  // |cache_in_browser| is |true|, then a copy of the shmem is kept by the
  // browser, and it is the caller's repsonsibility to call
  // FreeTransportDIB().  In all cases, the caller is responsible for deleting
  // the resulting TransportDIB.
  void AllocTransportDIB(size_t size,
                         bool cache_in_browser,
                         TransportDIB::Handle* result);

  // Called on the IO thread to handle the freeing of a transport DIB
  void FreeTransportDIB(TransportDIB::Id dib_id);
#endif

 private:
  // A class used to proxy a paint message.  PaintMsgProxy objects are created
  // on the IO thread and destroyed on the UI thread.
  class UpdateMsgProxy;
  friend class UpdateMsgProxy;
  friend class base::RefCountedThreadSafe<RenderWidgetHelper>;

  typedef std::deque<UpdateMsgProxy*> UpdateMsgProxyQueue;
  // Map from render_widget_id to a queue of live PaintMsgProxy instances.
  typedef base::hash_map<int, UpdateMsgProxyQueue > UpdateMsgProxyMap;

  ~RenderWidgetHelper();

  // Called on the UI thread to discard a paint message.
  void OnDiscardUpdateMsg(UpdateMsgProxy* proxy);

  // Called on the UI thread to dispatch a paint message if necessary.
  void OnDispatchUpdateMsg(UpdateMsgProxy* proxy);

  // Called on the UI thread to finish creating a window.
  void OnCreateWindowOnUI(const ViewHostMsg_CreateWindow_Params& params,
                          int route_id);

  // Called on the IO thread after a window was created on the UI thread.
  void OnCreateWindowOnIO(int route_id);

  // Called on the UI thread to finish creating a widget.
  void OnCreateWidgetOnUI(int opener_id,
                          int route_id,
                          WebKit::WebPopupType popup_type);

  // Called on the UI thread to create a fullscreen widget.
  void OnCreateFullscreenWidgetOnUI(int opener_id, int route_id);

  // Called on the IO thread to cancel resource requests for the render widget.
  void OnCancelResourceRequests(int render_widget_id);

  // Called on the IO thread to resume a cross-site response.
  void OnCrossSiteSwapOutACK(const ViewMsg_SwapOut_Params& params);

#if defined(OS_MACOSX)
  // Called on destruction to release all allocated transport DIBs
  void ClearAllocatedDIBs();

  // On OSX we keep file descriptors to all the allocated DIBs around until
  // the renderer frees them.
  base::Lock allocated_dibs_lock_;
  std::map<TransportDIB::Id, int> allocated_dibs_;
#endif

  // A map of live paint messages.  Must hold pending_paints_lock_ to access.
  // The UpdateMsgProxy objects are not owned by this map.  (See UpdateMsgProxy
  // for details about how the lifetime of instances are managed.)
  UpdateMsgProxyMap pending_paints_;
  base::Lock pending_paints_lock_;

  int render_process_id_;

  // Event used to implement WaitForUpdateMsg.
  base::WaitableEvent event_;

  // The next routing id to use.
  base::AtomicSequenceNumber next_routing_id_;

  ResourceDispatcherHost* resource_dispatcher_host_;

  DISALLOW_COPY_AND_ASSIGN(RenderWidgetHelper);
};

#endif  // CONTENT_BROWSER_RENDERER_HOST_RENDER_WIDGET_HELPER_H_
