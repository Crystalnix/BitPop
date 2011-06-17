// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PPAPI_PLUGIN_PPAPI_THREAD_H_
#define CONTENT_PPAPI_PLUGIN_PPAPI_THREAD_H_
#pragma once

#include "base/basictypes.h"
#include "base/memory/scoped_native_library.h"
#include "base/memory/scoped_ptr.h"
#include "base/process.h"
#include "build/build_config.h"
#include "content/common/child_thread.h"
#include "ppapi/c/pp_module.h"
#include "ppapi/proxy/dispatcher.h"
#include "ppapi/c/trusted/ppp_broker.h"

class FilePath;

namespace IPC {
struct ChannelHandle;
}

class PpapiThread : public ChildThread,
                    public pp::proxy::Dispatcher::Delegate {
 public:
  explicit PpapiThread(bool is_broker);
  ~PpapiThread();

 private:
  // ChildThread overrides.
  virtual bool OnMessageReceived(const IPC::Message& msg);

  // Dispatcher::Delegate implementation.
  virtual MessageLoop* GetIPCMessageLoop();
  virtual base::WaitableEvent* GetShutdownEvent();
  virtual std::set<PP_Instance>* GetGloballySeenInstanceIDSet();

  // Message handlers.
  void OnMsgLoadPlugin(const FilePath& path);
  void OnMsgCreateChannel(base::ProcessHandle host_process_handle,
                          int renderer_id);

  // Sets up the channel to the given renderer. On success, returns true and
  // fills the given ChannelHandle with the information from the new channel.
  bool SetupRendererChannel(base::ProcessHandle host_process_handle,
                            int renderer_id,
                            IPC::ChannelHandle* handle);

  // True if running in a broker process rather than a normal plugin process.
  bool is_broker_;

  base::ScopedNativeLibrary library_;

  pp::proxy::Dispatcher::GetInterfaceFunc get_plugin_interface_;

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

  DISALLOW_IMPLICIT_CONSTRUCTORS(PpapiThread);
};

#endif  // CONTENT_PPAPI_PLUGIN_PPAPI_THREAD_H_
