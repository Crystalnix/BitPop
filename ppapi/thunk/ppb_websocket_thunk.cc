// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/c/pp_errors.h"
#include "ppapi/c/pp_var.h"
#include "ppapi/thunk/common.h"
#include "ppapi/thunk/thunk.h"
#include "ppapi/thunk/enter.h"
#include "ppapi/thunk/ppb_websocket_api.h"
#include "ppapi/thunk/resource_creation_api.h"

namespace ppapi {
namespace thunk {

namespace {

PP_Resource Create(PP_Instance instance) {
  EnterFunction<ResourceCreationAPI> enter(instance, true);
  if (enter.failed())
    return 0;
  return enter.functions()->CreateWebSocket(instance);
}

PP_Bool IsWebSocket(PP_Resource resource) {
  EnterResource<PPB_WebSocket_API> enter(resource, false);
  return PP_FromBool(enter.succeeded());
}

int32_t Connect(PP_Resource resource,
                PP_Var url,
                const PP_Var protocols[],
                uint32_t protocol_count,
                PP_CompletionCallback callback) {
  EnterResource<PPB_WebSocket_API> enter(resource, false);
  if (enter.failed())
    return MayForceCallback(callback, PP_ERROR_BADRESOURCE);
  int32_t result =
      enter.object()->Connect(url, protocols, protocol_count, callback);
  return MayForceCallback(callback, result);
}

int32_t Close(PP_Resource resource,
              uint16_t code,
              PP_Var reason,
              PP_CompletionCallback callback) {
  EnterResource<PPB_WebSocket_API> enter(resource, false);
  if (enter.failed())
    return MayForceCallback(callback, PP_ERROR_BADRESOURCE);
  int32_t result = enter.object()->Close(code, reason, callback);
  return MayForceCallback(callback, result);
}

int32_t ReceiveMessage(PP_Resource resource,
                       PP_Var* message,
                       PP_CompletionCallback callback) {
  EnterResource<PPB_WebSocket_API> enter(resource, false);
  if (enter.failed())
    return MayForceCallback(callback, PP_ERROR_BADRESOURCE);
  int32_t result = enter.object()->ReceiveMessage(message, callback);
  return MayForceCallback(callback, result);
}

int32_t SendMessage(PP_Resource resource, PP_Var message) {
  EnterResource<PPB_WebSocket_API> enter(resource, false);
  if (enter.failed())
    return PP_ERROR_BADRESOURCE;
  return enter.object()->SendMessage(message);
}

uint64_t GetBufferedAmount(PP_Resource resource) {
  EnterResource<PPB_WebSocket_API> enter(resource, false);
  if (enter.failed())
    return 0;
  return enter.object()->GetBufferedAmount();
}

uint16_t GetCloseCode(PP_Resource resource) {
  EnterResource<PPB_WebSocket_API> enter(resource, false);
  if (enter.failed())
    return 0;
  return enter.object()->GetCloseCode();
}

PP_Var GetCloseReason(PP_Resource resource) {
  EnterResource<PPB_WebSocket_API> enter(resource, false);
  if (enter.failed())
    return PP_MakeUndefined();
  return enter.object()->GetCloseReason();
}

PP_Bool GetCloseWasClean(PP_Resource resource) {
  EnterResource<PPB_WebSocket_API> enter(resource, false);
  if (enter.failed())
    return PP_FALSE;
  return enter.object()->GetCloseWasClean();
}

PP_Var GetExtensions(PP_Resource resource) {
  EnterResource<PPB_WebSocket_API> enter(resource, false);
  if (enter.failed())
    return PP_MakeUndefined();
  return enter.object()->GetExtensions();
}

PP_Var GetProtocol(PP_Resource resource) {
  EnterResource<PPB_WebSocket_API> enter(resource, false);
  if (enter.failed())
    return PP_MakeUndefined();
  return enter.object()->GetProtocol();
}

PP_WebSocketReadyState GetReadyState(PP_Resource resource) {
  EnterResource<PPB_WebSocket_API> enter(resource, false);
  if (enter.failed())
    return PP_WEBSOCKETREADYSTATE_INVALID;
  return enter.object()->GetReadyState();
}

PP_Var GetURL(PP_Resource resource) {
  EnterResource<PPB_WebSocket_API> enter(resource, false);
  if (enter.failed())
    return PP_MakeUndefined();
  return enter.object()->GetURL();
}

const PPB_WebSocket_1_0 g_ppb_websocket_1_0_thunk = {
  &Create,
  &IsWebSocket,
  &Connect,
  &Close,
  &ReceiveMessage,
  &SendMessage,
  &GetBufferedAmount,
  &GetCloseCode,
  &GetCloseReason,
  &GetCloseWasClean,
  &GetExtensions,
  &GetProtocol,
  &GetReadyState,
  &GetURL
};

}  // namespace

const PPB_WebSocket_1_0* GetPPB_WebSocket_1_0_Thunk() {
  return &g_ppb_websocket_1_0_thunk;
}

}  // namespace thunk
}  // namespace ppapi
