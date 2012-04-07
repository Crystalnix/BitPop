// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PPAPI_PROXY_PPB_CURSOR_CONTROL_PROXY_H_
#define PPAPI_PROXY_PPB_CURSOR_CONTROL_PROXY_H_

#include "ppapi/c/pp_completion_callback.h"
#include "ppapi/c/pp_bool.h"
#include "ppapi/c/pp_instance.h"
#include "ppapi/c/pp_point.h"
#include "ppapi/c/pp_resource.h"
#include "ppapi/proxy/interface_proxy.h"
#include "ppapi/shared_impl/function_group_base.h"
#include "ppapi/shared_impl/host_resource.h"
#include "ppapi/thunk/ppb_cursor_control_api.h"

namespace ppapi {
namespace proxy {

class PPB_CursorControl_Proxy
    : public InterfaceProxy,
      public ppapi::thunk::PPB_CursorControl_FunctionAPI {
 public:
  explicit PPB_CursorControl_Proxy(Dispatcher* dispatcher);
  virtual ~PPB_CursorControl_Proxy();

  // FunctionGroupBase overrides.
  ppapi::thunk::PPB_CursorControl_FunctionAPI* AsPPB_CursorControl_FunctionAPI()
      OVERRIDE;

  // PPB_CursorControl_FunctionAPI implementation.
  virtual PP_Bool SetCursor(PP_Instance instance,
                            PP_CursorType_Dev type,
                            PP_Resource custom_image_id,
                            const PP_Point* hot_spot) OVERRIDE;
  virtual PP_Bool LockCursor(PP_Instance instance) OVERRIDE;
  virtual PP_Bool UnlockCursor(PP_Instance instance) OVERRIDE;
  virtual PP_Bool HasCursorLock(PP_Instance instance) OVERRIDE;
  virtual PP_Bool CanLockCursor(PP_Instance instance) OVERRIDE;

  // InterfaceProxy implementation.
  virtual bool OnMessageReceived(const IPC::Message& msg) OVERRIDE;

  static const ApiID kApiID = API_ID_PPB_CURSORCONTROL;

 private:
  // Message handlers.
  void OnMsgSetCursor(PP_Instance instance,
                      int32_t type,
                      const ppapi::HostResource& custom_image,
                      const PP_Point& hot_spot,
                      PP_Bool* result);
  void OnMsgLockCursor(PP_Instance instance,
                       PP_Bool* result);
  void OnMsgUnlockCursor(PP_Instance instance,
                         PP_Bool* result);
  void OnMsgHasCursorLock(PP_Instance instance,
                          PP_Bool* result);
  void OnMsgCanLockCursor(PP_Instance instance,
                          PP_Bool* result);
};

}  // namespace proxy
}  // namespace ppapi

#endif  // PPAPI_PROXY_PPB_CURSOR_CONTROL_PROXY_H_
