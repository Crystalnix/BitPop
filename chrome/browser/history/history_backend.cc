// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/history/history_backend.h"

#include <list>
#include <map>
#include <set>
#include <vector>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/file_util.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/scoped_vector.h"
#include "base/message_loop.h"
#include "base/metrics/histogram.h"
#include "base/string_util.h"
#include "base/time.h"
#include "chrome/browser/autocomplete/history_url_provider.h"
#include "chrome/browser/bookmarks/bookmark_service.h"
#include "chrome/browser/cancelable_request.h"
#include "chrome/browser/history/history_notifications.h"
#include "chrome/browser/history/history_publisher.h"
#include "chrome/browser/history/in_memory_history_backend.h"
#include "chrome/browser/history/page_usage_data.h"
#include "chrome/browser/history/top_sites.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/url_constants.h"
#include "content/browser/download/download_persistent_store_info.h"
#include "googleurl/src/gurl.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "net/base/registry_controlled_domain.h"

using base::Time;
using base::TimeDelta;
using base::TimeTicks;

/* The HistoryBackend consists of a number of components:

    HistoryDatabase (stores past 3 months of history)
      URLDatabase (stores a list of URLs)
      DownloadDatabase (stores a list of downloads)
      VisitDatabase (stores a list of visits for the URLs)
      VisitSegmentDatabase (stores groups of URLs for the most visited view).

    ArchivedDatabase (stores history older than 3 months)
      URLDatabase (stores a list of URLs)
      DownloadDatabase (stores a list of downloads)
      VisitDatabase (stores a list of visits for the URLs)

      (this does not store visit segments as they expire after 3 mos.)

    TextDatabaseManager (manages multiple text database for different times)
      TextDatabase (represents a single month of full-text index).
      ...more TextDatabase objects...

    ExpireHistoryBackend (manages moving things from HistoryDatabase to
                          the ArchivedDatabase and deleting)
*/

namespace history {

// How long we keep segment data for in days. Currently 3 months.
// This value needs to be greater or equal to
// MostVisitedModel::kMostVisitedScope but we don't want to introduce a direct
// dependency between MostVisitedModel and the history backend.
static const int kSegmentDataRetention = 90;

// How long we'll wait to do a commit, so that things are batched together.
static const int kCommitIntervalSeconds = 10;

// The amount of time before we re-fetch the favicon.
static const int kFaviconRefetchDays = 7;

// GetSessionTabs returns all open tabs, or tabs closed kSessionCloseTimeWindow
// seconds ago.
static const int kSessionCloseTimeWindowSecs = 10;

// The maximum number of items we'll allow in the redirect list before
// deleting some.
static const int kMaxRedirectCount = 32;

// The number of days old a history entry can be before it is considered "old"
// and is archived.
static const int kArchiveDaysThreshold = 90;

// Converts from PageUsageData to MostVisitedURL. |redirects| is a
// list of redirects for this URL. Empty list means no redirects.
MostVisitedURL MakeMostVisitedURL(const PageUsageData& page_data,
                                  const RedirectList& redirects) {
  MostVisitedURL mv;
  mv.url = page_data.GetURL();
  mv.title = page_data.GetTitle();
  if (redirects.empty()) {
    // Redirects must contain at least the target url.
    mv.redirects.push_back(mv.url);
  } else {
    mv.redirects = redirects;
    if (mv.redirects[mv.redirects.size() - 1] != mv.url) {
      // The last url must be the target url.
      mv.redirects.push_back(mv.url);
    }
  }
  return mv;
}

// This task is run on a timer so that commits happen at regular intervals
// so they are batched together. The important thing about this class is that
// it supports canceling of the task so the reference to the backend will be
// freed. The problem is that when history is shutting down, there is likely
// to be one of these commits still pending and holding a reference.
//
// The backend can call Cancel to have this task release the reference. The
// task will still run (if we ever get to processing the event before
// shutdown), but it will not do anything.
//
// Note that this is a refcounted object and is not a task in itself. It should
// be assigned to a RunnableMethod.
//
// TODO(brettw): bug 1165182: This should be replaced with a
// base::WeakPtrFactory which will handle everything automatically (like we do
// in ExpireHistoryBackend).
class CommitLaterTask : public base::RefCounted<CommitLaterTask> {
 public:
  explicit CommitLaterTask(HistoryBackend* history_backend)
      : history_backend_(history_backend) {
  }

  // The backend will call this function if it is being destroyed so that we
  // release our reference.
  void Cancel() {
    history_backend_ = NULL;
  }

  void RunCommit() {
    if (history_backend_.get())
      history_backend_->Commit();
  }

 private:
  friend class base::RefCounted<CommitLaterTask>;

  ~CommitLaterTask() {}

  scoped_refptr<HistoryBackend> history_backend_;
};

// Handles querying first the main database, then the full text database if that
// fails. It will optionally keep track of all URLs seen so duplicates can be
// eliminated. This is used by the querying sub-functions.
//
// TODO(brettw): This class may be able to be simplified or eliminated. After
// this was written, QueryResults can efficiently look up by URL, so the need
// for this extra set of previously queried URLs is less important.
class HistoryBackend::URLQuerier {
 public:
  URLQuerier(URLDatabase* main_db, URLDatabase* archived_db, bool track_unique)
      : main_db_(main_db),
        archived_db_(archived_db),
        track_unique_(track_unique) {
  }

  // When we're tracking unique URLs, returns true if this URL has been
  // previously queried. Only call when tracking unique URLs.
  bool HasURL(const GURL& url) {
    DCHECK(track_unique_);
    return unique_urls_.find(url) != unique_urls_.end();
  }

  bool GetRowForURL(const GURL& url, URLRow* row) {
    if (!main_db_->GetRowForURL(url, row)) {
      if (!archived_db_ || !archived_db_->GetRowForURL(url, row)) {
        // This row is neither in the main nor the archived DB.
        return false;
      }
    }

    if (track_unique_)
      unique_urls_.insert(url);
    return true;
  }

 private:
  URLDatabase* main_db_;  // Guaranteed non-NULL.
  URLDatabase* archived_db_;  // Possibly NULL.

  bool track_unique_;

  // When track_unique_ is set, this is updated with every URL seen so far.
  std::set<GURL> unique_urls_;

