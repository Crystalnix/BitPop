// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_GDATA_GDATA_DOWNLOAD_OBSERVER_H_
#define CHROME_BROWSER_CHROMEOS_GDATA_GDATA_DOWNLOAD_OBSERVER_H_

#include <map>

#include "base/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/platform_file.h"
#include "content/public/browser/download_item.h"
#include "content/public/browser/download_manager.h"
#include "chrome/browser/chromeos/gdata/gdata_errorcode.h"

class Profile;

namespace gdata {

class DocumentEntry;
class GDataEntryProto;
class GDataFileSystemInterface;
class GDataUploader;
struct UploadFileInfo;

// Observes downloads to temporary local gdata folder. Schedules these
// downloads for upload to gdata service.
class GDataDownloadObserver : public content::DownloadManager::Observer,
                              public content::DownloadItem::Observer {
 public:
  GDataDownloadObserver(GDataUploader* uploader,
                        GDataFileSystemInterface* file_system);
  virtual ~GDataDownloadObserver();

  // Become an observer of  DownloadManager.
  void Initialize(content::DownloadManager* download_manager,
                  const FilePath& gdata_tmp_download_path);

  typedef base::Callback<void(const FilePath&)>
    SubstituteGDataDownloadPathCallback;
  static void SubstituteGDataDownloadPath(Profile* profile,
      const FilePath& gdata_path, content::DownloadItem* download,
      const SubstituteGDataDownloadPathCallback& callback);

  // Sets gdata path, for example, '/special/drive/MyFolder/MyFile',
  // to external data in |download|. Also sets display name and
  // makes |download| a temporary.
  static void SetDownloadParams(const FilePath& gdata_path,
                                content::DownloadItem* download);

  // Gets the gdata_path from external data in |download|.
  // GetGDataPath may return an empty path in case SetGDataPath was not
  // previously called or there was some other internal error
  // (there is a DCHECK for this).
  static FilePath GetGDataPath(content::DownloadItem* download);

  // Checks if there is a GData upload associated with |download|
  static bool IsGDataDownload(content::DownloadItem* download);

  // Checks if |download| is ready to complete. Returns true if |download| has
  // no GData upload associated with it or if the GData upload has already
  // completed. This method is called by the ChromeDownloadManagerDelegate to
  // check if the download is ready to complete.  If the download is not yet
  // ready to complete and |complete_callback| is not null, then
  // |complete_callback| will be called on the UI thread when the download
  // becomes ready to complete.  If this method is called multiple times with
  // the download not ready to complete, only the last |complete_callback|
  // passed to this method for |download| will be called.
  static bool IsReadyToComplete(
      content::DownloadItem* download,
      const base::Closure& complete_callback);

  // Returns the count of bytes confirmed as uploaded so far for |download|.
  static int64 GetUploadedBytes(content::DownloadItem* download);

  // Returns the progress of the upload of |download| as a percentage. If the
  // progress is unknown, returns -1.
  static int PercentComplete(content::DownloadItem* download);

  // Create a temporary file |gdata_tmp_download_path| in
  // |gdata_tmp_download_dir|. Must be called on a thread that allows file
  // operations.
  static void GetGDataTempDownloadPath(const FilePath& gdata_tmp_download_dir,
                                       FilePath* gdata_tmp_download_path);

 private:
  // DownloadManager overrides.
  virtual void ManagerGoingDown(content::DownloadManager* manager) OVERRIDE;
  virtual void ModelChanged(content::DownloadManager* manager) OVERRIDE;

  // DownloadItem overrides.
  virtual void OnDownloadUpdated(content::DownloadItem* download) OVERRIDE;
  virtual void OnDownloadOpened(content::DownloadItem* download) OVERRIDE {}

  // Adds/Removes |download| to pending_downloads_.
  // Also start/stop observing |download|.
  void AddPendingDownload(content::DownloadItem* download);
  void RemovePendingDownload(content::DownloadItem* download);

  // Remove our external data and remove our observers from |download|
  void DetachFromDownload(content::DownloadItem* download);

  // Starts the upload of a downloaded/downloading file.
  void UploadDownloadItem(content::DownloadItem* download);

  // Updates metadata of ongoing upload if it exists.
  void UpdateUpload(content::DownloadItem* download);

  // Checks if this DownloadItem should be uploaded.
  bool ShouldUpload(content::DownloadItem* download);

  // Creates UploadFileInfo and initializes it using DownloadItem*.
  void CreateUploadFileInfo(content::DownloadItem* download);

  // Callback for handling results of GDataFileSystem::GetEntryInfoByPath()
  // initiated by CreateUploadFileInfo(). This callback reads the directory
  // entry to determine the upload path, then calls StartUpload() to actually
  // start the upload.
  void OnGetEntryInfoByPath(
      int32 download_id,
      scoped_ptr<UploadFileInfo> upload_file_info,
      GDataFileError error,
      scoped_ptr<GDataEntryProto> entry_proto);

  // Starts the upload.
  void StartUpload(int32 download_id,
                   scoped_ptr<UploadFileInfo> upload_file_info);

  // Callback invoked by GDataUploader when the upload associated with
  // |download_id| has completed. |error| indicated whether the
  // call was successful. This function takes ownership of DocumentEntry from
  // |upload_file_info| for use by MoveFileToGDataCache(). It also invokes the
  // MaybeCompleteDownload() method on the DownloadItem to allow it to complete.
  void OnUploadComplete(int32 download_id,
                        GDataFileError error,
                        scoped_ptr<UploadFileInfo> upload_file_info);

  // Moves the downloaded file to gdata cache.
  // Must be called after GDataDownloadObserver receives COMPLETE notification.
  void MoveFileToGDataCache(content::DownloadItem* download);

  // Private data.
  // The uploader owned by GDataSystemService. Used to trigger file uploads.
  GDataUploader* gdata_uploader_;
  // The file system owned by GDataSystemService.
  GDataFileSystemInterface* file_system_;
  // Observe the DownloadManager for new downloads.
  content::DownloadManager* download_manager_;

  // Temporary download location directory.
  FilePath gdata_tmp_download_path_;

  // Map of pending downloads.
  typedef std::map<int32, content::DownloadItem*> DownloadMap;
  DownloadMap pending_downloads_;

  // Note: This should remain the last member so it'll be destroyed and
  // invalidate its weak pointers before any other members are destroyed.
  base::WeakPtrFactory<GDataDownloadObserver> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(GDataDownloadObserver);
};

}  // namespace gdata

#endif  // CHROME_BROWSER_CHROMEOS_GDATA_GDATA_DOWNLOAD_OBSERVER_H_
