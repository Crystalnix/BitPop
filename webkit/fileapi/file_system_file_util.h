// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBKIT_FILEAPI_FILE_SYSTEM_FILE_UTIL_H_
#define WEBKIT_FILEAPI_FILE_SYSTEM_FILE_UTIL_H_

#include "base/callback.h"
#include "base/file_path.h"
#include "base/file_util.h"
#include "base/file_util_proxy.h"
#include "base/memory/ref_counted.h"
#include "base/memory/singleton.h"
#include "base/platform_file.h"
#include "base/tracked_objects.h"
#include "webkit/fileapi/file_system_types.h"

namespace base {
struct PlatformFileInfo;
class MessageLoopProxy;
class Time;
}

namespace fileapi {

using base::PlatformFile;
using base::PlatformFileError;
class FileSystemOperationContext;

// A large part of this implementation is taken from base::FileUtilProxy.
// TODO(dmikurube, kinuko): Clean up base::FileUtilProxy to factor out common
// routines. It includes dropping FileAPI-specific routines from FileUtilProxy.
class FileSystemFileUtil {
 public:
  static FileSystemFileUtil* GetInstance();

  // Creates or opens a file with the given flags.  It is invalid to pass NULL
  // for the callback.
  // If PLATFORM_FILE_CREATE is set in |file_flags| it always tries to create
  // a new file at the given |file_path| and calls back with
  // PLATFORM_FILE_ERROR_FILE_EXISTS if the |file_path| already exists.
  virtual PlatformFileError CreateOrOpen(
      FileSystemOperationContext* context,
      const FilePath& file_path,
      int file_flags,
      PlatformFile* file_handle,
      bool* created);

  // Close the given file handle.
  virtual PlatformFileError Close(
      FileSystemOperationContext* context,
      PlatformFile);

  // Ensures that the given |file_path| exist.  This creates a empty new file
  // at |file_path| if the |file_path| does not exist.
  // If a new file han not existed and is created at the |file_path|,
  // |created| of the callback argument is set true and |error code|
  // is set PLATFORM_FILE_OK.
  // If the file already exists, |created| is set false and |error code|
  // is set PLATFORM_FILE_OK.
  // If the file hasn't existed but it couldn't be created for some other
  // reasons, |created| is set false and |error code| indicates the error.
  virtual PlatformFileError EnsureFileExists(
      FileSystemOperationContext* context,
      const FilePath& file_path, bool* created);

  // Maps |virtual_path| given |context| into |local_path| which represents
  // physical file location on the host OS. This may not always make sense for
  // all subclasses.
  virtual PlatformFileError GetLocalFilePath(
      FileSystemOperationContext* context,
      const FilePath& virtual_path,
      FilePath* local_path);

  // Retrieves the information about a file.  It is invalid to pass NULL for the
  // callback.
  virtual PlatformFileError GetFileInfo(
      FileSystemOperationContext* context,
      const FilePath& file_,
      base::PlatformFileInfo* file_info,
      FilePath* platform_path);

  virtual PlatformFileError ReadDirectory(
      FileSystemOperationContext* context,
      const FilePath& file_path,
      std::vector<base::FileUtilProxy::Entry>* entries);

  // Creates directory at given path. It's an error to create
  // if |exclusive| is true and dir already exists.
  virtual PlatformFileError CreateDirectory(
      FileSystemOperationContext* context,
      const FilePath& file_path,
      bool exclusive,
      bool recursive);

  // TODO(dmikurube): Make this method non-virtual if it's possible.
  // It conflicts with LocalFileSystemFileUtil for now.
  //
  // Copies a file or a directory from |src_file_path| to |dest_file_path|
  // Error cases:
  // If destination's parent doesn't exist.
  // If source dir exists but destination path is an existing file.
  // If source file exists but destination path is an existing directory.
  // If source is a parent of destination.
  // If source doesn't exist.
  virtual PlatformFileError Copy(
      FileSystemOperationContext* context,
      const FilePath& src_file_path,
      const FilePath& dest_file_path);

  // TODO(dmikurube): Make this method non-virtual if it's possible.
  // It conflicts with LocalFileSystemFileUtil for now.
  //
  // Moves a file or a directory from src_file_path to dest_file_path.
  // Error cases are similar to Copy method's error cases.
  virtual PlatformFileError Move(
      FileSystemOperationContext* context,
      const FilePath& src_file_path,
      const FilePath& dest_file_path);

  // Deletes a file or a directory.
  // It is an error to delete a non-empty directory with recursive=false.
  virtual PlatformFileError Delete(
      FileSystemOperationContext* context,
      const FilePath& file_path,
      bool recursive);

  // Touches a file. The callback can be NULL.
  virtual PlatformFileError Touch(
      FileSystemOperationContext* context,
      const FilePath& file_path,
      const base::Time& last_access_time,
      const base::Time& last_modified_time);

  // Truncates a file to the given length. If |length| is greater than the
  // current length of the file, the file will be extended with zeroes.
  // The callback can be NULL.
  virtual PlatformFileError Truncate(
      FileSystemOperationContext* context,
      const FilePath& path,
      int64 length);

  // It will be implemented by each subclass such as FileSystemFileEnumerator.
  class AbstractFileEnumerator {
   public:
    virtual ~AbstractFileEnumerator() {}

    // Returns an empty string if there are no more results.
    virtual FilePath Next() = 0;

    virtual bool IsDirectory() = 0;
  };

 protected:
  FileSystemFileUtil() { }

  // This also removes the destination directory if it's non-empty and all
  // other checks are passed (so that the copy/move correctly overwrites the
  // destination).
  // This method is non-virtual, not to be overridden.
  PlatformFileError PerformCommonCheckAndPreparationForMoveAndCopy(
      FileSystemOperationContext* unused,
      const FilePath& src_file_path,
      const FilePath& dest_file_path);

  virtual bool PathExists(
      FileSystemOperationContext* unused,
      const FilePath& file_path);

  virtual bool DirectoryExists(
      FileSystemOperationContext* unused,
      const FilePath& file_path);

  virtual bool IsDirectoryEmpty(
      FileSystemOperationContext* unused,
      const FilePath& file_path);

  // Copies or moves a single file.
  virtual PlatformFileError CopyOrMoveFile(
      FileSystemOperationContext* context,
      const FilePath& src_file_path,
      const FilePath& dest_file_path,
      bool copy);

  // Performs recursive copy by calling CopyOrMoveFile for individual files.
  // Operations for recursive traversal are encapsulated in this method.
  // It assumes src_file_path and dest_file_path have passed
  // PerformCommonCheckAndPreparationForMoveAndCopy().
  // This method is non-virtual, not to be overridden.
  PlatformFileError CopyDirectory(
      FileSystemOperationContext* context,
      const FilePath& src_file_path,
      const FilePath& dest_file_path);

  // Returns a pointer to a new instance of AbstractFileEnumerator which is
  // implemented for each FileUtil subclass. The instance needs to be freed
  // by the caller.
  virtual AbstractFileEnumerator* CreateFileEnumerator(
      const FilePath& root_path);

  friend struct DefaultSingletonTraits<FileSystemFileUtil>;
  DISALLOW_COPY_AND_ASSIGN(FileSystemFileUtil);
};

}  // namespace fileapi

#endif  // WEBKIT_FILEAPI_FILE_SYSTEM_FILE_UTIL_H_
