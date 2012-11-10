// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/tools/test_shell/simple_file_system.h"

#include "base/bind.h"
#include "base/file_path.h"
#include "base/message_loop.h"
#include "base/message_loop_proxy.h"
#include "base/time.h"
#include "base/utf_string_conversions.h"
#include "googleurl/src/gurl.h"
#include "net/base/mime_util.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebDocument.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebFileInfo.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebFileSystemCallbacks.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebFileSystemEntry.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebFrame.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebSecurityOrigin.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebURL.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebVector.h"
#include "webkit/blob/blob_storage_controller.h"
#include "webkit/fileapi/file_system_task_runners.h"
#include "webkit/fileapi/file_system_url.h"
#include "webkit/fileapi/file_system_util.h"
#include "webkit/fileapi/mock_file_system_options.h"
#include "webkit/glue/webkit_glue.h"
#include "webkit/tools/test_shell/simple_file_writer.h"

using base::WeakPtr;

using WebKit::WebFileInfo;
using WebKit::WebFileSystem;
using WebKit::WebFileSystemCallbacks;
using WebKit::WebFileSystemEntry;
using WebKit::WebFileWriter;
using WebKit::WebFileWriterClient;
using WebKit::WebFrame;
using WebKit::WebSecurityOrigin;
using WebKit::WebString;
using WebKit::WebURL;
using WebKit::WebVector;

using webkit_blob::BlobData;
using webkit_blob::BlobStorageController;
using fileapi::FileSystemContext;
using fileapi::FileSystemOperationInterface;
using fileapi::FileSystemTaskRunners;
using fileapi::FileSystemURL;

namespace {
MessageLoop* g_io_thread;
webkit_blob::BlobStorageController* g_blob_storage_controller;

void RegisterBlob(const GURL& blob_url, const FilePath& file_path) {
  DCHECK(g_blob_storage_controller);

  FilePath::StringType extension = file_path.Extension();
  if (!extension.empty())
    extension = extension.substr(1);  // Strip leading ".".

  // This may fail, but then we'll be just setting the empty mime type.
  std::string mime_type;
  net::GetWellKnownMimeTypeFromExtension(extension, &mime_type);

  BlobData::Item item;
  item.SetToFile(file_path, 0, -1, base::Time());
  g_blob_storage_controller->StartBuildingBlob(blob_url);
  g_blob_storage_controller->AppendBlobDataItem(blob_url, item);
  g_blob_storage_controller->FinishBuildingBlob(blob_url, mime_type);
}

}  // namespace

SimpleFileSystem::SimpleFileSystem() {
  if (file_system_dir_.CreateUniqueTempDir()) {
    file_system_context_ = new FileSystemContext(
        FileSystemTaskRunners::CreateMockTaskRunners(),
        NULL /* special storage policy */,
        NULL /* quota manager */,
        file_system_dir_.path(),
        fileapi::CreateAllowFileAccessOptions());
  } else {
    LOG(WARNING) << "Failed to create a temp dir for the filesystem."
                    "FileSystem feature will be disabled.";
  }
}

SimpleFileSystem::~SimpleFileSystem() {
}

void SimpleFileSystem::OpenFileSystem(
    WebFrame* frame, WebFileSystem::Type web_filesystem_type,
    long long, bool create,
    WebFileSystemCallbacks* callbacks) {
  if (!frame || !file_system_context_.get()) {
    // The FileSystem temp directory was not initialized successfully.
    callbacks->didFail(WebKit::WebFileErrorSecurity);
    return;
  }

  fileapi::FileSystemType type;
  if (web_filesystem_type == WebFileSystem::TypeTemporary)
    type = fileapi::kFileSystemTypeTemporary;
  else if (web_filesystem_type == WebFileSystem::TypePersistent)
    type = fileapi::kFileSystemTypePersistent;
  else if (web_filesystem_type == WebFileSystem::TypeExternal)
    type = fileapi::kFileSystemTypeExternal;
  else {
    // Unknown type filesystem is requested.
    callbacks->didFail(WebKit::WebFileErrorSecurity);
    return;
  }

  GURL origin_url(frame->document().securityOrigin().toString());
  file_system_context_->OpenFileSystem(
      origin_url, type, create, OpenFileSystemHandler(callbacks));
}

void SimpleFileSystem::move(
    const WebURL& src_path,
    const WebURL& dest_path, WebFileSystemCallbacks* callbacks) {
  FileSystemURL src_url(src_path);
  FileSystemURL dest_url(dest_path);
  if (!HasFilePermission(src_url, FILE_PERMISSION_WRITE) ||
      !HasFilePermission(dest_url, FILE_PERMISSION_CREATE)) {
    callbacks->didFail(WebKit::WebFileErrorSecurity);
    return;
  }
  GetNewOperation(src_url)->Move(src_url, dest_url,
                                 FinishHandler(callbacks));
}

