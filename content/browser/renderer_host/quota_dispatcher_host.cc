// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/quota_dispatcher_host.h"

#include "base/memory/scoped_callback_factory.h"
#include "content/browser/quota_permission_context.h"
#include "content/common/quota_messages.h"
#include "googleurl/src/gurl.h"
#include "net/base/net_util.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebStorageQuotaError.h"
#include "webkit/quota/quota_manager.h"

using quota::QuotaManager;
using quota::QuotaStatusCode;
using quota::StorageType;
using WebKit::WebStorageQuotaError;

// Created one per request to carry the request's request_id around.
// Dispatches requests from renderer/worker to the QuotaManager and
// sends back the response to the renderer/worker.
class QuotaDispatcherHost::RequestDispatcher {
 public:
  RequestDispatcher(QuotaDispatcherHost* dispatcher_host,
                    int request_id)
      : dispatcher_host_(dispatcher_host),
        request_id_(request_id) {
    dispatcher_host_->outstanding_requests_.AddWithID(this, request_id_);
  }
  virtual ~RequestDispatcher() {}

 protected:
  // Subclass must call this when it's done with the request.
  void Completed() {
    dispatcher_host_->outstanding_requests_.Remove(request_id_);
  }

  QuotaDispatcherHost* dispatcher_host() const { return dispatcher_host_; }
  quota::QuotaManager* quota_manager() const {
    return dispatcher_host_->quota_manager_;
  }
  QuotaPermissionContext* permission_context() const {
    return dispatcher_host_->permission_context_.get();
  }
  int render_process_id() const { return dispatcher_host_->process_id_; }
  int request_id() const { return request_id_; }

 private:
  QuotaDispatcherHost* dispatcher_host_;
  int request_id_;
};

class QuotaDispatcherHost::QueryUsageAndQuotaDispatcher
    : public RequestDispatcher {
 public:
  QueryUsageAndQuotaDispatcher(
      QuotaDispatcherHost* dispatcher_host,
      int request_id)
      : RequestDispatcher(dispatcher_host, request_id),
        callback_factory_(ALLOW_THIS_IN_INITIALIZER_LIST(this)) {}
  virtual ~QueryUsageAndQuotaDispatcher() {}

  void QueryStorageUsageAndQuota(const GURL& origin, StorageType type) {
    quota_manager()->GetUsageAndQuota(origin, type,
        callback_factory_.NewCallback(
            &QueryUsageAndQuotaDispatcher::DidQueryStorageUsageAndQuota));
  }

 private:
  void DidQueryStorageUsageAndQuota(
      QuotaStatusCode status, int64 usage, int64 quota) {
    DCHECK(dispatcher_host());
    if (status != quota::kQuotaStatusOk) {
      dispatcher_host()->Send(new QuotaMsg_DidFail(
          request_id(), static_cast<WebStorageQuotaError>(status)));
    } else {
      dispatcher_host()->Send(new QuotaMsg_DidQueryStorageUsageAndQuota(
          request_id(), usage, quota));
    }
    Completed();
  }

  base::ScopedCallbackFactory<QueryUsageAndQuotaDispatcher> callback_factory_;
};

