// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RESOURCE_CONTEXT_H_
#define CONTENT_BROWSER_RESOURCE_CONTEXT_H_

#include "base/basictypes.h"
#include "base/memory/ref_counted.h"

class ChromeAppCacheService;
class ChromeBlobStorageContext;
namespace fileapi {
class FileSystemContext;
}  // namespace fileapi
namespace net {
class HostResolver;
class URLRequestContext;
}  // namespace net
namespace webkit_database {
class DatabaseTracker;
}  // namespace webkit_database

namespace content {

// ResourceContext contains the relevant context information required for
// resource loading. It lives on the IO thread, although it is constructed on
// the UI thread. ResourceContext doesn't own anything it points to, it just
// holds pointers to relevant objects to resource loading.
class ResourceContext {
 public:
  virtual ~ResourceContext();

  net::HostResolver* host_resolver() const;
  void set_host_resolver(net::HostResolver* host_resolver);

  net::URLRequestContext* request_context() const;
  void set_request_context(net::URLRequestContext* request_context);

  ChromeAppCacheService* appcache_service() const;
  void set_appcache_service(ChromeAppCacheService* service);

  webkit_database::DatabaseTracker* database_tracker() const;
  void set_database_tracker(webkit_database::DatabaseTracker* tracker);

  fileapi::FileSystemContext* file_system_context() const;
  void set_file_system_context(fileapi::FileSystemContext* context);

  ChromeBlobStorageContext* blob_storage_context() const;
  void set_blob_storage_context(ChromeBlobStorageContext* context);

 protected:
  ResourceContext();

 private:
  virtual void EnsureInitialized() const = 0;

  net::HostResolver* host_resolver_;
  net::URLRequestContext* request_context_;
  ChromeAppCacheService* appcache_service_;
  webkit_database::DatabaseTracker* database_tracker_;
  fileapi::FileSystemContext* file_system_context_;
  ChromeBlobStorageContext* blob_storage_context_;

  DISALLOW_COPY_AND_ASSIGN(ResourceContext);
};

}  // namespace content

#endif  // CONTENT_BROWSER_RESOURCE_CONTEXT_H_
