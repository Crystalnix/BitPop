// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PPAPI_PROXY_PPP_GRAPHICS_3D_PROXY_H_
#define PPAPI_PROXY_PPP_GRAPHICS_3D_PROXY_H_

#include "ppapi/c/pp_instance.h"
#include "ppapi/proxy/host_resource.h"
#include "ppapi/proxy/interface_proxy.h"

struct PPP_Graphics3D_Dev;

namespace pp {
namespace proxy {

class PPP_Graphics3D_Proxy : public InterfaceProxy {
 public:
  PPP_Graphics3D_Proxy(Dispatcher* dispatcher, const void* target_interface);
  virtual ~PPP_Graphics3D_Proxy();

  static const Info* GetInfo();

  const PPP_Graphics3D_Dev* ppp_graphics_3d_target() const {
    return reinterpret_cast<const PPP_Graphics3D_Dev*>(target_interface());
  }

  // InterfaceProxy implementation.
  virtual bool OnMessageReceived(const IPC::Message& msg);

 private:
  // Message handlers.
  void OnMsgContextLost(PP_Instance instance);
};

}  // namespace proxy
}  // namespace pp

#endif  // PPAPI_PROXY_PPP_GRAPHICS_3D_PROXY_H_