void SimpleFileSystem::copy(
    const WebURL& src_path, const WebURL& dest_path,
    WebFileSystemCallbacks* callbacks) {
  FileSystemURL src_url(src_path);
  FileSystemURL dest_url(dest_path);
  if (!HasFilePermission(src_url, FILE_PERMISSION_READ) ||
      !HasFilePermission(dest_url, FILE_PERMISSION_CREATE)) {
    callbacks->didFail(WebKit::WebFileErrorSecurity);
    return;
  }
  GetNewOperation(src_url)->Copy(src_url, dest_url,
                                 FinishHandler(callbacks));
}

void SimpleFileSystem::remove(
    const WebURL& path, WebFileSystemCallbacks* callbacks) {
  FileSystemURL url(path);
  if (!HasFilePermission(url, FILE_PERMISSION_WRITE)) {
    callbacks->didFail(WebKit::WebFileErrorSecurity);
    return;
  }
  GetNewOperation(url)->Remove(url, false /* recursive */,
                               FinishHandler(callbacks));
}

void SimpleFileSystem::removeRecursively(
    const WebURL& path, WebFileSystemCallbacks* callbacks) {
  FileSystemURL url(path);
  if (!HasFilePermission(url, FILE_PERMISSION_WRITE)) {
    callbacks->didFail(WebKit::WebFileErrorSecurity);
    return;
  }
  GetNewOperation(url)->Remove(url, true /* recursive */,
                               FinishHandler(callbacks));
}

void SimpleFileSystem::readMetadata(
    const WebURL& path, WebFileSystemCallbacks* callbacks) {
  FileSystemURL url(path);
  if (!HasFilePermission(url, FILE_PERMISSION_READ)) {
    callbacks->didFail(WebKit::WebFileErrorSecurity);
    return;
  }
  GetNewOperation(url)->GetMetadata(url, GetMetadataHandler(callbacks));
}

void SimpleFileSystem::createFile(
    const WebURL& path, bool exclusive, WebFileSystemCallbacks* callbacks) {
  FileSystemURL url(path);
  if (!HasFilePermission(url, FILE_PERMISSION_CREATE)) {
    callbacks->didFail(WebKit::WebFileErrorSecurity);
    return;
  }
  GetNewOperation(url)->CreateFile(url, exclusive, FinishHandler(callbacks));
}

void SimpleFileSystem::createDirectory(
    const WebURL& path, bool exclusive, WebFileSystemCallbacks* callbacks) {
  FileSystemURL url(path);
  if (!HasFilePermission(url, FILE_PERMISSION_CREATE)) {
    callbacks->didFail(WebKit::WebFileErrorSecurity);
    return;
  }
  GetNewOperation(url)->CreateDirectory(url, exclusive, false,
                                        FinishHandler(callbacks));
}

void SimpleFileSystem::fileExists(
    const WebURL& path, WebFileSystemCallbacks* callbacks) {
  FileSystemURL url(path);
  if (!HasFilePermission(url, FILE_PERMISSION_READ)) {
    callbacks->didFail(WebKit::WebFileErrorSecurity);
    return;
  }
  GetNewOperation(url)->FileExists(url, FinishHandler(callbacks));
}

void SimpleFileSystem::directoryExists(
    const WebURL& path, WebFileSystemCallbacks* callbacks) {
  FileSystemURL url(path);
  if (!HasFilePermission(url, FILE_PERMISSION_READ)) {
    callbacks->didFail(WebKit::WebFileErrorSecurity);
    return;
  }
  GetNewOperation(url)->DirectoryExists(url, FinishHandler(callbacks));
}

void SimpleFileSystem::readDirectory(
    const WebURL& path, WebFileSystemCallbacks* callbacks) {
  FileSystemURL url(path);
  if (!HasFilePermission(url, FILE_PERMISSION_READ)) {
    callbacks->didFail(WebKit::WebFileErrorSecurity);
    return;
  }
  GetNewOperation(url)->ReadDirectory(url, ReadDirectoryHandler(callbacks));
}

WebFileWriter* SimpleFileSystem::createFileWriter(
    const WebURL& path, WebFileWriterClient* client) {
  return new SimpleFileWriter(path, client, file_system_context_.get());
}

void SimpleFileSystem::createSnapshotFileAndReadMetadata(
    const WebURL& blobURL,
    const WebURL& path,
    WebFileSystemCallbacks* callbacks) {
  FileSystemURL url(path);
  if (!HasFilePermission(url, FILE_PERMISSION_READ)) {
    callbacks->didFail(WebKit::WebFileErrorSecurity);
    return;
  }
  GetNewOperation(url)->CreateSnapshotFile(
      url, SnapshotFileHandler(blobURL, callbacks));
}

// static
void SimpleFileSystem::InitializeOnIOThread(
    webkit_blob::BlobStorageController* blob_storage_controller) {
  g_io_thread = MessageLoop::current();
  g_blob_storage_controller = blob_storage_controller;
}

// static
void SimpleFileSystem::CleanupOnIOThread() {
  g_io_thread = NULL;
  g_blob_storage_controller = NULL;
}

bool SimpleFileSystem::HasFilePermission(
    const fileapi::FileSystemURL& url, FilePermission permission) {
  // Disallow writing on isolated file system, otherwise return ok.
  return (url.type() != fileapi::kFileSystemTypeIsolated ||
          permission == FILE_PERMISSION_READ);
}

