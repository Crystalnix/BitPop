// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_DOWNLOAD_DOWNLOAD_ITEM_IMPL_H_
#define CONTENT_BROWSER_DOWNLOAD_DOWNLOAD_ITEM_IMPL_H_

#include <string>

#include "base/basictypes.h"
#include "base/file_path.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/time.h"
#include "base/timer.h"
#include "content/browser/download/download_net_log_parameters.h"
#include "content/browser/download/download_request_handle.h"
#include "content/common/content_export.h"
#include "content/public/browser/download_id.h"
#include "content/public/browser/download_item.h"
#include "googleurl/src/gurl.h"
#include "net/base/net_errors.h"
#include "net/base/net_log.h"

class DownloadItemImplDelegate;

// See download_item.h for usage.
class CONTENT_EXPORT DownloadItemImpl : public content::DownloadItem {
 public:
  // Note that it is the responsibility of the caller to ensure that a
  // DownloadItemImplDelegate passed to a DownloadItemImpl constructor
  // outlives the DownloadItemImpl.

  // Constructing from persistent store:
  // |bound_net_log| is constructed externally for our use.
  DownloadItemImpl(DownloadItemImplDelegate* delegate,
                   content::DownloadId download_id,
                   const content::DownloadPersistentStoreInfo& info,
                   const net::BoundNetLog& bound_net_log);

  // Constructing for a regular download.
  // |bound_net_log| is constructed externally for our use.
  DownloadItemImpl(DownloadItemImplDelegate* delegate,
                   const DownloadCreateInfo& info,
                   scoped_ptr<DownloadRequestHandleInterface> request_handle,
                   bool is_otr,
                   const net::BoundNetLog& bound_net_log);

  // Constructing for the "Save Page As..." feature:
  // |bound_net_log| is constructed externally for our use.
  DownloadItemImpl(DownloadItemImplDelegate* delegate,
                   const FilePath& path,
                   const GURL& url,
                   bool is_otr,
                   content::DownloadId download_id,
                   const std::string& mime_type,
                   const net::BoundNetLog& bound_net_log);

  virtual ~DownloadItemImpl();

  // Implementation functions (not part of the DownloadItem interface).

  // Called when the target path has been determined. |target_path| is the
  // suggested target path. |disposition| indicates how the target path should
  // be used (see TargetDisposition). |danger_type| is the danger level of
  // |target_path| as determined by the caller. |intermediate_path| is the path
  // to use to store the download until OnDownloadCompleting() is called.
  virtual void OnDownloadTargetDetermined(
      const FilePath& target_path,
      TargetDisposition disposition,
      content::DownloadDangerType danger_type,
      const FilePath& intermediate_path);

  // Indicate that an error has occurred on the download.
  virtual void Interrupt(content::DownloadInterruptReason reason);

  // Mark the item as having been persisted.
  virtual void SetIsPersisted();

  // Set the item's DB handle.
  virtual void SetDbHandle(int64 handle);

  // Cancels the off-thread aspects of the download.
  // TODO(rdsmith): This should be private and only called from
  // DownloadItem::Cancel/Interrupt; it isn't now because we can't
  // call those functions from
  // DownloadManager::FileSelectionCancelled() without doing some
  // rewrites of the DownloadManager queues.
  virtual void OffThreadCancel();

  // Called when the downloaded file is removed.
  virtual void OnDownloadedFileRemoved();

  // Called when the download is ready to complete.
  // This may perform final rename if necessary and will eventually call
  // DownloadItem::Completed().
  virtual void OnDownloadCompleting();

  // Called periodically from the download thread, or from the UI thread
  // for saving packages.
  // |bytes_so_far| is the number of bytes received so far.
  // |hash_state| is the current hash state.
  virtual void UpdateProgress(int64 bytes_so_far,
                              int64 bytes_per_sec,
                              const std::string& hash_state);

  // Called by SavePackage to display progress when the DownloadItem
  // should be considered complete.
  virtual void MarkAsComplete();

  // Called when all data has been saved. Only has display effects.
  virtual void OnAllDataSaved(int64 size, const std::string& final_hash);

