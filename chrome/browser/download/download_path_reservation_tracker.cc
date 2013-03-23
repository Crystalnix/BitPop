// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/download/download_path_reservation_tracker.h"

#include <map>

#include "base/bind.h"
#include "base/callback.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/stl_util.h"
#include "chrome/browser/download/download_util.h"
#include "chrome/common/chrome_paths.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/download_id.h"
#include "content/public/browser/download_item.h"

using content::BrowserThread;
using content::DownloadId;
using content::DownloadItem;

namespace {

typedef std::map<content::DownloadId, FilePath> ReservationMap;

// Map of download path reservations. Each reserved path is associated with a
// DownloadId. This object is destroyed in |Revoke()| when there are no more
// reservations.
//
// It is not an error, although undesirable, to have multiple DownloadIds that
// are mapped to the same path. This can happen if a reservation is created that
// is supposed to overwrite an existing reservation.
ReservationMap* g_reservation_map = NULL;

// Observes a DownloadItem for changes to its target path and state. Updates or
// revokes associated download path reservations as necessary. Created, invoked
// and destroyed on the UI thread.
class DownloadItemObserver : public DownloadItem::Observer {
 public:
  explicit DownloadItemObserver(DownloadItem& download_item);

 private:
  virtual ~DownloadItemObserver();

  // DownloadItem::Observer
  virtual void OnDownloadUpdated(DownloadItem* download) OVERRIDE;
  virtual void OnDownloadDestroyed(DownloadItem* download) OVERRIDE;

  DownloadItem& download_item_;

  // Last known target path for the download.
  FilePath last_target_path_;

  DISALLOW_COPY_AND_ASSIGN(DownloadItemObserver);
};

// Returns true if the given path is in use by a path reservation.
bool IsPathReserved(const FilePath& path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));
  // No reservation map => no reservations.
  if (g_reservation_map == NULL)
    return false;
  // Unfortunately path normalization doesn't work reliably for non-existant
  // files. So given a FilePath, we can't derive a normalized key that we can
  // use for lookups. We only expect a small number of concurrent downloads at
  // any given time, so going through all of them shouldn't be too slow.
  for (ReservationMap::const_iterator iter = g_reservation_map->begin();
       iter != g_reservation_map->end(); ++iter) {
    if (iter->second == path)
      return true;
  }
  return false;
}

// Returns true if the given path is in use by any path reservation or the
// file system. Called on the FILE thread.
bool IsPathInUse(const FilePath& path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));
  // If there is a reservation, then the path is in use.
  if (IsPathReserved(path))
    return true;

  // If the path exists in the file system, then the path is in use.
  if (file_util::PathExists(path))
    return true;

  return false;
}

// Called on the FILE thread to reserve a download path. This method:
// - Creates directory |default_download_path| if it doesn't exist.
// - Verifies that the parent directory of |suggested_path| exists and is
//   writeable.
// - Uniquifies |suggested_path| if |should_uniquify_path| is true.
// - Schedules |callback| on the UI thread with the reserved path and a flag
//   indicating whether the returned path has been successfully verified.
void CreateReservation(
    DownloadId download_id,
    const FilePath& suggested_path,
    const FilePath& default_download_path,
    bool should_uniquify,
    const DownloadPathReservationTracker::ReservedPathCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));
  DCHECK(download_id.IsValid());
  DCHECK(suggested_path.IsAbsolute());

  // Create a reservation map if one doesn't exist. It will be automatically
  // deleted when all the reservations are revoked.
  if (g_reservation_map == NULL)
    g_reservation_map = new ReservationMap;

  ReservationMap& reservations = *g_reservation_map;
  DCHECK(!ContainsKey(reservations, download_id));

  FilePath target_path(suggested_path.NormalizePathSeparators());
  bool is_path_writeable = true;
  bool has_conflicts = false;

  // Create the default download path if it doesn't already exist and is where
  // we are going to create the downloaded file. |target_path| might point
  // elsewhere if this was a programmatic download.
  if (!default_download_path.empty() &&
      default_download_path == target_path.DirName() &&
      !file_util::DirectoryExists(default_download_path)) {
    file_util::CreateDirectory(default_download_path);
  }

  // Check writability of the suggested path. If we can't write to it, default
  // to the user's "My Documents" directory. We'll prompt them in this case.
  FilePath dir = target_path.DirName();
  FilePath filename = target_path.BaseName();
  if (!file_util::PathIsWritable(dir)) {
    DVLOG(1) << "Unable to write to directory \"" << dir.value() << "\"";
    is_path_writeable = false;
    PathService::Get(chrome::DIR_USER_DOCUMENTS, &dir);
    target_path = dir.Append(filename);
  }

  if (is_path_writeable && should_uniquify && IsPathInUse(target_path)) {
    has_conflicts = true;
    for (int uniquifier = 1;
         uniquifier <= DownloadPathReservationTracker::kMaxUniqueFiles;
         ++uniquifier) {
      FilePath path_to_check(target_path.InsertBeforeExtensionASCII(
          StringPrintf(" (%d)", uniquifier)));
      if (!IsPathInUse(path_to_check)) {
        target_path = path_to_check;
        has_conflicts = false;
        break;
      }
    }
  }
  reservations[download_id] = target_path;
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(callback, target_path, (is_path_writeable && !has_conflicts)));
}

