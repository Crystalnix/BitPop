// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/history/top_sites_backend.h"

#include "base/bind.h"
#include "base/file_path.h"
#include "base/file_util.h"
#include "base/memory/ref_counted.h"
#include "chrome/browser/history/top_sites_database.h"
#include "content/public/browser/browser_thread.h"

using content::BrowserThread;

namespace history {

TopSitesBackend::TopSitesBackend()
    : db_(new TopSitesDatabase()) {
}

void TopSitesBackend::Init(const FilePath& path) {
  db_path_ = path;
  BrowserThread::PostTask(
      BrowserThread::DB, FROM_HERE,
      base::Bind(&TopSitesBackend::InitDBOnDBThread, this, path));
}

void TopSitesBackend::Shutdown() {
  BrowserThread::PostTask(
      BrowserThread::DB, FROM_HERE,
      base::Bind(&TopSitesBackend::ShutdownDBOnDBThread, this));
}

TopSitesBackend::Handle TopSitesBackend::GetMostVisitedThumbnails(
    CancelableRequestConsumerBase* consumer,
    const GetMostVisitedThumbnailsCallback& callback) {
  GetMostVisitedThumbnailsRequest* request =
      new GetMostVisitedThumbnailsRequest(callback);
  request->value = new MostVisitedThumbnails;
  AddRequest(request, consumer);
  BrowserThread::PostTask(
      BrowserThread::DB, FROM_HERE,
      base::Bind(&TopSitesBackend::GetMostVisitedThumbnailsOnDBThread, this,
                 make_scoped_refptr(request)));
  return request->handle();
}

void TopSitesBackend::UpdateTopSites(const TopSitesDelta& delta) {
  BrowserThread::PostTask(
      BrowserThread::DB, FROM_HERE,
      base::Bind(&TopSitesBackend::UpdateTopSitesOnDBThread, this, delta));
}

void TopSitesBackend::SetPageThumbnail(const MostVisitedURL& url,
                                       int url_rank,
                                       const Images& thumbnail) {
  BrowserThread::PostTask(
      BrowserThread::DB, FROM_HERE,
      base::Bind(&TopSitesBackend::SetPageThumbnailOnDBThread, this, url,
                 url_rank, thumbnail));
}

void TopSitesBackend::ResetDatabase() {
  BrowserThread::PostTask(
      BrowserThread::DB, FROM_HERE,
      base::Bind(&TopSitesBackend::ResetDatabaseOnDBThread, this, db_path_));
}

TopSitesBackend::Handle TopSitesBackend::DoEmptyRequest(
    CancelableRequestConsumerBase* consumer,
    const EmptyRequestCallback& callback) {
  EmptyRequestRequest* request = new EmptyRequestRequest(callback);
  AddRequest(request, consumer);
  BrowserThread::PostTask(
      BrowserThread::DB, FROM_HERE,
      base::Bind(&TopSitesBackend::DoEmptyRequestOnDBThread, this,
                 make_scoped_refptr(request)));
  return request->handle();
}

TopSitesBackend::~TopSitesBackend() {
  DCHECK(!db_.get());  // Shutdown should have happened first (which results in
                       // nulling out db).
}

void TopSitesBackend::InitDBOnDBThread(const FilePath& path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::DB));
  if (!db_->Init(path)) {
    NOTREACHED() << "Failed to initialize database.";
    db_.reset();
  }
}

void TopSitesBackend::ShutdownDBOnDBThread() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::DB));
  db_.reset();
}

void TopSitesBackend::GetMostVisitedThumbnailsOnDBThread(
    scoped_refptr<GetMostVisitedThumbnailsRequest> request) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::DB));

  if (request->canceled())
    return;

  bool may_need_history_migration = false;
  if (db_.get()) {
    db_->GetPageThumbnails(&(request->value->most_visited),
                           &(request->value->url_to_images_map));
    may_need_history_migration = db_->may_need_history_migration();
  }
  request->ForwardResult(request->handle(), request->value,
                         may_need_history_migration);
}

void TopSitesBackend::UpdateTopSitesOnDBThread(const TopSitesDelta& delta) {
  if (!db_.get())
    return;

  for (size_t i = 0; i < delta.deleted.size(); ++i)
    db_->RemoveURL(delta.deleted[i]);

  for (size_t i = 0; i < delta.added.size(); ++i)
    db_->SetPageThumbnail(delta.added[i].url, delta.added[i].rank, Images());

  for (size_t i = 0; i < delta.moved.size(); ++i)
    db_->UpdatePageRank(delta.moved[i].url, delta.moved[i].rank);
}

void TopSitesBackend::SetPageThumbnailOnDBThread(const MostVisitedURL& url,
                                                 int url_rank,
                                                 const Images& thumbnail) {
  if (!db_.get())
    return;

  db_->SetPageThumbnail(url, url_rank, thumbnail);
}

void TopSitesBackend::ResetDatabaseOnDBThread(const FilePath& file_path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::DB));
  db_.reset(NULL);
  file_util::Delete(db_path_, false);
  db_.reset(new TopSitesDatabase());
  InitDBOnDBThread(db_path_);
}

void TopSitesBackend::DoEmptyRequestOnDBThread(
    scoped_refptr<EmptyRequestRequest> request) {
  request->ForwardResult(request->handle());
}

}  // namespace history
