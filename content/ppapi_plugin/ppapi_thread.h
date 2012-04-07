// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PPAPI_PLUGIN_PPAPI_THREAD_H_
#define CONTENT_PPAPI_PLUGIN_PPAPI_THREAD_H_
#pragma once

#include <map>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "base/process.h"
#include "base/scoped_native_library.h"
#include "build/build_config.h"
#include "content/common/child_thread.h"
#include "ppapi/c/pp_module.h"
#include "ppapi/c/trusted/ppp_broker.h"
#include "ppapi/proxy/plugin_dispatcher.h"
#include "ppapi/proxy/plugin_globals.h"
#include "ppapi/proxy/plugin_proxy_delegate.h"

class FilePath;
class PpapiWebKitPlatformSupportImpl;

namespace IPC {
struct ChannelHandle;
}

class PpapiThread : public ChildThread,
                    public ppapi::proxy::PluginDispatcher::PluginDelegate,
                    public ppapi::proxy::PluginProxyDelegate {
 public:
  explicit PpapiThread(bool is_broker);
  virtual ~PpapiThread();

 private:
  // ChildThread overrides.
  virtual bool OnMessageReceived(const IPC::Message& msg) OVERRIDE;

  // PluginDispatcher::PluginDelegate implementation.
  virtual std::set<PP_Instance>* GetGloballySeenInstanceIDSet() OVERRIDE;
  virtual base::MessageLoopProxy* GetIPCMessageLoop() OVERRIDE;
  virtual base::WaitableEvent* GetShutdownEvent() OVERRIDE;
  virtual uint32 Register(
      ppapi::proxy::PluginDispatcher* plugin_dispatcher) OVERRIDE;
  virtual void Unregister(uint32 plugin_dispatcher_id) OVERRIDE;

  // PluginProxyDelegate.
  virtual bool SendToBrowser(IPC::Message* msg) OVERRIDE;
  virtual void PreCacheFont(const void* logfontw) OVERRIDE;

  // Message handlers.
  void OnMsgLoadPlugin(const FilePath& path);
  void OnMsgCreateChannel(base::ProcessHandle host_process_handle,
                          int renderer_id);
  void OnMsgSetNetworkState(bool online);
  void OnPluginDispatcherMessageReceived(const IPC::Message& msg);

  // Sets up the channel to the given renderer. On success, returns true and
  // fills the given ChannelHandle with the information from the new channel.
  bool SetupRendererChannel(base::ProcessHandle host_process_handle,
                            int renderer_id,
                            IPC::ChannelHandle* handle);

  // Sets up the name of the plugin for logging using the given path.
  void SavePluginName(const FilePath& path);

  // True if running in a broker process rather than a normal plugin process.
  bool is_broker_;

  base::ScopedNativeLibrary library_;

  // Global state tracking for the proxy.
  ppapi::proxy::PluginGlobals plugin_globals_;

  ppapi::proxy::Dispatcher::GetInterfaceFunc get_plugin_interface_;

  // Callback to call when a new instance connects to the broker.
  // Used only when is_broker_.
  PP_ConnectInstance_Func connect_instance_func_;

  // Local concept of the module ID. Some functions take this. It's necessary
  // for the in-process PPAPI to handle this properly, but for proxied it's
  // unnecessary. The proxy talking to multiple renderers means that each
  // renderer has a different idea of what the module ID is for this plugin.
  // To force people to "do the right thing" we generate a random module ID
  // and pass it around as necessary.
  PP_Module local_pp_module_;

  // See Dispatcher::Delegate::GetGloballySeenInstanceIDSet.
  std::set<PP_Instance> globally_seen_instance_ids_;

  // The PluginDispatcher instances contained in the map are not owned by it.
  std::map<uint32, ppapi::proxy::PluginDispatcher*> plugin_dispatchers_;
  uint32 next_plugin_dispatcher_id_;

  // The WebKitPlatformSupport implementation.
  scoped_ptr<PpapiWebKitPlatformSupportImpl> webkit_platform_support_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(PpapiThread);
};

#endif  // CONTENT_PPAPI_PLUGIN_PPAPI_THREAD_H_
