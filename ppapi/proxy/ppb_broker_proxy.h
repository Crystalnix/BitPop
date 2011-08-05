// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PPAPI_PPB_BROKER_PROXY_H_
#define PPAPI_PPB_BROKER_PROXY_H_

#include "base/sync_socket.h"
#include "ipc/ipc_platform_file.h"
#include "ppapi/c/pp_instance.h"
#include "ppapi/cpp/completion_callback.h"
#include "ppapi/proxy/interface_proxy.h"
#include "ppapi/proxy/proxy_non_thread_safe_ref_count.h"


struct PPB_BrokerTrusted;

namespace pp {
namespace proxy {

class HostResource;

class PPB_Broker_Proxy : public InterfaceProxy {
 public:
  PPB_Broker_Proxy(Dispatcher* dispatcher, const void* target_interface);
  virtual ~PPB_Broker_Proxy();

  static const Info* GetInfo();

  static PP_Resource CreateProxyResource(PP_Instance instance);

  const PPB_BrokerTrusted* ppb_broker_target() const {
    return static_cast<const PPB_BrokerTrusted*>(target_interface());
  }

  // InterfaceProxy implementation.
  virtual bool OnMessageReceived(const IPC::Message& msg);

 private:
  // Message handlers.
  void OnMsgCreate(PP_Instance instance, HostResource* result_resource);
  void OnMsgConnect(const HostResource& broker);
  void OnMsgConnectComplete(const HostResource& broker,
                            IPC::PlatformFileForTransit foreign_socket_handle,
                            int32_t result);

  void ConnectCompleteInHost(int32_t result, const HostResource& host_resource);

  CompletionCallbackFactory<PPB_Broker_Proxy,
                            ProxyNonThreadSafeRefCount> callback_factory_;
};

}  // namespace proxy
}  // namespace pp

#endif  // PPAPI_PPB_BROKER_PROXY_H_
