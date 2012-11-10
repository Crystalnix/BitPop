// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_protocols.h"

#include <algorithm>

#include "base/compiler_specific.h"
#include "base/file_path.h"
#include "base/logging.h"
#include "base/memory/weak_ptr.h"
#include "base/message_loop.h"
#include "base/path_service.h"
#include "base/string_util.h"
#include "base/stringprintf.h"
#include "base/threading/thread_restrictions.h"
#include "base/threading/worker_pool.h"
#include "build/build_config.h"
#include "chrome/browser/extensions/extension_info_map.h"
#include "chrome/browser/net/chrome_url_request_context.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_file_util.h"
#include "chrome/common/extensions/extension_resource.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/resource_request_info.h"
#include "googleurl/src/url_util.h"
#include "grit/component_extension_resources_map.h"
#include "net/base/mime_util.h"
#include "net/base/net_errors.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_response_info.h"
#include "net/url_request/url_request_error_job.h"
#include "net/url_request/url_request_file_job.h"
#include "net/url_request/url_request_simple_job.h"
#include "ui/base/layout.h"
#include "ui/base/resource/resource_bundle.h"

using content::ResourceRequestInfo;
using extensions::Extension;

namespace {

net::HttpResponseHeaders* BuildHttpHeaders(
    const std::string& content_security_policy, bool send_cors_header) {
  std::string raw_headers;
  raw_headers.append("HTTP/1.1 200 OK");
  if (!content_security_policy.empty()) {
    raw_headers.append(1, '\0');
    raw_headers.append("X-WebKit-CSP: ");
    raw_headers.append(content_security_policy);
  }

  if (send_cors_header) {
    raw_headers.append(1, '\0');
    raw_headers.append("Access-Control-Allow-Origin: *");
  }
  raw_headers.append(2, '\0');
  return new net::HttpResponseHeaders(raw_headers);
}

void ReadMimeTypeFromFile(const FilePath& filename,
                          std::string* mime_type,
                          bool* result) {
  *result = net::GetMimeTypeFromFile(filename, mime_type);
}

class URLRequestResourceBundleJob : public net::URLRequestSimpleJob {
 public:
  URLRequestResourceBundleJob(
      net::URLRequest* request, const FilePath& filename, int resource_id,
      const std::string& content_security_policy, bool send_cors_header)
      : net::URLRequestSimpleJob(request),
        filename_(filename),
        resource_id_(resource_id),
        weak_factory_(ALLOW_THIS_IN_INITIALIZER_LIST(this)) {
    response_info_.headers = BuildHttpHeaders(content_security_policy,
                                              send_cors_header);
  }

  // Overridden from URLRequestSimpleJob:
  virtual int GetData(std::string* mime_type,
                      std::string* charset,
                      std::string* data,
                      const net::CompletionCallback& callback) const OVERRIDE {
    const ResourceBundle& rb = ResourceBundle::GetSharedInstance();
    *data = rb.GetRawDataResource(
        resource_id_, ui::SCALE_FACTOR_NONE).as_string();

    std::string* read_mime_type = new std::string;
    bool* read_result = new bool;
    bool posted = base::WorkerPool::PostTaskAndReply(
        FROM_HERE,
        base::Bind(&ReadMimeTypeFromFile, filename_,
                   base::Unretained(read_mime_type),
                   base::Unretained(read_result)),
        base::Bind(&URLRequestResourceBundleJob::OnMimeTypeRead,
                   weak_factory_.GetWeakPtr(),
                   mime_type, charset, data,
                   base::Owned(read_mime_type),
                   base::Owned(read_result),
                   callback),
        true /* task is slow */);
    DCHECK(posted);

    return net::ERR_IO_PENDING;
  }

  virtual void GetResponseInfo(net::HttpResponseInfo* info) {
    *info = response_info_;
  }

 private:
  virtual ~URLRequestResourceBundleJob() { }

  void OnMimeTypeRead(std::string* out_mime_type,
                      std::string* charset,
                      std::string* data,
                      std::string* read_mime_type,
                      bool* read_result,
                      const net::CompletionCallback& callback) {
    *out_mime_type = *read_mime_type;
    if (StartsWithASCII(*read_mime_type, "text/", false)) {
      // All of our HTML files should be UTF-8 and for other resource types
      // (like images), charset doesn't matter.
      DCHECK(IsStringUTF8(*data));
      *charset = "utf-8";
    }
    int result = *read_result? net::OK: net::ERR_INVALID_URL;
    callback.Run(result);
  }

  // We need the filename of the resource to determine the mime type.
  FilePath filename_;

  // The resource bundle id to load.
  int resource_id_;

  net::HttpResponseInfo response_info_;

