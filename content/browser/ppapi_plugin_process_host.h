// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_PPAPI_PLUGIN_PROCESS_HOST_H_
#define CONTENT_BROWSER_PPAPI_PLUGIN_PROCESS_HOST_H_
#pragma once

#include <queue>

#include "base/basictypes.h"
#include "base/file_path.h"
#include "base/memory/ref_counted.h"
#include "content/browser/browser_child_process_host.h"
#include "content/browser/renderer_host/pepper_message_filter.h"

struct PepperPluginInfo;

namespace content {
class ResourceContext;
}

namespace net {
class HostResolver;
}

class PpapiPluginProcessHost : public BrowserChildProcessHost {
 public:
  class Client {
   public:
    // Gets the information about the renderer that's requesting the channel.
    virtual void GetChannelInfo(base::ProcessHandle* renderer_handle,
                                int* renderer_id) = 0;

    // Called when the channel is asynchronously opened to the plugin or on
    // error. On error, the parameters should be:
    //   base::kNullProcessHandle
    //   IPC::ChannelHandle()
    virtual void OnChannelOpened(base::ProcessHandle plugin_process_handle,
                                 const IPC::ChannelHandle& channel_handle) = 0;

    // Returns the resource context for the renderer requesting the channel.
    virtual const content::ResourceContext* GetResourceContext() = 0;
  };

  // You must call Init before doing anything else.
  PpapiPluginProcessHost(net::HostResolver* host_resolver);
  virtual ~PpapiPluginProcessHost();

  // Actually launches the process with the given plugin info. Returns true
  // on success (the process was spawned).
  bool Init(const PepperPluginInfo& info);

  // Opens a new channel to the plugin. The client will be notified when the
  // channel is ready or if there's an error.
  void OpenChannelToPlugin(Client* client);

  const FilePath& plugin_path() const { return plugin_path_; }

  // The client pointer must remain valid until its callback is issued.

 private:
  void RequestPluginChannel(Client* client);

  virtual bool CanShutdown();
  virtual void OnProcessLaunched();

  virtual bool OnMessageReceived(const IPC::Message& msg);
  virtual void OnChannelConnected(int32 peer_pid);
  virtual void OnChannelError();

  void CancelRequests();

  // IPC message handlers.
  void OnRendererPluginChannelCreated(const IPC::ChannelHandle& handle);

  // Handles most requests from the plugin.
  scoped_refptr<PepperMessageFilter> filter_;

  // Channel requests that we are waiting to send to the plugin process once
  // the channel is opened.
  std::vector<Client*> pending_requests_;

  // Channel requests that we have already sent to the plugin process, but
  // haven't heard back about yet.
  std::queue<Client*> sent_requests_;

  // Path to the plugin library.
  FilePath plugin_path_;

  DISALLOW_COPY_AND_ASSIGN(PpapiPluginProcessHost);
};

#endif  // CONTENT_BROWSER_PPAPI_PLUGIN_PROCESS_HOST_H_

