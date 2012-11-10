// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/download/download_file_manager.h"

#include <set>
#include <string>

#include "base/bind.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "base/stl_util.h"
#include "base/utf_string_conversions.h"
#include "content/browser/download/base_file.h"
#include "content/browser/download/download_create_info.h"
#include "content/browser/download/download_file_impl.h"
#include "content/browser/download/download_interrupt_reasons_impl.h"
#include "content/browser/download/download_request_handle.h"
#include "content/browser/download/download_stats.h"
#include "content/browser/power_save_blocker.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/download_manager_delegate.h"
#include "googleurl/src/gurl.h"
#include "net/base/io_buffer.h"

using content::BrowserThread;
using content::DownloadFile;
using content::DownloadId;
using content::DownloadManager;

namespace {

class DownloadFileFactoryImpl
    : public DownloadFileManager::DownloadFileFactory {
 public:
  DownloadFileFactoryImpl() {}

  virtual content::DownloadFile* CreateFile(
      DownloadCreateInfo* info,
      scoped_ptr<content::ByteStreamReader> stream,
      DownloadManager* download_manager,
      bool calculate_hash,
      const net::BoundNetLog& bound_net_log) OVERRIDE;
};

DownloadFile* DownloadFileFactoryImpl::CreateFile(
    DownloadCreateInfo* info,
    scoped_ptr<content::ByteStreamReader> stream,
    DownloadManager* download_manager,
    bool calculate_hash,
    const net::BoundNetLog& bound_net_log) {
  return new DownloadFileImpl(
      info, stream.Pass(), new DownloadRequestHandle(info->request_handle),
      download_manager, calculate_hash,
      scoped_ptr<content::PowerSaveBlocker>(
          new content::PowerSaveBlocker(
              content::PowerSaveBlocker::kPowerSaveBlockPreventAppSuspension,
              "Download in progress")).Pass(),
      bound_net_log);
}

}  // namespace

DownloadFileManager::DownloadFileManager(DownloadFileFactory* factory)
    : download_file_factory_(factory) {
  if (download_file_factory_ == NULL)
    download_file_factory_.reset(new DownloadFileFactoryImpl);
}

DownloadFileManager::~DownloadFileManager() {
  DCHECK(downloads_.empty());
}

void DownloadFileManager::Shutdown() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  BrowserThread::PostTask(
      BrowserThread::FILE, FROM_HERE,
      base::Bind(&DownloadFileManager::OnShutdown, this));
}

void DownloadFileManager::OnShutdown() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));
  STLDeleteValues(&downloads_);
}

void DownloadFileManager::CreateDownloadFile(
    scoped_ptr<DownloadCreateInfo> info,
    scoped_ptr<content::ByteStreamReader> stream,
    scoped_refptr<DownloadManager> download_manager, bool get_hash,
    const net::BoundNetLog& bound_net_log,
    const CreateDownloadFileCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));
  DCHECK(info.get());
  VLOG(20) << __FUNCTION__ << "()" << " info = " << info->DebugString();

  scoped_ptr<DownloadFile> download_file(download_file_factory_->CreateFile(
      info.get(), stream.Pass(), download_manager, get_hash, bound_net_log));

  content::DownloadInterruptReason interrupt_reason(
      download_file->Initialize());
  if (interrupt_reason == content::DOWNLOAD_INTERRUPT_REASON_NONE) {
    DCHECK(GetDownloadFile(info->download_id) == NULL);
    downloads_[info->download_id] = download_file.release();
  }

  BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
                          base::Bind(callback, interrupt_reason));
}

DownloadFile* DownloadFileManager::GetDownloadFile(
    DownloadId global_id) {
  DownloadFileMap::iterator it = downloads_.find(global_id);
  return it == downloads_.end() ? NULL : it->second;
}

// This method will be sent via a user action, or shutdown on the UI thread, and
// run on the download thread. Since this message has been sent from the UI
// thread, the download may have already completed and won't exist in our map.
void DownloadFileManager::CancelDownload(DownloadId global_id) {
  VLOG(20) << __FUNCTION__ << "()" << " id = " << global_id;
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));
  DownloadFileMap::iterator it = downloads_.find(global_id);
  if (it == downloads_.end())
    return;

  DownloadFile* download_file = it->second;
  VLOG(20) << __FUNCTION__ << "()"
           << " download_file = " << download_file->DebugString();
  download_file->Cancel();

  EraseDownload(global_id);
}

void DownloadFileManager::CompleteDownload(
    DownloadId global_id, const base::Closure& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));

  if (!ContainsKey(downloads_, global_id))
    return;

  DownloadFile* download_file = downloads_[global_id];

  VLOG(20) << " " << __FUNCTION__ << "()"
           << " id = " << global_id
           << " download_file = " << download_file->DebugString();

  // Done here on Windows so that anti-virus scanners invoked by
  // the attachment service actually see the data; see
  // http://crbug.com/127999.
  // Done here for mac because we only want to do this once; see
  // http://crbug.com/13120 for details.
  // Other platforms don't currently do source annotation.
  download_file->AnnotateWithSourceInformation();

  download_file->Detach();

  EraseDownload(global_id);

  // Notify our caller we've let it go.
  BrowserThread::PostTask(BrowserThread::UI, FROM_HERE, callback);
}

void DownloadFileManager::OnDownloadManagerShutdown(DownloadManager* manager) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));
  DCHECK(manager);

  std::set<DownloadFile*> to_remove;

  for (DownloadFileMap::iterator i = downloads_.begin();
       i != downloads_.end(); ++i) {
    DownloadFile* download_file = i->second;
    if (download_file->GetDownloadManager() == manager) {
      download_file->CancelDownloadRequest();
      to_remove.insert(download_file);
    }
  }

  for (std::set<DownloadFile*>::iterator i = to_remove.begin();
       i != to_remove.end(); ++i) {
    downloads_.erase((*i)->GlobalId());
    delete *i;
  }
}

// Actions from the UI thread and run on the download thread

void DownloadFileManager::RenameDownloadFile(
    DownloadId global_id,
    const FilePath& full_path,
    bool overwrite_existing_file,
    const RenameCompletionCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));
  DownloadFile* download_file = GetDownloadFile(global_id);
  if (!download_file) {
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        base::Bind(callback, content::DOWNLOAD_INTERRUPT_REASON_FILE_FAILED,
                   FilePath()));
    return;
  }

  download_file->Rename(full_path, overwrite_existing_file, callback);
}

int DownloadFileManager::NumberOfActiveDownloads() const {
  return downloads_.size();
}

void DownloadFileManager::EraseDownload(DownloadId global_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));

  if (!ContainsKey(downloads_, global_id))
    return;

  DownloadFile* download_file = downloads_[global_id];

  VLOG(20) << " " << __FUNCTION__ << "()"
           << " id = " << global_id
           << " download_file = " << download_file->DebugString();

  downloads_.erase(global_id);

  delete download_file;
}
