// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/proxy/ppb_url_loader_proxy.h"

#include <algorithm>
#include <deque>
#include <vector>

#include "base/logging.h"
#include "build/build_config.h"
#include "ppapi/c/pp_completion_callback.h"
#include "ppapi/c/pp_errors.h"
#include "ppapi/c/pp_resource.h"
#include "ppapi/c/ppb_url_loader.h"
#include "ppapi/c/private/ppb_proxy_private.h"
#include "ppapi/c/trusted/ppb_url_loader_trusted.h"
#include "ppapi/proxy/host_dispatcher.h"
#include "ppapi/proxy/plugin_dispatcher.h"
#include "ppapi/proxy/plugin_resource.h"
#include "ppapi/proxy/plugin_resource_tracker.h"
#include "ppapi/proxy/ppapi_messages.h"
#include "ppapi/proxy/ppb_url_response_info_proxy.h"

#if defined(OS_LINUX)
#include <sys/shm.h>
#endif

namespace pp {
namespace proxy {

class URLLoader : public PluginResource {
 public:
  URLLoader(const HostResource& resource);
  virtual ~URLLoader();

  // Resource overrides.
  virtual URLLoader* AsURLLoader() { return this; }

  PP_Resource GetResponseInfo();

  // Appends the given data to the buffer_.
  void PushBuffer(const char* data, size_t data_size);

  // Reads the give bytes out of the buffer_, placing them in the given output
  // buffer, and removes the bytes from the buffer.
  //
  // The size must be not more than the current size of the buffer.
  void PopBuffer(void* output_buffer, int32_t output_size);

  // Initialized to -1. Will be set to nonnegative values by the UpdateProgress
  // message when the values are known.
  int64_t bytes_sent_;
  int64_t total_bytes_to_be_sent_;
  int64_t bytes_received_;
  int64_t total_bytes_to_be_received_;

  // When an asynchronous read is pending, this will contain the callback and
  // the buffer to put the data.
  PP_CompletionCallback current_read_callback_;
  void* current_read_buffer_;
  int32_t current_read_buffer_size_;

  // A buffer of all the data that's been sent to us from the host that we
  // have yet to send out to the plugin.
  std::deque<char> buffer_;

  // Cached copy of the response info. When nonzero, we're holding a reference
  // to this resource.
  PP_Resource response_info_;

