// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBKIT_FILEAPI_FILE_SYSTEM_CONTEXT_H_
#define WEBKIT_FILEAPI_FILE_SYSTEM_CONTEXT_H_

#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "webkit/quota/special_storage_policy.h"

class FilePath;
class GURL;

namespace base {
class MessageLoopProxy;
}

namespace fileapi {

class FileSystemContext;
class FileSystemPathManager;
class FileSystemUsageTracker;
class SandboxMountPointProvider;

struct DefaultContextDeleter;

// This class keeps and provides a file system context for FileSystem API.
class FileSystemContext
    : public base::RefCountedThreadSafe<FileSystemContext,
                                        DefaultContextDeleter> {
 public:
  FileSystemContext(
      scoped_refptr<base::MessageLoopProxy> file_message_loop,
      scoped_refptr<base::MessageLoopProxy> io_message_loop,
      scoped_refptr<quota::SpecialStoragePolicy> special_storage_policy,
      const FilePath& profile_path,
      bool is_incognito,
      bool allow_file_access_from_files,
      bool unlimited_quota,
      FileSystemPathManager* path_manager);
  ~FileSystemContext();

  // This method can be called on any thread.
  bool IsStorageUnlimited(const GURL& origin);

  void DeleteDataForOriginOnFileThread(const GURL& origin_url);

  FileSystemPathManager* path_manager() { return path_manager_.get(); }
  FileSystemUsageTracker* usage_tracker() { return usage_tracker_.get(); }

 private:
  friend struct DefaultContextDeleter;
  void DeleteOnCorrectThread() const;
  SandboxMountPointProvider* sandbox_provider() const;

  scoped_refptr<base::MessageLoopProxy> file_message_loop_;
  scoped_refptr<base::MessageLoopProxy> io_message_loop_;

  scoped_refptr<quota::SpecialStoragePolicy> special_storage_policy_;
  const bool allow_file_access_from_files_;
  const bool unlimited_quota_;

  scoped_ptr<FileSystemPathManager> path_manager_;
  scoped_ptr<FileSystemUsageTracker> usage_tracker_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(FileSystemContext);
};

struct DefaultContextDeleter {
  static void Destruct(const FileSystemContext* context) {
    context->DeleteOnCorrectThread();
  }
};

}  // namespace fileapi

#endif  // WEBKIT_FILEAPI_FILE_SYSTEM_CONTEXT_H_
