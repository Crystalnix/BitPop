// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBKIT_DATABASE_VFS_BACKEND_H_
#define WEBKIT_DATABASE_VFS_BACKEND_H_

#include "base/platform_file.h"
#include "base/process.h"
#include "base/string16.h"

class FilePath;

namespace webkit_database {

class VfsBackend {
 public:
  static void GetFileHandleForProcess(base::ProcessHandle process_handle,
                                      const base::PlatformFile& file_handle,
                                      base::PlatformFile* target_handle,
                                      bool close_source_handle);

  static void OpenFile(const FilePath& file_path,
                       int desired_flags,
                       base::PlatformFile* file_handle);

  static void OpenTempFileInDirectory(const FilePath& dir_path,
                                      int desired_flags,
                                      base::PlatformFile* file_handle);

  static int DeleteFile(const FilePath& file_path, bool sync_dir);

  static uint32 GetFileAttributes(const FilePath& file_path);

  static int64 GetFileSize(const FilePath& file_path);

  // Used to make decisions in the DatabaseDispatcherHost.
  static bool FileTypeIsMainDB(int desired_flags);
  static bool FileTypeIsJournal(int desired_flags);
  static bool OpenTypeIsReadWrite(int desired_flags);

 private:
  static bool OpenFileFlagsAreConsistent(int desired_flags);
};

} // namespace webkit_database

#endif  // WEBKIT_DATABASE_VFS_BACKEND_H_
