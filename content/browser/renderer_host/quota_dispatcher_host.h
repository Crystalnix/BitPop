// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RENDERER_HOST_QUOTA_DISPATCHER_HOST_H_
#define CONTENT_BROWSER_RENDERER_HOST_QUOTA_DISPATCHER_HOST_H_

#include "base/basictypes.h"
#include "base/id_map.h"
#include "content/browser/browser_message_filter.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebStorageQuotaType.h"

class GURL;

namespace IPC {
class Message;
}

namespace quota {
class QuotaManager;
}

class QuotaPermissionContext;

class QuotaDispatcherHost : public BrowserMessageFilter {
 public:
  QuotaDispatcherHost(int process_id,
                      quota::QuotaManager* quota_manager,
                      QuotaPermissionContext* permission_context);
  virtual ~QuotaDispatcherHost();
  virtual bool OnMessageReceived(const IPC::Message& message,
                                 bool* message_was_ok);

 private:
  class RequestDispatcher;
  class QueryUsageAndQuotaDispatcher;
  class RequestQuotaDispatcher;

  void OnQueryStorageUsageAndQuota(
      int request_id,
      const GURL& origin_url,
      WebKit::WebStorageQuotaType type);
  void OnRequestStorageQuota(
      int render_view_id,
      int request_id,
      const GURL& origin_url,
      WebKit::WebStorageQuotaType type,
      int64 requested_size);

  // The ID of this process.
  int process_id_;

  quota::QuotaManager* quota_manager_;
  scoped_refptr<QuotaPermissionContext> permission_context_;

  IDMap<RequestDispatcher, IDMapOwnPointer> outstanding_requests_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(QuotaDispatcherHost);
};

#endif  // CONTENT_BROWSER_RENDERER_HOST_QUOTA_DISPATCHER_HOST_H_
