// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBKIT_FILEAPI_OBFUSCATED_FILE_UTIL_H_
#define WEBKIT_FILEAPI_OBFUSCATED_FILE_UTIL_H_

#include <map>
#include <string>
#include <vector>

#include "base/file_path.h"
#include "base/file_util_proxy.h"
#include "base/platform_file.h"
#include "base/timer.h"
#include "webkit/blob/shareable_file_reference.h"
#include "webkit/fileapi/file_system_directory_database.h"
#include "webkit/fileapi/file_system_file_util.h"
#include "webkit/fileapi/file_system_origin_database.h"
#include "webkit/fileapi/file_system_types.h"
#include "webkit/fileapi/file_system_url.h"
#include "webkit/fileapi/fileapi_export.h"

namespace base {
struct PlatformFileInfo;
class Time;
}

class GURL;

namespace fileapi {

class FileSystemOperationContext;

// The overall implementation philosophy of this class is that partial failures
// should leave us with an intact database; we'd prefer to leak the occasional
// backing file than have a database entry whose backing file is missing.  When
// doing FSCK operations, if you find a loose backing file with no reference,
// you may safely delete it.
//
// This class must be deleted on the FILE thread, because that's where
// DropDatabases needs to be called.
class FILEAPI_EXPORT_PRIVATE ObfuscatedFileUtil : public FileSystemFileUtil {
 public:
  // Origin enumerator interface.
  // An instance of this interface is assumed to be called on the file thread.
  class AbstractOriginEnumerator {
   public:
    virtual ~AbstractOriginEnumerator() {}

    // Returns the next origin.  Returns empty if there are no more origins.
    virtual GURL Next() = 0;

    // Returns the current origin's information.
    virtual bool HasFileSystemType(FileSystemType type) const = 0;
  };

  explicit ObfuscatedFileUtil(const FilePath& file_system_directory);
  virtual ~ObfuscatedFileUtil();

  // FileSystemFileUtil overrides.
  virtual base::PlatformFileError CreateOrOpen(
      FileSystemOperationContext* context,
      const FileSystemURL& url,
      int file_flags,
      base::PlatformFile* file_handle,
      bool* created) OVERRIDE;
  virtual PlatformFileError Close(
      FileSystemOperationContext* context,
      PlatformFile file) OVERRIDE;
  virtual base::PlatformFileError EnsureFileExists(
      FileSystemOperationContext* context,
      const FileSystemURL& url, bool* created) OVERRIDE;
  virtual base::PlatformFileError CreateDirectory(
      FileSystemOperationContext* context,
      const FileSystemURL& url,
      bool exclusive,
      bool recursive) OVERRIDE;
  virtual base::PlatformFileError GetFileInfo(
      FileSystemOperationContext* context,
      const FileSystemURL& url,
      base::PlatformFileInfo* file_info,
      FilePath* platform_file) OVERRIDE;
  virtual AbstractFileEnumerator* CreateFileEnumerator(
      FileSystemOperationContext* context,
      const FileSystemURL& root_url,
      bool recursive) OVERRIDE;
  virtual base::PlatformFileError GetLocalFilePath(
      FileSystemOperationContext* context,
      const FileSystemURL& file_system_url,
      FilePath* local_path) OVERRIDE;
  virtual base::PlatformFileError Touch(
      FileSystemOperationContext* context,
      const FileSystemURL& url,
      const base::Time& last_access_time,
      const base::Time& last_modified_time) OVERRIDE;
  virtual base::PlatformFileError Truncate(
      FileSystemOperationContext* context,
      const FileSystemURL& url,
      int64 length) OVERRIDE;
  virtual bool PathExists(
      FileSystemOperationContext* context,
      const FileSystemURL& url) OVERRIDE;
  virtual bool DirectoryExists(
      FileSystemOperationContext* context,
      const FileSystemURL& url) OVERRIDE;
  virtual bool IsDirectoryEmpty(
      FileSystemOperationContext* context,
      const FileSystemURL& url) OVERRIDE;
  virtual base::PlatformFileError CopyOrMoveFile(
      FileSystemOperationContext* context,
      const FileSystemURL& src_url,
      const FileSystemURL& dest_url,
      bool copy) OVERRIDE;
  virtual PlatformFileError CopyInForeignFile(
        FileSystemOperationContext* context,
        const FilePath& src_file_path,
        const FileSystemURL& dest_url) OVERRIDE;
  virtual base::PlatformFileError DeleteFile(
      FileSystemOperationContext* context,
      const FileSystemURL& url) OVERRIDE;
  virtual base::PlatformFileError DeleteSingleDirectory(
      FileSystemOperationContext* context,
      const FileSystemURL& url) OVERRIDE;
  virtual base::PlatformFileError CreateSnapshotFile(
      FileSystemOperationContext* context,
      const FileSystemURL& url,
      base::PlatformFileInfo* file_info,
      FilePath* platform_path,
      SnapshotFilePolicy* policy) OVERRIDE;

  // Gets the topmost directory specific to this origin and type.  This will
  // contain both the directory database's files and all the backing file
  // subdirectories.
  // Returns an empty path if the directory is undefined (e.g. because |type|
  // is invalid). If the directory is defined, it will be returned, even if
  // there is a file system error (e.g. the directory doesn't exist on disk and
  // |create| is false). Callers should always check |error_code| to make sure
  // the returned path is usable.
  FilePath GetDirectoryForOriginAndType(
      const GURL& origin,
      FileSystemType type,
      bool create,
      base::PlatformFileError* error_code);

