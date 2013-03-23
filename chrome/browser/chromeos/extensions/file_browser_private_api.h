// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_EXTENSIONS_FILE_BROWSER_PRIVATE_API_H_
#define CHROME_BROWSER_CHROMEOS_EXTENSIONS_FILE_BROWSER_PRIVATE_API_H_

#include <map>
#include <queue>
#include <string>
#include <vector>

#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/platform_file.h"
#include "chrome/browser/chromeos/drive/drive_file_error.h"
#include "chrome/browser/chromeos/extensions/file_browser_event_router.h"
#include "chrome/browser/chromeos/extensions/zip_file_creator.h"
#include "chrome/browser/extensions/extension_function.h"
#include "chrome/browser/prefs/pref_service.h"
#include "googleurl/src/url_util.h"

class GURL;

namespace fileapi {
class FileSystemContext;
}

namespace drive {
struct SearchResultInfo;
class DriveCacheEntry;
struct DriveWebAppInfo;
class DriveWebAppsRegistry;
}

namespace ui {
struct SelectedFileInfo;
}

// Implements the chrome.fileBrowserPrivate.requestLocalFileSystem method.
class RequestLocalFileSystemFunction : public AsyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("fileBrowserPrivate.requestLocalFileSystem");

 protected:
  virtual ~RequestLocalFileSystemFunction() {}

  // AsyncExtensionFunction overrides.
  virtual bool RunImpl() OVERRIDE;

 private:
  class LocalFileSystemCallbackDispatcher;

  void RespondSuccessOnUIThread(const std::string& name,
                                const GURL& root_path);
  void RespondFailedOnUIThread(base::PlatformFileError error_code);
  void RequestOnFileThread(
      scoped_refptr<fileapi::FileSystemContext> file_system_context,
      const GURL& source_url,
      int child_id);
};

// Implements the chrome.fileBrowserPrivate.addFileWatch method.
class FileWatchBrowserFunctionBase : public AsyncExtensionFunction {
 protected:
  virtual ~FileWatchBrowserFunctionBase() {}

  virtual bool PerformFileWatchOperation(
      scoped_refptr<FileBrowserEventRouter> event_router,
      const FilePath& local_path, const FilePath& virtual_path,
      const std::string& extension_id) = 0;

  // AsyncExtensionFunction overrides.
  virtual bool RunImpl() OVERRIDE;

 private:
  void RespondOnUIThread(bool success);
  void RunFileWatchOperationOnFileThread(
      scoped_refptr<FileBrowserEventRouter> event_router,
      const GURL& file_url,
      const std::string& extension_id);
};

// Implements the chrome.fileBrowserPrivate.addFileWatch method.
class AddFileWatchBrowserFunction : public FileWatchBrowserFunctionBase {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("fileBrowserPrivate.addFileWatch");

 protected:
  virtual ~AddFileWatchBrowserFunction() {}

  virtual bool PerformFileWatchOperation(
      scoped_refptr<FileBrowserEventRouter> event_router,
      const FilePath& local_path, const FilePath& virtual_path,
      const std::string& extension_id) OVERRIDE;
};


// Implements the chrome.fileBrowserPrivate.removeFileWatch method.
class RemoveFileWatchBrowserFunction : public FileWatchBrowserFunctionBase {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("fileBrowserPrivate.removeFileWatch");

 protected:
  virtual ~RemoveFileWatchBrowserFunction() {}

  virtual bool PerformFileWatchOperation(
      scoped_refptr<FileBrowserEventRouter> event_router,
      const FilePath& local_path, const FilePath& virtual_path,
      const std::string& extension_id) OVERRIDE;
};