FileSystemOperationInterface* SimpleFileSystem::GetNewOperation(
    const fileapi::FileSystemURL& url) {
  return file_system_context_->CreateFileSystemOperation(url);
}

FileSystemOperationInterface::StatusCallback
SimpleFileSystem::FinishHandler(WebFileSystemCallbacks* callbacks) {
  return base::Bind(&SimpleFileSystem::DidFinish,
                    AsWeakPtr(), base::Unretained(callbacks));
}

FileSystemOperationInterface::ReadDirectoryCallback
SimpleFileSystem::ReadDirectoryHandler(WebFileSystemCallbacks* callbacks) {
  return base::Bind(&SimpleFileSystem::DidReadDirectory,
                    AsWeakPtr(), base::Unretained(callbacks));
}

FileSystemOperationInterface::GetMetadataCallback
SimpleFileSystem::GetMetadataHandler(WebFileSystemCallbacks* callbacks) {
  return base::Bind(&SimpleFileSystem::DidGetMetadata,
                    AsWeakPtr(), base::Unretained(callbacks));
}

FileSystemContext::OpenFileSystemCallback
SimpleFileSystem::OpenFileSystemHandler(WebFileSystemCallbacks* callbacks) {
  return base::Bind(&SimpleFileSystem::DidOpenFileSystem,
                    AsWeakPtr(), base::Unretained(callbacks));
}

FileSystemOperationInterface::SnapshotFileCallback
SimpleFileSystem::SnapshotFileHandler(const GURL& blob_url,
                                      WebFileSystemCallbacks* callbacks) {
  return base::Bind(&SimpleFileSystem::DidCreateSnapshotFile,
                    AsWeakPtr(), blob_url, base::Unretained(callbacks));
}

void SimpleFileSystem::DidFinish(WebFileSystemCallbacks* callbacks,
                                 base::PlatformFileError result) {
  if (result == base::PLATFORM_FILE_OK)
    callbacks->didSucceed();
  else
    callbacks->didFail(fileapi::PlatformFileErrorToWebFileError(result));
}

void SimpleFileSystem::DidGetMetadata(WebFileSystemCallbacks* callbacks,
                                      base::PlatformFileError result,
                                      const base::PlatformFileInfo& info,
                                      const FilePath& platform_path) {
  if (result == base::PLATFORM_FILE_OK) {
    WebFileInfo web_file_info;
    web_file_info.length = info.size;
    web_file_info.modificationTime = info.last_modified.ToDoubleT();
    web_file_info.type = info.is_directory ?
        WebFileInfo::TypeDirectory : WebFileInfo::TypeFile;
    web_file_info.platformPath =
        webkit_glue::FilePathToWebString(platform_path);
    callbacks->didReadMetadata(web_file_info);
  } else {
    callbacks->didFail(fileapi::PlatformFileErrorToWebFileError(result));
  }
}

void SimpleFileSystem::DidReadDirectory(
    WebFileSystemCallbacks* callbacks,
    base::PlatformFileError result,
    const std::vector<base::FileUtilProxy::Entry>& entries,
    bool has_more) {
  if (result == base::PLATFORM_FILE_OK) {
    std::vector<WebFileSystemEntry> web_entries_vector;
    for (std::vector<base::FileUtilProxy::Entry>::const_iterator it =
            entries.begin(); it != entries.end(); ++it) {
      WebFileSystemEntry entry;
      entry.name = webkit_glue::FilePathStringToWebString(it->name);
      entry.isDirectory = it->is_directory;
      web_entries_vector.push_back(entry);
    }
    WebVector<WebKit::WebFileSystemEntry> web_entries = web_entries_vector;
    callbacks->didReadDirectory(web_entries, has_more);
  } else {
    callbacks->didFail(fileapi::PlatformFileErrorToWebFileError(result));
  }
}

void SimpleFileSystem::DidOpenFileSystem(
    WebFileSystemCallbacks* callbacks,
    base::PlatformFileError result,
    const std::string& name, const GURL& root) {
  if (result == base::PLATFORM_FILE_OK) {
    if (!root.is_valid())
      callbacks->didFail(WebKit::WebFileErrorSecurity);
    else
      callbacks->didOpenFileSystem(WebString::fromUTF8(name), root);
  } else {
    callbacks->didFail(fileapi::PlatformFileErrorToWebFileError(result));
  }
}

void SimpleFileSystem::DidCreateSnapshotFile(
    const GURL& blob_url,
    WebFileSystemCallbacks* callbacks,
    base::PlatformFileError result,
    const base::PlatformFileInfo& info,
    const FilePath& platform_path,
    const scoped_refptr<webkit_blob::ShareableFileReference>& file_ref) {
  DCHECK(g_io_thread);
  if (result == base::PLATFORM_FILE_OK) {
    g_io_thread->PostTask(
        FROM_HERE,
        base::Bind(&RegisterBlob, blob_url, platform_path));
  }
  DidGetMetadata(callbacks, result, info, platform_path);
}
