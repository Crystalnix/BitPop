// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/plugins/ppapi/ppb_broker_impl.h"

#include "base/logging.h"
#include "webkit/plugins/ppapi/common.h"
#include "webkit/plugins/ppapi/plugin_module.h"

using ::ppapi::thunk::PPB_Broker_API;

namespace webkit {
namespace ppapi {

namespace {

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

// static
PP_Resource PPB_Broker_Impl::Create(PP_Instance instance_id) {
  PluginInstance* instance = ResourceTracker::Get()->GetInstance(instance_id);
  if (!instance)
    return 0;
  scoped_refptr<PPB_Broker_Impl> broker(new PPB_Broker_Impl(instance));
  return broker->GetReference();
}

PPB_Broker_Impl* PPB_Broker_Impl::AsPPB_Broker_Impl() {
  return this;
}

PPB_Broker_API* PPB_Broker_Impl::AsPPB_Broker_API() {
  return this;
}

int32_t PPB_Broker_Impl::Connect(PP_CompletionCallback connect_callback) {
  if (!connect_callback.func) {
    // Synchronous calls are not supported.
    return PP_ERROR_BADARGUMENT;
  }

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

  broker_ = instance()->delegate()->ConnectToPpapiBroker(this);
  if (!broker_) {
    scoped_refptr<TrackedCompletionCallback> callback;
    callback.swap(connect_callback_);
    callback->Abort();
    return PP_ERROR_FAILED;
  }

  return PP_OK_COMPLETIONPENDING;
}

int32_t PPB_Broker_Impl::GetHandle(int32_t* handle) {
  if (pipe_handle_ == PlatformFileToInt(base::kInvalidPlatformFileValue))
    return PP_ERROR_FAILED;  // Handle not set yet.
  *handle = pipe_handle_;
  return PP_OK;
}

// Transfers ownership of the handle to the plugin.
void PPB_Broker_Impl::BrokerConnected(int32_t handle, int32_t result) {
  DCHECK(pipe_handle_ ==
         PlatformFileToInt(base::kInvalidPlatformFileValue));
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
