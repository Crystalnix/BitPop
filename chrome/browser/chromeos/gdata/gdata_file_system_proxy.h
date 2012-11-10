// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_GDATA_GDATA_FILE_SYSTEM_PROXY_H_
#define CHROME_BROWSER_CHROMEOS_GDATA_GDATA_FILE_SYSTEM_PROXY_H_

#include "chrome/browser/chromeos/gdata/gdata_errorcode.h"
#include "chrome/browser/chromeos/gdata/gdata_file_system_interface.h"
#include "webkit/chromeos/fileapi/remote_file_system_proxy.h"

namespace fileapi {
class FileSystemURL;
}

namespace gdata {

class GDataEntryProto;
class GDataFileSystemInterface;

// Implementation of File API's remote file system proxy for GData file system.
class GDataFileSystemProxy : public fileapi::RemoteFileSystemProxyInterface {
 public:
  // |file_system| is the GDataFileSystem instance owned by GDataSystemService.
  explicit GDataFileSystemProxy(GDataFileSystemInterface* file_system);

  // fileapi::RemoteFileSystemProxyInterface overrides.
  virtual void GetFileInfo(
      const fileapi::FileSystemURL& url,
      const fileapi::FileSystemOperationInterface::GetMetadataCallback&
          callback) OVERRIDE;
  virtual void Copy(
      const fileapi::FileSystemURL& src_url,
      const fileapi::FileSystemURL& dest_url,
      const fileapi::FileSystemOperationInterface::StatusCallback& callback)
          OVERRIDE;
  virtual void Move(
      const fileapi::FileSystemURL& src_url,
      const fileapi::FileSystemURL& dest_url,
      const fileapi::FileSystemOperationInterface::StatusCallback& callback)
          OVERRIDE;
  virtual void ReadDirectory(const fileapi::FileSystemURL& url,
     const fileapi::FileSystemOperationInterface::ReadDirectoryCallback&
         callback) OVERRIDE;
  virtual void Remove(
      const fileapi::FileSystemURL& url, bool recursive,
      const fileapi::FileSystemOperationInterface::StatusCallback& callback)
          OVERRIDE;
  virtual void CreateDirectory(
      const fileapi::FileSystemURL& file_url,
      bool exclusive,
      bool recursive,
      const fileapi::FileSystemOperationInterface::StatusCallback& callback)
          OVERRIDE;
  virtual void CreateFile(
      const fileapi::FileSystemURL& file_url,
      bool exclusive,
      const fileapi::FileSystemOperationInterface::StatusCallback& callback)
          OVERRIDE;
  virtual void Truncate(
      const fileapi::FileSystemURL& file_url, int64 length,
      const fileapi::FileSystemOperationInterface::StatusCallback& callback)
          OVERRIDE;
  virtual void CreateSnapshotFile(
      const fileapi::FileSystemURL& url,
      const fileapi::FileSystemOperationInterface::SnapshotFileCallback&
      callback) OVERRIDE;
  virtual void CreateWritableSnapshotFile(
      const fileapi::FileSystemURL& url,
      const fileapi::WritableSnapshotFile& callback) OVERRIDE;
  virtual void OpenFile(
      const fileapi::FileSystemURL& url,
      int file_flags,
      base::ProcessHandle peer_handle,
      const fileapi::FileSystemOperationInterface::OpenFileCallback&
          callback) OVERRIDE;
  virtual void NotifyCloseFile(const fileapi::FileSystemURL& url) OVERRIDE;
  // TODO(zelidrag): More methods to follow as we implement other parts of FSO.

 protected:
  virtual ~GDataFileSystemProxy();

 private:
  // Checks if a given |url| belongs to this file system. If it does,
  // the call will return true and fill in |file_path| with a file path of
  // a corresponding element within this file system.
  static bool ValidateUrl(const fileapi::FileSystemURL& url,
                          FilePath* file_path);