 private:
  DISALLOW_COPY_AND_ASSIGN(URLLoader);
};

URLLoader::URLLoader(const HostResource& resource)
    : PluginResource(resource),
      bytes_sent_(-1),
      total_bytes_to_be_sent_(-1),
      bytes_received_(-1),
      total_bytes_to_be_received_(-1),
      current_read_callback_(PP_MakeCompletionCallback(NULL, NULL)),
      current_read_buffer_(NULL),
      current_read_buffer_size_(0),
      response_info_(0) {
}

URLLoader::~URLLoader() {
  // Always need to fire completion callbacks to prevent a leak in the plugin.
  if (current_read_callback_.func) {
    // TODO(brettw) the callbacks at this level should be refactored with a
    // more automatic tracking system like we have in the renderer.
    MessageLoop::current()->PostTask(FROM_HERE, NewRunnableFunction(
        current_read_callback_.func, current_read_callback_.user_data,
        static_cast<int32_t>(PP_ERROR_ABORTED)));
  }

  if (response_info_)
    PluginResourceTracker::GetInstance()->ReleaseResource(response_info_);
}

PP_Resource URLLoader::GetResponseInfo() {
  if (!response_info_) {
    PluginDispatcher* dispatcher = PluginDispatcher::GetForInstance(instance());
    if (!dispatcher)
      return 0;

    HostResource response_id;
    dispatcher->Send(new PpapiHostMsg_PPBURLLoader_GetResponseInfo(
        INTERFACE_ID_PPB_URL_LOADER, host_resource(), &response_id));
    if (response_id.is_null())
      return 0;

    response_info_ = PPB_URLResponseInfo_Proxy::CreateResponseForResource(
        response_id);
  }

  // The caller expects to get a ref, and we want to keep holding ours.
  PluginResourceTracker::GetInstance()->AddRefResource(response_info_);
  return response_info_;
}

void URLLoader::PushBuffer(const char* data, size_t data_size) {
  buffer_.insert(buffer_.end(), data, data + data_size);
}

void URLLoader::PopBuffer(void* output_buffer, int32_t output_size) {
  CHECK(output_size <= static_cast<int32_t>(buffer_.size()));
  std::copy(buffer_.begin(),
            buffer_.begin() + output_size,
            static_cast<char*>(output_buffer));
  buffer_.erase(buffer_.begin(),
                buffer_.begin() + output_size);
}

namespace {

// The maximum size we'll read into the plugin without being explicitly
// asked for a larger buffer.
static const int32_t kMaxReadBufferSize = 16777216;  // 16MB

// Converts the given loader ID to the dispatcher associated with it and the
// loader object. Returns true if the object was found.
bool RoutingDataFromURLLoader(PP_Resource loader_id,
                              URLLoader** loader_object,
                              PluginDispatcher** dispatcher) {
  *loader_object = PluginResource::GetAs<URLLoader>(loader_id);
  if (!*loader_object)
    return false;
  *dispatcher = PluginDispatcher::GetForInstance((*loader_object)->instance());
  return !!*dispatcher;
}

// Plugin PPB_URLLoader implmentation ------------------------------------------

PP_Resource Create(PP_Instance instance_id) {
  PluginDispatcher* dispatcher = PluginDispatcher::GetForInstance(instance_id);
  if (!dispatcher)
    return 0;

  HostResource result;
  dispatcher->Send(new PpapiHostMsg_PPBURLLoader_Create(
      INTERFACE_ID_PPB_URL_LOADER, instance_id, &result));
  if (result.is_null())
    return 0;
  return PPB_URLLoader_Proxy::TrackPluginResource(result);
}

PP_Bool IsURLLoader(PP_Resource resource) {
  URLLoader* object = PluginResource::GetAs<URLLoader>(resource);
  return BoolToPPBool(!!object);
}

int32_t Open(PP_Resource loader_id,
             PP_Resource request_id,
             PP_CompletionCallback callback) {
  URLLoader* loader_object;
  PluginDispatcher* dispatcher;
  if (!RoutingDataFromURLLoader(loader_id, &loader_object, &dispatcher))
    return PP_ERROR_BADRESOURCE;
  PluginResource* request_object =
      PluginResourceTracker::GetInstance()->GetResourceObject(request_id);
  if (!request_object)
    return PP_ERROR_BADRESOURCE;

  dispatcher->Send(new PpapiHostMsg_PPBURLLoader_Open(
      INTERFACE_ID_PPB_URL_LOADER, loader_object->host_resource(),
      request_object->host_resource(),
      dispatcher->callback_tracker().SendCallback(callback)));
  return PP_OK_COMPLETIONPENDING;
}

int32_t FollowRedirect(PP_Resource loader_id,
                       PP_CompletionCallback callback) {
  URLLoader* loader_object;
  PluginDispatcher* dispatcher;
  if (!RoutingDataFromURLLoader(loader_id, &loader_object, &dispatcher))
    return PP_ERROR_BADRESOURCE;

  dispatcher->Send(new PpapiHostMsg_PPBURLLoader_FollowRedirect(
      INTERFACE_ID_PPB_URL_LOADER, loader_object->host_resource(),
      dispatcher->callback_tracker().SendCallback(callback)));
  return PP_OK_COMPLETIONPENDING;
}

PP_Bool GetUploadProgress(PP_Resource loader_id,
                          int64_t* bytes_sent,
                          int64_t* total_bytes_to_be_sent) {
  URLLoader* object = PluginResource::GetAs<URLLoader>(loader_id);
  if (!object || object->bytes_sent_ == -1) {
    *bytes_sent = 0;
    *total_bytes_to_be_sent = 0;
    return PP_FALSE;
  }
  *bytes_sent = object->bytes_sent_;
  *total_bytes_to_be_sent = object->total_bytes_to_be_sent_;
  return PP_TRUE;
}

PP_Bool GetDownloadProgress(PP_Resource loader_id,
                            int64_t* bytes_received,
                            int64_t* total_bytes_to_be_received) {
  URLLoader* object = PluginResource::GetAs<URLLoader>(loader_id);
  if (!object || object->bytes_received_ == -1) {
    *bytes_received = 0;
    *total_bytes_to_be_received = 0;
    return PP_FALSE;
  }
  *bytes_received = object->bytes_received_;
  *total_bytes_to_be_received = object->total_bytes_to_be_received_;
  return PP_TRUE;
}

PP_Resource GetResponseInfo(PP_Resource loader_id) {
  URLLoader* object = PluginResource::GetAs<URLLoader>(loader_id);
  if (!object)
    return 0;
  return object->GetResponseInfo();
}

int32_t ReadResponseBody(PP_Resource loader_id,
                         void* buffer,
                         int32_t bytes_to_read,
                         PP_CompletionCallback callback) {
  URLLoader* object;
  PluginDispatcher* dispatcher;
  if (!RoutingDataFromURLLoader(loader_id, &object, &dispatcher))
    return PP_ERROR_BADRESOURCE;

  if (!buffer || bytes_to_read <= 0)
    return PP_ERROR_BADARGUMENT;  // Must specify an output buffer.
  if (object->current_read_callback_.func)
    return PP_ERROR_INPROGRESS;  // Can only have one request pending.

  // Currently we don't support sync calls to read. We'll need to revisit
  // how this works when we allow blocking calls (from background threads).
  if (!callback.func)
    return PP_ERROR_BADARGUMENT;

  if (static_cast<size_t>(bytes_to_read) <= object->buffer_.size()) {
    // Special case: we've buffered enough data to be able to synchronously
    // return data to the caller. Do so without making IPCs.
    object->PopBuffer(buffer, bytes_to_read);
    return bytes_to_read;
  }

  object->current_read_callback_ = callback;
  object->current_read_buffer_ = buffer;
  object->current_read_buffer_size_ = bytes_to_read;

  dispatcher->Send(new PpapiHostMsg_PPBURLLoader_ReadResponseBody(
      INTERFACE_ID_PPB_URL_LOADER,
      object->host_resource(), bytes_to_read));
  return PP_OK_COMPLETIONPENDING;
}

int32_t FinishStreamingToFile(PP_Resource loader_id,
                              PP_CompletionCallback callback) {
  URLLoader* loader_object;
  PluginDispatcher* dispatcher;
  if (!RoutingDataFromURLLoader(loader_id, &loader_object, &dispatcher))
    return PP_ERROR_BADRESOURCE;

  dispatcher->Send(new PpapiHostMsg_PPBURLLoader_FinishStreamingToFile(
      INTERFACE_ID_PPB_URL_LOADER, loader_object->host_resource(),
      dispatcher->callback_tracker().SendCallback(callback)));
  return PP_OK_COMPLETIONPENDING;
}

void Close(PP_Resource loader_id) {
  URLLoader* loader_object;
  PluginDispatcher* dispatcher;
  if (!RoutingDataFromURLLoader(loader_id, &loader_object, &dispatcher))
    return;

  dispatcher->Send(new PpapiHostMsg_PPBURLLoader_Close(
      INTERFACE_ID_PPB_URL_LOADER, loader_object->host_resource()));
}

const PPB_URLLoader urlloader_interface = {
  &Create,
  &IsURLLoader,
  &Open,
  &FollowRedirect,
  &GetUploadProgress,
  &GetDownloadProgress,
  &GetResponseInfo,
  &ReadResponseBody,
  &FinishStreamingToFile,
  &Close
};

InterfaceProxy* CreateURLLoaderProxy(Dispatcher* dispatcher,
                                     const void* target_interface) {
  return new PPB_URLLoader_Proxy(dispatcher, target_interface);
}

// Plugin URLLoaderTrusted implementation --------------------------------------

void GrantUniversalAccess(PP_Resource loader_id) {
  URLLoader* loader_object;
  PluginDispatcher* dispatcher;
  if (!RoutingDataFromURLLoader(loader_id, &loader_object, &dispatcher))
    return;

  dispatcher->Send(
      new PpapiHostMsg_PPBURLLoaderTrusted_GrantUniversalAccess(
          INTERFACE_ID_PPB_URL_LOADER_TRUSTED, loader_object->host_resource()));
}

const PPB_URLLoaderTrusted urlloader_trusted_interface = {
  &GrantUniversalAccess,
  NULL,  // RegisterStatusCallback is used internally by the proxy only.
};

InterfaceProxy* CreateURLLoaderTrustedProxy(Dispatcher* dispatcher,
                                            const void* target_interface) {
  return new PPB_URLLoaderTrusted_Proxy(dispatcher, target_interface);
}

// Called in the renderer when the byte counts have changed. We send a message
// to the plugin to synchronize its counts so it can respond to status polls
// from the plugin.
void UpdateResourceLoadStatus(PP_Instance pp_instance,
                              PP_Resource pp_resource,
                              int64 bytes_sent,
                              int64 total_bytes_to_be_sent,
                              int64 bytes_received,
                              int64 total_bytes_to_be_received) {
  Dispatcher* dispatcher = HostDispatcher::GetForInstance(pp_instance);
  PPBURLLoader_UpdateProgress_Params params;
  params.instance = pp_instance;
  params.resource.SetHostResource(pp_instance, pp_resource);
  params.bytes_sent = bytes_sent;
  params.total_bytes_to_be_sent = total_bytes_to_be_sent;
  params.bytes_received = bytes_received;
  params.total_bytes_to_be_received = total_bytes_to_be_received;
  dispatcher->Send(new PpapiMsg_PPBURLLoader_UpdateProgress(
      INTERFACE_ID_PPB_URL_LOADER, params));
}

}  // namespace

// PPB_URLLoader_Proxy ---------------------------------------------------------

struct PPB_URLLoader_Proxy::ReadCallbackInfo {
  HostResource resource;
  std::string read_buffer;
};

PPB_URLLoader_Proxy::PPB_URLLoader_Proxy(Dispatcher* dispatcher,
                                         const void* target_interface)
    : InterfaceProxy(dispatcher, target_interface),
      callback_factory_(ALLOW_THIS_IN_INITIALIZER_LIST(this)),
      host_urlloader_trusted_interface_(NULL) {
}

PPB_URLLoader_Proxy::~PPB_URLLoader_Proxy() {
}

// static
PP_Resource PPB_URLLoader_Proxy::TrackPluginResource(
    const HostResource& url_loader_resource) {
  linked_ptr<URLLoader> object(new URLLoader(url_loader_resource));
  return PluginResourceTracker::GetInstance()->AddResource(object);
}

// static
const InterfaceProxy::Info* PPB_URLLoader_Proxy::GetInfo() {
  static const Info info = {
    &urlloader_interface,
    PPB_URLLOADER_INTERFACE,
    INTERFACE_ID_PPB_URL_LOADER,
    false,
    &CreateURLLoaderProxy,
  };
  return &info;
}

bool PPB_URLLoader_Proxy::OnMessageReceived(const IPC::Message& msg) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(PPB_URLLoader_Proxy, msg)
    IPC_MESSAGE_HANDLER(PpapiHostMsg_PPBURLLoader_Create,
                        OnMsgCreate)
    IPC_MESSAGE_HANDLER(PpapiHostMsg_PPBURLLoader_Open,
                        OnMsgOpen)
    IPC_MESSAGE_HANDLER(PpapiHostMsg_PPBURLLoader_FollowRedirect,
                        OnMsgFollowRedirect)
    IPC_MESSAGE_HANDLER(PpapiHostMsg_PPBURLLoader_GetResponseInfo,
                        OnMsgGetResponseInfo)
    IPC_MESSAGE_HANDLER(PpapiHostMsg_PPBURLLoader_ReadResponseBody,
                        OnMsgReadResponseBody)
    IPC_MESSAGE_HANDLER(PpapiHostMsg_PPBURLLoader_FinishStreamingToFile,
                        OnMsgFinishStreamingToFile)
    IPC_MESSAGE_HANDLER(PpapiHostMsg_PPBURLLoader_Close,
                        OnMsgClose)

