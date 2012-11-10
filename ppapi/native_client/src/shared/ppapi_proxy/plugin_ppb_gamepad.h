// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NATIVE_CLIENT_SRC_SHARED_PPAPI_PROXY_PLUGIN_PPB_GAMEPAD_H_
#define NATIVE_CLIENT_SRC_SHARED_PPAPI_PROXY_PLUGIN_PPB_GAMEPAD_H_

#include "native_client/src/include/nacl_macros.h"
#include "ppapi/c/ppb_gamepad.h"

namespace ppapi_proxy {

// Implements the untrusted side of the PPB_Gamepad interface.
class PluginGamepad {
 public:
  static const PPB_Gamepad* GetInterface();

 private:
  NACL_DISALLOW_COPY_AND_ASSIGN(PluginGamepad);
};

}  // namespace ppapi_proxy

#endif  // NATIVE_CLIENT_SRC_SHARED_PPAPI_PROXY_PLUGIN_PPB_GAMEPAD_H_
