// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_DOWNLOAD_DOWNLOAD_HISTORY_H_
#define CHROME_BROWSER_DOWNLOAD_DOWNLOAD_HISTORY_H_

#include <map>

#include "base/basictypes.h"
#include "base/callback.h"
#include "chrome/browser/cancelable_request.h"
#include "chrome/browser/history/history.h"

class Profile;

namespace base {
class Time;
}

namespace content {
class DownloadItem;
}

// Interacts with the HistoryService on behalf of the download subsystem.
class DownloadHistory {
 public:
  typedef base::Callback<void(bool)> VisitedBeforeDoneCallback;

  explicit DownloadHistory(Profile* profile);
  ~DownloadHistory();

  // Retrieves the next_id counter from the sql meta_table.
  // Should be much faster than Load so that we may delay downloads until after
  // this call with minimal performance penalty.
  void GetNextId(const HistoryService::DownloadNextIdCallback& callback);

  // Retrieves DownloadCreateInfos saved in the history.
  void Load(const HistoryService::DownloadQueryCallback& callback);

  // Checks whether |referrer_url| has been visited before today.  This takes
  // ownership of |callback|.
  void CheckVisitedReferrerBefore(int32 download_id,
                                  const GURL& referrer_url,
                                  const VisitedBeforeDoneCallback& callback);

  // Adds a new entry for a download to the history database.
  void AddEntry(content::DownloadItem* download_item,
                const HistoryService::DownloadCreateCallback& callback);

  // Updates the history entry for |download_item|.
  void UpdateEntry(content::DownloadItem* download_item);

  // Updates the download path for |download_item| to |new_path|.
  void UpdateDownloadPath(content::DownloadItem* download_item,
                          const FilePath& new_path);

  // Removes |download_item| from the history database.
  void RemoveEntry(content::DownloadItem* download_item);

  // Removes download-related history entries in the given time range.
  void RemoveEntriesBetween(const base::Time remove_begin,
                            const base::Time remove_end);

  // Returns a new unique database handle which will not collide with real ones.
  int64 GetNextFakeDbHandle();

 private:
  typedef std::map<HistoryService::Handle, VisitedBeforeDoneCallback>
      VisitedBeforeRequestsMap;

  void OnGotVisitCountToHost(HistoryService::Handle handle,
                             bool found_visits,
                             int count,
                             base::Time first_visit);

  Profile* profile_;

  // In case we don't have a valid db_handle, we use |fake_db_handle_| instead.
  // This is useful for incognito mode or when the history database is offline.
  // Downloads are expected to have unique handles, so we decrement the next
  // fake handle value on every use.
  int64 next_fake_db_handle_;

  CancelableRequestConsumer history_consumer_;

  // The outstanding requests made by CheckVisitedReferrerBefore().
  VisitedBeforeRequestsMap visited_before_requests_;

  DISALLOW_COPY_AND_ASSIGN(DownloadHistory);
};

#endif  // CHROME_BROWSER_DOWNLOAD_DOWNLOAD_HISTORY_H_
