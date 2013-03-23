// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/drive/file_system/move_operation.h"

#include "chrome/browser/chromeos/drive/drive.pb.h"
#include "chrome/browser/chromeos/drive/drive_cache.h"
#include "chrome/browser/chromeos/drive/drive_file_system_util.h"
#include "chrome/browser/chromeos/drive/file_system/operation_observer.h"
#include "chrome/browser/google_apis/drive_service_interface.h"
#include "content/public/browser/browser_thread.h"

using content::BrowserThread;

namespace drive {
namespace file_system {

MoveOperation::MoveOperation(
    google_apis::DriveServiceInterface* drive_service,
    DriveResourceMetadata* metadata,
    OperationObserver* observer)
  : drive_service_(drive_service),
    metadata_(metadata),
    observer_(observer),
    weak_ptr_factory_(ALLOW_THIS_IN_INITIALIZER_LIST(this)) {
}

MoveOperation::~MoveOperation() {
}

void MoveOperation::Move(const FilePath& src_file_path,
                         const FilePath& dest_file_path,
                         const FileOperationCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  metadata_->GetEntryInfoPairByPaths(
      src_file_path,
      dest_file_path.DirName(),
      base::Bind(&MoveOperation::MoveAfterGetEntryInfoPair,
                 weak_ptr_factory_.GetWeakPtr(),
                 dest_file_path,
                 callback));
}

void MoveOperation::MoveAfterGetEntryInfoPair(
    const FilePath& dest_file_path,
    const FileOperationCallback& callback,
    scoped_ptr<EntryInfoPairResult> result) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());
  DCHECK(result.get());

  if (result->first.error != DRIVE_FILE_OK) {
    callback.Run(result->first.error);
    return;
  } else if (result->second.error != DRIVE_FILE_OK) {
    callback.Run(result->second.error);
    return;
  }

  scoped_ptr<DriveEntryProto> dest_parent_proto = result->second.proto.Pass();
  if (!dest_parent_proto->file_info().is_directory()) {
    callback.Run(DRIVE_FILE_ERROR_NOT_A_DIRECTORY);
    return;
  }

  // If the file/directory is moved to the same directory, just rename it.
  const FilePath& src_file_path = result->first.path;
  const FilePath& dest_parent_path = result->second.path;
  DCHECK_EQ(dest_parent_path.value(), dest_file_path.DirName().value());
  if (src_file_path.DirName() == dest_parent_path) {
    FileMoveCallback final_file_path_update_callback =
        base::Bind(&MoveOperation::OnFilePathUpdated,
                   weak_ptr_factory_.GetWeakPtr(),
                   callback);

    Rename(src_file_path, dest_file_path.BaseName().value(),
           final_file_path_update_callback);
    return;
  }

  // Otherwise, the move operation involves three steps:
  // 1. Renames the file at |src_file_path| to basename(|dest_file_path|)
  //    within the same directory. The rename operation is a no-op if
  //    basename(|src_file_path|) equals to basename(|dest_file_path|).
  // 2. Removes the file from its parent directory (the file is not deleted, but
  //    just becomes orphaned).
  // 3. Adds the file to the parent directory of |dest_file_path|.
  //
  // TODO(kinaba): After the step 2, the file gets into the state with no parent
  // node. Our current implementation regards the state as belonging to the root
  // directory, so below the file is dealt as such. In fact, this is not the
  // case on the server side. No-parent and in-root is a different concept. We
  // need to make our implementation consistent to the server: crbug.com/171207.
  const FileMoveCallback add_file_to_directory_callback =
      base::Bind(&MoveOperation::AddEntryToDirectory,
                 weak_ptr_factory_.GetWeakPtr(),
                 dest_parent_path,
                 callback);

  const FileMoveCallback remove_file_from_directory_callback =
      base::Bind(&MoveOperation::RemoveEntryFromDirectory,
                 weak_ptr_factory_.GetWeakPtr(),
                 add_file_to_directory_callback);