  // Called by SavePackage to set the total number of bytes on the item.
  virtual void SetTotalBytes(int64 total_bytes);

  // Overridden from DownloadItem.
  virtual void AddObserver(DownloadItem::Observer* observer) OVERRIDE;
  virtual void RemoveObserver(DownloadItem::Observer* observer) OVERRIDE;
  virtual void UpdateObservers() OVERRIDE;
  virtual bool CanShowInFolder() OVERRIDE;
  virtual bool CanOpenDownload() OVERRIDE;
  virtual bool ShouldOpenFileBasedOnExtension() OVERRIDE;
  virtual void OpenDownload() OVERRIDE;
  virtual void ShowDownloadInShell() OVERRIDE;
  virtual void DangerousDownloadValidated() OVERRIDE;
  virtual void Cancel(bool user_cancel) OVERRIDE;
  virtual void DelayedDownloadOpened(bool auto_opened) OVERRIDE;
  virtual void Delete(DeleteReason reason) OVERRIDE;
  virtual void Remove() OVERRIDE;
  virtual bool TimeRemaining(base::TimeDelta* remaining) const OVERRIDE;
  virtual int64 CurrentSpeed() const OVERRIDE;
  virtual int PercentComplete() const OVERRIDE;
  virtual bool AllDataSaved() const OVERRIDE;
  virtual void TogglePause() OVERRIDE;
  virtual bool MatchesQuery(const string16& query) const OVERRIDE;
  virtual bool IsPartialDownload() const OVERRIDE;
  virtual bool IsInProgress() const OVERRIDE;
  virtual bool IsCancelled() const OVERRIDE;
  virtual bool IsInterrupted() const OVERRIDE;
  virtual bool IsComplete() const OVERRIDE;
  virtual DownloadState GetState() const OVERRIDE;
  virtual const FilePath& GetFullPath() const OVERRIDE;
  virtual const FilePath& GetTargetFilePath() const OVERRIDE;
  virtual TargetDisposition GetTargetDisposition() const OVERRIDE;
  virtual void OnContentCheckCompleted(
      content::DownloadDangerType danger_type) OVERRIDE;
  virtual const GURL& GetURL() const OVERRIDE;
  virtual const std::vector<GURL>& GetUrlChain() const OVERRIDE;
  virtual const GURL& GetOriginalUrl() const OVERRIDE;
  virtual const GURL& GetReferrerUrl() const OVERRIDE;
  virtual std::string GetSuggestedFilename() const OVERRIDE;
  virtual std::string GetContentDisposition() const OVERRIDE;
  virtual std::string GetMimeType() const OVERRIDE;
  virtual std::string GetOriginalMimeType() const OVERRIDE;
  virtual std::string GetReferrerCharset() const OVERRIDE;
  virtual std::string GetRemoteAddress() const OVERRIDE;
  virtual int64 GetTotalBytes() const OVERRIDE;
  virtual const std::string& GetHash() const OVERRIDE;
  virtual int64 GetReceivedBytes() const OVERRIDE;
  virtual const std::string& GetHashState() const OVERRIDE;
  virtual int32 GetId() const OVERRIDE;
  virtual content::DownloadId GetGlobalId() const OVERRIDE;
  virtual base::Time GetStartTime() const OVERRIDE;
  virtual base::Time GetEndTime() const OVERRIDE;
  virtual bool IsPersisted() const OVERRIDE;
  virtual int64 GetDbHandle() const OVERRIDE;
  virtual bool IsPaused() const OVERRIDE;
  virtual bool GetOpenWhenComplete() const OVERRIDE;
  virtual void SetOpenWhenComplete(bool open) OVERRIDE;
  virtual bool GetFileExternallyRemoved() const OVERRIDE;
  virtual SafetyState GetSafetyState() const OVERRIDE;
  virtual content::DownloadDangerType GetDangerType() const OVERRIDE;
  virtual bool IsDangerous() const OVERRIDE;
  virtual bool GetAutoOpened() OVERRIDE;
  virtual FilePath GetTargetName() const OVERRIDE;
  virtual const FilePath& GetForcedFilePath() const OVERRIDE;
  virtual bool HasUserGesture() const OVERRIDE;
  virtual content::PageTransition GetTransitionType() const OVERRIDE;
  virtual bool IsOtr() const OVERRIDE;
  virtual bool IsTemporary() const OVERRIDE;
  virtual void SetIsTemporary(bool temporary) OVERRIDE;
  virtual void SetOpened(bool opened) OVERRIDE;
  virtual bool GetOpened() const OVERRIDE;
  virtual const std::string& GetLastModifiedTime() const OVERRIDE;
  virtual const std::string& GetETag() const OVERRIDE;
  virtual content::DownloadInterruptReason GetLastReason() const OVERRIDE;
  virtual content::DownloadPersistentStoreInfo
      GetPersistentStoreInfo() const OVERRIDE;
  virtual content::BrowserContext* GetBrowserContext() const OVERRIDE;
  virtual content::WebContents* GetWebContents() const OVERRIDE;
  virtual FilePath GetFileNameToReportUser() const OVERRIDE;
  virtual void SetDisplayName(const FilePath& name) OVERRIDE;
  virtual FilePath GetUserVerifiedFilePath() const OVERRIDE;
  virtual std::string DebugString(bool verbose) const OVERRIDE;
  virtual void MockDownloadOpenForTesting() OVERRIDE;
  virtual ExternalData* GetExternalData(const void* key) OVERRIDE;
  virtual const ExternalData* GetExternalData(const void* key) const OVERRIDE;
  virtual void SetExternalData(const void* key, ExternalData* data) OVERRIDE;