// Implements the chrome.fileBrowserPrivate.getFileTasks method.
class GetFileTasksFileBrowserFunction : public AsyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("fileBrowserPrivate.getFileTasks");

 protected:
  virtual ~GetFileTasksFileBrowserFunction() {}

  // AsyncExtensionFunction overrides.
  virtual bool RunImpl() OVERRIDE;

 private:
  struct FileInfo {
    GURL file_url;
    FilePath file_path;
    std::string mime_type;
  };
  typedef std::vector<FileInfo> FileInfoList;

  // Typedef for holding a map from app_id to DriveWebAppInfo so
  // we can look up information on the apps.
  typedef std::map<std::string, drive::DriveWebAppInfo*> WebAppInfoMap;

  // Look up apps in the registry, and collect applications that match the file
  // paths given. Returns the intersection of all available application ids in
  // |available_apps| and a map of application ID to the Drive web application
  // info collected in |app_info| so details can be collected later. The caller
  // takes ownership of the pointers in |app_info|.
  static void IntersectAvailableDriveTasks(
      drive::DriveWebAppsRegistry* registry,
      const FileInfoList& file_info_list,
      WebAppInfoMap* app_info,
      std::set<std::string>* available_apps);

  // Takes a map of app_id to application information in |app_info|, and the set
  // of |available_apps| and adds Drive tasks to the |result_list| for each of
  // the |available_apps|.  If a default task is set in the result list,
  // then |default_already_set| is set to true.
  static void CreateDriveTasks(drive::DriveWebAppsRegistry* registry,
                               const WebAppInfoMap& app_info,
                               const std::set<std::string>& available_apps,
                               const std::set<std::string>& default_apps,
                               ListValue* result_list,
                               bool* default_already_set);

  // Looks in the preferences and finds any of the available apps that are
  // also listed as default apps for any of the files in the info list.
  void FindDefaultDriveTasks(const FileInfoList& file_info_list,
                             const std::set<std::string>& available_apps,
                             std::set<std::string>* default_apps);

  // Find the list of drive apps that can be used with the given file types. If
  // a default task is set in the result list, then |default_already_set| is set
  // to true.
  bool FindDriveAppTasks(const FileInfoList& file_info_list,
                         ListValue* result_list,
                         bool* default_already_set);

  // Find the list of Web Intent tasks that can be used with the given file
  // types, appending them to the |result_list|.
  bool FindWebIntentTasks(const std::vector<GURL>& file_urls,
                          ListValue* result_list);

  // Find the list of app file handlers that can be used with the given file
  // types, appending them to the |result_list|.
  bool FindAppTasks(const std::vector<GURL>& file_urls,
                    ListValue* result_list);
};

// Implements the chrome.fileBrowserPrivate.executeTask method.
class ExecuteTasksFileBrowserFunction : public AsyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("fileBrowserPrivate.executeTask");

  ExecuteTasksFileBrowserFunction();

  void OnTaskExecuted(bool success);

 protected:
  virtual ~ExecuteTasksFileBrowserFunction();

  // AsyncExtensionFunction overrides.
  virtual bool RunImpl() OVERRIDE;
};

// Implements the chrome.fileBrowserPrivate.setDefaultTask method.
class SetDefaultTaskFileBrowserFunction : public SyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("fileBrowserPrivate.setDefaultTask");

  SetDefaultTaskFileBrowserFunction();

 protected:
  virtual ~SetDefaultTaskFileBrowserFunction();

  // AsyncExtensionFunction overrides.
  virtual bool RunImpl() OVERRIDE;
};

// Parent class for the chromium extension APIs for the file dialog.
class FileBrowserFunction
    : public AsyncExtensionFunction {
 public:
  FileBrowserFunction();

 protected:
  typedef std::vector<GURL> UrlList;
  typedef std::vector<ui::SelectedFileInfo> SelectedFileInfoList;
  typedef base::Callback<void(const SelectedFileInfoList&)>
      GetLocalPathsCallback;

  virtual ~FileBrowserFunction();

  // Converts virtual paths to local paths by calling GetLocalPathsOnFileThread
  // on the file thread and call |callback| on the UI thread with the result.
  void GetLocalPathsOnFileThreadAndRunCallbackOnUIThread(
      const UrlList& file_urls,
      GetLocalPathsCallback callback);

  // Figure out the tab_id of the hosting tab.
  int32 GetTabId() const;

 private:
  // Converts virtual paths to local paths and call |callback| (on the UI
  // thread) with the results.
  // This method must be called from the file thread.
  void GetLocalPathsOnFileThread(
      scoped_refptr<fileapi::FileSystemContext> file_system_context,
      const UrlList& file_urls,
      GetLocalPathsCallback callback);
};

