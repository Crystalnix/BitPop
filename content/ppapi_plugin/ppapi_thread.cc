// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/ppapi_plugin/ppapi_thread.h"

#include <limits>

#include "base/process_util.h"
#include "base/rand_util.h"
#include "content/common/child_process.h"
#include "content/ppapi_plugin/broker_process_dispatcher.h"
#include "content/ppapi_plugin/plugin_process_dispatcher.h"
#include "ipc/ipc_channel_handle.h"
#include "ipc/ipc_sync_channel.h"
#include "ppapi/c/pp_errors.h"
#include "ppapi/c/ppp.h"
#include "ppapi/proxy/ppapi_messages.h"

typedef int32_t (*InitializeBrokerFunc)
    (PP_ConnectInstance_Func* connect_instance_func);

PpapiThread::PpapiThread(bool is_broker)
    : is_broker_(is_broker),
      get_plugin_interface_(NULL),
      connect_instance_func_(NULL),
      local_pp_module_(
          base::RandInt(0, std::numeric_limits<PP_Module>::max())) {
}

PpapiThread::~PpapiThread() {
  if (!library_.is_valid())
    return;

  // The ShutdownModule/ShutdownBroker function is optional.
  pp::proxy::ProxyChannel::ShutdownModuleFunc shutdown_function =
      is_broker_ ?
      reinterpret_cast<pp::proxy::ProxyChannel::ShutdownModuleFunc>(
          library_.GetFunctionPointer("PPP_ShutdownBroker")) :
      reinterpret_cast<pp::proxy::ProxyChannel::ShutdownModuleFunc>(
          library_.GetFunctionPointer("PPP_ShutdownModule"));
  if (shutdown_function)
    shutdown_function();
}

// The "regular" ChildThread implements this function and does some standard
// dispatching, then uses the message router. We don't actually need any of
// this so this function just overrides that one.
//
// Note that this function is called only for messages from the channel to the
// browser process. Messages from the renderer process are sent via a different
// channel that ends up at Dispatcher::OnMessageReceived.
bool PpapiThread::OnMessageReceived(const IPC::Message& msg) {
  IPC_BEGIN_MESSAGE_MAP(PpapiThread, msg)
    IPC_MESSAGE_HANDLER(PpapiMsg_LoadPlugin, OnMsgLoadPlugin)
    IPC_MESSAGE_HANDLER(PpapiMsg_CreateChannel, OnMsgCreateChannel)
  IPC_END_MESSAGE_MAP()
  return true;
}

MessageLoop* PpapiThread::GetIPCMessageLoop() {
  return ChildProcess::current()->io_message_loop();
}

base::WaitableEvent* PpapiThread::GetShutdownEvent() {
  return ChildProcess::current()->GetShutDownEvent();
}

std::set<PP_Instance>* PpapiThread::GetGloballySeenInstanceIDSet() {
  return &globally_seen_instance_ids_;
}

void PpapiThread::OnMsgLoadPlugin(const FilePath& path) {
  base::ScopedNativeLibrary library(base::LoadNativeLibrary(path, NULL));
  if (!library.is_valid())
    return;

  if (is_broker_) {
    // Get the InitializeBroker function (required).
    InitializeBrokerFunc init_broker =
        reinterpret_cast<InitializeBrokerFunc>(
            library.GetFunctionPointer("PPP_InitializeBroker"));
    if (!init_broker) {
      LOG(WARNING) << "No PPP_InitializeBroker in plugin library";
      return;
    }

    int32_t init_error = init_broker(&connect_instance_func_);
    if (init_error != PP_OK) {
      LOG(WARNING) << "InitBroker failed with error " << init_error;
      return;
    }
    if (!connect_instance_func_) {
      LOG(WARNING) << "InitBroker did not provide PP_ConnectInstance_Func";
      return;
    }
  } else {
    // Get the GetInterface function (required).
    get_plugin_interface_ =
        reinterpret_cast<pp::proxy::Dispatcher::GetInterfaceFunc>(
            library.GetFunctionPointer("PPP_GetInterface"));
    if (!get_plugin_interface_) {
      LOG(WARNING) << "No PPP_GetInterface in plugin library";
      return;
    }

    // Get the InitializeModule function (required).
    pp::proxy::Dispatcher::InitModuleFunc init_module =
        reinterpret_cast<pp::proxy::Dispatcher::InitModuleFunc>(
            library.GetFunctionPointer("PPP_InitializeModule"));
    if (!init_module) {
      LOG(WARNING) << "No PPP_InitializeModule in plugin library";
      return;
    }
    int32_t init_error = init_module(
        local_pp_module_,
        &pp::proxy::PluginDispatcher::GetInterfaceFromDispatcher);
    if (init_error != PP_OK) {
      LOG(WARNING) << "InitModule failed with error " << init_error;
      return;
    }
  }

  library_.Reset(library.Release());
}

void PpapiThread::OnMsgCreateChannel(base::ProcessHandle host_process_handle,
                                     int renderer_id) {
  IPC::ChannelHandle channel_handle;
  if (!library_.is_valid() ||  // Plugin couldn't be loaded.
      !SetupRendererChannel(host_process_handle, renderer_id,
                            &channel_handle)) {
    Send(new PpapiHostMsg_ChannelCreated(IPC::ChannelHandle()));
    return;
  }

  Send(new PpapiHostMsg_ChannelCreated(channel_handle));
}

bool PpapiThread::SetupRendererChannel(base::ProcessHandle host_process_handle,
                                       int renderer_id,
                                       IPC::ChannelHandle* handle) {
  DCHECK(is_broker_ == (connect_instance_func_ != NULL));
  DCHECK(is_broker_ == (get_plugin_interface_ == NULL));
  IPC::ChannelHandle plugin_handle;
  plugin_handle.name = StringPrintf("%d.r%d", base::GetCurrentProcId(),
                                    renderer_id);

  pp::proxy::ProxyChannel* dispatcher = NULL;
  bool init_result = false;
  if (is_broker_) {
    BrokerProcessDispatcher* broker_dispatcher =
      new BrokerProcessDispatcher(host_process_handle, connect_instance_func_);
      init_result = broker_dispatcher->InitBrokerWithChannel(this,
                                                             plugin_handle,
                                                             false);
      dispatcher = broker_dispatcher;
  } else {
    PluginProcessDispatcher* plugin_dispatcher =
        new PluginProcessDispatcher(host_process_handle, get_plugin_interface_);
    init_result = plugin_dispatcher->InitPluginWithChannel(this,
                                                           plugin_handle,
                                                           false);
    dispatcher = plugin_dispatcher;
  }

  if (!init_result) {
    delete dispatcher;
    return false;
  }

  handle->name = plugin_handle.name;
#if defined(OS_POSIX)
  // On POSIX, pass the renderer-side FD.
  handle->socket = base::FileDescriptor(::dup(dispatcher->GetRendererFD()),
                                        true);
#endif

  // From here, the dispatcher will manage its own lifetime according to the
  // lifetime of the attached channel.
  return true;
}
