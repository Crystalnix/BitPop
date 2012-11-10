// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/fileapi/media/device_media_file_util.h"

#include "base/file_util.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop_proxy.h"
#include "webkit/fileapi/file_system_operation_context.h"
#include "webkit/fileapi/file_system_url.h"
#include "webkit/fileapi/isolated_context.h"
#include "webkit/fileapi/media/filtering_file_enumerator.h"
#include "webkit/fileapi/media/media_device_interface_impl.h"
#include "webkit/fileapi/media/media_device_map_service.h"
#include "webkit/fileapi/media/media_path_filter.h"

using base::PlatformFileError;
using base::PlatformFileInfo;

namespace fileapi {

namespace {

const FilePath::CharType kDeviceMediaFileUtilTempDir[] =
    FILE_PATH_LITERAL("DeviceMediaFileSystem");

}  // namespace

DeviceMediaFileUtil::DeviceMediaFileUtil(const FilePath& profile_path)
    : profile_path_(profile_path) {
}

PlatformFileError DeviceMediaFileUtil::CreateOrOpen(
    FileSystemOperationContext* context,
    const FileSystemURL& url, int file_flags,
    PlatformFile* file_handle, bool* created) {
  return base::PLATFORM_FILE_ERROR_SECURITY;
}

PlatformFileError DeviceMediaFileUtil::Close(
    FileSystemOperationContext* context,
    PlatformFile file_handle) {
  // We don't allow open thus Close won't be called.
  return base::PLATFORM_FILE_ERROR_SECURITY;
}

PlatformFileError DeviceMediaFileUtil::EnsureFileExists(
    FileSystemOperationContext* context,
    const FileSystemURL& url,
    bool* created) {
  return base::PLATFORM_FILE_ERROR_SECURITY;
}

PlatformFileError DeviceMediaFileUtil::CreateDirectory(
    FileSystemOperationContext* context,
    const FileSystemURL& url,
    bool exclusive,
    bool recursive) {
  return base::PLATFORM_FILE_ERROR_SECURITY;
}

PlatformFileError DeviceMediaFileUtil::GetFileInfo(
    FileSystemOperationContext* context,
    const FileSystemURL& url,
    PlatformFileInfo* file_info,
    FilePath* platform_path) {
  if (!context->media_device())
    return base::PLATFORM_FILE_ERROR_NOT_FOUND;
  PlatformFileError error =
      context->media_device()->GetFileInfo(url.path(), file_info);
  if (error != base::PLATFORM_FILE_OK)
    return error;

  if (file_info->is_directory ||
      context->media_path_filter()->Match(url.path()))
    return base::PLATFORM_FILE_OK;
  return base::PLATFORM_FILE_ERROR_NOT_FOUND;
}

FileSystemFileUtil::AbstractFileEnumerator*
DeviceMediaFileUtil::CreateFileEnumerator(
    FileSystemOperationContext* context,
    const FileSystemURL& url,
    bool recursive) {
  if (!context->media_device())
    return new FileSystemFileUtil::EmptyFileEnumerator();
  return new FilteringFileEnumerator(
      make_scoped_ptr(
          context->media_device()->CreateFileEnumerator(url.path(), recursive)),
      context->media_path_filter());
}

PlatformFileError DeviceMediaFileUtil::GetLocalFilePath(
    FileSystemOperationContext* context,
    const FileSystemURL& file_system_url,
    FilePath* local_file_path) {
  return base::PLATFORM_FILE_ERROR_SECURITY;
}

PlatformFileError DeviceMediaFileUtil::Touch(
    FileSystemOperationContext* context,
    const FileSystemURL& url,
    const base::Time& last_access_time,
    const base::Time& last_modified_time) {
  if (!context->media_device())
    return base::PLATFORM_FILE_ERROR_NOT_FOUND;
  return context->media_device()->Touch(url.path(), last_access_time,
                                        last_modified_time);
}

PlatformFileError DeviceMediaFileUtil::Truncate(
    FileSystemOperationContext* context,
    const FileSystemURL& url,
    int64 length) {
  return base::PLATFORM_FILE_ERROR_SECURITY;
}

bool DeviceMediaFileUtil::PathExists(
    FileSystemOperationContext* context,
    const FileSystemURL& url) {
  if (!context->media_device())
    return false;

  FilePath path;
  PlatformFileInfo file_info;
  PlatformFileError error = GetFileInfo(context, url, &file_info, &path);
  return error == base::PLATFORM_FILE_OK;
}

bool DeviceMediaFileUtil::DirectoryExists(
    FileSystemOperationContext* context,
    const FileSystemURL& url) {
  if (!context->media_device())
    return false;
  return context->media_device()->DirectoryExists(url.path());
}

bool DeviceMediaFileUtil::IsDirectoryEmpty(
    FileSystemOperationContext* context,
    const FileSystemURL& url) {
  if (!context->media_device())
    return false;

  scoped_ptr<AbstractFileEnumerator> enumerator(
      CreateFileEnumerator(context, url, false));
  FilePath path;
  while (!(path = enumerator->Next()).empty()) {
    if (enumerator->IsDirectory() ||
        context->media_path_filter()->Match(path))
      return false;
  }
  return true;
}

PlatformFileError DeviceMediaFileUtil::CopyOrMoveFile(
    FileSystemOperationContext* context,
    const FileSystemURL& src_url,
    const FileSystemURL& dest_url,
    bool copy) {
  return base::PLATFORM_FILE_ERROR_SECURITY;
}

PlatformFileError DeviceMediaFileUtil::CopyInForeignFile(
    FileSystemOperationContext* context,
    const FilePath& src_file_path,
    const FileSystemURL& dest_url) {
  return base::PLATFORM_FILE_ERROR_SECURITY;
}

PlatformFileError DeviceMediaFileUtil::DeleteFile(
    FileSystemOperationContext* context,
    const FileSystemURL& url) {
  return base::PLATFORM_FILE_ERROR_SECURITY;
}

PlatformFileError DeviceMediaFileUtil::DeleteSingleDirectory(
    FileSystemOperationContext* context,
    const FileSystemURL& url) {
  return base::PLATFORM_FILE_ERROR_SECURITY;
}

base::PlatformFileError DeviceMediaFileUtil::CreateSnapshotFile(
    FileSystemOperationContext* context,
    const FileSystemURL& url,
    base::PlatformFileInfo* file_info,
    FilePath* local_path,
    SnapshotFilePolicy* snapshot_policy) {
  DCHECK(file_info);
  DCHECK(local_path);
  DCHECK(snapshot_policy);

  if (!context->media_device())
    return base::PLATFORM_FILE_ERROR_NOT_FOUND;

  // We return a temporary file as a snapshot.
  *snapshot_policy = FileSystemFileUtil::kSnapshotFileTemporary;

  // Create a temp file in "profile_path_/kDeviceMediaFileUtilTempDir".
  FilePath isolated_media_file_system_dir_path =
      profile_path_.Append(kDeviceMediaFileUtilTempDir);
  bool dir_exists = file_util::DirectoryExists(
      isolated_media_file_system_dir_path);
  if (!dir_exists &&
      !file_util::CreateDirectory(isolated_media_file_system_dir_path)) {
    LOG(WARNING) << "Could not create a directory for media snapshot file "
                 << isolated_media_file_system_dir_path.value();
    return base::PLATFORM_FILE_ERROR_FAILED;
  }

  bool file_created = file_util::CreateTemporaryFileInDir(
      isolated_media_file_system_dir_path, local_path);
  if (!file_created) {
    LOG(WARNING) << "Could not create a temporary file for media snapshot in "
                 << isolated_media_file_system_dir_path.value();
    return base::PLATFORM_FILE_ERROR_FAILED;
  }

  return context->media_device()->CreateSnapshotFile(
      url.path(), *local_path, file_info);
}

}  // namespace fileapi
