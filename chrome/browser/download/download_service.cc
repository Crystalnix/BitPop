// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/download/download_service.h"

#include "base/callback.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/download/chrome_download_manager_delegate.h"
#include "chrome/browser/download/download_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "content/public/browser/download_manager.h"

using content::DownloadManager;

DownloadService::DownloadService(Profile* profile)
    : download_manager_created_(false),
      profile_(profile) {
}

DownloadService::~DownloadService() {}

void DownloadService::OnManagerCreated(
    const DownloadService::OnManagerCreatedCallback& cb) {
  if (download_manager_created_) {
    cb.Run(manager_.get());
  } else {
    on_manager_created_callbacks_.push_back(cb);
  }
}

DownloadManager* DownloadService::GetDownloadManager() {
  if (!download_manager_created_) {
    // In case the delegate has already been set by
    // SetDownloadManagerDelegateForTesting.
    if (!manager_delegate_.get())
      manager_delegate_ = new ChromeDownloadManagerDelegate(profile_);
    manager_ = DownloadManager::Create(
        manager_delegate_.get(), g_browser_process->download_status_updater());
    manager_->Init(profile_);
    manager_delegate_->SetDownloadManager(manager_);
    download_manager_created_ = true;
    for (std::vector<OnManagerCreatedCallback>::iterator cb
         = on_manager_created_callbacks_.begin();
         cb != on_manager_created_callbacks_.end(); ++cb) {
      cb->Run(manager_.get());
    }
    on_manager_created_callbacks_.clear();
  }
  return manager_.get();
}

bool DownloadService::HasCreatedDownloadManager() {
  return download_manager_created_;
}

int DownloadService::DownloadCount() const {
  return download_manager_created_ ? manager_->InProgressCount() : 0;
}

// static
int DownloadService::DownloadCountAllProfiles() {
  std::vector<Profile*> profiles(
      g_browser_process->profile_manager()->GetLoadedProfiles());

  int count = 0;
  for (std::vector<Profile*>::iterator it = profiles.begin();
       it < profiles.end(); ++it) {
    count += DownloadServiceFactory::GetForProfile(*it)->DownloadCount();
    if ((*it)->HasOffTheRecordProfile())
      count += DownloadServiceFactory::GetForProfile(
          (*it)->GetOffTheRecordProfile())->DownloadCount();
  }

  return count;
}

void DownloadService::SetDownloadManagerDelegateForTesting(
    ChromeDownloadManagerDelegate* new_delegate) {
  // Guarantee everything is properly initialized.
  GetDownloadManager();

  manager_->SetDownloadManagerDelegate(new_delegate);
  new_delegate->SetDownloadManager(manager_);
  manager_delegate_ = new_delegate;
}

void DownloadService::Shutdown() {
  if (manager_.get()) {
    manager_->Shutdown();

    // The manager reference can be released any time after shutdown;
    // it will be destroyed when the last reference is released on the
    // FILE thread.
    // Resetting here will guarantee that any attempts to get the
    // DownloadManager after shutdown will return null.
    //
    // TODO(rdsmith): Figure out how to guarantee when the last reference
    // will be released and make DownloadManager not RefCountedThreadSafe<>.
    manager_.release();
  }
  if (manager_delegate_.get())
    manager_delegate_.release();
}
