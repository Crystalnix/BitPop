// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/plugins/ppapi/ppb_transport_impl.h"

#include "base/message_loop.h"
#include "base/string_util.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/base/net_util.h"
#include "net/socket/socket.h"
#include "ppapi/c/dev/ppb_transport_dev.h"
#include "ppapi/c/pp_completion_callback.h"
#include "ppapi/c/pp_errors.h"
#include "ppapi/shared_impl/callback_tracker.h"
#include "ppapi/shared_impl/ppapi_globals.h"
#include "ppapi/shared_impl/var.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebDocument.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebElement.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebPluginContainer.h"
#include "webkit/plugins/ppapi/common.h"
#include "webkit/plugins/ppapi/plugin_module.h"
#include "webkit/plugins/ppapi/ppapi_plugin_instance.h"
#include "webkit/plugins/ppapi/resource_helper.h"

using ppapi::StringVar;
using ppapi::thunk::PPB_Transport_API;
using ppapi::TrackedCallback;
using webkit_glue::P2PTransport;

namespace webkit {
namespace ppapi {

namespace {

const char kUdpProtocolName[] = "udp";
const char kTcpProtocolName[] = "tcp";

const int kMinBufferSize = 1024;
const int kMaxBufferSize = 1024 * 1024;
const int kMinAckDelay = 10;
const int kMaxAckDelay = 1000;

int MapNetError(int result) {
  if (result > 0)
    return result;

  switch (result) {
    case net::OK:
      return PP_OK;
    case net::ERR_IO_PENDING:
      return PP_OK_COMPLETIONPENDING;
    case net::ERR_INVALID_ARGUMENT:
      return PP_ERROR_BADARGUMENT;
    default:
      return PP_ERROR_FAILED;
  }
}

WebKit::WebFrame* GetFrameForResource(const ::ppapi::Resource* resource) {
  PluginInstance* plugin_instance =
      ResourceHelper::GetPluginInstance(resource);
  if (!plugin_instance)
    return NULL;
  return plugin_instance->container()->element().document().frame();
}

}  // namespace

PPB_Transport_Impl::PPB_Transport_Impl(PP_Instance instance)
    : Resource(instance),
      type_(PP_TRANSPORTTYPE_DATAGRAM),
      started_(false),
      writable_(false) {
}

PPB_Transport_Impl::~PPB_Transport_Impl() {
}

// static
PP_Resource PPB_Transport_Impl::Create(PP_Instance instance,
                                       const char* name,
                                       PP_TransportType type) {
  scoped_refptr<PPB_Transport_Impl> t(new PPB_Transport_Impl(instance));
  if (!t->Init(name, type))
    return 0;
  return t->GetReference();
}

PPB_Transport_API* PPB_Transport_Impl::AsPPB_Transport_API() {
  return this;
}

bool PPB_Transport_Impl::Init(const char* name, PP_TransportType type) {
  name_ = name;

  if (type != PP_TRANSPORTTYPE_DATAGRAM && type != PP_TRANSPORTTYPE_STREAM) {
    LOG(WARNING) << "Unknown transport type: " << type;
    return false;
  }
  type_ = type;

  PluginDelegate* plugin_delegate = ResourceHelper::GetPluginDelegate(this);
  if (!plugin_delegate)
    return false;
  p2p_transport_.reset(plugin_delegate->CreateP2PTransport());
  return p2p_transport_.get() != NULL;
}

PP_Bool PPB_Transport_Impl::IsWritable() {
  if (!p2p_transport_.get())
    return PP_FALSE;

  return PP_FromBool(writable_);
}

int32_t PPB_Transport_Impl::SetProperty(PP_TransportProperty property,
                                        PP_Var value) {
  // SetProperty() may be called only before Connect().
  if (started_)
    return PP_ERROR_FAILED;

  switch (property) {
    case PP_TRANSPORTPROPERTY_STUN_SERVER: {
      StringVar* value_str = StringVar::FromPPVar(value);
      if (!value_str)
        return PP_ERROR_BADARGUMENT;
      if (!net::ParseHostAndPort(value_str->value(), &config_.stun_server,
                                 &config_.stun_server_port)) {
        return PP_ERROR_BADARGUMENT;
      }
      break;
    }

    case PP_TRANSPORTPROPERTY_RELAY_SERVER: {
      StringVar* value_str = StringVar::FromPPVar(value);
      if (!value_str)
        return PP_ERROR_BADARGUMENT;
      if (!net::ParseHostAndPort(value_str->value(), &config_.relay_server,
                                 &config_.relay_server_port)) {
        return PP_ERROR_BADARGUMENT;
      }
      break;
    }

    case PP_TRANSPORTPROPERTY_RELAY_USERNAME: {
      StringVar* value_str = StringVar::FromPPVar(value);
      if (!value_str)
        return PP_ERROR_BADARGUMENT;
      config_.relay_username = value_str->value();
      break;
    }

    case PP_TRANSPORTPROPERTY_RELAY_PASSWORD: {
      StringVar* value_str = StringVar::FromPPVar(value);
      if (!value_str)
        return PP_ERROR_BADARGUMENT;
      config_.relay_password = value_str->value();
      break;
    }

    case PP_TRANSPORTPROPERTY_RELAY_MODE: {
      switch (value.value.as_int) {
        case PP_TRANSPORTRELAYMODE_TURN:
          config_.legacy_relay = false;
          break;
        case PP_TRANSPORTRELAYMODE_GOOGLE:
          config_.legacy_relay = true;
          break;
        default:
          return PP_ERROR_BADARGUMENT;
      }
      break;
    }

    case PP_TRANSPORTPROPERTY_TCP_RECEIVE_WINDOW: {
      if (type_ != PP_TRANSPORTTYPE_STREAM)
        return PP_ERROR_BADARGUMENT;

      int32_t int_value = value.value.as_int;
      if (value.type != PP_VARTYPE_INT32 || int_value < kMinBufferSize ||
          int_value > kMaxBufferSize) {
        return PP_ERROR_BADARGUMENT;
      }
      config_.tcp_receive_window = int_value;
      break;
    }

    case PP_TRANSPORTPROPERTY_TCP_SEND_WINDOW: {
      if (type_ != PP_TRANSPORTTYPE_STREAM)
        return PP_ERROR_BADARGUMENT;

      int32_t int_value = value.value.as_int;
      if (value.type != PP_VARTYPE_INT32 || int_value < kMinBufferSize ||
          int_value > kMaxBufferSize) {
        return PP_ERROR_BADARGUMENT;
      }
      config_.tcp_send_window = int_value;
      break;
    }

    case PP_TRANSPORTPROPERTY_TCP_NO_DELAY: {
      if (type_ != PP_TRANSPORTTYPE_STREAM)
        return PP_ERROR_BADARGUMENT;

      if (value.type != PP_VARTYPE_BOOL)
        return PP_ERROR_BADARGUMENT;
      config_.tcp_no_delay = PP_ToBool(value.value.as_bool);
      break;
    }

    case PP_TRANSPORTPROPERTY_TCP_ACK_DELAY: {
      if (type_ != PP_TRANSPORTTYPE_STREAM)
        return PP_ERROR_BADARGUMENT;

      int32_t int_value = value.value.as_int;
      if (value.type != PP_VARTYPE_INT32 || int_value < kMinAckDelay ||
          int_value > kMaxAckDelay) {
        return PP_ERROR_BADARGUMENT;
      }
      config_.tcp_ack_delay_ms = int_value;
      break;
    }

    case PP_TRANSPORTPROPERTY_DISABLE_TCP_TRANSPORT: {
      if (value.type != PP_VARTYPE_BOOL)
        return PP_ERROR_BADARGUMENT;
      config_.disable_tcp_transport = PP_ToBool(value.value.as_bool);
      break;
    }

    default:
      return PP_ERROR_BADARGUMENT;
  }

  return PP_OK;
}

int32_t PPB_Transport_Impl::Connect(PP_CompletionCallback callback) {
  if (!callback.func)
    return PP_ERROR_BLOCKS_MAIN_THREAD;
  if (!p2p_transport_.get())
    return PP_ERROR_FAILED;

  // Connect() has already been called.
  if (started_)
    return PP_ERROR_INPROGRESS;

  P2PTransport::Protocol protocol = (type_ == PP_TRANSPORTTYPE_STREAM) ?
      P2PTransport::PROTOCOL_TCP : P2PTransport::PROTOCOL_UDP;

  if (!p2p_transport_->Init(
          GetFrameForResource(this), name_, protocol, config_, this)) {
    return PP_ERROR_FAILED;
  }

  started_ = true;

  PluginModule* plugin_module = ResourceHelper::GetPluginModule(this);
  if (!plugin_module)
    return PP_ERROR_FAILED;

  connect_callback_ = new TrackedCallback(this, callback);
  return PP_OK_COMPLETIONPENDING;
}

int32_t PPB_Transport_Impl::GetNextAddress(PP_Var* address,
                                           PP_CompletionCallback callback) {
  if (!callback.func)
    return PP_ERROR_BLOCKS_MAIN_THREAD;
  if (!p2p_transport_.get())
    return PP_ERROR_FAILED;

  if (TrackedCallback::IsPending(next_address_callback_))
    return PP_ERROR_INPROGRESS;

  PluginModule* plugin_module = ResourceHelper::GetPluginModule(this);
  if (!plugin_module)
    return PP_ERROR_FAILED;

  if (!local_candidates_.empty()) {
    *address = StringVar::StringToPPVar(local_candidates_.front());
    local_candidates_.pop_front();
    return PP_OK;
  }

  next_address_callback_ = new TrackedCallback(this, callback);
  return PP_OK_COMPLETIONPENDING;
}

int32_t PPB_Transport_Impl::ReceiveRemoteAddress(PP_Var address) {
  if (!p2p_transport_.get())
    return PP_ERROR_FAILED;

  StringVar* address_str = StringVar::FromPPVar(address);
  if (!address_str)
    return PP_ERROR_BADARGUMENT;

  return p2p_transport_->AddRemoteCandidate(address_str->value()) ?
      PP_OK : PP_ERROR_FAILED;
}

int32_t PPB_Transport_Impl::Recv(void* data, uint32_t len,
                                 PP_CompletionCallback callback) {
  if (!callback.func)
    return PP_ERROR_BLOCKS_MAIN_THREAD;
  if (!p2p_transport_.get())
    return PP_ERROR_FAILED;

  if (TrackedCallback::IsPending(recv_callback_))
    return PP_ERROR_INPROGRESS;

  net::Socket* channel = p2p_transport_->GetChannel();
  if (!channel)
    return PP_ERROR_FAILED;

  PluginModule* plugin_module = ResourceHelper::GetPluginModule(this);
  if (!plugin_module)
    return PP_ERROR_FAILED;

  scoped_refptr<net::IOBuffer> buffer =
      new net::WrappedIOBuffer(static_cast<const char*>(data));
  int result = MapNetError(
      channel->Read(buffer, len, base::Bind(&PPB_Transport_Impl::OnRead,
                                            base::Unretained(this))));
  if (result == PP_OK_COMPLETIONPENDING)
    recv_callback_ = new TrackedCallback(this, callback);

  return result;
}

int32_t PPB_Transport_Impl::Send(const void* data, uint32_t len,
                                 PP_CompletionCallback callback) {
  if (!callback.func)
    return PP_ERROR_BLOCKS_MAIN_THREAD;
  if (!p2p_transport_.get())
    return PP_ERROR_FAILED;

  if (TrackedCallback::IsPending(send_callback_))
    return PP_ERROR_INPROGRESS;

  net::Socket* channel = p2p_transport_->GetChannel();
  if (!channel)
    return PP_ERROR_FAILED;

  PluginModule* plugin_module = ResourceHelper::GetPluginModule(this);
  if (!plugin_module)
    return PP_ERROR_FAILED;

  scoped_refptr<net::IOBuffer> buffer =
      new net::WrappedIOBuffer(static_cast<const char*>(data));
  int result = MapNetError(
      channel->Write(buffer, len, base::Bind(&PPB_Transport_Impl::OnWritten,
                                             base::Unretained(this))));
  if (result == PP_OK_COMPLETIONPENDING)
    send_callback_ = new TrackedCallback(this, callback);

  return result;
}

int32_t PPB_Transport_Impl::Close() {
  if (!p2p_transport_.get())
    return PP_ERROR_FAILED;

  p2p_transport_.reset();

  ::ppapi::PpapiGlobals::Get()->GetCallbackTrackerForInstance(
      pp_instance())->PostAbortForResource(pp_resource());
  return PP_OK;
}

void PPB_Transport_Impl::OnCandidateReady(const std::string& address) {
  // Store the candidate first before calling the callback.
  local_candidates_.push_back(address);

  if (TrackedCallback::IsPending(next_address_callback_))
    TrackedCallback::ClearAndRun(&next_address_callback_, PP_OK);
}

void PPB_Transport_Impl::OnStateChange(webkit_glue::P2PTransport::State state) {
  writable_ = (state | webkit_glue::P2PTransport::STATE_WRITABLE) != 0;
  if (writable_ && TrackedCallback::IsPending(connect_callback_))
    TrackedCallback::ClearAndRun(&connect_callback_, PP_OK);
}

void PPB_Transport_Impl::OnError(int error) {
  writable_ = false;
  if (TrackedCallback::IsPending(connect_callback_))
    TrackedCallback::ClearAndRun(&connect_callback_, PP_ERROR_FAILED);
}

void PPB_Transport_Impl::OnRead(int result) {
  DCHECK(TrackedCallback::IsPending(recv_callback_));
  TrackedCallback::ClearAndRun(&recv_callback_, MapNetError(result));
}

void PPB_Transport_Impl::OnWritten(int result) {
  DCHECK(TrackedCallback::IsPending(send_callback_));
  TrackedCallback::ClearAndRun(&send_callback_, MapNetError(result));
}

}  // namespace ppapi
}  // namespace webkit
