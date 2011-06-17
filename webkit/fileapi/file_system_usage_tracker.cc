// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/fileapi/file_system_usage_tracker.h"

#include <algorithm>

#include "base/file_path.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop_proxy.h"
#include "base/task.h"
#include "googleurl/src/gurl.h"
#include "webkit/fileapi/file_system_path_manager.h"
#include "webkit/fileapi/file_system_usage_cache.h"
#include "webkit/fileapi/sandbox_mount_point_provider.h"

namespace fileapi {

class FileSystemUsageTracker::GetUsageTask
    : public base::RefCountedThreadSafe<GetUsageTask> {
 public:
  GetUsageTask(
      FileSystemUsageTracker* tracker,
      scoped_refptr<base::MessageLoopProxy> file_message_loop,
      std::string fs_identifier,
      const FilePath& origin_base_path)
      : tracker_(tracker),
        file_message_loop_(file_message_loop),
        original_message_loop_(
            base::MessageLoopProxy::CreateForCurrentThread()),
        fs_identifier_(fs_identifier),
        fs_usage_(0),
        origin_base_path_(origin_base_path) {
  }

  virtual ~GetUsageTask() {}

  void Start() {
    DCHECK(tracker_);
    tracker_->RegisterUsageTask(this);
    file_message_loop_->PostTask(
        FROM_HERE, NewRunnableMethod(this, &GetUsageTask::RunOnFileThread));
  }

  void Cancel() {
    DCHECK(original_message_loop_->BelongsToCurrentThread());
    tracker_ = NULL;
  }

 private:
  void RunOnFileThread() {
    DCHECK(file_message_loop_->BelongsToCurrentThread());

    if (!file_util::DirectoryExists(origin_base_path_))
      fs_usage_ = 0;
    else {
      FilePath usage_file_path = origin_base_path_.AppendASCII(
          FileSystemUsageCache::kUsageFileName);
      fs_usage_ = FileSystemUsageCache::GetUsage(usage_file_path);

      if (fs_usage_ < 0) {
        FilePath content_file_path = origin_base_path_;
        if (FileSystemUsageCache::Exists(usage_file_path))
          FileSystemUsageCache::Delete(usage_file_path);
        fs_usage_ = file_util::ComputeDirectorySize(content_file_path);
        // fs_usage_ will include the size of .usage.
        // The result of ComputeDirectorySize does not include it.
        fs_usage_ += FileSystemUsageCache::kUsageFileSize;
        FileSystemUsageCache::UpdateUsage(usage_file_path, fs_usage_);
      }
    }

    original_message_loop_->PostTask(
        FROM_HERE, NewRunnableMethod(this, &GetUsageTask::Completed));
  }

  void Completed() {
    DCHECK(original_message_loop_->BelongsToCurrentThread());
    if (tracker_) {
      tracker_->UnregisterUsageTask(this);
      tracker_->DidGetOriginUsage(fs_identifier_, fs_usage_);
    }
  }

  FileSystemUsageTracker* tracker_;
  scoped_refptr<base::MessageLoopProxy> file_message_loop_;
  scoped_refptr<base::MessageLoopProxy> original_message_loop_;
  std::string fs_identifier_;
  int64 fs_usage_;
  FilePath origin_base_path_;
};

FileSystemUsageTracker::FileSystemUsageTracker(
    scoped_refptr<base::MessageLoopProxy> file_message_loop,
    const FilePath& profile_path,
    bool is_incognito)
    : file_message_loop_(file_message_loop),
      base_path_(profile_path.Append(
          SandboxMountPointProvider::kFileSystemDirectory)),
      is_incognito_(is_incognito) {
  DCHECK(file_message_loop);
}

FileSystemUsageTracker::~FileSystemUsageTracker() {
  std::for_each(running_usage_tasks_.begin(),
                running_usage_tasks_.end(),
                std::mem_fun(&GetUsageTask::Cancel));
}

void FileSystemUsageTracker::GetOriginUsage(
    const GURL& origin_url,
    fileapi::FileSystemType type,
    GetUsageCallback* callback_ptr) {
  DCHECK(callback_ptr);
  scoped_ptr<GetUsageCallback> callback(callback_ptr);

  if (is_incognito_) {
    // We don't support FileSystem in incognito mode yet.
    callback->Run(0);
    return;
  }

  std::string origin_identifier =
      SandboxMountPointProvider::GetOriginIdentifierFromURL(origin_url);
  std::string type_string =
      FileSystemPathManager::GetFileSystemTypeString(type);
  std::string fs_identifier = origin_identifier + ":" + type_string;

  if (pending_usage_callbacks_.find(fs_identifier) !=
      pending_usage_callbacks_.end()) {
    // Another get usage task is running.  Add the callback to
    // the pending queue and return.
    pending_usage_callbacks_[fs_identifier].push_back(callback.release());
    return;
  }

  // Get the filesystem base path (i.e. "FileSystem/<origin>/<type>",
  // without unique part).
  FilePath origin_base_path =
      SandboxMountPointProvider::GetFileSystemBaseDirectoryForOriginAndType(
          base_path_, origin_identifier, type);
  if (origin_base_path.empty()) {
    // The directory does not exist.
    callback->Run(0);
    return;
  }

  pending_usage_callbacks_[fs_identifier].push_back(callback.release());
  scoped_refptr<GetUsageTask> task(
      new GetUsageTask(this, file_message_loop_, fs_identifier,
                       origin_base_path));
  task->Start();
}

void FileSystemUsageTracker::RegisterUsageTask(GetUsageTask* task) {
  running_usage_tasks_.push_back(task);
}

void FileSystemUsageTracker::UnregisterUsageTask(GetUsageTask* task) {
  DCHECK(running_usage_tasks_.front() == task);
  running_usage_tasks_.pop_front();
}

void FileSystemUsageTracker::DidGetOriginUsage(
    const std::string& fs_identifier, int64 usage) {
  PendingUsageCallbackMap::iterator cb_list_iter =
      pending_usage_callbacks_.find(fs_identifier);
  DCHECK(cb_list_iter != pending_usage_callbacks_.end());
  PendingCallbackList cb_list = cb_list_iter->second;
  for (PendingCallbackList::iterator cb_iter = cb_list.begin();
        cb_iter != cb_list.end();
        ++cb_iter) {
    scoped_ptr<GetUsageCallback> callback(*cb_iter);
    callback->Run(usage);
  }
  pending_usage_callbacks_.erase(cb_list_iter);
}

}  // namespace fileapi
