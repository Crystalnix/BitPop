// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The DownloadManager object manages the process of downloading, including
// updates to the history system and providing the information for displaying
// the downloads view in the Destinations tab. There is one DownloadManager per
// active browser context in Chrome.
//
// Download observers:
// Objects that are interested in notifications about new downloads, or progress
// updates for a given download must implement one of the download observer
// interfaces:
//   DownloadManager::Observer:
//     - allows observers, primarily views, to be notified when changes to the
//       set of all downloads (such as new downloads, or deletes) occur
// Use AddObserver() / RemoveObserver() on the appropriate download object to
// receive state updates.
//
// Download state persistence:
// The DownloadManager uses the history service for storing persistent
// information about the state of all downloads. The history system maintains a
// separate table for this called 'downloads'. At the point that the
// DownloadManager is constructed, we query the history service for the state of
// all persisted downloads.

#ifndef CONTENT_PUBLIC_BROWSER_DOWNLOAD_MANAGER_H_
#define CONTENT_PUBLIC_BROWSER_DOWNLOAD_MANAGER_H_

#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/callback.h"
#include "base/file_path.h"
#include "base/gtest_prod_util.h"
#include "base/sequenced_task_runner_helpers.h"
#include "base/time.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/download_id.h"
#include "content/public/browser/download_interrupt_reasons.h"
#include "content/public/browser/download_item.h"
#include "net/base/net_errors.h"
#include "net/base/net_log.h"

class DownloadRequestHandle;
class GURL;
struct DownloadCreateInfo;
struct DownloadRetrieveInfo;