  DISALLOW_COPY_AND_ASSIGN(URLQuerier);
};

// HistoryBackend --------------------------------------------------------------

HistoryBackend::HistoryBackend(const FilePath& history_dir,
                               int id,
                               Delegate* delegate,
                               BookmarkService* bookmark_service)
    : delegate_(delegate),
      id_(id),
      history_dir_(history_dir),
      ALLOW_THIS_IN_INITIALIZER_LIST(expirer_(this, bookmark_service)),
      recent_redirects_(kMaxRedirectCount),
      backend_destroy_message_loop_(NULL),
      segment_queried_(false),
      bookmark_service_(bookmark_service) {
}

HistoryBackend::~HistoryBackend() {
  DCHECK(!scheduled_commit_) << "Deleting without cleanup";
  ReleaseDBTasks();

  // First close the databases before optionally running the "destroy" task.
  if (db_.get()) {
    // Commit the long-running transaction.
    db_->CommitTransaction();
    db_.reset();
  }
  if (thumbnail_db_.get()) {
    thumbnail_db_->CommitTransaction();
    thumbnail_db_.reset();
  }
  if (archived_db_.get()) {
    archived_db_->CommitTransaction();
    archived_db_.reset();
  }
  if (text_database_.get()) {
    text_database_->CommitTransaction();
    text_database_.reset();
  }

  if (!backend_destroy_task_.is_null()) {
    // Notify an interested party (typically a unit test) that we're done.
    DCHECK(backend_destroy_message_loop_);
    backend_destroy_message_loop_->PostTask(FROM_HERE, backend_destroy_task_);
  }
}

void HistoryBackend::Init(const std::string& languages, bool force_fail) {
  if (!force_fail)
    InitImpl(languages);
  delegate_->DBLoaded(id_);
}

void HistoryBackend::SetOnBackendDestroyTask(MessageLoop* message_loop,
                                             const base::Closure& task) {
  if (!backend_destroy_task_.is_null())
    DLOG(WARNING) << "Setting more than one destroy task, overriding";
  backend_destroy_message_loop_ = message_loop;
  backend_destroy_task_ = task;
}

void HistoryBackend::Closing() {
  // Any scheduled commit will have a reference to us, we must make it
  // release that reference before we can be destroyed.
  CancelScheduledCommit();

  // Release our reference to the delegate, this reference will be keeping the
  // history service alive.
  delegate_.reset();
}

void HistoryBackend::NotifyRenderProcessHostDestruction(const void* host) {
  tracker_.NotifyRenderProcessHostDestruction(host);
}

FilePath HistoryBackend::GetThumbnailFileName() const {
  return history_dir_.Append(chrome::kThumbnailsFilename);
}

FilePath HistoryBackend::GetFaviconsFileName() const {
  return history_dir_.Append(chrome::kFaviconsFilename);
}

FilePath HistoryBackend::GetArchivedFileName() const {
  return history_dir_.Append(chrome::kArchivedHistoryFilename);
}

SegmentID HistoryBackend::GetLastSegmentID(VisitID from_visit) {
  // Set is used to detect referrer loops.  Should not happen, but can
  // if the database is corrupt.
  std::set<VisitID> visit_set;
  VisitID visit_id = from_visit;
  while (visit_id) {
    VisitRow row;
    if (!db_->GetRowForVisit(visit_id, &row))
      return 0;
    if (row.segment_id)
      return row.segment_id;  // Found a visit in this change with a segment.

    // Check the referrer of this visit, if any.
    visit_id = row.referring_visit;

    if (visit_set.find(visit_id) != visit_set.end()) {
      NOTREACHED() << "Loop in referer chain, giving up";
      break;
    }
    visit_set.insert(visit_id);
  }
  return 0;
}

SegmentID HistoryBackend::UpdateSegments(
    const GURL& url,
    VisitID from_visit,
    VisitID visit_id,
    content::PageTransition transition_type,
    const Time ts) {
  if (!db_.get())
    return 0;

  // We only consider main frames.
  if (!content::PageTransitionIsMainFrame(transition_type))
    return 0;

  SegmentID segment_id = 0;
  content::PageTransition t =
      content::PageTransitionStripQualifier(transition_type);

  // Are we at the beginning of a new segment?
  if (t == content::PAGE_TRANSITION_TYPED ||
      t == content::PAGE_TRANSITION_AUTO_BOOKMARK) {
    // If so, create or get the segment.
    std::string segment_name = db_->ComputeSegmentName(url);
    URLID url_id = db_->GetRowForURL(url, NULL);
    if (!url_id)
      return 0;

    if (!(segment_id = db_->GetSegmentNamed(segment_name))) {
      if (!(segment_id = db_->CreateSegment(url_id, segment_name))) {
        NOTREACHED();
        return 0;
      }
    } else {
      // Note: if we update an existing segment, we update the url used to
      // represent that segment in order to minimize stale most visited
      // images.
      db_->UpdateSegmentRepresentationURL(segment_id, url_id);
    }
  } else {
    // Note: it is possible there is no segment ID set for this visit chain.
    // This can happen if the initial navigation wasn't AUTO_BOOKMARK or
    // TYPED. (For example GENERATED). In this case this visit doesn't count
    // toward any segment.
    if (!(segment_id = GetLastSegmentID(from_visit)))
      return 0;
  }

  // Set the segment in the visit.
  if (!db_->SetSegmentID(visit_id, segment_id)) {
    NOTREACHED();
    return 0;
  }

  // Finally, increase the counter for that segment / day.
  if (!db_->IncreaseSegmentVisitCount(segment_id, ts, 1)) {
    NOTREACHED();
    return 0;
  }
  return segment_id;
}

void HistoryBackend::AddPage(scoped_refptr<HistoryAddPageArgs> request) {
  if (!db_.get())
    return;

  // Will be filled with the URL ID and the visit ID of the last addition.
  std::pair<URLID, VisitID> last_ids(0, tracker_.GetLastVisit(
      request->id_scope, request->page_id, request->referrer));

  VisitID from_visit_id = last_ids.second;

  // If a redirect chain is given, we expect the last item in that chain to be
  // the final URL.
  DCHECK(request->redirects.empty() ||
         request->redirects.back() == request->url);

  // Avoid duplicating times in the database, at least as long as pages are
  // added in order. However, we don't want to disallow pages from recording
  // times earlier than our last_recorded_time_, because someone might set
  // their machine's clock back.
  if (last_requested_time_ == request->time) {
    last_recorded_time_ = last_recorded_time_ + TimeDelta::FromMicroseconds(1);
  } else {
    last_requested_time_ = request->time;
    last_recorded_time_ = last_requested_time_;
  }

  // If the user is adding older history, we need to make sure our times
  // are correct.
  if (request->time < first_recorded_time_)
    first_recorded_time_ = request->time;

  content::PageTransition transition =
    content::PageTransitionStripQualifier(request->transition);
  bool is_keyword_generated =
      (transition == content::PAGE_TRANSITION_KEYWORD_GENERATED);

  // If the user is navigating to a not-previously-typed intranet hostname,
  // change the transition to TYPED so that the omnibox will learn that this is
  // a known host.
  bool has_redirects = request->redirects.size() > 1;
  if (content::PageTransitionIsMainFrame(request->transition) &&
      (transition != content::PAGE_TRANSITION_TYPED) && !is_keyword_generated) {
    const GURL& origin_url(has_redirects ?
        request->redirects[0] : request->url);
    if (origin_url.SchemeIs(chrome::kHttpScheme) ||
        origin_url.SchemeIs(chrome::kHttpsScheme) ||
        origin_url.SchemeIs(chrome::kFtpScheme)) {
      std::string host(origin_url.host());
      if ((net::RegistryControlledDomainService::GetRegistryLength(
          host, false) == 0) && !db_->IsTypedHost(host)) {
        transition = content::PAGE_TRANSITION_TYPED;
        request->transition = content::PageTransitionFromInt(transition |
            content::PageTransitionGetQualifier(request->transition));
      }
    }
  }

  if (!has_redirects) {
    // The single entry is both a chain start and end.
    content::PageTransition t = content::PageTransitionFromInt(
        request->transition |
        content::PAGE_TRANSITION_CHAIN_START |
        content::PAGE_TRANSITION_CHAIN_END);

    // No redirect case (one element means just the page itself).
    last_ids = AddPageVisit(request->url, last_recorded_time_,
                            last_ids.second, t, request->visit_source);

    // Update the segment for this visit. KEYWORD_GENERATED visits should not
    // result in changing most visited, so we don't update segments (most
    // visited db).
    if (!is_keyword_generated) {
      UpdateSegments(request->url, from_visit_id, last_ids.second, t,
                     last_recorded_time_);
    }
  } else {
    // Redirect case. Add the redirect chain.

    content::PageTransition redirect_info =
        content::PAGE_TRANSITION_CHAIN_START;

    if (request->redirects[0].SchemeIs(chrome::kAboutScheme)) {
      // When the redirect source + referrer is "about" we skip it. This
      // happens when a page opens a new frame/window to about:blank and then
      // script sets the URL to somewhere else (used to hide the referrer). It
      // would be nice to keep all these redirects properly but we don't ever
      // see the initial about:blank load, so we don't know where the
      // subsequent client redirect came from.
      //
      // In this case, we just don't bother hooking up the source of the
      // redirects, so we remove it.
      request->redirects.erase(request->redirects.begin());
    } else if (request->transition & content::PAGE_TRANSITION_CLIENT_REDIRECT) {
      redirect_info = content::PAGE_TRANSITION_CLIENT_REDIRECT;
      // The first entry in the redirect chain initiated a client redirect.
      // We don't add this to the database since the referrer is already
      // there, so we skip over it but change the transition type of the first
      // transition to client redirect.
      //
      // The referrer is invalid when restoring a session that features an
      // https tab that redirects to a different host or to http. In this
      // case we don't need to reconnect the new redirect with the existing
      // chain.
      if (request->referrer.is_valid()) {
        DCHECK(request->referrer == request->redirects[0]);
        request->redirects.erase(request->redirects.begin());

        // If the navigation entry for this visit has replaced that for the
        // first visit, remove the CHAIN_END marker from the first visit. This
        // can be called a lot, for example, the page cycler, and most of the
        // time we won't have changed anything.
        VisitRow visit_row;
        if (request->did_replace_entry &&
            db_->GetRowForVisit(last_ids.second, &visit_row) &&
            visit_row.transition | content::PAGE_TRANSITION_CHAIN_END) {
          visit_row.transition = content::PageTransitionFromInt(
              visit_row.transition & ~content::PAGE_TRANSITION_CHAIN_END);
          db_->UpdateVisitRow(visit_row);
        }
      }
    }

    for (size_t redirect_index = 0; redirect_index < request->redirects.size();
         redirect_index++) {
      content::PageTransition t =
          content::PageTransitionFromInt(transition | redirect_info);

      // If this is the last transition, add a CHAIN_END marker
      if (redirect_index == (request->redirects.size() - 1)) {
        t = content::PageTransitionFromInt(
            t | content::PAGE_TRANSITION_CHAIN_END);
      }

      // Record all redirect visits with the same timestamp. We don't display
      // them anyway, and if we ever decide to, we can reconstruct their order
      // from the redirect chain.
      last_ids = AddPageVisit(request->redirects[redirect_index],
                              last_recorded_time_, last_ids.second,
                              t, request->visit_source);
      if (t & content::PAGE_TRANSITION_CHAIN_START) {
        // Update the segment for this visit.
        UpdateSegments(request->redirects[redirect_index],
                       from_visit_id, last_ids.second, t, last_recorded_time_);
      }

      // Subsequent transitions in the redirect list must all be sever
      // redirects.
      redirect_info = content::PAGE_TRANSITION_SERVER_REDIRECT;
    }

    // Last, save this redirect chain for later so we can set titles & favicons
    // on the redirected pages properly. It is indexed by the destination page.
    recent_redirects_.Put(request->url, request->redirects);
  }

  // TODO(brettw) bug 1140015: Add an "add page" notification so the history
  // views can keep in sync.

  // Add the last visit to the tracker so we can get outgoing transitions.
  // TODO(evanm): Due to http://b/1194536 we lose the referrers of a subframe
  // navigation anyway, so last_visit_id is always zero for them.  But adding
  // them here confuses main frame history, so we skip them for now.
  if (transition != content::PAGE_TRANSITION_AUTO_SUBFRAME &&
      transition != content::PAGE_TRANSITION_MANUAL_SUBFRAME &&
      !is_keyword_generated) {
    tracker_.AddVisit(request->id_scope, request->page_id, request->url,
                      last_ids.second);
  }

  if (text_database_.get()) {
    text_database_->AddPageURL(request->url, last_ids.first, last_ids.second,
                               last_recorded_time_);
  }

  ScheduleCommit();
}

void HistoryBackend::InitImpl(const std::string& languages) {
  DCHECK(!db_.get()) << "Initializing HistoryBackend twice";
  // In the rare case where the db fails to initialize a dialog may get shown
  // the blocks the caller, yet allows other messages through. For this reason
  // we only set db_ to the created database if creation is successful. That
  // way other methods won't do anything as db_ is still NULL.

  TimeTicks beginning_time = TimeTicks::Now();

  // Compute the file names. Note that the index file can be removed when the
  // text db manager is finished being hooked up.
  FilePath history_name = history_dir_.Append(chrome::kHistoryFilename);
  FilePath thumbnail_name = GetThumbnailFileName();
  FilePath archived_name = GetArchivedFileName();
  FilePath tmp_bookmarks_file = history_dir_.Append(
      chrome::kHistoryBookmarksFileName);

  // History database.
  db_.reset(new HistoryDatabase());
  sql::InitStatus status = db_->Init(history_name, tmp_bookmarks_file);
  switch (status) {
    case sql::INIT_OK:
      break;
    case sql::INIT_FAILURE:
      // A NULL db_ will cause all calls on this object to notice this error
      // and to not continue.
      delegate_->NotifyProfileError(id_, status);
      db_.reset();
      return;
    default:
      NOTREACHED();
  }

  // Fill the in-memory database and send it back to the history service on the
  // main thread.
  InMemoryHistoryBackend* mem_backend = new InMemoryHistoryBackend;
  if (mem_backend->Init(history_name, history_dir_, db_.get(), languages))
    delegate_->SetInMemoryBackend(id_, mem_backend);  // Takes ownership of
                                                      // pointer.
  else
    delete mem_backend;  // Error case, run without the in-memory DB.
  db_->BeginExclusiveMode();  // Must be after the mem backend read the data.

  // Create the history publisher which needs to be passed on to the text and
  // thumbnail databases for publishing history.
  history_publisher_.reset(new HistoryPublisher());
  if (!history_publisher_->Init()) {
    // The init may fail when there are no indexers wanting our history.
    // Hence no need to log the failure.
    history_publisher_.reset();
  }

  // Full-text database. This has to be first so we can pass it to the
  // HistoryDatabase for migration.
  text_database_.reset(new TextDatabaseManager(history_dir_,
                                               db_.get(), db_.get()));
  if (!text_database_->Init(history_publisher_.get())) {
    LOG(WARNING) << "Text database initialization failed, running without it.";
    text_database_.reset();
  }
  if (db_->needs_version_17_migration()) {
    // See needs_version_17_migration() decl for more. In this case, we want
    // to erase all the text database files. This must be done after the text
    // database manager has been initialized, since it knows about all the
    // files it manages.
    text_database_->DeleteAll();
  }

  // Thumbnail database.
  thumbnail_db_.reset(new ThumbnailDatabase());
  if (!db_->GetNeedsThumbnailMigration()) {
    // No convertion needed - use new filename right away.
    thumbnail_name = GetFaviconsFileName();
  }
  if (thumbnail_db_->Init(thumbnail_name,
                          history_publisher_.get(),
                          db_.get()) != sql::INIT_OK) {
    // Unlike the main database, we don't error out when the database is too
    // new because this error is much less severe. Generally, this shouldn't
    // happen since the thumbnail and main datbase versions should be in sync.
    // We'll just continue without thumbnails & favicons in this case or any
    // other error.
    LOG(WARNING) << "Could not initialize the thumbnail database.";
    thumbnail_db_.reset();
  }

  if (db_->GetNeedsThumbnailMigration()) {
    VLOG(1) << "Starting TopSites migration";
    delegate_->StartTopSitesMigration(id_);
  }

  // Archived database.
  if (db_->needs_version_17_migration()) {
    // See needs_version_17_migration() decl for more. In this case, we want
    // to delete the archived database and need to do so before we try to
    // open the file. We can ignore any error (maybe the file doesn't exist).
    file_util::Delete(archived_name, false);
  }
  archived_db_.reset(new ArchivedDatabase());
  if (!archived_db_->Init(archived_name)) {
    LOG(WARNING) << "Could not initialize the archived database.";
    archived_db_.reset();
  }

  // Tell the expiration module about all the nice databases we made. This must
  // happen before db_->Init() is called since the callback ForceArchiveHistory
  // may need to expire stuff.
  //
  // *sigh*, this can all be cleaned up when that migration code is removed.
  // The main DB initialization should intuitively be first (not that it
  // actually matters) and the expirer should be set last.
  expirer_.SetDatabases(db_.get(), archived_db_.get(),
                        thumbnail_db_.get(), text_database_.get());

  // Open the long-running transaction.
  db_->BeginTransaction();
  if (thumbnail_db_.get())
    thumbnail_db_->BeginTransaction();
  if (archived_db_.get())
    archived_db_->BeginTransaction();
  if (text_database_.get())
    text_database_->BeginTransaction();

  // Get the first item in our database.
  db_->GetStartDate(&first_recorded_time_);

  // Start expiring old stuff.
  expirer_.StartArchivingOldStuff(TimeDelta::FromDays(kArchiveDaysThreshold));

  HISTOGRAM_TIMES("History.InitTime",
                  TimeTicks::Now() - beginning_time);
}

std::pair<URLID, VisitID> HistoryBackend::AddPageVisit(
    const GURL& url,
    Time time,
    VisitID referring_visit,
    content::PageTransition transition,
    VisitSource visit_source) {
  // Top-level frame navigations are visible, everything else is hidden
  bool new_hidden = !content::PageTransitionIsMainFrame(transition);

  // NOTE: This code must stay in sync with
  // ExpireHistoryBackend::ExpireURLsForVisits().
  // TODO(pkasting): http://b/1148304 We shouldn't be marking so many URLs as
  // typed, which would eliminate the need for this code.
  int typed_increment = 0;
  content::PageTransition transition_type =
      content::PageTransitionStripQualifier(transition);
  if ((transition_type == content::PAGE_TRANSITION_TYPED &&
      !content::PageTransitionIsRedirect(transition)) ||
      transition_type == content::PAGE_TRANSITION_KEYWORD_GENERATED)
    typed_increment = 1;

  // See if this URL is already in the DB.
  URLRow url_info(url);
  URLID url_id = db_->GetRowForURL(url, &url_info);
  if (url_id) {
    // Update of an existing row.
    if (content::PageTransitionStripQualifier(transition) !=
        content::PAGE_TRANSITION_RELOAD)
      url_info.set_visit_count(url_info.visit_count() + 1);
    if (typed_increment)
      url_info.set_typed_count(url_info.typed_count() + typed_increment);
    url_info.set_last_visit(time);

    // Only allow un-hiding of pages, never hiding.
    if (!new_hidden)
      url_info.set_hidden(false);

    db_->UpdateURLRow(url_id, url_info);
  } else {
    // Addition of a new row.
    url_info.set_visit_count(1);
    url_info.set_typed_count(typed_increment);
    url_info.set_last_visit(time);
    url_info.set_hidden(new_hidden);

    url_id = db_->AddURL(url_info);
    if (!url_id) {
      NOTREACHED() << "Adding URL failed.";
      return std::make_pair(0, 0);
    }
    url_info.id_ = url_id;

    // We don't actually add the URL to the full text index at this point. It
    // might be nice to do this so that even if we get no title or body, the
    // user can search for URL components and get the page.
    //
    // However, in most cases, we'll get at least a title and usually contents,
    // and this add will be redundant, slowing everything down. As a result,
    // we ignore this edge case.
  }

  // Add the visit with the time to the database.
  VisitRow visit_info(url_id, time, referring_visit, transition, 0);
  VisitID visit_id = db_->AddVisit(&visit_info, visit_source);

  if (visit_info.visit_time < first_recorded_time_)
    first_recorded_time_ = visit_info.visit_time;

  // Broadcast a notification of the visit.
  if (visit_id) {
    URLVisitedDetails* details = new URLVisitedDetails;
    details->transition = transition;
    details->row = url_info;
    // TODO(meelapshah) Disabled due to potential PageCycler regression.
    // Re-enable this.
    // GetMostRecentRedirectsTo(url, &details->redirects);
    BroadcastNotifications(chrome::NOTIFICATION_HISTORY_URL_VISITED, details);
  } else {
    VLOG(0) << "Failed to build visit insert statement:  "
            << "url_id = " << url_id;
  }

  return std::make_pair(url_id, visit_id);
}

void HistoryBackend::AddPagesWithDetails(const std::vector<URLRow>& urls,
                                         VisitSource visit_source) {
  if (!db_.get())
    return;

  scoped_ptr<URLsModifiedDetails> modified(new URLsModifiedDetails);
  for (std::vector<URLRow>::const_iterator i = urls.begin();
       i != urls.end(); ++i) {
    DCHECK(!i->last_visit().is_null());

    // We will add to either the archived database or the main one depending on
    // the date of the added visit.
    URLDatabase* url_database;
    VisitDatabase* visit_database;
    if (IsExpiredVisitTime(i->last_visit())) {
      if (!archived_db_.get())
        return;  // No archived database to save it to, just forget this.
      url_database = archived_db_.get();
      visit_database = archived_db_.get();
    } else {
      url_database = db_.get();
      visit_database = db_.get();
    }

    URLRow existing_url;
    URLID url_id = url_database->GetRowForURL(i->url(), &existing_url);
    if (!url_id) {
      // Add the page if it doesn't exist.
      url_id = url_database->AddURL(*i);
      if (!url_id) {
        NOTREACHED() << "Could not add row to DB";
        return;
      }

      if (i->typed_count() > 0)
        modified->changed_urls.push_back(*i);
    }

    // Add the page to the full text index. This function is also used for
    // importing. Even though we don't have page contents, we can at least
    // add the title and URL to the index so they can be searched. We don't
    // bother to delete any already-existing FTS entries for the URL, since
    // this is normally called on import.
    //
    // If you ever import *after* first run (selecting import from the menu),
    // then these additional entries will "shadow" the originals when querying
    // for the most recent match only, and the user won't get snippets. This is
    // a very minor issue, and fixing it will make import slower, so we don't
    // bother.
    bool has_indexed = false;
    if (text_database_.get()) {
      // We do not have to make it update the visit database, below, we will
      // create the visit entry with the indexed flag set.
      has_indexed = text_database_->AddPageData(i->url(), url_id, 0,
                                                i->last_visit(),
                                                i->title(), string16());
    }

    // Sync code manages the visits itself.
    if (visit_source != SOURCE_SYNCED) {
      // Make up a visit to correspond to the last visit to the page.
      VisitRow visit_info(url_id, i->last_visit(), 0,
                          content::PageTransitionFromInt(
                              content::PAGE_TRANSITION_LINK |
                              content::PAGE_TRANSITION_CHAIN_START |
                              content::PAGE_TRANSITION_CHAIN_END), 0);
      visit_info.is_indexed = has_indexed;
      if (!visit_database->AddVisit(&visit_info, visit_source)) {
        NOTREACHED() << "Adding visit failed.";
        return;
      }

      if (visit_info.visit_time < first_recorded_time_)
        first_recorded_time_ = visit_info.visit_time;
    }
  }

  // Broadcast a notification for typed URLs that have been modified. This
  // will be picked up by the in-memory URL database on the main thread.
  //
  // TODO(brettw) bug 1140015: Add an "add page" notification so the history
  // views can keep in sync.
  BroadcastNotifications(chrome::NOTIFICATION_HISTORY_TYPED_URLS_MODIFIED,
                         modified.release());

  ScheduleCommit();
}

bool HistoryBackend::IsExpiredVisitTime(const base::Time& time) {
  return time < expirer_.GetCurrentArchiveTime();
}

void HistoryBackend::SetPageTitle(const GURL& url,
                                  const string16& title) {
  if (!db_.get())
    return;

  // Search for recent redirects which should get the same title. We make a
  // dummy list containing the exact URL visited if there are no redirects so
  // the processing below can be the same.
  history::RedirectList dummy_list;
  history::RedirectList* redirects;
  RedirectCache::iterator iter = recent_redirects_.Get(url);
  if (iter != recent_redirects_.end()) {
    redirects = &iter->second;

    // This redirect chain should have the destination URL as the last item.
    DCHECK(!redirects->empty());
    DCHECK(redirects->back() == url);
  } else {
    // No redirect chain stored, make up one containing the URL we want so we
    // can use the same logic below.
    dummy_list.push_back(url);
    redirects = &dummy_list;
  }

  bool typed_url_changed = false;
  std::vector<URLRow> changed_urls;
  for (size_t i = 0; i < redirects->size(); i++) {
    URLRow row;
    URLID row_id = db_->GetRowForURL(redirects->at(i), &row);
    if (row_id && row.title() != title) {
      row.set_title(title);
      db_->UpdateURLRow(row_id, row);
      changed_urls.push_back(row);
      if (row.typed_count() > 0)
        typed_url_changed = true;
    }
  }

  // Broadcast notifications for typed URLs that have changed. This will
  // update the in-memory database.
  //
  // TODO(brettw) bug 1140020: Broadcast for all changes (not just typed),
  // in which case some logic can be removed.
  if (typed_url_changed) {
    URLsModifiedDetails* modified =
        new URLsModifiedDetails;
    for (size_t i = 0; i < changed_urls.size(); i++) {
      if (changed_urls[i].typed_count() > 0)
        modified->changed_urls.push_back(changed_urls[i]);
    }
    BroadcastNotifications(chrome::NOTIFICATION_HISTORY_TYPED_URLS_MODIFIED,
                           modified);
  }

  // Update the full text index.
  if (text_database_.get())
    text_database_->AddPageTitle(url, title);

  // Only bother committing if things changed.
  if (!changed_urls.empty())
    ScheduleCommit();
}

void HistoryBackend::AddPageNoVisitForBookmark(const GURL& url) {
  if (!db_.get())
    return;

  URLRow url_info(url);
  URLID url_id = db_->GetRowForURL(url, &url_info);
  if (url_id) {
    // URL is already known, nothing to do.
    return;
  }
  url_info.set_last_visit(Time::Now());
  // Mark the page hidden. If the user types it in, it'll unhide.
  url_info.set_hidden(true);

  db_->AddURL(url_info);
}

void HistoryBackend::IterateURLs(HistoryService::URLEnumerator* iterator) {
  if (db_.get()) {
    HistoryDatabase::URLEnumerator e;
    if (db_->InitURLEnumeratorForEverything(&e)) {
      URLRow info;
      while (e.GetNextURL(&info)) {
        iterator->OnURL(info.url());
      }
      iterator->OnComplete(true);  // Success.
      return;
    }
  }
  iterator->OnComplete(false);  // Failure.
}

bool HistoryBackend::GetAllTypedURLs(std::vector<history::URLRow>* urls) {
  if (db_.get())
    return db_->GetAllTypedUrls(urls);
  return false;
}

bool HistoryBackend::GetVisitsForURL(URLID id, VisitVector* visits) {
  if (db_.get())
    return db_->GetVisitsForURL(id, visits);
  return false;
}

bool HistoryBackend::GetMostRecentVisitsForURL(URLID id,
                                               int max_visits,
                                               VisitVector* visits) {
  if (db_.get())
    return db_->GetMostRecentVisitsForURL(id, max_visits, visits);
  return false;
}

bool HistoryBackend::UpdateURL(URLID id, const history::URLRow& url) {
  if (db_.get())
    return db_->UpdateURLRow(id, url);
  return false;
}

bool HistoryBackend::AddVisits(const GURL& url,
                               const std::vector<VisitInfo>& visits,
                               VisitSource visit_source) {
  if (db_.get()) {
    for (std::vector<VisitInfo>::const_iterator visit = visits.begin();
         visit != visits.end(); ++visit) {
      if (!AddPageVisit(
              url, visit->first, 0, visit->second, visit_source).first) {
        return false;
      }
    }
    ScheduleCommit();
    return true;
  }
  return false;
}

bool HistoryBackend::RemoveVisits(const VisitVector& visits) {
  if (!db_.get())
    return false;

  expirer_.ExpireVisits(visits);
  ScheduleCommit();
  return true;
}

bool HistoryBackend::GetVisitsSource(const VisitVector& visits,
                                     VisitSourceMap* sources) {
  if (!db_.get())
    return false;

  db_->GetVisitsSource(visits, sources);
  return true;
}

bool HistoryBackend::GetURL(const GURL& url, history::URLRow* url_row) {
  if (db_.get())
    return db_->GetRowForURL(url, url_row) != 0;
  return false;
}

void HistoryBackend::QueryURL(scoped_refptr<QueryURLRequest> request,
                              const GURL& url,
                              bool want_visits) {
  if (request->canceled())
    return;

  bool success = false;
  URLRow* row = &request->value.a;
  VisitVector* visits = &request->value.b;
  if (db_.get()) {
    if (db_->GetRowForURL(url, row)) {
      // Have a row.
      success = true;

      // Optionally query the visits.
      if (want_visits)
        db_->GetVisitsForURL(row->id(), visits);
    }
  }
  request->ForwardResult(request->handle(), success, row, visits);
}

// Segment usage ---------------------------------------------------------------

void HistoryBackend::DeleteOldSegmentData() {
  if (db_.get())
    db_->DeleteSegmentData(Time::Now() -
                           TimeDelta::FromDays(kSegmentDataRetention));
}

void HistoryBackend::SetSegmentPresentationIndex(SegmentID segment_id,
                                                 int index) {
  if (db_.get())
    db_->SetSegmentPresentationIndex(segment_id, index);
}

void HistoryBackend::QuerySegmentUsage(
    scoped_refptr<QuerySegmentUsageRequest> request,
    const Time from_time,
    int max_result_count) {
  if (request->canceled())
    return;

  if (db_.get()) {
    db_->QuerySegmentUsage(from_time, max_result_count, &request->value.get());

    // If this is the first time we query segments, invoke
    // DeleteOldSegmentData asynchronously. We do this to cleanup old
    // entries.
    if (!segment_queried_) {
      segment_queried_ = true;
      MessageLoop::current()->PostTask(
          FROM_HERE,
          base::Bind(&HistoryBackend::DeleteOldSegmentData, this));
    }
  }
  request->ForwardResult(request->handle(), &request->value.get());
}

// Keyword visits --------------------------------------------------------------

void HistoryBackend::SetKeywordSearchTermsForURL(const GURL& url,
                                                 TemplateURLID keyword_id,
                                                 const string16& term) {
  if (!db_.get())
    return;

  // Get the ID for this URL.
  URLRow url_row;
  if (!db_->GetRowForURL(url, &url_row)) {
    // There is a small possibility the url was deleted before the keyword
    // was added. Ignore the request.
    return;
  }

  db_->SetKeywordSearchTermsForURL(url_row.id(), keyword_id, term);

  // details is deleted by BroadcastNotifications.
  KeywordSearchTermDetails* details = new KeywordSearchTermDetails;
  details->url = url;
  details->keyword_id = keyword_id;
  details->term = term;
  BroadcastNotifications(
      chrome::NOTIFICATION_HISTORY_KEYWORD_SEARCH_TERM_UPDATED, details);
  ScheduleCommit();
}

void HistoryBackend::DeleteAllSearchTermsForKeyword(
    TemplateURLID keyword_id) {
  if (!db_.get())
    return;

  db_->DeleteAllSearchTermsForKeyword(keyword_id);
  // TODO(sky): bug 1168470. Need to move from archive dbs too.
  ScheduleCommit();
}

void HistoryBackend::GetMostRecentKeywordSearchTerms(
    scoped_refptr<GetMostRecentKeywordSearchTermsRequest> request,
    TemplateURLID keyword_id,
    const string16& prefix,
    int max_count) {
  if (request->canceled())
    return;

  if (db_.get()) {
    db_->GetMostRecentKeywordSearchTerms(keyword_id, prefix, max_count,
                                         &(request->value));
  }
  request->ForwardResult(request->handle(), &request->value);
}

// Downloads -------------------------------------------------------------------

void HistoryBackend::GetNextDownloadId(
    scoped_refptr<DownloadNextIdRequest> request) {
  if (request->canceled()) return;
  if (db_.get()) {
    request->value = db_->next_download_id();
  } else {
    request->value = 0;
  }
  request->ForwardResult(request->value);
}

// Get all the download entries from the database.
void HistoryBackend::QueryDownloads(
    scoped_refptr<DownloadQueryRequest> request) {
  if (request->canceled())
    return;
  if (db_.get())
    db_->QueryDownloads(&request->value);
  request->ForwardResult(&request->value);
}

// Clean up entries that has been corrupted (because of the crash, for example).
void HistoryBackend::CleanUpInProgressEntries() {
  if (db_.get()) {
    // If some "in progress" entries were not updated when Chrome exited, they
    // need to be cleaned up.
    db_->CleanUpInProgressEntries();
  }
}

// Update a particular download entry.
void HistoryBackend::UpdateDownload(const DownloadPersistentStoreInfo& data) {
  if (db_.get())
    db_->UpdateDownload(data);
}

// Update the path of a particular download entry.
void HistoryBackend::UpdateDownloadPath(const FilePath& path,
                                        int64 db_handle) {
  if (db_.get())
    db_->UpdateDownloadPath(path, db_handle);
}

// Create a new download entry and pass back the db_handle to it.
void HistoryBackend::CreateDownload(
    scoped_refptr<DownloadCreateRequest> request,
    int32 id,
    const DownloadPersistentStoreInfo& history_info) {
  int64 db_handle = 0;
  if (!request->canceled()) {
    if (db_.get())
      db_handle = db_->CreateDownload(history_info);
    request->ForwardResult(id, db_handle);
  }
}

void HistoryBackend::RemoveDownload(int64 db_handle) {
  if (db_.get())
    db_->RemoveDownload(db_handle);
}

void HistoryBackend::RemoveDownloadsBetween(const Time remove_begin,
                                            const Time remove_end) {
  if (db_.get())
    db_->RemoveDownloadsBetween(remove_begin, remove_end);
}

void HistoryBackend::QueryHistory(scoped_refptr<QueryHistoryRequest> request,
                                  const string16& text_query,
                                  const QueryOptions& options) {
  if (request->canceled())
    return;

  TimeTicks beginning_time = TimeTicks::Now();

  if (db_.get()) {
    if (text_query.empty()) {
      // Basic history query for the main database.
      QueryHistoryBasic(db_.get(), db_.get(), options, &request->value);

      // Now query the archived database. This is a bit tricky because we don't
      // want to query it if the queried time range isn't going to find anything
      // in it.
      // TODO(brettw) bug 1171036: do blimpie querying for the archived database
      // as well.
      // if (archived_db_.get() &&
      //     expirer_.GetCurrentArchiveTime() - TimeDelta::FromDays(7)) {
    } else {
      // Full text history query.
      QueryHistoryFTS(text_query, options, &request->value);
    }
  }

  request->ForwardResult(request->handle(), &request->value);

  UMA_HISTOGRAM_TIMES("History.QueryHistory",
                      TimeTicks::Now() - beginning_time);
}

// Basic time-based querying of history.
void HistoryBackend::QueryHistoryBasic(URLDatabase* url_db,
                                       VisitDatabase* visit_db,
                                       const QueryOptions& options,
                                       QueryResults* result) {
  // First get all visits.
  VisitVector visits;
  visit_db->GetVisibleVisitsInRange(options.begin_time, options.end_time,
                                    options.max_count, &visits);
  DCHECK(options.max_count == 0 ||
         static_cast<int>(visits.size()) <= options.max_count);

  // Now add them and the URL rows to the results.
  URLResult url_result;
  for (size_t i = 0; i < visits.size(); i++) {
    const VisitRow visit = visits[i];

    // Add a result row for this visit, get the URL info from the DB.
    if (!url_db->GetURLRow(visit.url_id, &url_result)) {
      VLOG(0) << "Failed to get id " << visit.url_id
              << " from history.urls.";
      continue;  // DB out of sync and URL doesn't exist, try to recover.
    }

    if (!url_result.url().is_valid()) {
      VLOG(0) << "Got invalid URL from history.urls with id "
              << visit.url_id << ":  "
              << url_result.url().possibly_invalid_spec();
      continue;  // Don't report invalid URLs in case of corruption.
    }

    // The archived database may be out of sync with respect to starring,
    // titles, last visit date, etc. Therefore, we query the main DB if the
    // current URL database is not the main one.
    if (url_db == db_.get()) {
      // Currently querying the archived DB, update with the main database to
      // catch any interesting stuff. This will update it if it exists in the
      // main DB, and do nothing otherwise.
      db_->GetRowForURL(url_result.url(), &url_result);
    }

    url_result.set_visit_time(visit.visit_time);

    // We don't set any of the query-specific parts of the URLResult, since
    // snippets and stuff don't apply to basic querying.
    result->AppendURLBySwapping(&url_result);
  }

  if (options.begin_time <= first_recorded_time_)
    result->set_reached_beginning(true);
}

void HistoryBackend::QueryHistoryFTS(const string16& text_query,
                                     const QueryOptions& options,
                                     QueryResults* result) {
  if (!text_database_.get())
    return;

  // Full text query, first get all the FTS results in the time range.
  std::vector<TextDatabase::Match> fts_matches;
  Time first_time_searched;
  text_database_->GetTextMatches(text_query, options,
                                 &fts_matches, &first_time_searched);

  URLQuerier querier(db_.get(), archived_db_.get(), true);

  // Now get the row and visit information for each one.
  URLResult url_result;  // Declare outside loop to prevent re-construction.
  for (size_t i = 0; i < fts_matches.size(); i++) {
    if (options.max_count != 0 &&
        static_cast<int>(result->size()) >= options.max_count)
      break;  // Got too many items.

    // Get the URL, querying the main and archived databases as necessary. If
    // this is not found, the history and full text search databases are out
    // of sync and we give up with this result.
    if (!querier.GetRowForURL(fts_matches[i].url, &url_result))
      continue;

    if (!url_result.url().is_valid())
      continue;  // Don't report invalid URLs in case of corruption.

    // Copy over the FTS stuff that the URLDatabase doesn't know about.
    // We do this with swap() to avoid copying, since we know we don't
    // need the original any more. Note that we override the title with the
    // one from FTS, since that will match the title_match_positions (the
    // FTS title and the history DB title may differ).
    url_result.set_title(fts_matches[i].title);
    url_result.title_match_positions_.swap(
        fts_matches[i].title_match_positions);
    url_result.snippet_.Swap(&fts_matches[i].snippet);

    // The visit time also comes from the full text search database. Since it
    // has the time, we can avoid an extra query of the visits table.
    url_result.set_visit_time(fts_matches[i].time);

    // Add it to the vector, this will clear our |url_row| object as a
    // result of the swap.
    result->AppendURLBySwapping(&url_result);
  }

  if (options.begin_time <= first_recorded_time_)
    result->set_reached_beginning(true);
}

// Frontend to GetMostRecentRedirectsFrom from the history thread.
void HistoryBackend::QueryRedirectsFrom(
    scoped_refptr<QueryRedirectsRequest> request,
    const GURL& url) {
  if (request->canceled())
    return;
  bool success = GetMostRecentRedirectsFrom(url, &request->value);
  request->ForwardResult(request->handle(), url, success, &request->value);
}

void HistoryBackend::QueryRedirectsTo(
    scoped_refptr<QueryRedirectsRequest> request,
    const GURL& url) {
  if (request->canceled())
    return;
  bool success = GetMostRecentRedirectsTo(url, &request->value);
  request->ForwardResult(request->handle(), url, success, &request->value);
}

void HistoryBackend::GetVisibleVisitCountToHost(
    scoped_refptr<GetVisibleVisitCountToHostRequest> request,
    const GURL& url) {
  if (request->canceled())
    return;
  int count = 0;
  Time first_visit;
  const bool success = db_.get() &&
      db_->GetVisibleVisitCountToHost(url, &count, &first_visit);
  request->ForwardResult(request->handle(), success, count, first_visit);
}

void HistoryBackend::QueryTopURLsAndRedirects(
    scoped_refptr<QueryTopURLsAndRedirectsRequest> request,
    int result_count) {
  if (request->canceled())
    return;

  if (!db_.get()) {
    request->ForwardResult(request->handle(), false, NULL, NULL);
    return;
  }

  std::vector<GURL>* top_urls = &request->value.a;
  history::RedirectMap* redirects = &request->value.b;

  ScopedVector<PageUsageData> data;
  db_->QuerySegmentUsage(base::Time::Now() - base::TimeDelta::FromDays(90),
      result_count, &data.get());

  for (size_t i = 0; i < data.size(); ++i) {
    top_urls->push_back(data[i]->GetURL());
    RefCountedVector<GURL>* list = new RefCountedVector<GURL>;
    GetMostRecentRedirectsFrom(top_urls->back(), &list->data);
    (*redirects)[top_urls->back()] = list;
  }

  request->ForwardResult(request->handle(), true, top_urls, redirects);
}

// Will replace QueryTopURLsAndRedirectsRequest.
void HistoryBackend::QueryMostVisitedURLs(
    scoped_refptr<QueryMostVisitedURLsRequest> request,
    int result_count,
    int days_back) {
  if (request->canceled())
    return;

  if (!db_.get()) {
    // No History Database - return an empty list.
    request->ForwardResult(request->handle(), MostVisitedURLList());
    return;
  }

  MostVisitedURLList* result = &request->value;
  QueryMostVisitedURLsImpl(result_count, days_back, result);
  request->ForwardResult(request->handle(), *result);
}

void HistoryBackend::QueryMostVisitedURLsImpl(int result_count,
                                              int days_back,
                                              MostVisitedURLList* result) {
  if (!db_.get())
    return;

  ScopedVector<PageUsageData> data;
  db_->QuerySegmentUsage(base::Time::Now() -
                         base::TimeDelta::FromDays(days_back),
                         result_count, &data.get());

  for (size_t i = 0; i < data.size(); ++i) {
    PageUsageData* current_data = data[i];
    RedirectList redirects;
    GetMostRecentRedirectsFrom(current_data->GetURL(), &redirects);
    MostVisitedURL url = MakeMostVisitedURL(*current_data, redirects);
    result->push_back(url);
  }
}

void HistoryBackend::GetRedirectsFromSpecificVisit(
    VisitID cur_visit, history::RedirectList* redirects) {
  // Follow any redirects from the given visit and add them to the list.
  // It *should* be impossible to get a circular chain here, but we check
  // just in case to avoid infinite loops.
  GURL cur_url;
  std::set<VisitID> visit_set;
  visit_set.insert(cur_visit);
  while (db_->GetRedirectFromVisit(cur_visit, &cur_visit, &cur_url)) {
    if (visit_set.find(cur_visit) != visit_set.end()) {
      NOTREACHED() << "Loop in visit chain, giving up";
      return;
    }
    visit_set.insert(cur_visit);
    redirects->push_back(cur_url);
  }
}

void HistoryBackend::GetRedirectsToSpecificVisit(
    VisitID cur_visit,
    history::RedirectList* redirects) {
  // Follow redirects going to cur_visit. These are added to |redirects| in
  // the order they are found. If a redirect chain looks like A -> B -> C and
  // |cur_visit| = C, redirects will be {B, A} in that order.
  if (!db_.get())
    return;

  GURL cur_url;
  std::set<VisitID> visit_set;
  visit_set.insert(cur_visit);
  while (db_->GetRedirectToVisit(cur_visit, &cur_visit, &cur_url)) {
    if (visit_set.find(cur_visit) != visit_set.end()) {
      NOTREACHED() << "Loop in visit chain, giving up";
      return;
    }
    visit_set.insert(cur_visit);
    redirects->push_back(cur_url);
  }
}

bool HistoryBackend::GetMostRecentRedirectsFrom(
    const GURL& from_url,
    history::RedirectList* redirects) {
  redirects->clear();
  if (!db_.get())
    return false;

  URLID from_url_id = db_->GetRowForURL(from_url, NULL);
  VisitID cur_visit = db_->GetMostRecentVisitForURL(from_url_id, NULL);
  if (!cur_visit)
    return false;  // No visits for URL.

  GetRedirectsFromSpecificVisit(cur_visit, redirects);
  return true;
}

bool HistoryBackend::GetMostRecentRedirectsTo(
    const GURL& to_url,
    history::RedirectList* redirects) {
  redirects->clear();
  if (!db_.get())
    return false;

  URLID to_url_id = db_->GetRowForURL(to_url, NULL);
  VisitID cur_visit = db_->GetMostRecentVisitForURL(to_url_id, NULL);
  if (!cur_visit)
    return false;  // No visits for URL.

  GetRedirectsToSpecificVisit(cur_visit, redirects);
  return true;
}

void HistoryBackend::ScheduleAutocomplete(HistoryURLProvider* provider,
                                          HistoryURLProviderParams* params) {
  // ExecuteWithDB should handle the NULL database case.
  provider->ExecuteWithDB(this, db_.get(), params);
}

void HistoryBackend::SetPageContents(const GURL& url,
                                     const string16& contents) {
  // This is histogrammed in the text database manager.
  if (!text_database_.get())
    return;
  text_database_->AddPageContents(url, contents);
}

void HistoryBackend::SetPageThumbnail(
    const GURL& url,
    const gfx::Image* thumbnail,
    const ThumbnailScore& score) {
  if (!db_.get() || !thumbnail_db_.get())
    return;

  URLRow url_row;
  URLID url_id = db_->GetRowForURL(url, &url_row);
  if (url_id) {
    thumbnail_db_->SetPageThumbnail(url, url_id, thumbnail, score,
                                    url_row.last_visit());
  }

  ScheduleCommit();
}

void HistoryBackend::GetPageThumbnail(
    scoped_refptr<GetPageThumbnailRequest> request,
    const GURL& page_url) {
  if (request->canceled())
    return;

  scoped_refptr<RefCountedBytes> data;
  GetPageThumbnailDirectly(page_url, &data);

  request->ForwardResult(request->handle(), data);
}

void HistoryBackend::GetPageThumbnailDirectly(
    const GURL& page_url,
    scoped_refptr<RefCountedBytes>* data) {
  if (thumbnail_db_.get()) {
    *data = new RefCountedBytes;

    // Time the result.
    TimeTicks beginning_time = TimeTicks::Now();

    history::RedirectList redirects;
    URLID url_id;
    bool success = false;

    // If there are some redirects, try to get a thumbnail from the last
    // redirect destination.
    if (GetMostRecentRedirectsFrom(page_url, &redirects) &&
        !redirects.empty()) {
      if ((url_id = db_->GetRowForURL(redirects.back(), NULL)))
        success = thumbnail_db_->GetPageThumbnail(url_id, &(*data)->data());
    }

    // If we don't have a thumbnail from redirects, try the URL directly.
    if (!success) {
      if ((url_id = db_->GetRowForURL(page_url, NULL)))
        success = thumbnail_db_->GetPageThumbnail(url_id, &(*data)->data());
    }

    // In this rare case, we start to mine the older redirect sessions
    // from the visit table to try to find a thumbnail.
    if (!success) {
      success = GetThumbnailFromOlderRedirect(page_url, &(*data)->data());
    }

    if (!success)
      *data = NULL;  // This will tell the callback there was an error.

    UMA_HISTOGRAM_TIMES("History.GetPageThumbnail",
                        TimeTicks::Now() - beginning_time);
  }
}

void HistoryBackend::MigrateThumbnailsDatabase() {
  // If there is no History DB, we can't record that the migration was done.
  // It will be recorded on the next run.
  if (db_.get()) {
    // If there is no thumbnail DB, we can still record a successful migration.
    if (thumbnail_db_.get()) {
      thumbnail_db_->RenameAndDropThumbnails(GetThumbnailFileName(),
                                             GetFaviconsFileName());
    }
    db_->ThumbnailMigrationDone();
  }
}

bool HistoryBackend::GetThumbnailFromOlderRedirect(
    const GURL& page_url,
    std::vector<unsigned char>* data) {
  // Look at a few previous visit sessions.
  VisitVector older_sessions;
  URLID page_url_id = db_->GetRowForURL(page_url, NULL);
  static const int kVisitsToSearchForThumbnail = 4;
  db_->GetMostRecentVisitsForURL(
      page_url_id, kVisitsToSearchForThumbnail, &older_sessions);

  // Iterate across all those previous visits, and see if any of the
  // final destinations of those redirect chains have a good thumbnail
  // for us.
  bool success = false;
  for (VisitVector::const_iterator it = older_sessions.begin();
       !success && it != older_sessions.end(); ++it) {
    history::RedirectList redirects;
    if (it->visit_id) {
      GetRedirectsFromSpecificVisit(it->visit_id, &redirects);

      if (!redirects.empty()) {
        URLID url_id;
        if ((url_id = db_->GetRowForURL(redirects.back(), NULL)))
          success = thumbnail_db_->GetPageThumbnail(url_id, data);
      }
    }
  }

  return success;
}

void HistoryBackend::GetFavicon(scoped_refptr<GetFaviconRequest> request,
                                const GURL& icon_url,
                                int icon_types) {
  UpdateFaviconMappingAndFetchImpl(NULL, icon_url, request, icon_types);
}

void HistoryBackend::UpdateFaviconMappingAndFetch(
    scoped_refptr<GetFaviconRequest> request,
    const GURL& page_url,
    const GURL& icon_url,
    IconType icon_type) {
  UpdateFaviconMappingAndFetchImpl(&page_url, icon_url, request, icon_type);
}

void HistoryBackend::SetFaviconOutOfDateForPage(const GURL& page_url) {
  std::vector<IconMapping> icon_mappings;

  if (!thumbnail_db_.get() ||
      !thumbnail_db_->GetIconMappingsForPageURL(page_url,
                                                &icon_mappings))
    return;

  for (std::vector<IconMapping>::iterator m = icon_mappings.begin();
       m != icon_mappings.end(); ++m) {
    thumbnail_db_->SetFaviconLastUpdateTime(m->icon_id, Time());
  }
  ScheduleCommit();
}

void HistoryBackend::CloneFavicon(const GURL& old_page_url,
                                  const GURL& new_page_url) {
  if (!thumbnail_db_.get())
    return;

  // Prevent cross-domain cloning.
  if (old_page_url.GetOrigin() != new_page_url.GetOrigin())
    return;

  thumbnail_db_->CloneIconMapping(old_page_url, new_page_url);
  ScheduleCommit();
}

void HistoryBackend::SetImportedFavicons(
    const std::vector<ImportedFaviconUsage>& favicon_usage) {
  if (!db_.get() || !thumbnail_db_.get())
    return;

  Time now = Time::Now();

  // Track all URLs that had their favicons set or updated.
  std::set<GURL> favicons_changed;

  for (size_t i = 0; i < favicon_usage.size(); i++) {
    FaviconID favicon_id = thumbnail_db_->GetFaviconIDForFaviconURL(
        favicon_usage[i].favicon_url, history::FAVICON, NULL);
    if (!favicon_id) {
      // This favicon doesn't exist yet, so we create it using the given data.
      favicon_id = thumbnail_db_->AddFavicon(favicon_usage[i].favicon_url,
                                             history::FAVICON);
      if (!favicon_id)
        continue;  // Unable to add the favicon.
      thumbnail_db_->SetFavicon(favicon_id,
          new RefCountedBytes(favicon_usage[i].png_data), now);
    }

    // Save the mapping from all the URLs to the favicon.
    BookmarkService* bookmark_service = GetBookmarkService();
    for (std::set<GURL>::const_iterator url = favicon_usage[i].urls.begin();
         url != favicon_usage[i].urls.end(); ++url) {
      URLRow url_row;
      if (!db_->GetRowForURL(*url, &url_row)) {
        // If the URL is present as a bookmark, add the url in history to
        // save the favicon mapping. This will match with what history db does
        // for regular bookmarked URLs with favicons - when history db is
        // cleaned, we keep an entry in the db with 0 visits as long as that
        // url is bookmarked.
        if (bookmark_service && bookmark_service_->IsBookmarked(*url)) {
          URLRow url_info(*url);
          url_info.set_visit_count(0);
          url_info.set_typed_count(0);
          url_info.set_last_visit(base::Time());
          url_info.set_hidden(false);
          db_->AddURL(url_info);
          thumbnail_db_->AddIconMapping(*url, favicon_id);
          favicons_changed.insert(*url);
        }
      } else {
        if (!thumbnail_db_->GetIconMappingForPageURL(*url, FAVICON, NULL)) {
          // URL is present in history, update the favicon *only* if it is not
          // set already.
          thumbnail_db_->AddIconMapping(*url, favicon_id);
          favicons_changed.insert(*url);
        }
      }
    }
  }

  if (!favicons_changed.empty()) {
    // Send the notification about the changed favicon URLs.
    FaviconChangeDetails* changed_details = new FaviconChangeDetails;
    changed_details->urls.swap(favicons_changed);
    BroadcastNotifications(chrome::NOTIFICATION_FAVICON_CHANGED,
                           changed_details);
  }
}

void HistoryBackend::UpdateFaviconMappingAndFetchImpl(
    const GURL* page_url,
    const GURL& icon_url,
    scoped_refptr<GetFaviconRequest> request,
    int icon_types) {
  // Check only a single type was given when the page_url was specified.
  DCHECK(!page_url || (page_url && (icon_types == FAVICON ||
      icon_types == TOUCH_ICON || icon_types == TOUCH_PRECOMPOSED_ICON)));

  if (request->canceled())
    return;

  FaviconData favicon;

  if (thumbnail_db_.get()) {
    const FaviconID favicon_id =
        thumbnail_db_->GetFaviconIDForFaviconURL(
            icon_url, icon_types, &favicon.icon_type);
    if (favicon_id) {
      scoped_refptr<RefCountedBytes> data = new RefCountedBytes();
      favicon.known_icon = true;
      Time last_updated;
      if (thumbnail_db_->GetFavicon(favicon_id, &last_updated, &data->data(),
                                    NULL)) {
        favicon.expired = (Time::Now() - last_updated) >
            TimeDelta::FromDays(kFaviconRefetchDays);
        favicon.image_data = data;
      }

      if (page_url)
        SetFaviconMapping(*page_url, favicon_id, favicon.icon_type);
    }
    // else case, haven't cached entry yet. Caller is responsible for
    // downloading the favicon and invoking SetFavicon.
  }
  request->ForwardResult(request->handle(), favicon);
}

void HistoryBackend::GetFaviconForURL(
    scoped_refptr<GetFaviconRequest> request,
    const GURL& page_url,
    int icon_types) {
  if (request->canceled())
    return;

  FaviconData favicon;

  // Get the favicon from DB.
  GetFaviconFromDB(page_url, icon_types, &favicon);

  request->ForwardResult(request->handle(), favicon);
}

void HistoryBackend::SetFavicon(
    const GURL& page_url,
    const GURL& icon_url,
    scoped_refptr<RefCountedMemory> data,
    IconType icon_type) {
  DCHECK(data.get());
  if (!thumbnail_db_.get() || !db_.get())
    return;

  FaviconID id = thumbnail_db_->GetFaviconIDForFaviconURL(
      icon_url, icon_type, NULL);
  if (!id)
    id = thumbnail_db_->AddFavicon(icon_url, icon_type);

  // Set the image data.
  thumbnail_db_->SetFavicon(id, data, Time::Now());

  SetFaviconMapping(page_url, id, icon_type);
}

void HistoryBackend::SetFaviconMapping(const GURL& page_url,
                                       FaviconID id,
                                       IconType icon_type) {
  if (!thumbnail_db_.get())
    return;

  // Find all the pages whose favicons we should set, we want to set it for
  // all the pages in the redirect chain if it redirected.
  history::RedirectList dummy_list;
  history::RedirectList* redirects;
  RedirectCache::iterator iter = recent_redirects_.Get(page_url);
  if (iter != recent_redirects_.end()) {
    redirects = &iter->second;

    // This redirect chain should have the destination URL as the last item.
    DCHECK(!redirects->empty());
    DCHECK(redirects->back() == page_url);
  } else {
    // No redirect chain stored, make up one containing the URL we want to we
    // can use the same logic below.
    dummy_list.push_back(page_url);
    redirects = &dummy_list;
  }

  std::set<GURL> favicons_changed;

  // Save page <-> favicon association.
  for (history::RedirectList::const_iterator i(redirects->begin());
       i != redirects->end(); ++i) {
    FaviconID replaced_id;
    if (AddOrUpdateIconMapping(*i, id, icon_type, &replaced_id)) {
      // The page's favicon ID changed. This means that the one we just
      // changed from could have been orphaned, and we need to re-check it.
      // This is not super fast, but this case will get triggered rarely,
      // since normally a page will always map to the same favicon ID. It
      // will mostly happen for favicons we import.
      if (replaced_id && !thumbnail_db_->HasMappingFor(replaced_id))
        thumbnail_db_->DeleteFavicon(replaced_id);

      favicons_changed.insert(*i);
    }
  }

  // Send the notification about the changed favicons.
  FaviconChangeDetails* changed_details = new FaviconChangeDetails;
  changed_details->urls.swap(favicons_changed);
  BroadcastNotifications(chrome::NOTIFICATION_FAVICON_CHANGED,
                         changed_details);

  ScheduleCommit();
}

bool HistoryBackend::AddOrUpdateIconMapping(const GURL& page_url,
                                            FaviconID id,
                                            IconType icon_type,
                                            FaviconID* replaced_icon) {
  *replaced_icon = 0;
  std::vector<IconMapping> icon_mappings;
  if (!thumbnail_db_->GetIconMappingsForPageURL(page_url, &icon_mappings)) {
    // There is no mapping add it directly.
    thumbnail_db_->AddIconMapping(page_url, id);
    return true;
  }
  // Iterate all matched icon mappings,
  // a. If the given icon id and matched icon id are same, return.
  // b. If the given icon type and matched icon type are same, but icon id
  //    are not, update the IconMapping.
  // c. If the given icon_type and matched icon type are not same, but
  //    either of them is ICON_TOUCH or ICON_PRECOMPOSED_TOUCH, update the
  //    IconMapping.
  // d. Otherwise add a icon mapping.
  for (std::vector<IconMapping>::iterator m = icon_mappings.begin();
       m != icon_mappings.end(); ++m) {
    if (m->icon_id == id)
      // The mapping is already there.
      return false;

    if ((icon_type == TOUCH_ICON && m->icon_type == TOUCH_PRECOMPOSED_ICON) ||
        (icon_type == TOUCH_PRECOMPOSED_ICON && m->icon_type == TOUCH_ICON) ||
        (icon_type == m->icon_type)) {
      thumbnail_db_->UpdateIconMapping(m->mapping_id, id);
      *replaced_icon = m->icon_id;
      return true;
    }
  }
  thumbnail_db_->AddIconMapping(page_url, id);
  return true;
}

void HistoryBackend::Commit() {
  if (!db_.get())
    return;

  // Note that a commit may not actually have been scheduled if a caller
  // explicitly calls this instead of using ScheduleCommit. Likewise, we
  // may reset the flag written by a pending commit. But this is OK! It
  // will merely cause extra commits (which is kind of the idea). We
  // could optimize more for this case (we may get two extra commits in
  // some cases) but it hasn't been important yet.
  CancelScheduledCommit();

  db_->CommitTransaction();
  DCHECK(db_->transaction_nesting() == 0) << "Somebody left a transaction open";
  db_->BeginTransaction();

  if (thumbnail_db_.get()) {
    thumbnail_db_->CommitTransaction();
    DCHECK(thumbnail_db_->transaction_nesting() == 0) <<
        "Somebody left a transaction open";
    thumbnail_db_->BeginTransaction();
  }

  if (archived_db_.get()) {
    archived_db_->CommitTransaction();
    archived_db_->BeginTransaction();
  }

  if (text_database_.get()) {
    text_database_->CommitTransaction();
    text_database_->BeginTransaction();
  }
}

void HistoryBackend::ScheduleCommit() {
  if (scheduled_commit_.get())
    return;
  scheduled_commit_ = new CommitLaterTask(this);
  MessageLoop::current()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&CommitLaterTask::RunCommit, scheduled_commit_.get()),
      base::TimeDelta::FromSeconds(kCommitIntervalSeconds));
}

