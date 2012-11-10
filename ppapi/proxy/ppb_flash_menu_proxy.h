// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PPAPI_PPB_FLASH_MENU_PROXY_H_
#define PPAPI_PPB_FLASH_MENU_PROXY_H_

#include "ppapi/proxy/interface_proxy.h"
#include "ppapi/proxy/proxy_completion_callback_factory.h"
#include "ppapi/utility/completion_callback_factory.h"

struct PP_Flash_Menu;
struct PP_Point;

namespace ppapi {

class HostResource;

namespace proxy {

class SerializedFlashMenu;

class PPB_Flash_Menu_Proxy : public InterfaceProxy {
 public:
  PPB_Flash_Menu_Proxy(Dispatcher* dispatcher);
  virtual ~PPB_Flash_Menu_Proxy();

  static PP_Resource CreateProxyResource(PP_Instance instance_id,
                                         const PP_Flash_Menu* menu_data);

  // InterfaceProxy implementation.
  virtual bool OnMessageReceived(const IPC::Message& msg);

  static const ApiID kApiID = API_ID_PPB_FLASH_MENU;

 private:
  struct ShowRequest;

  void OnMsgCreate(PP_Instance instance_id,
                   const SerializedFlashMenu& menu_data,
                   ppapi::HostResource* resource);
  void OnMsgShow(const ppapi::HostResource& menu,
                 const PP_Point& location);
  void OnMsgShowACK(const ppapi::HostResource& menu,
                    int32_t selected_id,
                    int32_t result);
  void SendShowACKToPlugin(int32_t result, ShowRequest* request);

  ProxyCompletionCallbackFactory<PPB_Flash_Menu_Proxy> callback_factory_;
};

}  // namespace proxy
}  // namespace ppapi

#endif  // PPAPI_PPB_FLASH_MENU_PROXY_H_
