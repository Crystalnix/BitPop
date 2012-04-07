// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "native_client/src/shared/ppapi_proxy/browser_ppp_instance.h"

#include <stdio.h>
#include <string.h>
#include <limits>

#include "native_client/src/include/checked_cast.h"
#include "native_client/src/include/nacl_scoped_ptr.h"
#include "native_client/src/include/portability.h"
#include "native_client/src/shared/ppapi_proxy/browser_globals.h"
#include "native_client/src/shared/ppapi_proxy/browser_ppp.h"
#include "native_client/src/shared/ppapi_proxy/trusted/srpcgen/ppp_rpc.h"
#include "native_client/src/shared/ppapi_proxy/utility.h"
#include "native_client/src/shared/ppapi_proxy/view_data.h"
#include "ppapi/c/pp_resource.h"
#include "ppapi/c/pp_var.h"
#include "ppapi/c/ppp_instance.h"
#include "ppapi/c/ppb_view.h"

using nacl::scoped_array;
using ppapi_proxy::ViewData;

namespace ppapi_proxy {

namespace {

char* ArgArraySerialize(int argc,
                        const char* array[],
                        uint32_t* serial_size) {
  uint32_t used = 0;
  for (int i = 0; i < argc; ++i) {
    // Note that strlen() cannot ever return SIZE_T_MAX, since that would imply
    // that there were no nulls anywhere in memory, which would lead to
    // strlen() never terminating. So this assignment is safe.
    size_t len = strlen(array[i]) + 1;
    if (len > std::numeric_limits<uint32_t>::max()) {
      // Overflow, input string is too long.
      return NULL;
    }
    if (used > std::numeric_limits<uint32_t>::max() - len) {
      // Overflow, output string is too long.
      return NULL;
    }
    used += static_cast<uint32_t>(len);
  }
  // Note that there is a check against numeric_limits<uint32_t> in the code
  // above, which is why this cast is safe.
  *serial_size = used;
  char* serial_array = new char[used];

  size_t pos = 0;
  for (int i = 0; i < argc; ++i) {
    size_t len = strlen(array[i]) + 1;
    strncpy(serial_array + pos, array[i], len);
    pos += len;
  }
  return serial_array;
}

PP_Bool DidCreate(PP_Instance instance,
                  uint32_t argc,
                  const char* argn[],
                  const char* argv[]) {
  DebugPrintf("PPP_Instance::DidCreate: instance=%"NACL_PRId32"\n", instance);
  uint32_t argn_size;
  scoped_array<char> argn_serial(ArgArraySerialize(argc, argn, &argn_size));
  if (argn_serial.get() == NULL) {
    return PP_FALSE;
  }
  uint32_t argv_size;
  scoped_array<char> argv_serial(ArgArraySerialize(argc, argv, &argv_size));
  if (argv_serial.get() == NULL) {
    return PP_FALSE;
  }
  int32_t int_argc = static_cast<int32_t>(argc);
  int32_t success;
  NaClSrpcError srpc_result =
      PppInstanceRpcClient::PPP_Instance_DidCreate(GetMainSrpcChannel(instance),
                                                   instance,
                                                   int_argc,
                                                   argn_size,
                                                   argn_serial.get(),
                                                   argv_size,
                                                   argv_serial.get(),
                                                   &success);
  DebugPrintf("PPP_Instance::DidCreate: %s\n",
              NaClSrpcErrorString(srpc_result));
  if (srpc_result != NACL_SRPC_RESULT_OK) {
    return PP_FALSE;
  }
  return PP_FromBool(success);
}

void DidDestroy(PP_Instance instance) {
  DebugPrintf("PPP_Instance::DidDestroy: instance=%"NACL_PRId32"\n", instance);
  NaClSrpcError srpc_result = PppInstanceRpcClient::PPP_Instance_DidDestroy(
      GetMainSrpcChannel(instance), instance);
  DebugPrintf("PPP_Instance::DidDestroy: %s\n",
              NaClSrpcErrorString(srpc_result));
}

void DidChangeView(PP_Instance instance, PP_Resource view) {
  DebugPrintf("PPP_Instance::DidChangeView: instance=%"NACL_PRId32"\n",
              instance);
  ViewData view_data;

  const PPB_View* view_interface = PPBViewInterface();
  view_interface->GetRect(view, &view_data.viewport_rect);
  view_data.is_fullscreen = view_interface->IsFullscreen(view);
  view_data.is_page_visible = view_interface->IsPageVisible(view);
  view_interface->GetClipRect(view, &view_data.clip_rect);

  NaClSrpcError srpc_result = PppInstanceRpcClient::PPP_Instance_DidChangeView(
      GetMainSrpcChannel(instance),
      instance,
      view,
      sizeof(ViewData),
      reinterpret_cast<char*>(&view_data));
  DebugPrintf("PPP_Instance::DidChangeView: %s\n",
              NaClSrpcErrorString(srpc_result));
}

void DidChangeFocus(PP_Instance instance, PP_Bool has_focus) {
  DebugPrintf("PPP_Instance::DidChangeFocus: instance=%"NACL_PRId32", "
              "has_focus = %d\n", instance, has_focus);
  NaClSrpcError srpc_result = PppInstanceRpcClient::PPP_Instance_DidChangeFocus(
      GetMainSrpcChannel(instance),
      instance,
      PP_ToBool(has_focus));
  DebugPrintf("PPP_Instance::DidChangeFocus: %s\n",
              NaClSrpcErrorString(srpc_result));
}

PP_Bool HandleDocumentLoad(PP_Instance instance, PP_Resource url_loader) {
  DebugPrintf("PPP_Instance::HandleDocumentLoad: instance=%"NACL_PRId32", "
              "url_loader=%"NACL_PRId32"\n", instance, url_loader);

  int32_t result = 0;
  NaClSrpcError srpc_result =
      PppInstanceRpcClient::PPP_Instance_HandleDocumentLoad(
          GetMainSrpcChannel(instance),
          instance,
          url_loader,
          &result);

  DebugPrintf("PPP_Instance::HandleDocumentLoad: %s\n",
              NaClSrpcErrorString(srpc_result));
  if (srpc_result == NACL_SRPC_RESULT_OK && result)
    return PP_TRUE;
  return PP_FALSE;
}

}  // namespace

const PPP_Instance* BrowserInstance::GetInterface() {
  static const PPP_Instance instance_interface = {
    DidCreate,
    DidDestroy,
    DidChangeView,
    DidChangeFocus,
    HandleDocumentLoad
  };
  return &instance_interface;
}

}  // namespace ppapi_proxy
