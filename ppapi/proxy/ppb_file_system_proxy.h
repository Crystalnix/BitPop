// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PPAPI_PROXY_PPB_FILE_SYSTEM_PROXY_H_
#define PPAPI_PROXY_PPB_FILE_SYSTEM_PROXY_H_

#include <string>

#include "base/basictypes.h"
#include "ppapi/c/pp_instance.h"
#include "ppapi/c/pp_resource.h"
#include "ppapi/c/pp_time.h"
#include "ppapi/c/ppb_file_system.h"
#include "ppapi/proxy/interface_proxy.h"
#include "ppapi/proxy/proxy_completion_callback_factory.h"
#include "ppapi/utility/completion_callback_factory.h"

namespace ppapi {

class HostResource;

namespace proxy {

class PPB_FileSystem_Proxy : public InterfaceProxy {
 public:
  explicit PPB_FileSystem_Proxy(Dispatcher* dispatcher);
  virtual ~PPB_FileSystem_Proxy();

  static const Info* GetInfo();

  static PP_Resource CreateProxyResource(PP_Instance instance,
                                         PP_FileSystemType type);

  // InterfaceProxy implementation.
  virtual bool OnMessageReceived(const IPC::Message& msg);

  static const ApiID kApiID = API_ID_PPB_FILE_SYSTEM;

 private:
  // Message handlers.
  void OnMsgCreate(PP_Instance instance,
                   int type,
                   ppapi::HostResource* result);
  void OnMsgOpen(const ppapi::HostResource& filesystem,
                 int64_t expected_size);

  void OnMsgOpenComplete(const ppapi::HostResource& filesystem,
                         int32_t result);

  void OpenCompleteInHost(int32_t result,
                          const ppapi::HostResource& host_resource);

  ProxyCompletionCallbackFactory<PPB_FileSystem_Proxy> callback_factory_;

  DISALLOW_COPY_AND_ASSIGN(PPB_FileSystem_Proxy);
};

}  // namespace proxy
}  // namespace ppapi

#endif  // PPAPI_PROXY_PPB_FILE_SYSTEM_PROXY_H_