  Rename(src_file_path, dest_file_path.BaseName().value(),
         remove_file_from_directory_callback);
}

void MoveOperation::OnFilePathUpdated(const FileOperationCallback& callback,
                                        DriveFileError error,
                                        const FilePath& /* file_path */) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  callback.Run(error);
}

void MoveOperation::Rename(const FilePath& file_path,
                             const FilePath::StringType& new_name,
                             const FileMoveCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  // It is a no-op if the file is renamed to the same name.
  if (file_path.BaseName().value() == new_name) {
    callback.Run(DRIVE_FILE_OK, file_path);
    return;
  }

  // Get the edit URL of an entry at |file_path|.
  metadata_->GetEntryInfoByPath(
      file_path,
      base::Bind(
          &MoveOperation::RenameAfterGetEntryInfo,
          weak_ptr_factory_.GetWeakPtr(),
          file_path,
          new_name,
          callback));
}

void MoveOperation::RenameAfterGetEntryInfo(
    const FilePath& file_path,
    const FilePath::StringType& new_name,
    const FileMoveCallback& callback,
    DriveFileError error,
    scoped_ptr<DriveEntryProto> entry_proto) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  if (error != DRIVE_FILE_OK) {
    callback.Run(error, file_path);
    return;
  }
  DCHECK(entry_proto.get());

  // Drop the .g<something> extension from |new_name| if the file being
  // renamed is a hosted document and |new_name| has the same .g<something>
  // extension as the file.
  FilePath::StringType file_name = new_name;
  if (entry_proto->has_file_specific_info() &&
      entry_proto->file_specific_info().is_hosted_document()) {
    FilePath new_file(file_name);
    if (new_file.Extension() ==
        entry_proto->file_specific_info().document_extension()) {
      file_name = new_file.RemoveExtension().value();
    }
  }

  // The edit URL can be empty for non-editable files (such as files shared with
  // read-only privilege).
  if (entry_proto->edit_url().empty()) {
    callback.Run(DRIVE_FILE_ERROR_ACCESS_DENIED, file_path);
    return;
  }

  drive_service_->RenameResource(
      GURL(entry_proto->edit_url()),
      file_name,
      base::Bind(&MoveOperation::RenameEntryLocally,
                 weak_ptr_factory_.GetWeakPtr(),
                 file_path,
                 file_name,
                 callback));
}

void MoveOperation::RenameEntryLocally(
    const FilePath& file_path,
    const FilePath::StringType& new_name,
    const FileMoveCallback& callback,
    google_apis::GDataErrorCode status) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  const DriveFileError error = util::GDataToDriveFileError(status);
  if (error != DRIVE_FILE_OK) {
    callback.Run(error, FilePath());
    return;
  }

  metadata_->RenameEntry(
      file_path,
      new_name,
      base::Bind(&MoveOperation::NotifyAndRunFileMoveCallback,
                 weak_ptr_factory_.GetWeakPtr(),
                 callback));
}

void MoveOperation::RemoveEntryFromDirectory(
    const FileMoveCallback& callback,
    DriveFileError error,
    const FilePath& file_path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  metadata_->GetEntryInfoPairByPaths(
      file_path,
      file_path.DirName(),
      base::Bind(
          &MoveOperation::RemoveEntryFromDirectoryAfterEntryInfoPair,
          weak_ptr_factory_.GetWeakPtr(),
          callback));
}