void HistoryBackend::CancelScheduledCommit() {
  if (scheduled_commit_) {
    scheduled_commit_->Cancel();
    scheduled_commit_ = NULL;
  }
}

void HistoryBackend::ProcessDBTaskImpl() {
  if (!db_.get()) {
    // db went away, release all the refs.
    ReleaseDBTasks();
    return;
  }

  // Remove any canceled tasks.
  while (!db_task_requests_.empty() && db_task_requests_.front()->canceled()) {
    db_task_requests_.front()->Release();
    db_task_requests_.pop_front();
  }
  if (db_task_requests_.empty())
    return;

  // Run the first task.
  HistoryDBTaskRequest* request = db_task_requests_.front();
  db_task_requests_.pop_front();
  if (request->value->RunOnDBThread(this, db_.get())) {
    // The task is done. Notify the callback.
    request->ForwardResult();
    // We AddRef'd the request before adding, need to release it now.
    request->Release();
  } else {
    // Tasks wants to run some more. Schedule it at the end of current tasks.
    db_task_requests_.push_back(request);
    // And process it after an invoke later.
    MessageLoop::current()->PostTask(
        FROM_HERE, base::Bind(&HistoryBackend::ProcessDBTaskImpl, this));
  }
}

void HistoryBackend::ReleaseDBTasks() {
  for (std::list<HistoryDBTaskRequest*>::iterator i =
       db_task_requests_.begin(); i != db_task_requests_.end(); ++i) {
    (*i)->Release();
  }
  db_task_requests_.clear();
}

