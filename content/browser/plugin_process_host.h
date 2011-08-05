// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_PLUGIN_PROCESS_HOST_H_
#define CONTENT_BROWSER_PLUGIN_PROCESS_HOST_H_
#pragma once

#include "build/build_config.h"

#include <queue>
#include <set>
#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/memory/ref_counted.h"
#include "content/browser/browser_child_process_host.h"
#include "webkit/plugins/npapi/webplugininfo.h"
#include "ui/gfx/native_widget_types.h"

namespace gfx {
class Rect;
}

namespace IPC {
struct ChannelHandle;
}

class GURL;

// Represents the browser side of the browser <--> plugin communication
// channel.  Different plugins run in their own process, but multiple instances
// of the same plugin run in the same process.  There will be one
// PluginProcessHost per plugin process, matched with a corresponding
// PluginProcess running in the plugin process.  The browser is responsible for
// starting the plugin process when a plugin is created that doesn't already
// have a process.  After that, most of the communication is directly between
// the renderer and plugin processes.
class PluginProcessHost : public BrowserChildProcessHost {
 public:
  class Client {
   public:
    // Returns a opaque unique identifier for the process requesting
    // the channel.
    virtual int ID() = 0;
    virtual bool OffTheRecord() = 0;
    virtual void SetPluginInfo(const webkit::npapi::WebPluginInfo& info) = 0;
    // The client should delete itself when one of these methods is called.
    virtual void OnChannelOpened(const IPC::ChannelHandle& handle) = 0;
    virtual void OnError() = 0;

   protected:
    virtual ~Client() {}
  };

  PluginProcessHost();
  virtual ~PluginProcessHost();

  // Initialize the new plugin process, returning true on success. This must
  // be called before the object can be used.
  bool Init(const webkit::npapi::WebPluginInfo& info, const std::string& locale);

  // Force the plugin process to shutdown (cleanly).
  virtual void ForceShutdown();

  virtual bool OnMessageReceived(const IPC::Message& msg);
  virtual void OnChannelConnected(int32 peer_pid);
  virtual void OnChannelError();

  // Tells the plugin process to create a new channel for communication with a
  // renderer.  When the plugin process responds with the channel name,
  // OnChannelOpened in the client is called.
  void OpenChannelToPlugin(Client* client);

  // This function is called on the IO thread once we receive a reply from the
  // modal HTML dialog (in the form of a JSON string). This function forwards
  // that reply back to the plugin that requested the dialog.
  void OnModalDialogResponse(const std::string& json_retval,
                             IPC::Message* sync_result);

#if defined(OS_MACOSX)
  // This function is called on the IO thread when the browser becomes the
  // active application.
  void OnAppActivation();
#endif

  const webkit::npapi::WebPluginInfo& info() const { return info_; }

#if defined(OS_WIN)
  // Tracks plugin parent windows created on the browser UI thread.
  void AddWindow(HWND window);
#endif

 private:
  // Sends a message to the plugin process to request creation of a new channel
  // for the given mime type.
  void RequestPluginChannel(Client* client);

  // Message handlers.
  void OnChannelCreated(const IPC::ChannelHandle& channel_handle);

#if defined(OS_WIN)
  void OnPluginWindowDestroyed(HWND window, HWND parent);
  void OnReparentPluginWindow(HWND window, HWND parent);
#endif

#if defined(USE_X11)
  void OnMapNativeViewId(gfx::NativeViewId id, gfx::PluginWindowHandle* output);
#endif

#if defined(OS_MACOSX)
  void OnPluginSelectWindow(uint32 window_id, gfx::Rect window_rect,
                            bool modal);
  void OnPluginShowWindow(uint32 window_id, gfx::Rect window_rect,
                          bool modal);
  void OnPluginHideWindow(uint32 window_id, gfx::Rect window_rect);
  void OnPluginSetCursorVisibility(bool visible);
#endif

  virtual bool CanShutdown();

  void CancelRequests();

  // These are channel requests that we are waiting to send to the
  // plugin process once the channel is opened.
  std::vector<Client*> pending_requests_;

  // These are the channel requests that we have already sent to
  // the plugin process, but haven't heard back about yet.
  std::queue<Client*> sent_requests_;

  // Information about the plugin.
  webkit::npapi::WebPluginInfo info_;

#if defined(OS_WIN)
  // Tracks plugin parent windows created on the UI thread.
  std::set<HWND> plugin_parent_windows_set_;
#endif
#if defined(OS_MACOSX)
  // Tracks plugin windows currently visible.
  std::set<uint32> plugin_visible_windows_set_;
  // Tracks full screen windows currently visible.
  std::set<uint32> plugin_fullscreen_windows_set_;
  // Tracks modal windows currently visible.
  std::set<uint32> plugin_modal_windows_set_;
  // Tracks the current visibility of the cursor.
  bool plugin_cursor_visible_;
#endif

  DISALLOW_COPY_AND_ASSIGN(PluginProcessHost);
};

#endif  // CONTENT_BROWSER_PLUGIN_PROCESS_HOST_H_