// Called on the FILE thread to update the path of the reservation associated
// with |download_id| to |new_path|.
void UpdateReservation(DownloadId download_id, const FilePath& new_path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));
  DCHECK(g_reservation_map != NULL);
  ReservationMap::iterator iter = g_reservation_map->find(download_id);
  if (iter != g_reservation_map->end()) {
    iter->second = new_path;
  } else {
    // This would happen if an UpdateReservation() notification was scheduled on
    // the FILE thread before ReserveInternal(), or after a Revoke()
    // call. Neither should happen.
    NOTREACHED();
  }
}

// Called on the FILE thread to remove the path reservation associated with
// |download_id|.
void RevokeReservation(DownloadId download_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));
  DCHECK(g_reservation_map != NULL);
  DCHECK(ContainsKey(*g_reservation_map, download_id));
  g_reservation_map->erase(download_id);
  if (g_reservation_map->size() == 0) {
    // No more reservations. Delete map.
    delete g_reservation_map;
    g_reservation_map = NULL;
  }
}

DownloadItemObserver::DownloadItemObserver(DownloadItem& download_item)
    : download_item_(download_item),
      last_target_path_(download_item.GetTargetFilePath()) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  download_item_.AddObserver(this);
}

DownloadItemObserver::~DownloadItemObserver() {
  download_item_.RemoveObserver(this);
}

void DownloadItemObserver::OnDownloadUpdated(DownloadItem* download) {
  switch (download->GetState()) {
    case DownloadItem::IN_PROGRESS: {
      // Update the reservation.
      FilePath new_target_path = download->GetTargetFilePath();
      if (new_target_path != last_target_path_) {
        BrowserThread::PostTask(
            BrowserThread::FILE, FROM_HERE,
            base::Bind(&UpdateReservation, download->GetGlobalId(),
                       new_target_path));
        last_target_path_ = new_target_path;
      }
      break;
    }

    case DownloadItem::COMPLETE:
      // If the download is complete, then it has already been renamed to the
      // final name. The existence of the file on disk is sufficient to prevent
      // conflicts from now on.

    case DownloadItem::CANCELLED:
      // We no longer need the reservation if the download is being removed.

    case DownloadItem::INTERRUPTED:
      // The download filename will need to be re-generated when the download is
      // restarted. Holding on to the reservation now would prevent the name
      // from being used for a subsequent retry attempt.

      BrowserThread::PostTask(
          BrowserThread::FILE, FROM_HERE,
          base::Bind(&RevokeReservation, download->GetGlobalId()));
      delete this;
      break;

    case DownloadItem::MAX_DOWNLOAD_STATE:
      // Compiler appeasement.
      NOTREACHED();
  }
}

void DownloadItemObserver::OnDownloadDestroyed(DownloadItem* download) {
  // This shouldn't happen. We should catch either COMPLETE, CANCELLED, or
  // INTERRUPTED first.
  BrowserThread::PostTask(BrowserThread::FILE, FROM_HERE,
      base::Bind(&RevokeReservation, download->GetGlobalId()));
  delete this;
}

}  // namespace

// static
void DownloadPathReservationTracker::GetReservedPath(
    DownloadItem& download_item,
    const FilePath& target_path,
    const FilePath& default_path,
    bool uniquify_path,
    const ReservedPathCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  // Attach an observer to the download item so that we know when the target
  // path changes and/or the download is no longer active.
  new DownloadItemObserver(download_item);
  // DownloadItemObserver deletes itself.

  BrowserThread::PostTask(
      BrowserThread::FILE, FROM_HERE,
      base::Bind(&CreateReservation, download_item.GetGlobalId(),
                 target_path, default_path, uniquify_path, callback));
}

// static
bool DownloadPathReservationTracker::IsPathInUseForTesting(
    const FilePath& path) {
  return IsPathInUse(path);
}