// Select a single file.  Closes the dialog window.
class SelectFileFunction : public FileBrowserFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("fileBrowserPrivate.selectFile");

  SelectFileFunction() {}

 protected:
  virtual ~SelectFileFunction() {}

  // AsyncExtensionFunction overrides.
  virtual bool RunImpl() OVERRIDE;

 private:
  // A callback method to handle the result of
  // GetLocalPathsOnFileThreadAndRunCallbackOnUIThread.
  void GetLocalPathsResponseOnUIThread(const SelectedFileInfoList& files);
};

// View multiple selected files.  Window stays open.
class ViewFilesFunction : public FileBrowserFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("fileBrowserPrivate.viewFiles");

  ViewFilesFunction();

 protected:
  virtual ~ViewFilesFunction();

  // AsyncExtensionFunction overrides.
  virtual bool RunImpl() OVERRIDE;
};

// Select multiple files.  Closes the dialog window.
class SelectFilesFunction : public FileBrowserFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("fileBrowserPrivate.selectFiles");

  SelectFilesFunction();

 protected:
  virtual ~SelectFilesFunction();

  // AsyncExtensionFunction overrides.
  virtual bool RunImpl() OVERRIDE;

 private:
  // A callback method to handle the result of
  // GetLocalPathsOnFileThreadAndRunCallbackOnUIThread.
  void GetLocalPathsResponseOnUIThread(const SelectedFileInfoList& files);
};

// Cancel file selection Dialog.  Closes the dialog window.
class CancelFileDialogFunction : public FileBrowserFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("fileBrowserPrivate.cancelDialog");

  CancelFileDialogFunction() {}

 protected:
  virtual ~CancelFileDialogFunction() {}

  // AsyncExtensionFunction overrides.
  virtual bool RunImpl() OVERRIDE;
};

// Mount a device or a file.
class AddMountFunction : public FileBrowserFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("fileBrowserPrivate.addMount");

  AddMountFunction();

 protected:
  virtual ~AddMountFunction();

  // AsyncExtensionFunction overrides.
  virtual bool RunImpl() OVERRIDE;

 private:
  // A callback method to handle the result of
  // GetLocalPathsOnFileThreadAndRunCallbackOnUIThread.
  void GetLocalPathsResponseOnUIThread(const std::string& mount_type_str,
                                       const SelectedFileInfoList& files);
  // A callback method to handle the result of SetMountedState.
  void OnMountedStateSet(const std::string& mount_type,
                         const FilePath::StringType& file_name,
                         drive::DriveFileError error,
                         const FilePath& file_path);
};

// Unmounts selected device. Expects mount point path as an argument.
class RemoveMountFunction : public FileBrowserFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("fileBrowserPrivate.removeMount");

  RemoveMountFunction();

 protected:
  virtual ~RemoveMountFunction();

  // AsyncExtensionFunction overrides.
  virtual bool RunImpl() OVERRIDE;

 private:
  // A callback method to handle the result of
  // GetLocalPathsOnFileThreadAndRunCallbackOnUIThread.
  void GetLocalPathsResponseOnUIThread(const SelectedFileInfoList& files);
};

class GetMountPointsFunction : public AsyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("fileBrowserPrivate.getMountPoints");

  GetMountPointsFunction();

 protected:
  virtual ~GetMountPointsFunction();

  // AsyncExtensionFunction overrides.
  virtual bool RunImpl() OVERRIDE;
};

// Formats Device given its mount path.
class FormatDeviceFunction : public FileBrowserFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("fileBrowserPrivate.formatDevice");

  FormatDeviceFunction();

 protected:
  virtual ~FormatDeviceFunction();

  // AsyncExtensionFunction overrides.
  virtual bool RunImpl() OVERRIDE;
};

