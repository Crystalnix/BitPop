// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBKIT_FILEAPI_FILE_SYSTEM_OPERATION_H_
#define WEBKIT_FILEAPI_FILE_SYSTEM_OPERATION_H_

#include <vector>

#include "base/file_path.h"
#include "base/file_util_proxy.h"
#include "base/gtest_prod_util.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_callback_factory.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop_proxy.h"
#include "base/platform_file.h"
#include "base/process.h"
#include "googleurl/src/gurl.h"
#include "webkit/fileapi/file_system_types.h"
#include "webkit/fileapi/file_system_operation_context.h"

namespace base {
class Time;
}

namespace net {
class URLRequest;
class URLRequestContext;
}  // namespace net

class GURL;

namespace fileapi {

class FileSystemCallbackDispatcher;
class FileSystemContext;
class FileWriterDelegate;
class FileSystemOperationTest;

// This class is designed to serve one-time file system operation per instance.
// Only one method(CreateFile, CreateDirectory, Copy, Move, DirectoryExists,
// GetMetadata, ReadDirectory and Remove) may be called during the lifetime of
// this object and it should be called no more than once.
// This class is self-destructed and an instance automatically gets deleted
// when its operation is finished.
class FileSystemOperation {
 public:
  // |dispatcher| will be owned by this class.
  // |file_system_file_util| is optional; if supplied, it will not be deleted by
  // the class.  It's expecting a pointer to a singleton.
  FileSystemOperation(FileSystemCallbackDispatcher* dispatcher,
                      scoped_refptr<base::MessageLoopProxy> proxy,
                      FileSystemContext* file_system_context,
                      FileSystemFileUtil* file_system_file_util);
  virtual ~FileSystemOperation();

  void OpenFileSystem(const GURL& origin_url,
                      fileapi::FileSystemType type,
                      bool create);
  void CreateFile(const GURL& path,
                  bool exclusive);
  void CreateDirectory(const GURL& path,
                       bool exclusive,
                       bool recursive);
  void Copy(const GURL& src_path,
            const GURL& dest_path);
  void Move(const GURL& src_path,
            const GURL& dest_path);
  void DirectoryExists(const GURL& path);
  void FileExists(const GURL& path);
  void GetMetadata(const GURL& path);
  void ReadDirectory(const GURL& path);
  void Remove(const GURL& path, bool recursive);
  void Write(scoped_refptr<net::URLRequestContext> url_request_context,
             const GURL& path,
             const GURL& blob_url,
             int64 offset);
  void Truncate(const GURL& path, int64 length);
  void TouchFile(const GURL& path,
                 const base::Time& last_access_time,
                 const base::Time& last_modified_time);
  void OpenFile(
      const GURL& path,
      int file_flags,
      base::ProcessHandle peer_handle);
  void GetLocalPath(const GURL& path);

  // Try to cancel the current operation [we support cancelling write or
  // truncate only].  Report failure for the current operation, then tell the
  // passed-in operation to report success.
  void Cancel(FileSystemOperation* cancel_operation);

 private:
  FileSystemContext* file_system_context() const {
    return file_system_operation_context_.file_system_context();
  }

  FileSystemOperationContext* file_system_operation_context() {
    return &file_system_operation_context_;
  }
  friend class FileSystemOperationTest;
  friend class FileSystemOperationWriteTest;

  // A callback used for OpenFileSystem.
  void DidGetRootPath(bool success,
                      const FilePath& path,
                      const std::string& name);

  // Callback for CreateFile for |exclusive|=true cases.
  void DidEnsureFileExistsExclusive(base::PlatformFileError rv,
                                    bool created);

  // Callback for CreateFile for |exclusive|=false cases.
  void DidEnsureFileExistsNonExclusive(base::PlatformFileError rv,
                                       bool created);

  // Generic callback that translates platform errors to WebKit error codes.
  void DidFinishFileOperation(base::PlatformFileError rv);

