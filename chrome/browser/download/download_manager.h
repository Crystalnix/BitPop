// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The DownloadManager object manages the process of downloading, including
// updates to the history system and providing the information for displaying
// the downloads view in the Destinations tab. There is one DownloadManager per
// active profile in Chrome.
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

#ifndef CHROME_BROWSER_DOWNLOAD_DOWNLOAD_MANAGER_H_
#define CHROME_BROWSER_DOWNLOAD_DOWNLOAD_MANAGER_H_
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/file_path.h"
#include "base/gtest_prod_util.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/scoped_ptr.h"
#include "base/time.h"
#include "chrome/browser/download/download_item.h"
#include "chrome/browser/download/download_status_updater_delegate.h"
#include "chrome/browser/download/download_process_handle.h"
#include "chrome/browser/ui/shell_dialogs.h"
#include "content/browser/browser_thread.h"

class DownloadFileManager;
class DownloadHistory;
class DownloadPrefs;
class DownloadStatusUpdater;
class GURL;
class Profile;
class ResourceDispatcherHost;
class TabContents;
struct DownloadCreateInfo;
struct DownloadHistoryInfo;
struct DownloadSaveInfo;

// Browser's download manager: manages all downloads and destination view.
class DownloadManager
    : public base::RefCountedThreadSafe<DownloadManager,
                                        BrowserThread::DeleteOnUIThread>,
      public DownloadStatusUpdaterDelegate,
      public SelectFileDialog::Listener {
 public:
  explicit DownloadManager(DownloadStatusUpdater* status_updater);

  // Shutdown the download manager. Must be called before destruction.
  void Shutdown();

  // Interface to implement for observers that wish to be informed of changes
  // to the DownloadManager's collection of downloads.
  class Observer {
   public:
    // New or deleted download, observers should query us for the current set
    // of downloads.
    virtual void ModelChanged() = 0;

    // Called when the DownloadManager is being destroyed to prevent Observers
    // from calling back to a stale pointer.
    virtual void ManagerGoingDown() {}

    // Called immediately after the DownloadManager puts up a select file
    // dialog.
    // |id| indicates which download opened the dialog.
    virtual void SelectFileDialogDisplayed(int32 id) {}

   protected:
    virtual ~Observer() {}
  };

  // Return all temporary downloads that reside in the specified directory.
  void GetTemporaryDownloads(const FilePath& dir_path,
                             std::vector<DownloadItem*>* result);

  // Return all non-temporary downloads in the specified directory that are
  // are in progress or have completed.
  void GetAllDownloads(const FilePath& dir_path,
                       std::vector<DownloadItem*>* result);

  // Return all non-temporary downloads in the specified directory that are
  // in-progress (including dangerous downloads waiting for user confirmation).
  void GetCurrentDownloads(const FilePath& dir_path,
                           std::vector<DownloadItem*>* result);

  // Returns all non-temporary downloads matching |query|. Empty query matches
  // everything.
  void SearchDownloads(const string16& query,
                       std::vector<DownloadItem*>* result);

  // Returns true if initialized properly.
  bool Init(Profile* profile);

  // Notifications sent from the download thread to the UI thread
  void StartDownload(int32 id);
  void UpdateDownload(int32 download_id, int64 size);
  // |hash| is sha256 hash for the downloaded file. It is empty when the hash
  // is not available.
  void OnResponseCompleted(int32 download_id, int64 size, int os_error,
                           const std::string& hash);

  // Called from a view when a user clicks a UI button or link.
  void DownloadCancelled(int32 download_id);
  void PauseDownload(int32 download_id, bool pause);
  void RemoveDownload(int64 download_handle);

  // Determine if the download is ready for completion, i.e. has had
  // all data saved, and completed the filename determination and
  // history insertion.
  bool IsDownloadReadyForCompletion(DownloadItem* download);

  // If all pre-requisites have been met, complete download processing, i.e.
  // do internal cleanup, file rename, and potentially auto-open.
  // (Dangerous downloads still may block on user acceptance after this
  // point.)
  void MaybeCompleteDownload(DownloadItem* download);

  // Called when the download is renamed to its final name.
  // |uniquifier| is a number used to make unique names for the file.  It is
  // only valid for the DANGEROUS_BUT_VALIDATED state of the download item.
  void OnDownloadRenamedToFinalName(int download_id,
                                    const FilePath& full_path,
                                    int uniquifier);

  // Remove downloads after remove_begin (inclusive) and before remove_end
  // (exclusive). You may pass in null Time values to do an unbounded delete
  // in either direction.
  int RemoveDownloadsBetween(const base::Time remove_begin,
                             const base::Time remove_end);

  // Remove downloads will delete all downloads that have a timestamp that is
  // the same or more recent than |remove_begin|. The number of downloads
  // deleted is returned back to the caller.
  int RemoveDownloads(const base::Time remove_begin);

  // Remove all downloads will delete all downloads. The number of downloads
  // deleted is returned back to the caller.
  int RemoveAllDownloads();

  // Final download manager transition for download: Update the download
  // history and remove the download from |active_downloads_|.
  void DownloadCompleted(int32 download_id);

  // Called when a Save Page As download is started. Transfers ownership
  // of |download_item| to the DownloadManager.
  void SavePageAsDownloadStarted(DownloadItem* download_item);

  // Download the object at the URL. Used in cases such as "Save Link As..."
  void DownloadUrl(const GURL& url,
                   const GURL& referrer,
                   const std::string& referrer_encoding,
                   TabContents* tab_contents);

  // Download the object at the URL and save it to the specified path. The
  // download is treated as the temporary download and thus will not appear
  // in the download history. Used in cases such as drag and drop.
  void DownloadUrlToFile(const GURL& url,
                         const GURL& referrer,
                         const std::string& referrer_encoding,
                         const DownloadSaveInfo& save_info,
                         TabContents* tab_contents);

  // Allow objects to observe the download creation process.
  void AddObserver(Observer* observer);

  // Remove a download observer from ourself.
  void RemoveObserver(Observer* observer);

  // Methods called on completion of a query sent to the history system.
  void OnQueryDownloadEntriesComplete(
      std::vector<DownloadHistoryInfo>* entries);
  void OnCreateDownloadEntryComplete(int32 download_id, int64 db_handle);

  // Display a new download in the appropriate browser UI.
  void ShowDownloadInBrowser(DownloadItem* download);

  // The number of in progress (including paused) downloads.
  int in_progress_count() const {
    return static_cast<int>(in_progress_.size());
  }

  Profile* profile() { return profile_; }

  DownloadPrefs* download_prefs() { return download_prefs_.get(); }

  // Creates the download item.  Must be called on the UI thread.
  void CreateDownloadItem(DownloadCreateInfo* info);

  // Clears the last download path, used to initialize "save as" dialogs.
  void ClearLastDownloadPath();

  // Tests if a file type should be opened automatically.
  bool ShouldOpenFileBasedOnExtension(const FilePath& path) const;

  // Overridden from DownloadStatusUpdaterDelegate:
  virtual bool IsDownloadProgressKnown();
  virtual int64 GetInProgressDownloadCount();
  virtual int64 GetReceivedDownloadBytes();
  virtual int64 GetTotalDownloadBytes();

  // Overridden from SelectFileDialog::Listener:
  virtual void FileSelected(const FilePath& path, int index, void* params);
  virtual void FileSelectionCanceled(void* params);

  // Returns true if this download should show the "dangerous file" warning.
  // Various factors are considered, such as the type of the file, whether a
  // user action initiated the download, and whether the user has explicitly
  // marked the file type as "auto open".
  bool IsDangerous(const DownloadItem& download,
                   const DownloadStateInfo& state,
                   bool visited_referrer_before);

  // Called when the user has validated the download of a dangerous file.
  void DangerousDownloadValidated(DownloadItem* download);

  // Callback function after url is checked with safebrowsing service.
  void CheckDownloadUrlDone(int32 download_id, bool is_dangerous_url);

  // Callback function after we check whether the referrer URL has been visited
  // before today.
  void CheckVisitedReferrerBeforeDone(int32 download_id,
                                      bool visited_referrer_before);

  // Callback function after download file hash is checked with safebrowsing
  // service.
  void CheckDownloadHashDone(int32 download_id, bool is_dangerous_hash);

 private:
  // For testing.
  friend class DownloadManagerTest;
  friend class MockDownloadManager;

  // This class is used to let an incognito DownloadManager observe changes to
  // a normal DownloadManager, to propagate ModelChanged() calls from the parent
  // DownloadManager to the observers of the incognito DownloadManager.
  class OtherDownloadManagerObserver : public Observer {
   public:
    explicit OtherDownloadManagerObserver(
        DownloadManager* observing_download_manager);
    virtual ~OtherDownloadManagerObserver();

    // Observer interface.
    virtual void ModelChanged();
    virtual void ManagerGoingDown();

   private:
    // The incognito download manager.
    DownloadManager* observing_download_manager_;

    // The original profile's download manager.
    DownloadManager* observed_download_manager_;
  };

  friend struct BrowserThread::DeleteOnThread<BrowserThread::UI>;
  friend class DeleteTask<DownloadManager>;
  friend class OtherDownloadManagerObserver;

  virtual ~DownloadManager();

  // Called on the download thread to check whether the suggested file path
  // exists.  We don't check if the file exists on the UI thread to avoid UI
  // stalls from interacting with the file system.
  void CheckIfSuggestedPathExists(int32 download_id,
                                  DownloadStateInfo state,
                                  const FilePath& default_path);

  // Called on the UI thread once the DownloadManager has determined whether the
  // suggested file path exists.
  void OnPathExistenceAvailable(int32 download_id,
                                DownloadStateInfo new_state);

  // Called back after a target path for the file to be downloaded to has been
  // determined, either automatically based on the suggested file name, or by
  // the user in a Save As dialog box.
  void ContinueDownloadWithPath(DownloadItem* download,
                                const FilePath& chosen_file);

  // Download cancel helper function.
  // |process_handle| is passed by value because it is ultimately passed to
  // other threads, and this way we don't have to worry about object lifetimes.
  void DownloadCancelledInternal(int download_id,
                                 DownloadProcessHandle process_handle);

  // All data has been downloaded.
  // |hash| is sha256 hash for the downloaded file. It is empty when the hash
  // is not available.
  void OnAllDataSaved(int32 download_id, int64 size, const std::string& hash);

  // An error occurred in the download.
  void OnDownloadError(int32 download_id, int64 size, int os_error);

  // Updates the app icon about the overall download progress.
  void UpdateAppIcon();

  // Makes the ResourceDispatcherHost pause/un-pause a download request.
  // Called on the IO thread.
  // |process_handle| is passed by value because this is called from other
  // threads, and this way we don't have to worry about object lifetimes.
  void PauseDownloadRequest(ResourceDispatcherHost* rdh,
                            DownloadProcessHandle process_handle,
                            bool pause);

  // Inform observers that the model has changed.
  void NotifyModelChanged();

  // Get the download item from the history map.  Useful after the item has
  // been removed from the active map, or was retrieved from the history DB.
  DownloadItem* GetDownloadItem(int id);

  // Get the download item from the active map.  Useful when the item is not
  // yet in the history map.
  DownloadItem* GetActiveDownloadItem(int id);

  // Debugging routine to confirm relationship between below
  // containers; no-op if NDEBUG.
  void AssertContainersConsistent() const;

  // |downloads_| is the owning set for all downloads known to the
  // DownloadManager.  This includes downloads started by the user in
  // this session, downloads initialized from the history system, and
  // "save page as" downloads.  All other DownloadItem containers in
  // the DownloadManager are maps; they do not own the DownloadItems.
  // Note that this is the only place (with any functional implications;
  // see save_page_as_downloads_ below) that "save page as" downloads are
  // kept, as the DownloadManager's only job is to hold onto those
  // until destruction.
  //
  // |history_downloads_| is map of all downloads in this profile. The key
  // is the handle returned by the history system, which is unique
  // across sessions.
  //
  // |active_downloads_| is a map of all downloads that are currently being
  // processed. The key is the ID assigned by the ResourceDispatcherHost,
  // which is unique for the current session.
  //
  // |in_progress_| is a map of all downloads that are in progress and that have
  // not yet received a valid history handle. The key is the ID assigned by the
  // ResourceDispatcherHost, which is unique for the current session.
  //
  // |save_page_as_downloads_| (if defined) is a collection of all the
  // downloads the "save page as" system has given to us to hold onto
  // until we are destroyed.  It is only used for debugging.
  //
  // When a download is created through a user action, the corresponding
  // DownloadItem* is placed in |active_downloads_| and remains there until the
  // download is in a terminal state (COMPLETE or CANCELLED).  It is also
  // placed in |in_progress_| and remains there until it has received a
  // valid handle from the history system. Once it has a valid handle, the
  // DownloadItem* is placed in the |history_downloads_| map.  When the
  // download reaches a terminal state, it is removed from |in_progress_|.
  // Downloads from past sessions read from a persisted state from the
  // history system are placed directly into |history_downloads_| since
  // they have valid handles in the history system.
  typedef std::set<DownloadItem*> DownloadSet;
  typedef base::hash_map<int64, DownloadItem*> DownloadMap;

  DownloadSet downloads_;
  DownloadMap history_downloads_;
  DownloadMap in_progress_;
  DownloadMap active_downloads_;
#if !defined(NDEBUG)
  DownloadSet save_page_as_downloads_;
#endif

  // True if the download manager has been initialized and requires a shutdown.
  bool shutdown_needed_;

  // Observers that want to be notified of changes to the set of downloads.
  ObserverList<Observer> observers_;

  // The current active profile.
  Profile* profile_;

  scoped_ptr<DownloadHistory> download_history_;

  scoped_ptr<DownloadPrefs> download_prefs_;

  // Non-owning pointer for handling file writing on the download_thread_.
  DownloadFileManager* file_manager_;

  // Non-owning pointer for updating the download status.
  base::WeakPtr<DownloadStatusUpdater> status_updater_;

  // The user's last choice for download directory. This is only used when the
  // user wants us to prompt for a save location for each download.
  FilePath last_download_path_;

  // The "Save As" dialog box used to ask the user where a file should be
  // saved.
  scoped_refptr<SelectFileDialog> select_file_dialog_;

  scoped_ptr<OtherDownloadManagerObserver> other_download_manager_observer_;

  DISALLOW_COPY_AND_ASSIGN(DownloadManager);
};

#endif  // CHROME_BROWSER_DOWNLOAD_DOWNLOAD_MANAGER_H_