void MoveOperation::RemoveEntryFromDirectoryAfterEntryInfoPair(
    const FileMoveCallback& callback,
    scoped_ptr<EntryInfoPairResult> result) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());
  DCHECK(result.get());

  const FilePath& file_path = result->first.path;
  if (result->first.error != DRIVE_FILE_OK) {
    callback.Run(result->first.error, file_path);
    return;
  } else if (result->second.error != DRIVE_FILE_OK) {
    callback.Run(result->second.error, file_path);
    return;
  }

  scoped_ptr<DriveEntryProto> entry_proto = result->first.proto.Pass();
  scoped_ptr<DriveEntryProto> dir_proto = result->second.proto.Pass();

  if (!dir_proto->file_info().is_directory()) {
    callback.Run(DRIVE_FILE_ERROR_NOT_A_DIRECTORY, file_path);
    return;
  }

  // The entry is moved to the root directory.
  drive_service_->RemoveResourceFromDirectory(
      GURL(dir_proto->content_url()),
      entry_proto->resource_id(),
      base::Bind(&MoveOperation::MoveEntryToDirectory,
                 weak_ptr_factory_.GetWeakPtr(),
                 file_path,
                 FilePath(kDriveRootDirectory),
                 base::Bind(&MoveOperation::NotifyAndRunFileMoveCallback,
                            weak_ptr_factory_.GetWeakPtr(),
                            callback)));
}

// TODO(zork): Share with CopyOperation.
// See: crbug.com/150050
void MoveOperation::AddEntryToDirectory(const FilePath& directory_path,
                                        const FileOperationCallback& callback,
                                        DriveFileError error,
                                        const FilePath& file_path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  metadata_->GetEntryInfoPairByPaths(
      file_path,
      directory_path,
      base::Bind(
          &MoveOperation::AddEntryToDirectoryAfterGetEntryInfoPair,
          weak_ptr_factory_.GetWeakPtr(),
          callback));
}

// TODO(zork): Share with CopyOperation.
// See: crbug.com/150050
void MoveOperation::AddEntryToDirectoryAfterGetEntryInfoPair(
    const FileOperationCallback& callback,
    scoped_ptr<EntryInfoPairResult> result) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());
  DCHECK(result.get());

  if (result->first.error != DRIVE_FILE_OK) {
    callback.Run(result->first.error);
    return;
  } else if (result->second.error != DRIVE_FILE_OK) {
    callback.Run(result->second.error);
    return;
  }

  scoped_ptr<DriveEntryProto> src_proto = result->first.proto.Pass();
  scoped_ptr<DriveEntryProto> dir_proto = result->second.proto.Pass();

  if (!dir_proto->file_info().is_directory()) {
    callback.Run(DRIVE_FILE_ERROR_NOT_A_DIRECTORY);
    return;
  }

  const FilePath& file_path = result->first.path;
  const FilePath& dir_path = result->second.path;
  drive_service_->AddResourceToDirectory(
      GURL(dir_proto->content_url()),
      GURL(src_proto->edit_url()),
      base::Bind(&MoveOperation::MoveEntryToDirectory,
                 weak_ptr_factory_.GetWeakPtr(),
                 file_path,
                 dir_path,
                 base::Bind(&MoveOperation::NotifyAndRunFileOperationCallback,
                            weak_ptr_factory_.GetWeakPtr(),
                            callback)));
}

// TODO(zork): Share with CopyOperation.
// See: crbug.com/150050
void MoveOperation::MoveEntryToDirectory(
    const FilePath& file_path,
    const FilePath& directory_path,
    const FileMoveCallback& callback,
    google_apis::GDataErrorCode status) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  const DriveFileError error = util::GDataToDriveFileError(status);
  if (error != DRIVE_FILE_OK) {
    callback.Run(error, FilePath());
    return;
  }

  metadata_->MoveEntryToDirectory(file_path, directory_path, callback);
}

// TODO(zork): Share with CopyOperation.
// See: crbug.com/150050
void MoveOperation::NotifyAndRunFileOperationCallback(
    const FileOperationCallback& callback,
    DriveFileError error,
    const FilePath& moved_file_path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  if (error == DRIVE_FILE_OK)
    observer_->OnDirectoryChangedByOperation(moved_file_path.DirName());

  callback.Run(error);
}

void MoveOperation::NotifyAndRunFileMoveCallback(
    const FileMoveCallback& callback,
    DriveFileError error,
    const FilePath& moved_file_path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  if (error == DRIVE_FILE_OK)
    observer_->OnDirectoryChangedByOperation(moved_file_path.DirName());

  callback.Run(error, moved_file_path);
}

}  // namespace file_system
}  // namespace drive