  // Deletes the topmost directory specific to this origin and type.  This will
  // delete its directory database.
  bool DeleteDirectoryForOriginAndType(const GURL& origin, FileSystemType type);

  // This will migrate a filesystem from the old passthrough sandbox into the
  // new obfuscated one.  It won't obfuscate the old filenames [it will maintain
  // the old structure, but move it to a new root], but any new files created
  // will go into the new standard locations.  This will be completely
  // transparent to the user.  This migration is atomic in that it won't alter
  // the source data until it's done, and that will be with a single directory
  // move [the directory with the unguessable name will move into the new
  // filesystem storage directory].  However, if this fails partway through, it
  // might leave a seemingly-valid database for this origin.  When it starts up,
  // it will clear any such database, just in case.
  bool MigrateFromOldSandbox(
      const GURL& origin, FileSystemType type, const FilePath& root);

  // TODO(ericu): This doesn't really feel like it belongs in this class.
  // The previous version lives in FileSystemPathManager, but perhaps
  // SandboxMountPointProvider would be better?
  static FilePath::StringType GetDirectoryNameForType(FileSystemType type);

  // This method and all methods of its returned class must be called only on
  // the FILE thread.  The caller is responsible for deleting the returned
  // object.
  AbstractOriginEnumerator* CreateOriginEnumerator();

  // Deletes a directory database from the database list in the ObfuscatedFSFU
  // and destroys the database on the disk.
  bool DestroyDirectoryDatabase(const GURL& origin, FileSystemType type);

  // Computes a cost for storing a given file in the obfuscated FSFU.
  // As the cost of a file is independent of the cost of its parent directories,
  // this ignores all but the BaseName of the supplied path.  In order to
  // compute the cost of adding a multi-segment directory recursively, call this
  // on each path segment and add the results.
  static int64 ComputeFilePathCost(const FilePath& path);

 private:
  typedef FileSystemDirectoryDatabase::FileId FileId;
  typedef FileSystemDirectoryDatabase::FileInfo FileInfo;

  friend class ObfuscatedFileEnumerator;

  base::PlatformFileError GetFileInfoInternal(
      FileSystemDirectoryDatabase* db,
      FileSystemOperationContext* context,
      const GURL& origin,
      FileSystemType type,
      FileId file_id,
      FileInfo* local_info,
      base::PlatformFileInfo* file_info,
      FilePath* platform_file_path);

  // Creates a new file, both the underlying backing file and the entry in the
  // database.  |dest_file_info| is an in-out parameter.  Supply the name and
  // parent_id; data_path is ignored.  On success, data_path will
  // always be set to the relative path [from the root of the type-specific
  // filesystem directory] of a NEW backing file, and handle, if supplied, will
  // hold open PlatformFile for the backing file, which the caller is
  // responsible for closing.  If you supply a path in |source_path|, it will be
  // used as a source from which to COPY data.
  // Caveat: do not supply handle if you're also supplying a data path.  It was
  // easier not to support this, and no code has needed it so far, so it will
  // DCHECK and handle will hold base::kInvalidPlatformFileValue.
  base::PlatformFileError CreateFile(
      FileSystemOperationContext* context,
      const FilePath& source_file_path,
      const GURL& dest_origin,
      FileSystemType dest_type,
      FileInfo* dest_file_info,
      int file_flags,
      base::PlatformFile* handle);

  // This converts from a relative path [as is stored in the FileInfo.data_path
  // field] to an absolute platform path that can be given to the native
  // filesystem.
  FilePath DataPathToLocalPath(
      const GURL& origin,
      FileSystemType type,
      const FilePath& data_file_path);

  // This returns NULL if |create| flag is false and a filesystem does not
  // exist for the given |origin_url| and |type|.
  // For read operations |create| should be false.
  FileSystemDirectoryDatabase* GetDirectoryDatabase(
      const GURL& origin_url, FileSystemType type, bool create);

  // Gets the topmost directory specific to this origin.  This will
  // contain both the filesystem type subdirectories.
  FilePath GetDirectoryForOrigin(const GURL& origin,
                                 bool create,
                                 base::PlatformFileError* error_code);

  void InvalidateUsageCache(FileSystemOperationContext* context,
                            const GURL& origin,
                            FileSystemType type);

  void MarkUsed();
  void DropDatabases();
  bool InitOriginDatabase(bool create);

  base::PlatformFileError GenerateNewLocalPath(
      FileSystemDirectoryDatabase* db,
      FileSystemOperationContext* context,
      const GURL& origin,
      FileSystemType type,
      FilePath* local_path);

  typedef std::map<std::string, FileSystemDirectoryDatabase*> DirectoryMap;
  DirectoryMap directories_;
  scoped_ptr<FileSystemOriginDatabase> origin_database_;
  FilePath file_system_directory_;
  base::OneShotTimer<ObfuscatedFileUtil> timer_;

  DISALLOW_COPY_AND_ASSIGN(ObfuscatedFileUtil);
};

}  // namespace fileapi

#endif  // WEBKIT_FILEAPI_OBFUSCATED_FILE_UTIL_H_
