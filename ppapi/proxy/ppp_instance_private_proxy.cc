// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/proxy/ppp_instance_private_proxy.h"

#include <algorithm>

#include "ppapi/c/pp_var.h"
#include "ppapi/c/private/ppp_instance_private.h"
#include "ppapi/proxy/host_dispatcher.h"
#include "ppapi/proxy/plugin_dispatcher.h"
#include "ppapi/proxy/plugin_resource_tracker.h"
#include "ppapi/proxy/ppapi_messages.h"

namespace pp {
namespace proxy {

namespace {

PP_Var GetInstanceObject(PP_Instance instance) {
  Dispatcher* dispatcher = HostDispatcher::GetForInstance(instance);
  ReceiveSerializedVarReturnValue result;
  dispatcher->Send(new PpapiMsg_PPPInstancePrivate_GetInstanceObject(
      INTERFACE_ID_PPP_INSTANCE_PRIVATE, instance, &result));
  return result.Return(dispatcher);
}

static const PPP_Instance_Private instance_private_interface = {
  &GetInstanceObject
};

InterfaceProxy* CreateInstancePrivateProxy(Dispatcher* dispatcher,
                                           const void* target_interface) {
  return new PPP_Instance_Private_Proxy(dispatcher, target_interface);
}

}  // namespace

PPP_Instance_Private_Proxy::PPP_Instance_Private_Proxy(
    Dispatcher* dispatcher, const void* target_interface)
    : InterfaceProxy(dispatcher, target_interface) {
}

PPP_Instance_Private_Proxy::~PPP_Instance_Private_Proxy() {
}

// static
const InterfaceProxy::Info* PPP_Instance_Private_Proxy::GetInfo() {
  static const Info info = {
    &instance_private_interface,
    PPP_INSTANCE_PRIVATE_INTERFACE,
    INTERFACE_ID_PPP_INSTANCE_PRIVATE,
    false,
    &CreateInstancePrivateProxy,
  };
  return &info;
}

bool PPP_Instance_Private_Proxy::OnMessageReceived(const IPC::Message& msg) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(PPP_Instance_Private_Proxy, msg)
    IPC_MESSAGE_HANDLER(PpapiMsg_PPPInstancePrivate_GetInstanceObject,
                        OnMsgGetInstanceObject)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void PPP_Instance_Private_Proxy::OnMsgGetInstanceObject(
    PP_Instance instance,
    SerializedVarReturnValue result) {
  result.Return(dispatcher(),
                ppp_instance_private_target()->GetInstanceObject(instance));
}

}  // namespace proxy
}  // namespace pp