  mutable base::WeakPtrFactory<URLRequestResourceBundleJob> weak_factory_;
};

class GeneratedBackgroundPageJob : public net::URLRequestSimpleJob {
 public:
  GeneratedBackgroundPageJob(net::URLRequest* request,
                             const scoped_refptr<const Extension> extension,
                             const std::string& content_security_policy)
      : net::URLRequestSimpleJob(request),
        extension_(extension) {
    const bool send_cors_headers = false;
    response_info_.headers = BuildHttpHeaders(content_security_policy,
                                              send_cors_headers);
  }

  // Overridden from URLRequestSimpleJob:
  virtual int GetData(std::string* mime_type,
                      std::string* charset,
                      std::string* data,
                      const net::CompletionCallback& callback) const OVERRIDE {
    *mime_type = "text/html";
    *charset = "utf-8";

    *data = "<!DOCTYPE html>\n<body>\n";
    for (size_t i = 0; i < extension_->background_scripts().size(); ++i) {
      *data += "<script src=\"";
      *data += extension_->background_scripts()[i];
      *data += "\"></script>\n";
    }

    return net::OK;
  }

  virtual void GetResponseInfo(net::HttpResponseInfo* info) {
    *info = response_info_;
  }

 private:
  virtual ~GeneratedBackgroundPageJob() {}

  scoped_refptr<const Extension> extension_;
  net::HttpResponseInfo response_info_;
};

void ReadResourceFilePath(const ExtensionResource& resource,
                          FilePath* file_path) {
  *file_path = resource.GetFilePath();
}

class URLRequestExtensionJob : public net::URLRequestFileJob {
 public:
  URLRequestExtensionJob(net::URLRequest* request,
                         const std::string& extension_id,
                         const FilePath& directory_path,
                         const std::string& content_security_policy,
                         bool send_cors_header)
    : net::URLRequestFileJob(request, FilePath()),
      // TODO(tc): Move all of these files into resources.pak so we don't break
      // when updating on Linux.
      resource_(extension_id, directory_path,
                extension_file_util::ExtensionURLToRelativeFilePath(
                    request->url())),
      weak_factory_(ALLOW_THIS_IN_INITIALIZER_LIST(this)) {
      response_info_.headers = BuildHttpHeaders(content_security_policy,
                                                send_cors_header);
  }

  virtual void GetResponseInfo(net::HttpResponseInfo* info) OVERRIDE {
    *info = response_info_;
  }

  virtual void Start() OVERRIDE {
    FilePath* read_file_path = new FilePath;
    bool posted = base::WorkerPool::PostTaskAndReply(
        FROM_HERE,
        base::Bind(&ReadResourceFilePath, resource_,
                   base::Unretained(read_file_path)),
        base::Bind(&URLRequestExtensionJob::OnFilePathRead,
                   weak_factory_.GetWeakPtr(),
                   base::Owned(read_file_path)),
        true /* task is slow */);
    DCHECK(posted);
  }

 private:
  virtual ~URLRequestExtensionJob() {}

  void OnFilePathRead(FilePath* read_file_path) {
    file_path_ = *read_file_path;
    URLRequestFileJob::Start();
  }