class QuotaDispatcherHost::RequestQuotaDispatcher
    : public RequestDispatcher {
 public:
  typedef RequestQuotaDispatcher self_type;

  RequestQuotaDispatcher(QuotaDispatcherHost* dispatcher_host,
                         int request_id,
                         const GURL& origin,
                         StorageType type,
                         int64 requested_quota,
                         int render_view_id)
      : RequestDispatcher(dispatcher_host, request_id),
        origin_(origin),
        host_(net::GetHostOrSpecFromURL(origin)),
        type_(type),
        current_quota_(0),
        requested_quota_(requested_quota),
        render_view_id_(render_view_id),
        callback_factory_(ALLOW_THIS_IN_INITIALIZER_LIST(this)) {}
  virtual ~RequestQuotaDispatcher() {}

  void Start() {
    DCHECK(type_ == quota::kStorageTypeTemporary ||
           type_ == quota::kStorageTypePersistent);
    if (type_ == quota::kStorageTypePersistent) {
      quota_manager()->GetPersistentHostQuota(
          host_,
          callback_factory_.NewCallback(&self_type::DidGetHostQuota));
    } else {
      quota_manager()->GetUsageAndQuota(
          origin_, type_,
          callback_factory_.NewCallback(
              &self_type::DidGetTemporaryUsageAndQuota));
    }
  }

 private:
  void DidGetHostQuota(QuotaStatusCode status,
                       const std::string& host,
                       StorageType type,
                       int64 quota) {
    DCHECK_EQ(type_, type);
    DCHECK_EQ(host_, host);
    if (status != quota::kQuotaStatusOk) {
      DidFinish(status, 0);
      return;
    }
    if (requested_quota_ <= quota) {
      // Seems like we can just let it go.
      DidFinish(quota::kQuotaStatusOk, requested_quota_);
      return;
    }
    current_quota_ = quota;
    // Otherwise we need to consult with the permission context and
    // possibly show an infobar.
    DCHECK(permission_context());
    permission_context()->RequestQuotaPermission(
        origin_, type_, requested_quota_, render_process_id(), render_view_id_,
        callback_factory_.NewCallback(&self_type::DidGetPermissionResponse));
  }

  void DidGetTemporaryUsageAndQuota(QuotaStatusCode status,
                                    int64 usage_unused,
                                    int64 quota) {
    DidFinish(status, std::min(requested_quota_, quota));
  }

  void DidGetPermissionResponse(QuotaPermissionContext::Response response) {
    if (response != QuotaPermissionContext::kResponseAllow) {
      // User didn't allow the new quota.  Just returning the current quota.
      DidFinish(quota::kQuotaStatusOk, current_quota_);
      return;
    }
    // Now we're allowed to set the new quota.
    quota_manager()->SetPersistentHostQuota(
        host_, requested_quota_,
        callback_factory_.NewCallback(&self_type::DidSetHostQuota));
  }

  void DidSetHostQuota(QuotaStatusCode status,
                       const std::string& host,
                       StorageType type,
                       int64 new_quota) {
    DCHECK_EQ(host_, host);
    DCHECK_EQ(type_, type);
    DidFinish(status, new_quota);
  }

  void DidFinish(QuotaStatusCode status, int64 granted_quota) {
    DCHECK(dispatcher_host());
    if (status != quota::kQuotaStatusOk) {
      dispatcher_host()->Send(new QuotaMsg_DidFail(
          request_id(), static_cast<WebStorageQuotaError>(status)));
    } else {
      dispatcher_host()->Send(new QuotaMsg_DidGrantStorageQuota(
          request_id(), granted_quota));
    }
    Completed();
  }

  const GURL origin_;
  const std::string host_;
  const StorageType type_;
  int64 current_quota_;
  const int64 requested_quota_;
  const int render_view_id_;
  base::ScopedCallbackFactory<self_type> callback_factory_;
};

QuotaDispatcherHost::QuotaDispatcherHost(
    int process_id,
    QuotaManager* quota_manager,
    QuotaPermissionContext* permission_context)
    : process_id_(process_id),
      quota_manager_(quota_manager),
      permission_context_(permission_context) {
}

QuotaDispatcherHost::~QuotaDispatcherHost() {
}

bool QuotaDispatcherHost::OnMessageReceived(
    const IPC::Message& message, bool* message_was_ok) {
  *message_was_ok = true;
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP_EX(QuotaDispatcherHost, message, *message_was_ok)
    IPC_MESSAGE_HANDLER(QuotaHostMsg_QueryStorageUsageAndQuota,
                        OnQueryStorageUsageAndQuota)
    IPC_MESSAGE_HANDLER(QuotaHostMsg_RequestStorageQuota,
                        OnRequestStorageQuota)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP_EX()
  return handled;
}

void QuotaDispatcherHost::OnQueryStorageUsageAndQuota(
    int request_id,
    const GURL& origin,
    WebKit::WebStorageQuotaType type) {
  QueryUsageAndQuotaDispatcher* dispatcher = new QueryUsageAndQuotaDispatcher(
      this, request_id);
  dispatcher->QueryStorageUsageAndQuota(origin, static_cast<StorageType>(type));
}

void QuotaDispatcherHost::OnRequestStorageQuota(
    int render_view_id,
    int request_id,
    const GURL& origin,
    WebKit::WebStorageQuotaType type,
    int64 requested_size) {
  if (quota_manager_->IsStorageUnlimited(origin)) {
    // If the origin is marked 'unlimited' we always just return ok.
    Send(new QuotaMsg_DidGrantStorageQuota(request_id, requested_size));
    return;
  }

  StorageType storage_type = static_cast<StorageType>(type);
  if (storage_type != quota::kStorageTypeTemporary &&
      storage_type != quota::kStorageTypePersistent) {
    // Unsupported storage types.
    Send(new QuotaMsg_DidFail(
        request_id,
        WebKit::WebStorageQuotaErrorNotSupported));
    return;
  }

  RequestQuotaDispatcher* dispatcher = new RequestQuotaDispatcher(
      this, request_id, origin, storage_type,
      requested_size, render_view_id);
  dispatcher->Start();
}

COMPILE_ASSERT(int(WebKit::WebStorageQuotaTypeTemporary) == \
               int(quota::kStorageTypeTemporary), mismatching_enums);
COMPILE_ASSERT(int(WebKit::WebStorageQuotaTypePersistent) == \
               int(quota::kStorageTypePersistent), mismatching_enums);

COMPILE_ASSERT(int(WebKit::WebStorageQuotaErrorNotSupported) == \
               int(quota::kQuotaErrorNotSupported), mismatching_enums);
COMPILE_ASSERT(int(WebKit::WebStorageQuotaErrorAbort) == \
               int(quota::kQuotaErrorAbort), mismatching_enums);
