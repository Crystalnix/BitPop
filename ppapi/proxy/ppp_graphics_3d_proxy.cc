// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/proxy/ppp_graphics_3d_proxy.h"

#include "ppapi/c/dev/ppp_graphics_3d_dev.h"
#include "ppapi/proxy/host_dispatcher.h"
#include "ppapi/proxy/plugin_dispatcher.h"
#include "ppapi/proxy/ppapi_messages.h"

namespace pp {
namespace proxy {

namespace {

void ContextLost(PP_Instance instance) {
  HostDispatcher::GetForInstance(instance)->Send(
      new PpapiMsg_PPPGraphics3D_ContextLost(INTERFACE_ID_PPP_GRAPHICS_3D_DEV,
                                             instance));
}

static const PPP_Graphics3D_Dev graphics_3d_interface = {
  &ContextLost
};

InterfaceProxy* CreateGraphics3DProxy(Dispatcher* dispatcher,
                                      const void* target_interface) {
  return new PPP_Graphics3D_Proxy(dispatcher, target_interface);
}

}  // namespace

PPP_Graphics3D_Proxy::PPP_Graphics3D_Proxy(Dispatcher* dispatcher,
                                           const void* target_interface)
    : InterfaceProxy(dispatcher, target_interface) {
}

PPP_Graphics3D_Proxy::~PPP_Graphics3D_Proxy() {
}

// static
const InterfaceProxy::Info* PPP_Graphics3D_Proxy::GetInfo() {
  static const Info info = {
    &graphics_3d_interface,
    PPP_GRAPHICS_3D_DEV_INTERFACE,
    INTERFACE_ID_PPP_GRAPHICS_3D_DEV,
    false,
    &CreateGraphics3DProxy,
  };
  return &info;
}

bool PPP_Graphics3D_Proxy::OnMessageReceived(const IPC::Message& msg) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(PPP_Graphics3D_Proxy, msg)
    IPC_MESSAGE_HANDLER(PpapiMsg_PPPGraphics3D_ContextLost,
                        OnMsgContextLost)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void PPP_Graphics3D_Proxy::OnMsgContextLost(PP_Instance instance) {
  if (ppp_graphics_3d_target())
    ppp_graphics_3d_target()->Graphics3DContextLost(instance);
}

}  // namespace proxy
}  // namespace pp