////////////////////////////////////////////////////////////////////////////////
//
// Generic operations
//
////////////////////////////////////////////////////////////////////////////////

void HistoryBackend::DeleteURLs(const std::vector<GURL>& urls) {
  expirer_.DeleteURLs(urls);

  db_->GetStartDate(&first_recorded_time_);
  // Force a commit, if the user is deleting something for privacy reasons, we
  // want to get it on disk ASAP.
  Commit();
}

void HistoryBackend::DeleteURL(const GURL& url) {
  expirer_.DeleteURL(url);

  db_->GetStartDate(&first_recorded_time_);
  // Force a commit, if the user is deleting something for privacy reasons, we
  // want to get it on disk ASAP.
  Commit();
}

void HistoryBackend::ExpireHistoryBetween(
    scoped_refptr<CancelableRequest<base::Closure> > request,
    const std::set<GURL>& restrict_urls,
    Time begin_time,
    Time end_time) {
  if (request->canceled())
    return;

  if (db_.get()) {
    if (begin_time.is_null() && end_time.is_null() && restrict_urls.empty()) {
      // Special case deleting all history so it can be faster and to reduce the
      // possibility of an information leak.
      DeleteAllHistory();
    } else {
      // Clearing parts of history, have the expirer do the depend
      expirer_.ExpireHistoryBetween(restrict_urls, begin_time, end_time);

      // Force a commit, if the user is deleting something for privacy reasons,
      // we want to get it on disk ASAP.
      Commit();
    }
  }

  if (begin_time <= first_recorded_time_)
    db_->GetStartDate(&first_recorded_time_);

  request->ForwardResult();

  if (history_publisher_.get() && restrict_urls.empty())
    history_publisher_->DeleteUserHistoryBetween(begin_time, end_time);
}