    IPC_MESSAGE_HANDLER(PpapiMsg_PPBURLLoader_UpdateProgress,
                        OnMsgUpdateProgress)
    IPC_MESSAGE_HANDLER(PpapiMsg_PPBURLLoader_ReadResponseBody_Ack,
                        OnMsgReadResponseBodyAck)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  // TODO(brettw) handle bad messages!
  return handled;
}

void PPB_URLLoader_Proxy::PrepareURLLoaderForSendingToPlugin(
    PP_Resource resource) {
  // So the plugin can query load status, we need to register our status
  // callback before sending any URLLoader to the plugin.
  RegisterStatusCallback(resource);
}

void PPB_URLLoader_Proxy::OnMsgCreate(PP_Instance instance,
                                      HostResource* result) {
  result->SetHostResource(instance, ppb_url_loader_target()->Create(instance));
  PrepareURLLoaderForSendingToPlugin(result->host_resource());
}

void PPB_URLLoader_Proxy::OnMsgOpen(const HostResource& loader,
                                    const HostResource& request_info,
                                    uint32_t serialized_callback) {
  PP_CompletionCallback callback = ReceiveCallback(serialized_callback);
  int32_t result = ppb_url_loader_target()->Open(
      loader.host_resource(), request_info.host_resource(), callback);
  if (result != PP_OK_COMPLETIONPENDING)
    PP_RunCompletionCallback(&callback, result);
  // TODO(brettw) bug 73236 register for the status callbacks.
}

void PPB_URLLoader_Proxy::OnMsgFollowRedirect(
    const HostResource& loader,
    uint32_t serialized_callback) {
  PP_CompletionCallback callback = ReceiveCallback(serialized_callback);
  int32_t result = ppb_url_loader_target()->FollowRedirect(
      loader.host_resource(), callback);
  if (result != PP_OK_COMPLETIONPENDING)
    PP_RunCompletionCallback(&callback, result);
}

void PPB_URLLoader_Proxy::OnMsgGetResponseInfo(const HostResource& loader,
                                               HostResource* result) {
  result->SetHostResource(loader.instance(),
      ppb_url_loader_target()->GetResponseInfo(loader.host_resource()));
}

void PPB_URLLoader_Proxy::OnMsgReadResponseBody(
    const HostResource& loader,
    int32_t bytes_to_read) {
  // The plugin could be sending us malicious messages, don't accept negative
  // sizes.
  if (bytes_to_read < 0) {
    // TODO(brettw) kill plugin.
    bytes_to_read = 0;
  }

  // Read more than requested if there are bytes available for synchronous
  // reading. This prevents us from getting too far behind due to IPC message
  // latency. Any extra data will get buffered in the plugin.
  int32_t synchronously_available_bytes =
      static_cast<HostDispatcher*>(dispatcher())->ppb_proxy()->
          GetURLLoaderBufferedBytes(loader.host_resource());
  if (bytes_to_read < kMaxReadBufferSize) {
    // Grow the amount to read so we read ahead synchronously, if possible.
    bytes_to_read =
        std::max(bytes_to_read,
                 std::min(synchronously_available_bytes, kMaxReadBufferSize));
  }

  // This heap object will get deleted by the callback handler.
  // TODO(brettw) this will be leaked if the plugin closes the resource!
  // (Also including the plugin unloading and having the resource implicitly
  // destroyed. Depending on the cleanup ordering, we may not need the weak
  // pointer here.)
  ReadCallbackInfo* info = new ReadCallbackInfo;
  info->resource = loader;
  // TODO(brettw) have a way to check for out-of-memory.
  info->read_buffer.resize(bytes_to_read);

  CompletionCallback callback = callback_factory_.NewCallback(
      &PPB_URLLoader_Proxy::OnReadCallback, info);

  int32_t result = ppb_url_loader_target()->ReadResponseBody(
      loader.host_resource(), const_cast<char*>(info->read_buffer.c_str()),
      bytes_to_read, callback.pp_completion_callback());
  if (result != PP_OK_COMPLETIONPENDING) {
    // Send error (or perhaps success for synchronous reads) back to plugin.
    // The callback function is already set up to do this and also delete the
    // callback info.
    callback.Run(result);
  }
}

void PPB_URLLoader_Proxy::OnMsgFinishStreamingToFile(
    const HostResource& loader,
    uint32_t serialized_callback) {
  PP_CompletionCallback callback = ReceiveCallback(serialized_callback);
  int32_t result = ppb_url_loader_target()->FinishStreamingToFile(
      loader.host_resource(), callback);
  if (result != PP_OK_COMPLETIONPENDING)
    PP_RunCompletionCallback(&callback, result);
}

void PPB_URLLoader_Proxy::OnMsgClose(const HostResource& loader) {
  ppb_url_loader_target()->Close(loader.host_resource());
}

// Called in the Plugin.
void PPB_URLLoader_Proxy::OnMsgUpdateProgress(
    const PPBURLLoader_UpdateProgress_Params& params) {
  PP_Resource plugin_resource =
      PluginResourceTracker::GetInstance()->PluginResourceForHostResource(
          params.resource);
  if (!plugin_resource)
    return;
  URLLoader* object = PluginResource::GetAs<URLLoader>(plugin_resource);
  if (!object)
    return;

  object->bytes_sent_ = params.bytes_sent;
  object->total_bytes_to_be_sent_ = params.total_bytes_to_be_sent;
  object->bytes_received_ = params.bytes_received;
  object->total_bytes_to_be_received_ = params.total_bytes_to_be_received;
}

// Called in the Plugin.
void PPB_URLLoader_Proxy::OnMsgReadResponseBodyAck(
    const HostResource& host_resource,
    int32 result,
    const std::string& data) {
  PP_Resource plugin_resource =
      PluginResourceTracker::GetInstance()->PluginResourceForHostResource(
          host_resource);
  if (!plugin_resource)
    return;
  URLLoader* object = PluginResource::GetAs<URLLoader>(plugin_resource);
  if (!object)
    return;

  if (!object->current_read_callback_.func || !object->current_read_buffer_) {
    NOTREACHED();
    return;
  }

  // Append the data we requested to the internal buffer.
  // TODO(brettw) avoid double-copying data that's coming from IPC and going
  // into the plugin buffer (we can skip the internal buffer in this case).
  object->PushBuffer(data.data(), data.length());

  if (result >= 0) {
    // Fill the user buffer. We may get fewer bytes than requested in the
    // case of stream end.
    int32_t bytes_to_return =
        std::min(object->current_read_buffer_size_,
                 static_cast<int32_t>(object->buffer_.size()));
    object->PopBuffer(object->current_read_buffer_, bytes_to_return);
    result = bytes_to_return;
  }

  // The plugin should be able to make a new request from their callback, so
  // we have to clear our copy first.
  PP_CompletionCallback temp_callback = object->current_read_callback_;
  object->current_read_callback_ = PP_BlockUntilComplete();
  object->current_read_buffer_ = NULL;
  object->current_read_buffer_size_ = 0;
  PP_RunCompletionCallback(&temp_callback, result);
}

void PPB_URLLoader_Proxy::RegisterStatusCallback(PP_Resource resource) {
  DCHECK(!dispatcher()->IsPlugin());
  if (!host_urlloader_trusted_interface_) {
    host_urlloader_trusted_interface_ =
        static_cast<const PPB_URLLoaderTrusted*>(
            dispatcher()->GetLocalInterface(PPB_URLLOADERTRUSTED_INTERFACE));
    if (!host_urlloader_trusted_interface_) {
      NOTREACHED();
      return;
    }
  }

  host_urlloader_trusted_interface_->RegisterStatusCallback(
      resource,
      &UpdateResourceLoadStatus);
}

void PPB_URLLoader_Proxy::OnReadCallback(int32_t result,
                                         ReadCallbackInfo* info) {
  int32_t bytes_read = 0;
  if (result > 0)
    bytes_read = result;  // Positive results indicate bytes read.
  info->read_buffer.resize(bytes_read);

  dispatcher()->Send(new PpapiMsg_PPBURLLoader_ReadResponseBody_Ack(
      INTERFACE_ID_PPB_URL_LOADER, info->resource, result, info->read_buffer));

  delete info;
}

// PPB_URLLoaderTrusted_Proxy --------------------------------------------------

PPB_URLLoaderTrusted_Proxy::PPB_URLLoaderTrusted_Proxy(
    Dispatcher* dispatcher,
    const void* target_interface)
    : InterfaceProxy(dispatcher, target_interface) {
}

PPB_URLLoaderTrusted_Proxy::~PPB_URLLoaderTrusted_Proxy() {
}

// static
const InterfaceProxy::Info* PPB_URLLoaderTrusted_Proxy::GetInfo() {
  static const Info info = {
    &urlloader_trusted_interface,
    PPB_URLLOADERTRUSTED_INTERFACE,
    INTERFACE_ID_PPB_URL_LOADER_TRUSTED,
    true,
    &CreateURLLoaderTrustedProxy,
  };
  return &info;
}

bool PPB_URLLoaderTrusted_Proxy::OnMessageReceived(const IPC::Message& msg) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(PPB_URLLoaderTrusted_Proxy, msg)
    IPC_MESSAGE_HANDLER(PpapiHostMsg_PPBURLLoaderTrusted_GrantUniversalAccess,
                        OnMsgGrantUniversalAccess)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP();
  // TODO(brettw) handle bad messages!
  return handled;
}

void PPB_URLLoaderTrusted_Proxy::OnMsgGrantUniversalAccess(
    const HostResource& loader) {
  ppb_url_loader_trusted_target()->GrantUniversalAccess(loader.host_resource());
}

}  // namespace proxy
}  // namespace pp