// Sets last modified date in seconds of local file
class SetLastModifiedFunction : public FileBrowserFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("fileBrowserPrivate.setLastModified");

  SetLastModifiedFunction();

 protected:
  virtual ~SetLastModifiedFunction();

  void RunOperationOnFileThread(std::string file_url, time_t timestamp);

  // AsyncExtensionFunction overrides.
  virtual bool RunImpl() OVERRIDE;
};

class GetSizeStatsFunction : public FileBrowserFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("fileBrowserPrivate.getSizeStats");

  GetSizeStatsFunction();

 protected:
  virtual ~GetSizeStatsFunction();

  // AsyncExtensionFunction overrides.
  virtual bool RunImpl() OVERRIDE;

 private:
  void GetDriveAvailableSpaceCallback(drive::DriveFileError error,
                                      int64 bytes_total,
                                      int64 bytes_used);

  void GetSizeStatsCallbackOnUIThread(size_t total_size_kb,
                                      size_t remaining_size_kb);
  void CallGetSizeStatsOnFileThread(const std::string& mount_path);
};

// Retrieves devices meta-data. Expects volume's device path as an argument.
class GetVolumeMetadataFunction : public FileBrowserFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("fileBrowserPrivate.getVolumeMetadata");

  GetVolumeMetadataFunction();

 protected:
  virtual ~GetVolumeMetadataFunction();

  // AsyncExtensionFunction overrides.
  virtual bool RunImpl() OVERRIDE;
};

// Toggles fullscreen mode for the browser.
class ToggleFullscreenFunction : public SyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("fileBrowserPrivate.toggleFullscreen");

 protected:
  virtual ~ToggleFullscreenFunction() {}

  // SyncExtensionFunction overrides.
  virtual bool RunImpl() OVERRIDE;
};

// Checks if the browser is in fullscreen mode.
class IsFullscreenFunction : public SyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("fileBrowserPrivate.isFullscreen");

 protected:
  virtual ~IsFullscreenFunction() {}

  // SyncExtensionFunction overrides.
  virtual bool RunImpl() OVERRIDE;
};

// File Dialog Strings.
class FileDialogStringsFunction : public SyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("fileBrowserPrivate.getStrings");

  FileDialogStringsFunction() {}

 protected:
  virtual ~FileDialogStringsFunction() {}

  // SyncExtensionFunction overrides.
  virtual bool RunImpl() OVERRIDE;
};

// Retrieve property information for multiple files, returning a list of the
// same length as the input list of file URLs.  If a particular file has an
// error, then return a dictionary with the key "error" set to the error number
// (drive::DriveFileError) for that entry in the returned list.
class GetDriveFilePropertiesFunction : public FileBrowserFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("fileBrowserPrivate.getDriveFileProperties");

  GetDriveFilePropertiesFunction();

 protected:
  virtual ~GetDriveFilePropertiesFunction();

  void GetNextFileProperties();
  void CompleteGetFileProperties();

  // Virtual function that can be overridden to do operations on each virtual
  // file path and update its the properties.
  virtual void DoOperation(const FilePath& file_path,
                           base::DictionaryValue* properties,
                           scoped_ptr<drive::DriveEntryProto> entry_proto);

  void OnOperationComplete(const FilePath& file_path,
                           base::DictionaryValue* properties,
                           drive::DriveFileError error,
                           scoped_ptr<drive::DriveEntryProto> entry_proto);

  // AsyncExtensionFunction overrides.
  virtual bool RunImpl() OVERRIDE;

  // Builds list of file properies. Calls DoOperation for each file.
  void PrepareResults();

 private:
  void OnGetFileInfo(const FilePath& file_path,
                     base::DictionaryValue* property_dict,
                     drive::DriveFileError error,
                     scoped_ptr<drive::DriveEntryProto> entry_proto);

  void CacheStateReceived(base::DictionaryValue* property_dict,
                          bool success,
                          const drive::DriveCacheEntry& cache_entry);

  size_t current_index_;
  base::ListValue* path_list_;
  scoped_ptr<base::ListValue> file_properties_;
};