void HistoryBackend::URLsNoLongerBookmarked(const std::set<GURL>& urls) {
  if (!db_.get())
    return;

  for (std::set<GURL>::const_iterator i = urls.begin(); i != urls.end(); ++i) {
    URLRow url_row;
    if (!db_->GetRowForURL(*i, &url_row))
      continue;  // The URL isn't in the db; nothing to do.

    VisitVector visits;
    db_->GetVisitsForURL(url_row.id(), &visits);

    if (visits.empty())
      expirer_.DeleteURL(*i);  // There are no more visits; nuke the URL.
  }
}

void HistoryBackend::ProcessDBTask(
    scoped_refptr<HistoryDBTaskRequest> request) {
  DCHECK(request.get());
  if (request->canceled())
    return;

  bool task_scheduled = !db_task_requests_.empty();
  // Make sure we up the refcount of the request. ProcessDBTaskImpl will
  // release when done with the task.
  request->AddRef();
  db_task_requests_.push_back(request.get());
  if (!task_scheduled) {
    // No other tasks are scheduled. Process request now.
    ProcessDBTaskImpl();
  }
}

void HistoryBackend::BroadcastNotifications(
    int type,
    HistoryDetails* details_deleted) {
  // |delegate_| may be NULL if |this| is in the process of closing (closed by
  // HistoryService -> HistroyBackend::Closing().
  if (delegate_.get())
    delegate_->BroadcastNotifications(type, details_deleted);
  else
    delete details_deleted;
}

