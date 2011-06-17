// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/plugins/ppapi/ppb_broker_impl.h"

#include "base/logging.h"
#include "webkit/plugins/ppapi/common.h"
#include "webkit/plugins/ppapi/plugin_module.h"

namespace webkit {
namespace ppapi {

namespace {

// PPB_BrokerTrusted ----------------------------------------------------

PP_Resource CreateTrusted(PP_Instance instance_id) {
  PluginInstance* instance = ResourceTracker::Get()->GetInstance(instance_id);
  if (!instance)
    return 0;
  scoped_refptr<PPB_Broker_Impl> broker(new PPB_Broker_Impl(instance));
  return broker->GetReference();
}

PP_Bool IsBrokerTrusted(PP_Resource resource) {
  scoped_refptr<PPB_Broker_Impl> broker =
      Resource::GetAs<PPB_Broker_Impl>(resource);
  return BoolToPPBool(!!broker);
}

int32_t Connect(PP_Resource broker_id,
                PP_CompletionCallback connect_callback) {
  scoped_refptr<PPB_Broker_Impl> broker =
      Resource::GetAs<PPB_Broker_Impl>(broker_id);
  if (!broker)
    return PP_ERROR_BADRESOURCE;
  if (!connect_callback.func) {
    // Synchronous calls are not supported.
    return PP_ERROR_BADARGUMENT;
  }
  return broker->Connect(broker->instance()->delegate(), connect_callback);
}

int32_t GetHandle(PP_Resource broker_id, int32_t* handle) {
  scoped_refptr<PPB_Broker_Impl> broker =
      Resource::GetAs<PPB_Broker_Impl>(broker_id);
  if (!broker)
    return PP_ERROR_BADRESOURCE;
  if (!handle)
    return PP_ERROR_BADARGUMENT;
  return broker->GetHandle(handle);
}

const PPB_BrokerTrusted ppb_brokertrusted = {
  &CreateTrusted,
  &IsBrokerTrusted,
  &Connect,
  &GetHandle,
};

// TODO(ddorwin): Put conversion functions in a common place and/or add an
// invalid value to sync_socket.h.
int32_t PlatformFileToInt(base::PlatformFile handle) {
#if defined(OS_WIN)
  return static_cast<int32_t>(reinterpret_cast<intptr_t>(handle));
#elif defined(OS_POSIX)
  return handle;
#else
  #error Not implemented.
#endif
}

}  // namespace

// PPB_Broker_Impl ------------------------------------------------------

PPB_Broker_Impl::PPB_Broker_Impl(PluginInstance* instance)
    : Resource(instance),
      broker_(NULL),
      connect_callback_(),
      pipe_handle_(PlatformFileToInt(base::kInvalidPlatformFileValue)) {
}

PPB_Broker_Impl::~PPB_Broker_Impl() {
  if (broker_) {
    broker_->Disconnect(this);
    broker_ = NULL;
  }

  // The plugin owns the handle.
  pipe_handle_ = PlatformFileToInt(base::kInvalidPlatformFileValue);
}

const PPB_BrokerTrusted* PPB_Broker_Impl::GetTrustedInterface() {
  return &ppb_brokertrusted;
}

int32_t PPB_Broker_Impl::Connect(
    PluginDelegate* plugin_delegate,
    PP_CompletionCallback connect_callback) {
  // TODO(ddorwin): Return PP_ERROR_FAILED if plugin is in-process.

  if (broker_) {
    // May only be called once.
    return PP_ERROR_FAILED;
  }

  // The callback must be populated now in case we are connected to the broker
  // and BrokerConnected is called before ConnectToPpapiBroker returns.
  // Because it must be created now, it must be aborted and cleared if
  // ConnectToPpapiBroker fails.
  PP_Resource resource_id = GetReferenceNoAddRef();
  CHECK(resource_id);
  connect_callback_ = new TrackedCompletionCallback(
      instance()->module()->GetCallbackTracker(), resource_id,
      connect_callback);

  broker_ = plugin_delegate->ConnectToPpapiBroker(this);
  if (!broker_) {
    scoped_refptr<TrackedCompletionCallback> callback;
    callback.swap(connect_callback_);
    callback->Abort();
    return PP_ERROR_FAILED;
  }

  return PP_OK_COMPLETIONPENDING;
}

int32_t PPB_Broker_Impl::GetHandle(int32_t* handle) {
  *handle = pipe_handle_;
  return PP_OK;
}

PPB_Broker_Impl* PPB_Broker_Impl::AsPPB_Broker_Impl() {
  return this;
}

// Transfers ownership of the handle to the plugin.
void PPB_Broker_Impl::BrokerConnected(int32_t handle, int32_t result) {
  DCHECK(result == PP_OK ||
         handle == PlatformFileToInt(base::kInvalidPlatformFileValue));

  pipe_handle_ = handle;

  // Synchronous calls are not supported.
  DCHECK(connect_callback_.get() && !connect_callback_->completed());

  scoped_refptr<TrackedCompletionCallback> callback;
  callback.swap(connect_callback_);
  callback->Run(result);  // Will complete abortively if necessary.
}

}  // namespace ppapi
}  // namespace webkit
