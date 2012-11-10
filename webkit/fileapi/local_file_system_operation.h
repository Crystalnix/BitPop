// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBKIT_FILEAPI_LOCAL_FILE_SYSTEM_OPERATION_H_
#define WEBKIT_FILEAPI_LOCAL_FILE_SYSTEM_OPERATION_H_

#include <string>
#include <vector>

#include "base/file_path.h"
#include "base/file_util_proxy.h"
#include "base/gtest_prod_util.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/platform_file.h"
#include "base/process.h"
#include "webkit/fileapi/file_system_operation_context.h"
#include "webkit/fileapi/file_system_operation_interface.h"
#include "webkit/fileapi/file_system_types.h"
#include "webkit/fileapi/file_system_url.h"
#include "webkit/fileapi/fileapi_export.h"
#include "webkit/quota/quota_manager.h"

namespace base {
class Time;
}

namespace chromeos {
class CrosMountPointProvider;
}

namespace net {
class URLRequest;
class URLRequestContext;
}  // namespace net

namespace fileapi {

class FileSystemContext;
class FileSystemOperationTest;
class FileSystemURL;
class FileWriterDelegate;

// FileSystemOperation implementation for local file systems.
class FILEAPI_EXPORT LocalFileSystemOperation
    : public NON_EXPORTED_BASE(FileSystemOperationInterface) {
 public:
  virtual ~LocalFileSystemOperation();

  // FileSystemOperation overrides.
  virtual void CreateFile(const FileSystemURL& url,
                          bool exclusive,
                          const StatusCallback& callback) OVERRIDE;
  virtual void CreateDirectory(const FileSystemURL& url,
                               bool exclusive,
                               bool recursive,
                               const StatusCallback& callback) OVERRIDE;
  virtual void Copy(const FileSystemURL& src_url,
                    const FileSystemURL& dest_url,
                    const StatusCallback& callback) OVERRIDE;
  virtual void Move(const FileSystemURL& src_url,
                    const FileSystemURL& dest_url,
                    const StatusCallback& callback) OVERRIDE;
  virtual void DirectoryExists(const FileSystemURL& url,
                               const StatusCallback& callback) OVERRIDE;
  virtual void FileExists(const FileSystemURL& url,
                          const StatusCallback& callback) OVERRIDE;
  virtual void GetMetadata(const FileSystemURL& url,
                           const GetMetadataCallback& callback) OVERRIDE;
  virtual void ReadDirectory(const FileSystemURL& url,
                             const ReadDirectoryCallback& callback) OVERRIDE;
  virtual void Remove(const FileSystemURL& url, bool recursive,
                      const StatusCallback& callback) OVERRIDE;
  virtual void Write(const net::URLRequestContext* url_request_context,
                     const FileSystemURL& url,
                     const GURL& blob_url,
                     int64 offset,
                     const WriteCallback& callback) OVERRIDE;
  virtual void Truncate(const FileSystemURL& url, int64 length,
                        const StatusCallback& callback) OVERRIDE;
  virtual void TouchFile(const FileSystemURL& url,
                         const base::Time& last_access_time,
                         const base::Time& last_modified_time,
                         const StatusCallback& callback) OVERRIDE;
  virtual void OpenFile(const FileSystemURL& url,
                        int file_flags,
                        base::ProcessHandle peer_handle,
                        const OpenFileCallback& callback) OVERRIDE;
  virtual void NotifyCloseFile(const FileSystemURL& url) OVERRIDE;
  virtual void Cancel(const StatusCallback& cancel_callback) OVERRIDE;
  virtual LocalFileSystemOperation* AsLocalFileSystemOperation() OVERRIDE;
  virtual void CreateSnapshotFile(
      const FileSystemURL& path,
      const SnapshotFileCallback& callback) OVERRIDE;

  // Synchronously gets the platform path for the given |url|.
  void SyncGetPlatformPath(const FileSystemURL& url, FilePath* platform_path);

 private:
  class ScopedQuotaNotifier;

  enum SetUpMode {
    SETUP_FOR_READ,
    SETUP_FOR_WRITE,
    SETUP_FOR_CREATE,
  };

  // A struct to pass arguments to DidGetUsageAndQuotaAndRunTask
  // purely for compilation (as Bind doesn't recognize too many arguments).
  struct TaskParamsForDidGetQuota {
    TaskParamsForDidGetQuota();
    ~TaskParamsForDidGetQuota();
    FileSystemURL url;
    base::Closure task;
    base::Closure error_callback;
  };

  // Only MountPointProviders or testing class can create a
  // new operation directly.
  friend class FileSystemTestHelper;
  friend class IsolatedMountPointProvider;
  friend class SandboxMountPointProvider;
  friend class TestMountPointProvider;
  friend class chromeos::CrosMountPointProvider;

  friend class FileSystemOperationTest;
  friend class FileSystemOperationWriteTest;
  friend class FileWriterDelegateTest;
  friend class FileSystemQuotaTest;
  friend class LocalFileSystemTestOriginHelper;

  LocalFileSystemOperation(
      FileSystemContext* file_system_context,
      scoped_ptr<FileSystemOperationContext> operation_context);

  FileSystemContext* file_system_context() const {
    return operation_context_->file_system_context();
  }

  // The unit tests that need to specify and control the lifetime of the
  // file_util on their own should call this before performing the actual
  // operation. If it is given it will not be overwritten by the class.
  void set_override_file_util(FileSystemFileUtil* file_util) {
    src_util_ = file_util;
    dest_util_ = file_util;
  }

  // Queries the quota and usage and then runs the given |task|.
  // If an error occurs during the quota query it runs |error_callback| instead.
  void GetUsageAndQuotaThenRunTask(
      const FileSystemURL& url,
      const base::Closure& task,
      const base::Closure& error_callback);

  // Called after the quota info is obtained from the quota manager
  // (which is triggered by GetUsageAndQuotaThenRunTask).
  // Sets the quota info in the operation_context_ and then runs the given
  // |task| if the returned quota status is successful, otherwise runs
  // |error_callback|.
  void DidGetUsageAndQuotaAndRunTask(
      const TaskParamsForDidGetQuota& params,
      quota::QuotaStatusCode status,
      int64 usage, int64 quota);

  // The 'body' methods that perform the actual work (i.e. posting the
  // file task on proxy_) after the quota check.
  void DoCreateFile(const FileSystemURL& url,
                    const StatusCallback& callback, bool exclusive);
  void DoCreateDirectory(const FileSystemURL& url,
                         const StatusCallback& callback,
                         bool exclusive,
                         bool recursive);
  void DoCopy(const FileSystemURL& src,
              const FileSystemURL& dest,
              const StatusCallback& callback);
  void DoMove(const FileSystemURL& src,
              const FileSystemURL& dest,
              const StatusCallback& callback);
  void DoTruncate(const FileSystemURL& url,
                  const StatusCallback& callback, int64 length);
  void DoOpenFile(const FileSystemURL& url,
                  const OpenFileCallback& callback, int file_flags);

  // Callback for CreateFile for |exclusive|=true cases.
  void DidEnsureFileExistsExclusive(const StatusCallback& callback,
                                    base::PlatformFileError rv,
                                    bool created);

  // Callback for CreateFile for |exclusive|=false cases.
  void DidEnsureFileExistsNonExclusive(const StatusCallback& callback,
                                       base::PlatformFileError rv,
                                       bool created);

  // Generic callback that translates platform errors to WebKit error codes.
  void DidFinishFileOperation(const StatusCallback& callback,
                              base::PlatformFileError rv);

  void DidDirectoryExists(const StatusCallback& callback,
                          base::PlatformFileError rv,
                          const base::PlatformFileInfo& file_info,
                          const FilePath& unused);
  void DidFileExists(const StatusCallback& callback,
                     base::PlatformFileError rv,
                     const base::PlatformFileInfo& file_info,
                     const FilePath& unused);
  void DidGetMetadata(const GetMetadataCallback& callback,
                      base::PlatformFileError rv,
                      const base::PlatformFileInfo& file_info,
                      const FilePath& platform_path);
  void DidReadDirectory(const ReadDirectoryCallback& callback,
                        base::PlatformFileError rv,
                        const std::vector<base::FileUtilProxy::Entry>& entries,
                        bool has_more);
  void DidWrite(base::PlatformFileError rv,
                int64 bytes,
                bool complete);
  void DidTouchFile(const StatusCallback& callback,
                    base::PlatformFileError rv);
  void DidOpenFile(const OpenFileCallback& callback,
                   base::PlatformFileError rv,
                   base::PassPlatformFile file,
                   bool created);
  void DidCreateSnapshotFile(
      const SnapshotFileCallback& callback,
      base::PlatformFileError rv,
      const base::PlatformFileInfo& file_info,
      const FilePath& platform_path,
      FileSystemFileUtil::SnapshotFilePolicy snapshot_policy);

  // Checks the validity of a given |url| and populates |file_util| for |mode|.
  base::PlatformFileError SetUp(
      const FileSystemURL& url,
      FileSystemFileUtil** file_util,
      SetUpMode mode);

  // Used only for internal assertions.
  // Returns false if there's another inflight pending operation.
  bool SetPendingOperationType(OperationType type);

  scoped_ptr<FileSystemOperationContext> operation_context_;
  FileSystemFileUtil* src_util_;  // Not owned.
  FileSystemFileUtil* dest_util_;  // Not owned.

  // This is set before any write operations.  The destructor of
  // ScopedQuotaNotifier sends notification to the QuotaManager
  // to tell the update is done; so that we can make sure notify
  // the manager after any write operations are done.
  scoped_ptr<ScopedQuotaNotifier> scoped_quota_notifier_;

  // These are all used only by Write().
  friend class FileWriterDelegate;
  scoped_ptr<FileWriterDelegate> file_writer_delegate_;

  // write_callback is kept in this class for so that we can dispatch it when
  // the operation is cancelled. calcel_callback is kept for canceling a
  // Truncate() operation. We can't actually stop Truncate in another thread;
  // after it resumed from the working thread, cancellation takes place.
  WriteCallback write_callback_;
  StatusCallback cancel_callback_;
  void set_write_callback(const WriteCallback& write_callback) {
    write_callback_ = write_callback;
  }

  // Used only by OpenFile, in order to clone the file handle back to the
  // requesting process.
  base::ProcessHandle peer_handle_;

  // A flag to make sure we call operation only once per instance.
  OperationType pending_operation_;

  // LocalFileSystemOperation instance is usually deleted upon completion but
  // could be deleted while it has inflight callbacks when Cancel is called.
  base::WeakPtrFactory<LocalFileSystemOperation> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(LocalFileSystemOperation);
};

}  // namespace fileapi

#endif  // WEBKIT_FILEAPI_LOCAL_FILE_SYSTEM_OPERATION_H_