// Deleting --------------------------------------------------------------------

void HistoryBackend::DeleteAllHistory() {
  // Our approach to deleting all history is:
  //  1. Copy the bookmarks and their dependencies to new tables with temporary
  //     names.
  //  2. Delete the original tables. Since tables can not share pages, we know
  //     that any data we don't want to keep is now in an unused page.
  //  3. Renaming the temporary tables to match the original.
  //  4. Vacuuming the database to delete the unused pages.
  //
  // Since we are likely to have very few bookmarks and their dependencies
  // compared to all history, this is also much faster than just deleting from
  // the original tables directly.

  // Get the bookmarked URLs.
  std::vector<GURL> starred_urls;
  BookmarkService* bookmark_service = GetBookmarkService();
  if (bookmark_service)
    bookmark_service_->GetBookmarks(&starred_urls);

  std::vector<URLRow> kept_urls;
  for (size_t i = 0; i < starred_urls.size(); i++) {
    URLRow row;
    if (!db_->GetRowForURL(starred_urls[i], &row))
      continue;

    // Clear the last visit time so when we write these rows they are "clean."
    row.set_last_visit(Time());
    row.set_visit_count(0);
    row.set_typed_count(0);
    kept_urls.push_back(row);
  }

  // Clear thumbnail and favicon history. The favicons for the given URLs will
  // be kept.
  if (!ClearAllThumbnailHistory(&kept_urls)) {
    LOG(ERROR) << "Thumbnail history could not be cleared";
    // We continue in this error case. If the user wants to delete their
    // history, we should delete as much as we can.
  }

  // ClearAllMainHistory will change the IDs of the URLs in kept_urls. Therfore,
  // we clear the list afterwards to make sure nobody uses this invalid data.
  if (!ClearAllMainHistory(kept_urls))
    LOG(ERROR) << "Main history could not be cleared";
  kept_urls.clear();

  // Delete FTS files & archived history.
  if (text_database_.get()) {
    // We assume that the text database has one transaction on them that we need
    // to close & restart (the long-running history transaction).
    text_database_->CommitTransaction();
    text_database_->DeleteAll();
    text_database_->BeginTransaction();
  }

  if (archived_db_.get()) {
    // Close the database and delete the file.
    archived_db_.reset();
    FilePath archived_file_name = GetArchivedFileName();
    file_util::Delete(archived_file_name, false);

    // Now re-initialize the database (which may fail).
    archived_db_.reset(new ArchivedDatabase());
    if (!archived_db_->Init(archived_file_name)) {
      LOG(WARNING) << "Could not initialize the archived database.";
      archived_db_.reset();
    } else {
      // Open our long-running transaction on this database.
      archived_db_->BeginTransaction();
    }
  }

  db_->GetStartDate(&first_recorded_time_);

  // Send out the notfication that history is cleared. The in-memory datdabase
  // will pick this up and clear itself.
  URLsDeletedDetails* details = new URLsDeletedDetails;
  details->all_history = true;
  BroadcastNotifications(chrome::NOTIFICATION_HISTORY_URLS_DELETED, details);
}