// Pin/unpin multiple files in the cache, returning a list of file
// properties with the updated cache state.  The returned array is the
// same length as the input list of file URLs.  If a particular file
// has an error, then return a dictionary with the key "error" set to
// the error number (drive::DriveFileError) for that entry in the
// returned list.
class PinDriveFileFunction : public GetDriveFilePropertiesFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("fileBrowserPrivate.pinDriveFile");

  PinDriveFileFunction();

 protected:
  virtual ~PinDriveFileFunction();

  // AsyncExtensionFunction overrides.
  virtual bool RunImpl() OVERRIDE;

 private:
  // Actually do the pinning/unpinning of each file.
  virtual void DoOperation(
      const FilePath& file_path,
      base::DictionaryValue* properties,
      scoped_ptr<drive::DriveEntryProto> entry_proto) OVERRIDE;

  // Callback for SetPinState. Updates properties with error.
  void OnPinStateSet(const FilePath& path,
                     base::DictionaryValue* properties,
                     scoped_ptr<drive::DriveEntryProto> entry_proto,
                     drive::DriveFileError error);

  // True for pin, false for unpin.
  bool set_pin_;
};

// Get file locations for the given list of file URLs. Returns a list of
// location identifiers, like ['drive', 'local'], where 'drive' means the
// file is on gdata, and 'local' means the file is on the local drive.
class GetFileLocationsFunction : public FileBrowserFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("fileBrowserPrivate.getFileLocations");

  GetFileLocationsFunction();

 protected:
  virtual ~GetFileLocationsFunction();

  // AsyncExtensionFunction overrides.
  virtual bool RunImpl() OVERRIDE;
};

// Get gdata files for the given list of file URLs. Initiate downloading of
// gdata files if these are not cached. Return a list of local file names.
// This function puts empty strings instead of local paths for files could
// not be obtained. For instance, this can happen if the user specifies a new
// file name to save a file on gdata. There may be other reasons to fail. The
// file manager should check if the local paths returned from getDriveFiles()
// contain empty paths.
// TODO(satorux): Should we propagate error types to the JavasScript layer?
class GetDriveFilesFunction : public FileBrowserFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("fileBrowserPrivate.getDriveFiles");

  GetDriveFilesFunction();

 protected:
  virtual ~GetDriveFilesFunction();

  // AsyncExtensionFunction overrides.
  virtual bool RunImpl() OVERRIDE;

 private:
  // A callback method to handle the result of
  // GetLocalPathsOnFileThreadAndRunCallbackOnUIThread.
  void GetLocalPathsResponseOnUIThread(const SelectedFileInfoList& files);

  // Gets the file on the top of the |remaining_drive_paths_| or sends the
  // response if the queue is empty.
  void GetFileOrSendResponse();

  // Called by DriveFileSystem::GetFile(). Pops the file from
  // |remaining_drive_paths_|, and calls GetFileOrSendResponse().
  void OnFileReady(drive::DriveFileError error,
                   const FilePath& local_path,
                   const std::string& unused_mime_type,
                   drive::DriveFileType file_type);

  std::queue<FilePath> remaining_drive_paths_;
  ListValue* local_paths_;
};

// Implements the chrome.fileBrowserPrivate.executeTask method.
class GetFileTransfersFunction : public AsyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("fileBrowserPrivate.getFileTransfers");

  GetFileTransfersFunction();

 protected:
  virtual ~GetFileTransfersFunction();

  // AsyncExtensionFunction overrides.
  virtual bool RunImpl() OVERRIDE;

 private:
  ListValue* GetFileTransfersList();
};

// Implements the chrome.fileBrowserPrivate.cancelFileTransfers method.
class CancelFileTransfersFunction : public FileBrowserFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("fileBrowserPrivate.cancelFileTransfers");

  CancelFileTransfersFunction();

 protected:
  virtual ~CancelFileTransfersFunction();

  // AsyncExtensionFunction overrides.
  virtual bool RunImpl() OVERRIDE;
};