 private:
  // Construction common to all constructors. |active| should be true for new
  // downloads and false for downloads from the history.
  // |download_type| indicates to the net log system what kind of download
  // this is.
  void Init(bool active, download_net_logs::DownloadType download_type);

  // Returns true if the download still needs to be renamed to
  // GetTargetFilePath().
  bool NeedsRename() const;

  // If all pre-requisites have been met, complete download processing, i.e.  do
  // internal cleanup, file rename, and potentially auto-open.  (Dangerous
  // downloads still may block on user acceptance after this point.)
  void MaybeCompleteDownload();

  // Internal helper for maintaining consistent received and total sizes, and
  // setting the final hash.
  // Should only be called from |OnAllDataSaved|.
  void ProgressComplete(int64 bytes_so_far,
                        const std::string& final_hash);

  // Called when the entire download operation (including renaming etc)
  // is completed.
  void Completed();

  // Call to transition state; all state transitions should go through this.
  void TransitionTo(DownloadState new_state);

  // Set the |danger_type_| and invoke obserers if necessary.
  void SetDangerType(content::DownloadDangerType danger_type);

  // Set the |current_path_| to |new_path|.
  void SetFullPath(const FilePath& new_path);

  // Callback invoked when the download has been renamed to its final name.
  void OnDownloadRenamedToFinalName(content::DownloadInterruptReason reason,
                                    const FilePath& full_path);

  // Callback invoked when the download has been renamed to its intermediate
  // name.
  void OnDownloadRenamedToIntermediateName(
      content::DownloadInterruptReason reason, const FilePath& full_path);

  // Callback from file thread when we release the DownloadFile.
  void OnDownloadFileReleased();

  // The handle to the request information.  Used for operations outside the
  // download system.
  scoped_ptr<DownloadRequestHandleInterface> request_handle_;

  // Download ID assigned by DownloadResourceHandler.
  content::DownloadId download_id_;

  // Display name for the download. If this is empty, then the display name is
  // considered to be |target_path_.BaseName()|.
  FilePath display_name_;

  // Full path to the downloaded or downloading file. This is the path to the
  // physical file, if one exists. The final target path is specified by
  // |target_path_|. |current_path_| can be empty if the in-progress path hasn't
  // been determined.
  FilePath current_path_;

  // Target path of an in-progress download. We may be downloading to a
  // temporary or intermediate file (specified by |current_path_|.  Once the
  // download completes, we will rename the file to |target_path_|.
  FilePath target_path_;

  // Whether the target should be overwritten, uniquified or prompted for.
  TargetDisposition target_disposition_;

  // The chain of redirects that leading up to and including the final URL.
  std::vector<GURL> url_chain_;