bool HistoryBackend::ClearAllThumbnailHistory(
    std::vector<URLRow>* kept_urls) {
  if (!thumbnail_db_.get()) {
    // When we have no reference to the thumbnail database, maybe there was an
    // error opening it. In this case, we just try to blow it away to try to
    // fix the error if it exists. This may fail, in which case either the
    // file doesn't exist or there's no more we can do.
    file_util::Delete(GetThumbnailFileName(), false);
    return true;
  }

  // Create the duplicate favicon table, this is where the favicons we want
  // to keep will be stored.
  if (!thumbnail_db_->InitTemporaryFaviconsTable())
    return false;

  if (!thumbnail_db_->InitTemporaryIconMappingTable())
    return false;

  // This maps existing favicon IDs to the ones in the temporary table.
  typedef std::map<FaviconID, FaviconID> FaviconMap;
  FaviconMap copied_favicons;

  // Copy all unique favicons to the temporary table, and update all the
  // URLs to have the new IDs.
  for (std::vector<URLRow>::iterator i = kept_urls->begin();
       i != kept_urls->end(); ++i) {
    std::vector<IconMapping> icon_mappings;
    if (!thumbnail_db_->GetIconMappingsForPageURL(i->url(), &icon_mappings))
      continue;

    for (std::vector<IconMapping>::iterator m = icon_mappings.begin();
         m != icon_mappings.end(); ++m) {
      FaviconID old_id = m->icon_id;
      FaviconID new_id;
      FaviconMap::const_iterator found = copied_favicons.find(old_id);
      if (found == copied_favicons.end()) {
        new_id = thumbnail_db_->CopyToTemporaryFaviconTable(old_id);
        copied_favicons[old_id] = new_id;
      } else {
        // We already encountered a URL that used this favicon, use the ID we
        // previously got.
        new_id = found->second;
      }
      // Add Icon mapping, and we don't care wheteher it suceeded or not.
      thumbnail_db_->AddToTemporaryIconMappingTable(i->url(), new_id);
    }
  }

  // Rename the duplicate favicon and icon_mapping back table and recreate the
  // other tables. This will make the database consistent again.
  thumbnail_db_->CommitTemporaryFaviconTable();
  thumbnail_db_->CommitTemporaryIconMappingTable();

  thumbnail_db_->RecreateThumbnailTable();

  // Vacuum to remove all the pages associated with the dropped tables. There
  // must be no transaction open on the table when we do this. We assume that
  // our long-running transaction is open, so we complete it and start it again.
  DCHECK(thumbnail_db_->transaction_nesting() == 1);
  thumbnail_db_->CommitTransaction();
  thumbnail_db_->Vacuum();
  thumbnail_db_->BeginTransaction();
  return true;
}