  net::HttpResponseInfo response_info_;
  ExtensionResource resource_;
  base::WeakPtrFactory<URLRequestExtensionJob> weak_factory_;
};

bool ExtensionCanLoadInIncognito(const ResourceRequestInfo* info,
                                 const std::string& extension_id,
                                 ExtensionInfoMap* extension_info_map) {
  if (!extension_info_map->IsIncognitoEnabled(extension_id))
    return false;

  // Only allow incognito toplevel navigations to extension resources in
  // split mode. In spanning mode, the extension must run in a single process,
  // and an incognito tab prevents that.
  if (info->GetResourceType() == ResourceType::MAIN_FRAME) {
    const Extension* extension =
        extension_info_map->extensions().GetByID(extension_id);
    return extension && extension->incognito_split_mode();
  }

  return true;
}

// Returns true if an chrome-extension:// resource should be allowed to load.
// TODO(aa): This should be moved into ExtensionResourceRequestPolicy, but we
// first need to find a way to get CanLoadInIncognito state into the renderers.
bool AllowExtensionResourceLoad(net::URLRequest* request,
                                bool is_incognito,
                                ExtensionInfoMap* extension_info_map) {
  const ResourceRequestInfo* info = ResourceRequestInfo::ForRequest(request);

  // We have seen crashes where info is NULL: crbug.com/52374.
  if (!info) {
    LOG(ERROR) << "Allowing load of " << request->url().spec()
               << "from unknown origin. Could not find user data for "
               << "request.";
    return true;
  }

  if (is_incognito && !ExtensionCanLoadInIncognito(info, request->url().host(),
                                                   extension_info_map)) {
    return false;
  }

  return true;
}

// Returns true if the given URL references an icon in the given extension.
bool URLIsForExtensionIcon(const GURL& url, const Extension* extension) {
  DCHECK(url.SchemeIs(chrome::kExtensionScheme));

  if (!extension)
    return false;

  std::string path = url.path();
  DCHECK_EQ(url.host(), extension->id());
  DCHECK(path.length() > 0 && path[0] == '/');
  path = path.substr(1);
  return extension->icons().ContainsPath(path);
}

class ExtensionProtocolHandler
    : public net::URLRequestJobFactory::ProtocolHandler {
 public:
  ExtensionProtocolHandler(bool is_incognito,
                           ExtensionInfoMap* extension_info_map)
      : is_incognito_(is_incognito),
        extension_info_map_(extension_info_map) {}

  virtual ~ExtensionProtocolHandler() {}

  virtual net::URLRequestJob* MaybeCreateJob(
      net::URLRequest* request) const OVERRIDE;

 private:
  const bool is_incognito_;
  ExtensionInfoMap* const extension_info_map_;
  DISALLOW_COPY_AND_ASSIGN(ExtensionProtocolHandler);
};

// Creates URLRequestJobs for extension:// URLs.
net::URLRequestJob*
ExtensionProtocolHandler::MaybeCreateJob(net::URLRequest* request) const {
  // TODO(mpcomplete): better error code.
  if (!AllowExtensionResourceLoad(
           request, is_incognito_, extension_info_map_)) {
    return new net::URLRequestErrorJob(request, net::ERR_ADDRESS_UNREACHABLE);
  }

  // chrome-extension://extension-id/resource/path.js
  const std::string& extension_id = request->url().host();
  const Extension* extension =
      extension_info_map_->extensions().GetByID(extension_id);
  FilePath directory_path;
  if (extension)
    directory_path = extension->path();
  if (directory_path.value().empty()) {
    const Extension* disabled_extension =
        extension_info_map_->disabled_extensions().GetByID(extension_id);
    if (URLIsForExtensionIcon(request->url(), disabled_extension))
      directory_path = disabled_extension->path();
    if (directory_path.value().empty()) {
      LOG(WARNING) << "Failed to GetPathForExtension: " << extension_id;
      return NULL;
    }
  }

  std::string content_security_policy;
  bool send_cors_header = false;
  if (extension) {
    std::string resource_path = request->url().path();
    content_security_policy =
        extension->GetResourceContentSecurityPolicy(resource_path);
    if ((extension->manifest_version() >= 2 ||
             extension->HasWebAccessibleResources()) &&
        extension->IsResourceWebAccessible(resource_path))
      send_cors_header = true;
  }

  std::string path = request->url().path();
  if (path.size() > 1 &&
      path.substr(1) == extension_filenames::kGeneratedBackgroundPageFilename) {
    return new GeneratedBackgroundPageJob(
        request, extension, content_security_policy);
  }

  FilePath resources_path;
  FilePath relative_path;
  // Try to load extension resources from chrome resource file if
  // directory_path is a descendant of resources_path. resources_path
  // corresponds to src/chrome/browser/resources in source tree.
  if (PathService::Get(chrome::DIR_RESOURCES, &resources_path) &&
      // Since component extension resources are included in
      // component_extension_resources.pak file in resources_path, calculate
      // extension relative path against resources_path.
      resources_path.AppendRelativePath(directory_path, &relative_path)) {
    relative_path = relative_path.Append(
        extension_file_util::ExtensionURLToRelativeFilePath(request->url()));
    relative_path = relative_path.NormalizePathSeparators();

    // TODO(tc): Make a map of FilePath -> resource ids so we don't have to
    // covert to FilePaths all the time.  This will be more useful as we add
    // more resources.
    for (size_t i = 0; i < kComponentExtensionResourcesSize; ++i) {
      FilePath bm_resource_path =
          FilePath().AppendASCII(kComponentExtensionResources[i].name);
      bm_resource_path = bm_resource_path.NormalizePathSeparators();
      if (relative_path == bm_resource_path) {
        return new URLRequestResourceBundleJob(request, relative_path,
            kComponentExtensionResources[i].value, content_security_policy,
            send_cors_header);
      }
    }
  }

  return new URLRequestExtensionJob(request, extension_id, directory_path,
                                    content_security_policy, send_cors_header);
}

}  // namespace

net::URLRequestJobFactory::ProtocolHandler* CreateExtensionProtocolHandler(
    bool is_incognito,
    ExtensionInfoMap* extension_info_map) {
  return new ExtensionProtocolHandler(is_incognito, extension_info_map);
}