namespace content {

class BrowserContext;
class ByteStreamReader;
class DownloadManagerDelegate;
class DownloadQuery;
class DownloadUrlParameters;

// Browser's download manager: manages all downloads and destination view.
class CONTENT_EXPORT DownloadManager
    : public base::RefCountedThreadSafe<DownloadManager> {
 public:
  // A method that can be used in tests to ensure that all the internal download
  // classes have no pending downloads.
  static bool EnsureNoPendingDownloadsForTesting();

  // Sets/Gets the delegate for this DownloadManager. The delegate has to live
  // past its Shutdown method being called (by the DownloadManager).
  virtual void SetDelegate(DownloadManagerDelegate* delegate) = 0;
  virtual DownloadManagerDelegate* GetDelegate() const = 0;

  // Shutdown the download manager. Content calls this when BrowserContext is
  // being destructed. If the embedder needs this to be called earlier, it can
  // call it. In that case, the delegate's Shutdown() method will only be called
  // once.
  virtual void Shutdown() = 0;

  // Interface to implement for observers that wish to be informed of changes
  // to the DownloadManager's collection of downloads.
  class CONTENT_EXPORT Observer {
   public:
    // A DownloadItem was created.  Unlike ModelChanged, this item may be
    // visible before the filename is determined; in this case the return value
    // of GetTargetFileName() will be null.  This method may be called an
    // arbitrary number of times, e.g. when loading history on startup.  As a
    // result, consumers should avoid doing large amounts of work in
    // OnDownloadCreated().  TODO(<whoever>): When we've fully specified the
    // possible states of the DownloadItem in download_item.h and removed
    // ModelChanged, we should remove the caveat above.
    virtual void OnDownloadCreated(
        DownloadManager* manager, DownloadItem* item) {}

    // New or deleted download, observers should query us for the current set
    // of downloads.
    virtual void ModelChanged(DownloadManager* manager) {}

    // Called when the DownloadManager is being destroyed to prevent Observers
    // from calling back to a stale pointer.
    virtual void ManagerGoingDown(DownloadManager* manager) {}

   protected:
    virtual ~Observer() {}
  };

  typedef std::vector<DownloadItem*> DownloadVector;

  // If |dir_path| is empty, appends all temporary downloads to |*result|.
  // Otherwise, appends all temporary downloads that reside in |dir_path| to
  // |*result|.
  virtual void GetTemporaryDownloads(const FilePath& dir_path,
                                     DownloadVector* result) = 0;

  // If |dir_path| is empty, appends all non-temporary downloads to |*result|.
  // Otherwise, appends all non-temporary downloads that reside in |dir_path|
  // to |*result|.
  virtual void GetAllDownloads(const FilePath& dir_path,
                               DownloadVector* result) = 0;

  // Returns all non-temporary downloads matching |query|. Empty query matches
  // everything.
  virtual void SearchDownloads(const string16& query,
                               DownloadVector* result) = 0;

  // Returns true if initialized properly.
  virtual bool Init(BrowserContext* browser_context) = 0;

  // Called by a download source (Currently DownloadResourceHandler)
  // to initiate the non-source portions of a download.
  // Returns the id assigned to the download.  If the DownloadCreateInfo
  // specifies an id, that id will be used.
  virtual DownloadId StartDownload(
      scoped_ptr<DownloadCreateInfo> info,
      scoped_ptr<ByteStreamReader> stream) = 0;

  // Notifications sent from the download thread to the UI thread
  virtual void UpdateDownload(int32 download_id,
                              int64 bytes_so_far,
                              int64 bytes_per_sec,
                              const std::string& hash_state) = 0;

  // |download_id| is the ID of the download.
  // |size| is the number of bytes that have been downloaded.
  // |hash| is sha256 hash for the downloaded file. It is empty when the hash
  // is not available.
  virtual void OnResponseCompleted(int32 download_id, int64 size,
                           const std::string& hash) = 0;

  // Offthread target for cancelling a particular download.  Will be a no-op
  // if the download has already been cancelled.
  virtual void CancelDownload(int32 download_id) = 0;

  // Called when there is an error in the download.
  // |download_id| is the ID of the download.
  // |size| is the number of bytes that are currently downloaded.
  // |hash_state| is the current state of the hash of the data that has been
  // downloaded.
  // |reason| is a download interrupt reason code.
  virtual void OnDownloadInterrupted(
      int32 download_id,
      DownloadInterruptReason reason) = 0;

  // Remove downloads after remove_begin (inclusive) and before remove_end
  // (exclusive). You may pass in null Time values to do an unbounded delete
  // in either direction.
  virtual int RemoveDownloadsBetween(base::Time remove_begin,
                                     base::Time remove_end) = 0;

  // Remove downloads will delete all downloads that have a timestamp that is
  // the same or more recent than |remove_begin|. The number of downloads
  // deleted is returned back to the caller.
  virtual int RemoveDownloads(base::Time remove_begin) = 0;

  // Remove all downloads will delete all downloads. The number of downloads
  // deleted is returned back to the caller.
  virtual int RemoveAllDownloads() = 0;

  // See DownloadUrlParameters for details about controlling the download.
  virtual void DownloadUrl(scoped_ptr<DownloadUrlParameters> parameters) = 0;

  // Allow objects to observe the download creation process.
  virtual void AddObserver(Observer* observer) = 0;

  // Remove a download observer from ourself.
  virtual void RemoveObserver(Observer* observer) = 0;

  // Called by the embedder, after creating the download manager, to let it know
  // about downloads from previous runs of the browser.
  virtual void OnPersistentStoreQueryComplete(
      std::vector<DownloadPersistentStoreInfo>* entries) = 0;

  // Called by the embedder, in response to
  // DownloadManagerDelegate::AddItemToPersistentStore.
  virtual void OnItemAddedToPersistentStore(int32 download_id,
                                            int64 db_handle) = 0;

  // The number of in progress (including paused) downloads.
  virtual int InProgressCount() const = 0;

  virtual BrowserContext* GetBrowserContext() const = 0;

  // Checks whether downloaded files still exist. Updates state of downloads
  // that refer to removed files. The check runs in the background and may
  // finish asynchronously after this method returns.
  virtual void CheckForHistoryFilesRemoval() = 0;

  // Get the download item from the history map.  Useful after the item has
  // been removed from the active map, or was retrieved from the history DB.
  virtual DownloadItem* GetDownloadItem(int id) = 0;

  // Get the download item for |id| if present, no matter what type of download
  // it is or state it's in.
  virtual DownloadItem* GetDownload(int id) = 0;

  // Called when Save Page download is done.
  virtual void SavePageDownloadFinished(DownloadItem* download) = 0;

  // Get the download item from the active map.  Useful when the item is not
  // yet in the history map.
  virtual DownloadItem* GetActiveDownloadItem(int id) = 0;

  virtual bool GenerateFileHash() = 0;

 protected:
  virtual ~DownloadManager() {}

 private:
  friend class base::RefCountedThreadSafe<DownloadManager>;
};

}  // namespace content

#endif  // CONTENT_PUBLIC_BROWSER_DOWNLOAD_MANAGER_H_
