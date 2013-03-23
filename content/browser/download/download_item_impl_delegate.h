// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_DOWNLOAD_DOWNLOAD_ITEM_IMPL_DELEGATE_H_
#define CONTENT_BROWSER_DOWNLOAD_DOWNLOAD_ITEM_IMPL_DELEGATE_H_

#include "base/callback.h"
#include "base/file_path.h"
#include "content/common/content_export.h"
#include "content/public/browser/download_danger_type.h"
#include "content/public/browser/download_item.h"

namespace content {
class DownloadItemImpl;
class BrowserContext;

// Delegate for operations that a DownloadItemImpl can't do for itself.
// The base implementation of this class does nothing (returning false
// on predicates) so interfaces not of interest to a derived class may
// be left unimplemented.
class CONTENT_EXPORT DownloadItemImplDelegate {
 public:
  typedef base::Callback<void(
      const FilePath&,                  // Target path
      DownloadItem::TargetDisposition,  // overwrite/uniquify target
      DownloadDangerType,
      const FilePath&                   // Intermediate file path
                              )> DownloadTargetCallback;

  // The boolean argument indicates whether or not the download was
  // actually opened.
  typedef base::Callback<void(bool)> ShouldOpenDownloadCallback;

  DownloadItemImplDelegate();
  virtual ~DownloadItemImplDelegate();

  // Used for catching use-after-free errors.
  void Attach();
  void Detach();

  // Request determination of the download target from the delegate.
  virtual void DetermineDownloadTarget(
      DownloadItemImpl* download, const DownloadTargetCallback& callback);

  // Allows the delegate to delay completion of the download.  This function
  // will either return true (if the download may complete now) or will return
  // false and call the provided callback at some future point.  This function
  // may be called repeatedly.
  virtual bool ShouldCompleteDownload(
      DownloadItemImpl* download,
      const base::Closure& complete_callback);

  // Allows the delegate to override the opening of a download. If it returns
  // true then it's reponsible for opening the item.
  virtual bool ShouldOpenDownload(
      DownloadItemImpl* download, const ShouldOpenDownloadCallback& callback);

  // Tests if a file type should be opened automatically.
  virtual bool ShouldOpenFileBasedOnExtension(const FilePath& path);

  // Checks whether a downloaded file still exists and updates the
  // file's state if the file is already removed.
  // The check may or may not result in a later asynchronous call
  // to OnDownloadedFileRemoved().
  virtual void CheckForFileRemoval(DownloadItemImpl* download_item);

  // For contextual issues like language and prefs.
  virtual BrowserContext* GetBrowserContext() const;

  // Update the persistent store with our information.
  virtual void UpdatePersistence(DownloadItemImpl* download);

  // Handle any delegate portions of a state change operation on the
  // DownloadItem.
  virtual void DownloadOpened(DownloadItemImpl* download);
  virtual void DownloadRemoved(DownloadItemImpl* download);

  // Show the download in the browser.
  virtual void ShowDownloadInBrowser(DownloadItemImpl* download);

  // Assert consistent state for delgate object at various transitions.
  virtual void AssertStateConsistent(DownloadItemImpl* download) const;

 private:
  // For "Outlives attached DownloadItemImpl" invariant assertion.
  int count_;

  DISALLOW_COPY_AND_ASSIGN(DownloadItemImplDelegate);
};

}  // namespace content

#endif  // CONTENT_BROWSER_DOWNLOAD_DOWNLOAD_ITEM_IMPL_DELEGATE_H_
