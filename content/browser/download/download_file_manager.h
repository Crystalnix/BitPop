// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The DownloadFileManager owns a set of DownloadFile objects, each of which
// represent one in progress download and performs the disk IO for that
// download. The DownloadFileManager itself is a singleton object owned by the
// ResourceDispatcherHost.
//
// The DownloadFileManager uses the file_thread for performing file write
// operations, in order to avoid disk activity on either the IO (network) thread
// and the UI thread. It coordinates the notifications from the network and UI.
//
// A typical download operation involves multiple threads:
//
// Updating an in progress download
// io_thread
//      |----> data ---->|
//                     file_thread (writes to disk)
//                              |----> stats ---->|
//                                              ui_thread (feedback for user and
//                                                         updates to history)
//
// Cancel operations perform the inverse order when triggered by a user action:
// ui_thread (user click)
//    |----> cancel command ---->|
//                          file_thread (close file)
//                                 |----> cancel command ---->|
//                                                    io_thread (stops net IO
//                                                               for download)
//
// The DownloadFileManager tracks download requests, mapping from a download
// ID (unique integer created in the IO thread) to the DownloadManager for the
// tab (profile) where the download was initiated. In the event of a tab closure
// during a download, the DownloadFileManager will continue to route data to the
// appropriate DownloadManager. In progress downloads are cancelled for a
// DownloadManager that exits (such as when closing a profile).

#ifndef CONTENT_BROWSER_DOWNLOAD_DOWNLOAD_FILE_MANAGER_H_
#define CONTENT_BROWSER_DOWNLOAD_DOWNLOAD_FILE_MANAGER_H_
#pragma once

#include <map>

#include "base/atomic_sequence_num.h"
#include "base/basictypes.h"
#include "base/gtest_prod_util.h"
#include "base/hash_tables.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/timer.h"
#include "content/browser/download/interrupt_reasons.h"
#include "content/common/content_export.h"
#include "content/public/browser/download_id.h"
#include "net/base/net_errors.h"
#include "ui/gfx/native_widget_types.h"

struct DownloadCreateInfo;
class DownloadRequestHandle;
class FilePath;
class ResourceDispatcherHost;

namespace content {
class DownloadBuffer;
class DownloadFile;
class DownloadManager;
}

// Manages all in progress downloads.
class CONTENT_EXPORT DownloadFileManager
    : public base::RefCountedThreadSafe<DownloadFileManager> {
 public:
  class DownloadFileFactory {
   public:
    virtual ~DownloadFileFactory() {}

    virtual content::DownloadFile* CreateFile(
        DownloadCreateInfo* info,
        const DownloadRequestHandle& request_handle,
        content::DownloadManager* download_manager,
        bool calculate_hash) = 0;
  };

  // Takes ownership of the factory.
  // Passing in a NULL for |factory| will cause a default
  // |DownloadFileFactory| to be used.
  DownloadFileManager(ResourceDispatcherHost* rdh,
                      DownloadFileFactory* factory);

  // Called on shutdown on the UI thread.
  void Shutdown();

  // Called on UI thread to make DownloadFileManager start the download.
  void StartDownload(DownloadCreateInfo* info,
                     const DownloadRequestHandle& request_handle);

  // Handlers for notifications sent from the IO thread and run on the
  // FILE thread.
  void UpdateDownload(content::DownloadId global_id,
                      content::DownloadBuffer* buffer);

  // |reason| is the reason for interruption, if one occurs.
  // |security_info| contains SSL information (cert_id, cert_status,
  // security_bits, ssl_connection_status), which can be used to
  // fine-tune the error message.  It is empty if the transaction
  // was not performed securely.
  void OnResponseCompleted(content::DownloadId global_id,
                           InterruptReason reason,
                           const std::string& security_info);

  // Handlers for notifications sent from the UI thread and run on the
  // FILE thread.  These are both terminal actions with respect to the
  // download file, as far as the DownloadFileManager is concerned -- if
  // anything happens to the download file after they are called, it will
  // be ignored.
  void CancelDownload(content::DownloadId id);
  void CompleteDownload(content::DownloadId id);

  // Called on FILE thread by DownloadManager at the beginning of its shutdown.
  void OnDownloadManagerShutdown(content::DownloadManager* manager);

  // The DownloadManager in the UI thread has provided an intermediate
  // .crdownload name for the download specified by |id|.
  void RenameInProgressDownloadFile(content::DownloadId id,
                                    const FilePath& full_path);

  // The DownloadManager in the UI thread has provided a final name for the
  // download specified by |id|.
  // |overwrite_existing_file| prevents uniquification, and is used for SAFE
  // downloads, as the user may have decided to overwrite the file.
  // Sent from the UI thread and run on the FILE thread.
  void RenameCompletingDownloadFile(content::DownloadId id,
                                    const FilePath& full_path,
                                    bool overwrite_existing_file);

  // The number of downloads currently active on the DownloadFileManager.
  // Primarily for testing.
  int NumberOfActiveDownloads() const {
    return downloads_.size();
  }

 private:
  friend class base::RefCountedThreadSafe<DownloadFileManager>;
  friend class DownloadFileManagerTest;
  friend class DownloadManagerTest;
  FRIEND_TEST_ALL_PREFIXES(DownloadManagerTest, StartDownload);

  ~DownloadFileManager();

  // Timer helpers for updating the UI about the current progress of a download.
  void StartUpdateTimer();
  void StopUpdateTimer();
  void UpdateInProgressDownloads();

  // Clean up helper that runs on the download thread.
  void OnShutdown();

  // Creates DownloadFile on FILE thread and continues starting the download
  // process.
  void CreateDownloadFile(DownloadCreateInfo* info,
                          const DownloadRequestHandle& request_handle,
                          content::DownloadManager* download_manager,
                          bool hash_needed);

  // Called only on the download thread.
  content::DownloadFile* GetDownloadFile(content::DownloadId global_id);

  // Called only from RenameInProgressDownloadFile and
  // RenameCompletingDownloadFile on the FILE thread.
  // |rename_error| indicates what error caused the cancel.
  void CancelDownloadOnRename(content::DownloadId global_id,
                              net::Error rename_error);

  // Erases the download file with the given the download |id| and removes
  // it from the maps.
  void EraseDownload(content::DownloadId global_id);

  typedef base::hash_map<content::DownloadId, content::DownloadFile*>
      DownloadFileMap;

  // A map of all in progress downloads.  It owns the download files.
  DownloadFileMap downloads_;

  // Schedule periodic updates of the download progress. This timer
  // is controlled from the FILE thread, and posts updates to the UI thread.
  base::RepeatingTimer<DownloadFileManager> update_timer_;

  ResourceDispatcherHost* resource_dispatcher_host_;
  scoped_ptr<DownloadFileFactory> download_file_factory_;

  DISALLOW_COPY_AND_ASSIGN(DownloadFileManager);
};

#endif  // CONTENT_BROWSER_DOWNLOAD_DOWNLOAD_FILE_MANAGER_H_