bool HistoryBackend::ClearAllMainHistory(
    const std::vector<URLRow>& kept_urls) {
  // Create the duplicate URL table. We will copy the kept URLs into this.
  if (!db_->CreateTemporaryURLTable())
    return false;

  // Insert the URLs into the temporary table, we need to keep a map of changed
  // IDs since the ID will be different in the new table.
  typedef std::map<URLID, URLID> URLIDMap;
  URLIDMap old_to_new;  // Maps original ID to new one.
  for (std::vector<URLRow>::const_iterator i = kept_urls.begin();
       i != kept_urls.end();
       ++i) {
    URLID new_id = db_->AddTemporaryURL(*i);
    old_to_new[i->id()] = new_id;
  }

  // Replace the original URL table with the temporary one.
  if (!db_->CommitTemporaryURLTable())
    return false;

  // Delete the old tables and recreate them empty.
  db_->RecreateAllTablesButURL();

  // Vacuum to reclaim the space from the dropped tables. This must be done
  // when there is no transaction open, and we assume that our long-running
  // transaction is currently open.
  db_->CommitTransaction();
  db_->Vacuum();
  db_->BeginTransaction();
  db_->GetStartDate(&first_recorded_time_);

  return true;
}

BookmarkService* HistoryBackend::GetBookmarkService() {
  if (bookmark_service_)
    bookmark_service_->BlockTillLoaded();
  return bookmark_service_;
}

bool HistoryBackend::GetFaviconFromDB(
    const GURL& page_url,
    int icon_types,
    FaviconData* favicon) {
  DCHECK(favicon);

  if (!db_.get() || !thumbnail_db_.get())
    return false;

  bool success = false;
  // Time the query.
  TimeTicks beginning_time = TimeTicks::Now();

  std::vector<IconMapping> icon_mappings;
  // Iterate over the known icons looking for one that includes one of the
  // requested types.
  if (thumbnail_db_->GetIconMappingsForPageURL(page_url, &icon_mappings)) {
    Time last_updated;
    scoped_refptr<RefCountedBytes> data = new RefCountedBytes();
    for (std::vector<IconMapping>::iterator i = icon_mappings.begin();
         i != icon_mappings.end(); ++i) {
      if ((i->icon_type & icon_types) &&
          thumbnail_db_->GetFavicon(i->icon_id, &last_updated,
                                    &data->data(), &favicon->icon_url)) {
        favicon->known_icon = true;
        favicon->expired = (Time::Now() - last_updated) >
            TimeDelta::FromDays(kFaviconRefetchDays);
        favicon->icon_type = i->icon_type;
        favicon->image_data = data;
        success = true;
        break;
      }
    }
  }
  UMA_HISTOGRAM_TIMES("History.GetFavIconFromDB",  // historical name
                      TimeTicks::Now() - beginning_time);
  return success;
}

}  // namespace history