  // Helper callback for relaying reply for status callbacks to the
  // calling thread.
  void OnStatusCallback(
      const fileapi::FileSystemOperationInterface::StatusCallback& callback,
      gdata::GDataFileError error);

  // Helper callback for relaying reply for metadata retrieval request to the
  // calling thread.
  void OnGetMetadata(
      const FilePath& file_path,
      const fileapi::FileSystemOperationInterface::GetMetadataCallback&
          callback,
      GDataFileError error,
      scoped_ptr<gdata::GDataEntryProto> entry_proto);

  // Helper callback for relaying reply for GetEntryInfoByPath() to the
  // calling thread.
  void OnGetEntryInfoByPath(
      const FilePath& entry_path,
      const fileapi::FileSystemOperationInterface::SnapshotFileCallback&
          callback,
      GDataFileError error,
      scoped_ptr<GDataEntryProto> entry_proto);

  // Helper callback for relaying reply for ReadDirectory() to the calling
  // thread.
  void OnReadDirectory(
      const fileapi::FileSystemOperationInterface::ReadDirectoryCallback&
          callback,
      GDataFileError error,
      bool hide_hosted_documents,
      scoped_ptr<gdata::GDataEntryProtoVector> proto_entries);

  // Helper callback for relaying reply for CreateWritableSnapshotFile() to
  // the calling thread.
  void OnCreateWritableSnapshotFile(
      const FilePath& virtual_path,
      const fileapi::WritableSnapshotFile& callback,
      GDataFileError result,
      const FilePath& local_path);

  // Helper callback for closing the local cache file and committing the dirty
  // flag. This is triggered when the callback for CreateWritableSnapshotFile
  // released the refcounted reference to the file.
  void CloseWritableSnapshotFile(
      const FilePath& virtual_path,
      const FilePath& local_path);

  // Invoked during Truncate() operation. This is called when a local modifiable
  // cache is ready for truncation.
  void OnFileOpenedForTruncate(
      const FilePath& virtual_path,
      int64 length,
      const fileapi::FileSystemOperationInterface::StatusCallback& callback,
      GDataFileError open_result,
      const FilePath& local_cache_path);

  // Invoked during Truncate() operation. This is called when the truncation of
  // a local cache file is finished on FILE thread.
  void DidTruncate(
      const FilePath& virtual_path,
      const fileapi::FileSystemOperationInterface::StatusCallback& callback,
      base::PlatformFileError* truncate_result);

  // Invoked during OpenFile() operation when truncate or write flags are set.
  // This is called when a local modifiable cached file is ready for such
  // operation.
  void OnOpenFileForWriting(
      int file_flags,
      base::ProcessHandle peer_handle,
      const fileapi::FileSystemOperationInterface::OpenFileCallback& callback,
      GDataFileError gdata_error,
      const FilePath& local_cache_path);

  // Invoked during OpenFile() operation when file create flags are set.
  void OnCreateFileForOpen(
      const FilePath& file_path,
      int file_flags,
      base::ProcessHandle peer_handle,
      const fileapi::FileSystemOperationInterface::OpenFileCallback& callback,
      GDataFileError gdata_error);

  // Invoked during OpenFile() operation when base::PLATFORM_FILE_OPEN_TRUNCATED
  // flag is set. This is called when the truncation of a local cache file is
  // finished on FILE thread.
  void OnOpenAndTruncate(
      base::ProcessHandle peer_handle,
      const fileapi::FileSystemOperationInterface::OpenFileCallback& callback,
      base::PlatformFile* platform_file,
      base::PlatformFileError* truncate_result);

  // GDataFileSystem is owned by Profile, which outlives GDataFileSystemProxy,
  // which is owned by CrosMountPointProvider (i.e. by the time Profile is
  // removed, the file manager is already gone). Hence it's safe to use this as
  // a raw pointer.
  GDataFileSystemInterface* file_system_;
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_GDATA_GDATA_FILE_SYSTEM_PROXY_H_