// Implements the chrome.fileBrowserPrivate.transferFile method.
class TransferFileFunction : public FileBrowserFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("fileBrowserPrivate.transferFile");

  TransferFileFunction();

 protected:
  virtual ~TransferFileFunction();

  // AsyncExtensionFunction overrides.
  virtual bool RunImpl() OVERRIDE;

 private:
  // Helper callback for handling response from DriveFileSystem::TransferFile().
  void OnTransferCompleted(drive::DriveFileError error);
};

// Read setting value.
class GetPreferencesFunction : public SyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("fileBrowserPrivate.getPreferences");

 protected:
  virtual ~GetPreferencesFunction() {}

  virtual bool RunImpl() OVERRIDE;
};

// Write setting value.
class SetPreferencesFunction : public SyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("fileBrowserPrivate.setPreferences");

 protected:
  virtual ~SetPreferencesFunction() {}

  virtual bool RunImpl() OVERRIDE;
};

class SearchDriveFunction : public AsyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("fileBrowserPrivate.searchDrive");

  SearchDriveFunction();

 protected:
  virtual ~SearchDriveFunction();

  virtual bool RunImpl() OVERRIDE;

 private:
  // Callback fo OpenFileSystem called from RunImpl.
  void OnFileSystemOpened(base::PlatformFileError result,
                          const std::string& file_system_name,
                          const GURL& file_system_url);
  // Callback for google_apis::SearchAsync called after file system is opened.
  void OnSearch(drive::DriveFileError error,
                const GURL& next_feed,
                scoped_ptr<std::vector<drive::SearchResultInfo> > result_paths);

  // Query for which the search is being performed.
  std::string query_;
  std::string next_feed_;
  bool shared_with_me_;
  // Information about remote file system we will need to create file entries
  // to represent search results.
  std::string file_system_name_;
  GURL file_system_url_;
};

class ClearDriveCacheFunction : public AsyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("fileBrowserPrivate.clearDriveCache");

 protected:
  virtual ~ClearDriveCacheFunction() {}

  virtual bool RunImpl() OVERRIDE;
};

// Implements the chrome.fileBrowserPrivate.reloadDrive method,
// which is used to reload the file system metadata from the server.
class ReloadDriveFunction: public AsyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("fileBrowserPrivate.reloadDrive");

 protected:
  virtual ~ReloadDriveFunction() {}

  virtual bool RunImpl() OVERRIDE;
};

// Implements the chrome.fileBrowserPrivate.getNetworkConnectionState method.
class GetNetworkConnectionStateFunction : public SyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME(
      "fileBrowserPrivate.getNetworkConnectionState");

 protected:
  virtual ~GetNetworkConnectionStateFunction() {}

  virtual bool RunImpl() OVERRIDE;
};

// Implements the chrome.fileBrowserPrivate.requestDirectoryRefresh method.
class RequestDirectoryRefreshFunction : public SyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME(
      "fileBrowserPrivate.requestDirectoryRefresh");

 protected:
  virtual ~RequestDirectoryRefreshFunction() {}

  virtual bool RunImpl() OVERRIDE;
};

// Create a zip file for the selected files.
class ZipSelectionFunction : public FileBrowserFunction,
                             public extensions::ZipFileCreator::Observer {
 public:
  DECLARE_EXTENSION_FUNCTION_NAME("fileBrowserPrivate.zipSelection");

  ZipSelectionFunction();

 protected:
  virtual ~ZipSelectionFunction();

  // AsyncExtensionFunction overrides.
  virtual bool RunImpl() OVERRIDE;

  // extensions::ZipFileCreator::Delegate overrides.
  virtual void OnZipDone(bool success) OVERRIDE;

 private:
  scoped_refptr<extensions::ZipFileCreator> zip_file_creator_;
};

#endif  // CHROME_BROWSER_CHROMEOS_EXTENSIONS_FILE_BROWSER_PRIVATE_API_H_