  void DidDirectoryExists(base::PlatformFileError rv,
                          const base::PlatformFileInfo& file_info,
                          const FilePath& unused);
  void DidFileExists(base::PlatformFileError rv,
                     const base::PlatformFileInfo& file_info,
                     const FilePath& unused);
  void DidGetMetadata(base::PlatformFileError rv,
                      const base::PlatformFileInfo& file_info,
                      const FilePath& platform_path);
  void DidReadDirectory(
      base::PlatformFileError rv,
      const std::vector<base::FileUtilProxy::Entry>& entries);
  void DidWrite(
      base::PlatformFileError rv,
      int64 bytes,
      bool complete);
  void DidTouchFile(base::PlatformFileError rv);
  void DidOpenFile(
      base::PlatformFileError rv,
      base::PassPlatformFile file,
      bool created);
  void DidGetLocalPath(base::PlatformFileError rv,
                       const FilePath& local_path);

  // Helper for Write().
  void OnFileOpenedForWrite(
      base::PlatformFileError rv,
      base::PassPlatformFile file,
      bool created);

  // Checks the validity of a given |path| for reading, and cracks the path into
  // root URL and virtual path components.
  // Returns true if the given |path| is a valid FileSystem path.
  // Otherwise it calls dispatcher's DidFail method with
  // PLATFORM_FILE_ERROR_SECURITY and returns false.
  // (Note: this doesn't delete this when it calls DidFail and returns false;
  // it's the caller's responsibility.)
  bool VerifyFileSystemPathForRead(const GURL& path,
                                   GURL* root_url,
                                   FileSystemType* type,
                                   FilePath* virtual_path);

  // Checks the validity of a given |path| for writing, and cracks the path into
  // root URL and virtual path components.
  // Returns true if the given |path| is a valid FileSystem path, and
  // its origin embedded in the path has the right to write.
  // Otherwise it fires dispatcher's DidFail method with
  // PLATFORM_FILE_ERROR_SECURITY if the path is not valid for writing,
  // or with PLATFORM_FILE_ERROR_NO_SPACE if the origin is not allowed to
  // write to the storage.
  // In either case it returns false after firing DidFail.
  // If |create| flag is true this also checks if the |path| contains
  // any restricted names and chars. If it does, the call fires dispatcher's
  // DidFail with PLATFORM_FILE_ERROR_SECURITY and returns false.
  // (Note: this doesn't delete this when it calls DidFail and returns false;
  // it's the caller's responsibility.)
  bool VerifyFileSystemPathForWrite(const GURL& path,
                                    bool create,
                                    GURL* root_url,
                                    FileSystemType* type,
                                    FilePath* virtual_path);

#ifndef NDEBUG
  enum OperationType {
    kOperationNone,
    kOperationOpenFileSystem,
    kOperationCreateFile,
    kOperationCreateDirectory,
    kOperationCopy,
    kOperationMove,
    kOperationDirectoryExists,
    kOperationFileExists,
    kOperationGetMetadata,
    kOperationReadDirectory,
    kOperationRemove,
    kOperationWrite,
    kOperationTruncate,
    kOperationTouchFile,
    kOperationOpenFile,
    kOperationGetLocalPath,
    kOperationCancel,
  };

  // A flag to make sure we call operation only once per instance.
  OperationType pending_operation_;
#endif

  // Proxy for calling file_util_proxy methods.
  scoped_refptr<base::MessageLoopProxy> proxy_;

  scoped_ptr<FileSystemCallbackDispatcher> dispatcher_;

  FileSystemOperationContext file_system_operation_context_;

  base::ScopedCallbackFactory<FileSystemOperation> callback_factory_;

  // These are all used only by Write().
  friend class FileWriterDelegate;
  scoped_ptr<FileWriterDelegate> file_writer_delegate_;
  scoped_ptr<net::URLRequest> blob_request_;
  scoped_ptr<FileSystemOperation> cancel_operation_;

  // Used only by OpenFile, in order to clone the file handle back to the
  // requesting process.
  base::ProcessHandle peer_handle_;

  DISALLOW_COPY_AND_ASSIGN(FileSystemOperation);
};

}  // namespace fileapi

#endif  // WEBKIT_FILEAPI_FILE_SYSTEM_OPERATION_H_
