// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/proxy/ppb_broker_proxy.h"

#include "ppapi/c/pp_errors.h"
#include "ppapi/c/trusted/ppb_broker_trusted.h"
#include "ppapi/proxy/enter_proxy.h"
#include "ppapi/proxy/plugin_dispatcher.h"
#include "ppapi/proxy/plugin_resource.h"
#include "ppapi/proxy/ppapi_messages.h"
#include "ppapi/thunk/ppb_broker_api.h"
#include "ppapi/thunk/enter.h"
#include "ppapi/thunk/thunk.h"

namespace pp {
namespace proxy {

namespace {

base::PlatformFile IntToPlatformFile(int32_t handle) {
#if defined(OS_WIN)
  return reinterpret_cast<HANDLE>(static_cast<intptr_t>(handle));
#elif defined(OS_POSIX)
  return handle;
#else
  #error Not implemented.
#endif
}

int32_t PlatformFileToInt(base::PlatformFile handle) {
#if defined(OS_WIN)
  return static_cast<int32_t>(reinterpret_cast<intptr_t>(handle));
#elif defined(OS_POSIX)
  return handle;
#else
  #error Not implemented.
#endif
}

InterfaceProxy* CreateBrokerProxy(Dispatcher* dispatcher,
                                  const void* target_interface) {
  return new PPB_Broker_Proxy(dispatcher, target_interface);
}

}  // namespace

class Broker : public ppapi::thunk::PPB_Broker_API,
               public PluginResource {
 public:
  explicit Broker(const HostResource& resource);
  virtual ~Broker();

  // ResourceObjectBase overries.
  virtual ppapi::thunk::PPB_Broker_API* AsPPB_Broker_API() OVERRIDE;

  // PPB_Broker_API implementation.
  virtual int32_t Connect(PP_CompletionCallback connect_callback) OVERRIDE;
  virtual int32_t GetHandle(int32_t* handle) OVERRIDE;

  // Called by the proxy when the host side has completed the request.
  void ConnectComplete(IPC::PlatformFileForTransit socket_handle,
                       int32_t result);

 private:
  bool called_connect_;
  PP_CompletionCallback current_connect_callback_;

  // The plugin module owns the handle.
  // The host side transfers ownership of the handle to the plugin side when it
  // sends the IPC. This member holds the handle value for the plugin module
  // to read, but the plugin side of the proxy never takes ownership.
  base::SyncSocket::Handle socket_handle_;

  DISALLOW_COPY_AND_ASSIGN(Broker);
};

Broker::Broker(const HostResource& resource)
    : PluginResource(resource),
      called_connect_(false),
      current_connect_callback_(PP_MakeCompletionCallback(NULL, NULL)),
      socket_handle_(base::kInvalidPlatformFileValue) {
}

Broker::~Broker() {
  // Ensure the callback is always fired.
  if (current_connect_callback_.func) {
    // TODO(brettw) the callbacks at this level should be refactored with a
    // more automatic tracking system like we have in the renderer.
    MessageLoop::current()->PostTask(FROM_HERE, NewRunnableFunction(
        current_connect_callback_.func, current_connect_callback_.user_data,
        static_cast<int32_t>(PP_ERROR_ABORTED)));
  }

  socket_handle_ = base::kInvalidPlatformFileValue;
}

ppapi::thunk::PPB_Broker_API* Broker::AsPPB_Broker_API() {
  return this;
}

int32_t Broker::Connect(PP_CompletionCallback connect_callback) {
  if (!connect_callback.func) {
    // Synchronous calls are not supported.
    return PP_ERROR_BADARGUMENT;
  }

  if (current_connect_callback_.func)
    return PP_ERROR_INPROGRESS;
  else if (called_connect_)
    return PP_ERROR_FAILED;

  current_connect_callback_ = connect_callback;
  called_connect_ = true;

  bool success = GetDispatcher()->Send(new PpapiHostMsg_PPBBroker_Connect(
      INTERFACE_ID_PPB_BROKER, host_resource()));
  return success ?  PP_OK_COMPLETIONPENDING : PP_ERROR_FAILED;
}

int32_t Broker::GetHandle(int32_t* handle) {
  if (socket_handle_ == base::kInvalidPlatformFileValue)
    return PP_ERROR_FAILED;
  *handle = PlatformFileToInt(socket_handle_);
  return PP_OK;
}

void Broker::ConnectComplete(IPC::PlatformFileForTransit socket_handle,
                             int32_t result) {
  if (result == PP_OK) {
    DCHECK(socket_handle_ == base::kInvalidPlatformFileValue);
    socket_handle_ = IPC::PlatformFileForTransitToPlatformFile(socket_handle);
  } else {
    // The caller may still have given us a handle in the failure case.
    // The easiest way to clean it up is to just put it in an object
    // and then close them. This failure case is not performance critical.
    base::SyncSocket temp_socket(
        IPC::PlatformFileForTransitToPlatformFile(socket_handle));
  }

  if (!current_connect_callback_.func) {
    // The handle might leak if the plugin never calls GetHandle().
    return;
  }

  PP_RunAndClearCompletionCallback(&current_connect_callback_, result);
}

PPB_Broker_Proxy::PPB_Broker_Proxy(Dispatcher* dispatcher,
                                   const void* target_interface)
    : InterfaceProxy(dispatcher, target_interface) ,
      callback_factory_(ALLOW_THIS_IN_INITIALIZER_LIST(this)){
}

PPB_Broker_Proxy::~PPB_Broker_Proxy() {
}

// static
const InterfaceProxy::Info* PPB_Broker_Proxy::GetInfo() {
  static const Info info = {
    ppapi::thunk::GetPPB_Broker_Thunk(),
    PPB_BROKER_TRUSTED_INTERFACE,
    INTERFACE_ID_PPB_BROKER,
    true,
    &CreateBrokerProxy,
  };
  return &info;
}

// static
PP_Resource PPB_Broker_Proxy::CreateProxyResource(PP_Instance instance) {
  PluginDispatcher* dispatcher = PluginDispatcher::GetForInstance(instance);
  if (!dispatcher)
    return 0;

  HostResource result;
  dispatcher->Send(new PpapiHostMsg_PPBBroker_Create(
      INTERFACE_ID_PPB_BROKER, instance, &result));
  if (result.is_null())
    return 0;

  linked_ptr<Broker> object(new Broker(result));
  return PluginResourceTracker::GetInstance()->AddResource(object);
}

bool PPB_Broker_Proxy::OnMessageReceived(const IPC::Message& msg) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(PPB_Broker_Proxy, msg)
    IPC_MESSAGE_HANDLER(PpapiHostMsg_PPBBroker_Create, OnMsgCreate)
    IPC_MESSAGE_HANDLER(PpapiHostMsg_PPBBroker_Connect, OnMsgConnect)
    IPC_MESSAGE_HANDLER(PpapiMsg_PPBBroker_ConnectComplete,
                        OnMsgConnectComplete)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void PPB_Broker_Proxy::OnMsgCreate(PP_Instance instance,
                                   HostResource* result_resource) {
  result_resource->SetHostResource(
      instance,
      ppb_broker_target()->CreateTrusted(instance));
}

void PPB_Broker_Proxy::OnMsgConnect(const HostResource& broker) {
  CompletionCallback callback = callback_factory_.NewCallback(
      &PPB_Broker_Proxy::ConnectCompleteInHost, broker);

  int32_t result = ppb_broker_target()->Connect(
      broker.host_resource(),
      callback.pp_completion_callback());
  if (result !=  PP_OK_COMPLETIONPENDING)
    callback.Run(result);
}

// Called in the plugin to handle the connect callback.
// The proxy owns the handle and transfers it to the Broker. At that point,
// the plugin owns the handle and is responsible for closing it.
// The caller guarantees that socket_handle is not valid if result is not PP_OK.
void PPB_Broker_Proxy::OnMsgConnectComplete(
    const HostResource& resource,
    IPC::PlatformFileForTransit socket_handle,
    int32_t result) {
  DCHECK(result == PP_OK ||
         socket_handle == IPC::InvalidPlatformFileForTransit());

  EnterPluginFromHostResource<ppapi::thunk::PPB_Broker_API> enter(resource);
  if (enter.failed()) {
    // As in Broker::ConnectComplete, we need to close the resource on error.
    base::SyncSocket temp_socket(
        IPC::PlatformFileForTransitToPlatformFile(socket_handle));
  } else {
    static_cast<Broker*>(enter.object())->ConnectComplete(socket_handle,
                                                          result);
  }
}

// Callback on the host side.
// Transfers ownership of the handle to the plugin side. This function must
// either successfully call the callback or close the handle.
void PPB_Broker_Proxy::ConnectCompleteInHost(int32_t result,
                                             const HostResource& broker) {
  IPC::PlatformFileForTransit foreign_socket_handle =
      IPC::InvalidPlatformFileForTransit();
  if (result == PP_OK) {
    int32_t socket_handle = PlatformFileToInt(base::kInvalidPlatformFileValue);
    result = ppb_broker_target()->GetHandle(broker.host_resource(),
                                            &socket_handle);
    DCHECK(result == PP_OK ||
           socket_handle == PlatformFileToInt(base::kInvalidPlatformFileValue));

    if (result == PP_OK) {
      foreign_socket_handle =
          dispatcher()->ShareHandleWithRemote(IntToPlatformFile(socket_handle),
                                              true);
      if (foreign_socket_handle == IPC::InvalidPlatformFileForTransit()) {
        result = PP_ERROR_FAILED;
        // Assume the local handle was closed even if the foreign handle could
        // not be created.
      }
    }
  }
  DCHECK(result == PP_OK ||
         foreign_socket_handle == IPC::InvalidPlatformFileForTransit());

  bool success = dispatcher()->Send(new PpapiMsg_PPBBroker_ConnectComplete(
      INTERFACE_ID_PPB_BROKER, broker, foreign_socket_handle, result));

  if (!success || result != PP_OK) {
      // The plugin did not receive the handle, so it must be closed.
      // The easiest way to clean it up is to just put it in an object
      // and then close it. This failure case is not performance critical.
      // The handle could still leak if Send succeeded but the IPC later failed.
      base::SyncSocket temp_socket(
          IPC::PlatformFileForTransitToPlatformFile(foreign_socket_handle));
  }
}

}  // namespace proxy
}  // namespace pp