  // The URL of the page that initiated the download.
  GURL referrer_url_;

  // Filename suggestion from DownloadSaveInfo. It could, among others, be the
  // suggested filename in 'download' attribute of an anchor. Details:
  // http://www.whatwg.org/specs/web-apps/current-work/#downloading-hyperlinks
  std::string suggested_filename_;

  // If non-empty, contains an externally supplied path that should be used as
  // the target path.
  FilePath forced_file_path_;

  // Page transition that triggerred the download.
  content::PageTransition transition_type_;

  // Whether the download was triggered with a user gesture.
  bool has_user_gesture_;

  // Information from the request.
  // Content-disposition field from the header.
  std::string content_disposition_;

  // Mime-type from the header.  Subject to change.
  std::string mime_type_;

  // The value of the content type header sent with the downloaded item.  It
  // may be different from |mime_type_|, which may be set based on heuristics
  // which may look at the file extension and first few bytes of the file.
  std::string original_mime_type_;

  // The charset of the referring page where the download request comes from.
  // It's used to construct a suggested filename.
  std::string referrer_charset_;

  // The remote IP address where the download was fetched from.  Copied from
  // DownloadCreateInfo::remote_address.
  std::string remote_address_;

  // Total bytes expected.
  int64 total_bytes_;

  // Current received bytes.
  int64 received_bytes_;

  // Current speed. Calculated by the DownloadFile.
  int64 bytes_per_sec_;

  // Sha256 hash of the content.  This might be empty either because
  // the download isn't done yet or because the hash isn't needed
  // (ChromeDownloadManagerDelegate::GenerateFileHash() returned false).
  std::string hash_;

  // A blob containing the state of the hash algorithm.  Only valid while the
  // download is in progress.
  std::string hash_state_;

  // Server's time stamp for the file.
  std::string last_modified_time_;

  // Server's ETAG for the file.
  std::string etag_;

  // Last reason.
  content::DownloadInterruptReason last_reason_;

  // Start time for recording statistics.
  base::TimeTicks start_tick_;

  // The current state of this download.
  DownloadState state_;

  // Current danger type for the download.
  content::DownloadDangerType danger_type_;

  // The views of this item in the download shelf and download contents.
  ObserverList<Observer> observers_;

  // Time the download was started.
  base::Time start_time_;

  // Time the download completed.
  base::Time end_time_;

  // Our persistent store handle.
  int64 db_handle_;

  // Our delegate.
  DownloadItemImplDelegate* delegate_;

  // In progress downloads may be paused by the user, we note it here.
  bool is_paused_;

  // A flag for indicating if the download should be opened at completion.
  bool open_when_complete_;

  // A flag for indicating if the downloaded file is externally removed.
  bool file_externally_removed_;

  // Indicates if the download is considered potentially safe or dangerous
  // (executable files are typically considered dangerous).
  SafetyState safety_state_;

  // True if the download was auto-opened. We set this rather than using
  // an observer as it's frequently possible for the download to be auto opened
  // before the observer is added.
  bool auto_opened_;

  bool is_persisted_;

  // True if the download was initiated in an incognito window.
  bool is_otr_;

  // True if the item was downloaded temporarily.
  bool is_temporary_;

  // True if we've saved all the data for the download.
  bool all_data_saved_;

  // Did the user open the item either directly or indirectly (such as by
  // setting always open files of this type)? The shelf also sets this field
  // when the user closes the shelf before the item has been opened but should
  // be treated as though the user opened it.
  bool opened_;

  // Do we actually open downloads when requested?  For testing purposes only.
  bool open_enabled_;

  // Did the delegate delay calling Complete on this download?
  bool delegate_delayed_complete_;

  // External Data storage.  All objects in the store
  // are owned by the DownloadItemImpl.
  std::map<const void*, ExternalData*> external_data_map_;

  // Net log to use for this download.
  const net::BoundNetLog bound_net_log_;

  base::WeakPtrFactory<DownloadItemImpl> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(DownloadItemImpl);
};

#endif  // CONTENT_BROWSER_DOWNLOAD_DOWNLOAD_ITEM_IMPL_H_
