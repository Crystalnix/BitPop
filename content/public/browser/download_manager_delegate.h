// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_BROWSER_DOWNLOAD_MANAGER_DELEGATE_H_
#define CONTENT_PUBLIC_BROWSER_DOWNLOAD_MANAGER_DELEGATE_H_
#pragma once

#include "base/basictypes.h"
#include "base/callback.h"
#include "base/file_path.h"
#include "base/time.h"
#include "content/common/content_export.h"
#include "content/public/browser/save_page_type.h"

namespace content {

class DownloadId;
class DownloadItem;
class WebContents;

typedef base::Callback<void(const FilePath&, content::SavePageType)>
    SaveFilePathPickedCallback;

// Browser's download manager: manages all downloads and destination view.
class CONTENT_EXPORT DownloadManagerDelegate {
 public:
  virtual ~DownloadManagerDelegate() {}

  // Lets the delegate know that the download manager is shutting down.
  virtual void Shutdown() {}

  // Returns a new DownloadId.
  virtual DownloadId GetNextId();

  // Notifies the delegate that a download is starting. The delegate can return
  // false to delay the start of the download, in which case it should call
  // DownloadManager::RestartDownload when it's ready.
  virtual bool ShouldStartDownload(int32 download_id);

  // Asks the user for the path for a download. The delegate calls
  // DownloadManager::FileSelected or DownloadManager::FileSelectionCanceled to
  // give the answer.
  virtual void ChooseDownloadPath(WebContents* web_contents,
                                  const FilePath& suggested_path,
                                  void* data) {}

  // Allows the embedder to set an intermediate name for the download until it's
  // complete. If the embedder doesn't want this return the suggested path.
  virtual FilePath GetIntermediatePath(const FilePath& suggested_path);

  // Called when the download system wants to alert a WebContents that a
  // download has started, but the TabConetnts has gone away. This lets an
  // delegate return an alternative WebContents. The delegate can return NULL.
  virtual WebContents* GetAlternativeWebContentsToNotifyForDownload();

  // Tests if a file type should be opened automatically.
  virtual bool ShouldOpenFileBasedOnExtension(const FilePath& path);

  // Allows the delegate to override completion of the download.  If this
  // function returns false, the download completion is delayed and the
  // delegate is responsible for making sure that
  // DownloadItem::MaybeCompleteDownload is called at some point in the
  // future.  Note that at that point this function will be called again,
  // and is responsible for returning true when it really is ok for the
  // download to complete.
  virtual bool ShouldCompleteDownload(DownloadItem* item);

  // Allows the delegate to override opening the download. If this function
  // returns false, the delegate needs to call
  // DownloadItem::DelayedDownloadOpened when it's done with the item,
  // and is responsible for opening it.  This function is called
  // after the final rename, but before the download state is set to COMPLETED.
  virtual bool ShouldOpenDownload(DownloadItem* item);

  // Returns true if we need to generate a binary hash for downloads.
  virtual bool GenerateFileHash();

  // Notifies the delegate that a new download item is created. The
  // DownloadManager waits for the delegate to add information about this
  // download to its persistent store. When the delegate is done, it calls
  // DownloadManager::OnDownloadItemAddedToPersistentStore.
  virtual void AddItemToPersistentStore(DownloadItem* item) {}

  // Notifies the delegate that information about the given download has change,
  // so that it can update its persistent store.
  // Does not update |url|, |start_time|, |total_bytes|; uses |db_handle| only
  // to select the row in the database table to update.
  virtual void UpdateItemInPersistentStore(DownloadItem* item) {}

  // Notifies the delegate that path for the download item has changed, so that
  // it can update its persistent store.
  virtual void UpdatePathForItemInPersistentStore(
      DownloadItem* item,
      const FilePath& new_path) {}

  // Notifies the delegate that it should remove the download item from its
  // persistent store.
  virtual void RemoveItemFromPersistentStore(DownloadItem* item) {}

  // Notifies the delegate to remove downloads from the given time range.
  virtual void RemoveItemsFromPersistentStoreBetween(
      base::Time remove_begin,
      base::Time remove_end) {}

  // Retrieve the directories to save html pages and downloads to.
  virtual void GetSaveDir(WebContents* web_contents,
                          FilePath* website_save_dir,
                          FilePath* download_save_dir) {}

  // Asks the user for the path to save a page. The delegate calls the callback
  // to give the answer.
  virtual void ChooseSavePath(WebContents* web_contents,
                              const FilePath& suggested_path,
                              const FilePath::StringType& default_extension,
                              bool can_save_as_complete,
                              SaveFilePathPickedCallback callback) {}

  // Informs the delegate that the progress of downloads has changed.
  virtual void DownloadProgressUpdated() {}
};

}  // namespace content

#endif  // CONTENT_PUBLIC_BROWSER_DOWNLOAD_MANAGER_DELEGATE_H_
