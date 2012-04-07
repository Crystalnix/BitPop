// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/shared_impl/private/udp_socket_private_impl.h"

#include <string.h>

#include <algorithm>

#include "base/basictypes.h"
#include "base/bind.h"
#include "base/logging.h"
#include "base/message_loop.h"
#include "ppapi/c/pp_completion_callback.h"
#include "ppapi/c/pp_errors.h"

namespace ppapi {

const int32_t UDPSocketPrivateImpl::kMaxReadSize = 1024 * 1024;
const int32_t UDPSocketPrivateImpl::kMaxWriteSize = 1024 * 1024;

UDPSocketPrivateImpl::UDPSocketPrivateImpl(const HostResource& resource,
                                           uint32 socket_id)
    : Resource(resource) {
  Init(socket_id);
}

UDPSocketPrivateImpl::UDPSocketPrivateImpl(PP_Instance instance,
                                           uint32 socket_id)
    : Resource(instance) {
  Init(socket_id);
}

UDPSocketPrivateImpl::~UDPSocketPrivateImpl() {
}

thunk::PPB_UDPSocket_Private_API*
UDPSocketPrivateImpl::AsPPB_UDPSocket_Private_API() {
  return this;
}

int32_t UDPSocketPrivateImpl::Bind(const PP_NetAddress_Private* addr,
                                   PP_CompletionCallback callback) {
  if (!addr || !callback.func)
    return PP_ERROR_BADARGUMENT;
  if (bound_ || closed_)
    return PP_ERROR_FAILED;
  if (TrackedCallback::IsPending(bind_callback_))
    return PP_ERROR_INPROGRESS;
  // TODO(dmichael): use some other strategy for determining if an
  // operation is in progress

  bind_callback_ = new TrackedCallback(this, callback);

  // Send the request, the browser will call us back via BindACK.
  SendBind(*addr);
  return PP_OK_COMPLETIONPENDING;
}

int32_t UDPSocketPrivateImpl::RecvFrom(char* buffer,
                                       int32_t num_bytes,
                                       PP_CompletionCallback callback) {
  if (!buffer || num_bytes <= 0 || !callback.func)
    return PP_ERROR_BADARGUMENT;
  if (!bound_)
    return PP_ERROR_FAILED;
  if (TrackedCallback::IsPending(recvfrom_callback_))
    return PP_ERROR_INPROGRESS;

  read_buffer_ = buffer;
  bytes_to_read_ = std::min(num_bytes, kMaxReadSize);
  recvfrom_callback_ = new TrackedCallback(this, callback);

  // Send the request, the browser will call us back via RecvFromACK.
  SendRecvFrom(bytes_to_read_);
  return PP_OK_COMPLETIONPENDING;
}

PP_Bool UDPSocketPrivateImpl::GetRecvFromAddress(PP_NetAddress_Private* addr) {
  if (!addr)
    return PP_FALSE;

  *addr = recvfrom_addr_;
  return PP_TRUE;
}

int32_t UDPSocketPrivateImpl::SendTo(const char* buffer,
                                     int32_t num_bytes,
                                     const PP_NetAddress_Private* addr,
                                     PP_CompletionCallback callback) {
  if (!buffer || num_bytes <= 0 || !addr || !callback.func)
    return PP_ERROR_BADARGUMENT;
  if (!bound_)
    return PP_ERROR_FAILED;
  if (TrackedCallback::IsPending(sendto_callback_))
    return PP_ERROR_INPROGRESS;

  if (num_bytes > kMaxWriteSize)
    num_bytes = kMaxWriteSize;

  sendto_callback_ = new TrackedCallback(this, callback);

  // Send the request, the browser will call us back via SendToACK.
  SendSendTo(std::string(buffer, num_bytes), *addr);
  return PP_OK_COMPLETIONPENDING;
}

void UDPSocketPrivateImpl::Close() {
  if(closed_)
    return;

  bound_ = false;
  closed_ = true;

  SendClose();

  socket_id_ = 0;

  PostAbortIfNecessary(&bind_callback_);
  PostAbortIfNecessary(&recvfrom_callback_);
  PostAbortIfNecessary(&sendto_callback_);
}

void UDPSocketPrivateImpl::OnBindCompleted(bool succeeded) {
  if (!TrackedCallback::IsPending(bind_callback_)) {
    NOTREACHED();
    return;
  }

  if (succeeded)
    bound_ = true;

  TrackedCallback::ClearAndRun(&bind_callback_,
                               succeeded ? PP_OK : PP_ERROR_FAILED);
}

void UDPSocketPrivateImpl::OnRecvFromCompleted(
    bool succeeded,
    const std::string& data,
    const PP_NetAddress_Private& addr) {
  if (!TrackedCallback::IsPending(recvfrom_callback_) || !read_buffer_) {
    NOTREACHED();
    return;
  }

  if (succeeded) {
    CHECK_LE(static_cast<int32_t>(data.size()), bytes_to_read_);
    if (!data.empty())
      memcpy(read_buffer_, data.c_str(), data.size());
  }
  read_buffer_ = NULL;
  bytes_to_read_ = -1;
  recvfrom_addr_ = addr;

  TrackedCallback::ClearAndRun(&recvfrom_callback_,
      succeeded ? static_cast<int32_t>(data.size()) :
      static_cast<int32_t>(PP_ERROR_FAILED));
}

void UDPSocketPrivateImpl::OnSendToCompleted(bool succeeded,
                                             int32_t bytes_written) {
  if (!TrackedCallback::IsPending(sendto_callback_)) {
    NOTREACHED();
    return;
  }

  TrackedCallback::ClearAndRun(&sendto_callback_,
      succeeded ? bytes_written : static_cast<int32_t>(PP_ERROR_FAILED));
}

void UDPSocketPrivateImpl::Init(uint32 socket_id) {
  DCHECK(socket_id != 0);
  socket_id_ = socket_id;
  bound_ = false;
  closed_ = false;
  read_buffer_ = NULL;
  bytes_to_read_ = -1;

  recvfrom_addr_.size = 0;
  memset(recvfrom_addr_.data, 0,
         arraysize(recvfrom_addr_.data) * sizeof(*recvfrom_addr_.data));
}

void UDPSocketPrivateImpl::PostAbortIfNecessary(
    scoped_refptr<TrackedCallback>* callback) {
  if (callback->get())
    (*callback)->PostAbort();
}

}  // namespace ppapi
