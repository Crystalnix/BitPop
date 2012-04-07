// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NATIVE_CLIENT_SRC_SHARED_PPAPI_PROXY_PLUGIN_VAR_H_
#define NATIVE_CLIENT_SRC_SHARED_PPAPI_PROXY_PLUGIN_VAR_H_

#include <string>

#include "native_client/src/include/nacl_macros.h"
#include "native_client/src/include/portability.h"
#include "ppapi/c/pp_var.h"
#include "ppapi/c/ppb_var.h"
#include "ppapi/c/ppb_var_array_buffer.h"

namespace ppapi_proxy {

// Implements the plugin side of the PPB_Var interface.
// This implementation also determines how PP_Vars are represented internally
// in the proxied implementation.
class PluginVar {
 public:
  // Returns an interface pointer suitable to the PPAPI client.
  static const PPB_Var* GetInterface();
  // Returns the 1.0 interface to support backwards-compatibility.
  static const PPB_Var_1_0* GetInterface1_0();

  // Returns an interface pointer for the PPB_VarArrayBuffer interface.
  static const PPB_VarArrayBuffer* GetArrayBufferInterface();

  // String helpers.
  static PP_Var StringToPPVar(const std::string& str);
  static std::string PPVarToString(const PP_Var& var);

  // Printing and debugging.
  static void Print(const PP_Var& var);
  static std::string DebugString(const PP_Var& var);

 private:
  NACL_DISALLOW_COPY_AND_ASSIGN(PluginVar);
};

}  // namespace ppapi_proxy

#endif  // NATIVE_CLIENT_SRC_SHARED_PPAPI_PROXY_PLUGIN_VAR_H_

