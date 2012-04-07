// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/sessions/sync_session_context.h"

#include "chrome/browser/sync/sessions/debug_info_getter.h"
#include "chrome/browser/sync/sessions/session_state.h"
#include "chrome/browser/sync/util/extensions_activity_monitor.h"
#include "content/public/browser/browser_thread.h"

using content::BrowserThread;

namespace browser_sync {
namespace sessions {

SyncSessionContext::SyncSessionContext(
    ServerConnectionManager* connection_manager,
    syncable::DirectoryManager* directory_manager,
    ModelSafeWorkerRegistrar* model_safe_worker_registrar,
    const std::vector<SyncEngineEventListener*>& listeners,
    DebugInfoGetter* debug_info_getter)
    : resolver_(NULL),
      connection_manager_(connection_manager),
      directory_manager_(directory_manager),
      registrar_(model_safe_worker_registrar),
      extensions_activity_monitor_(new ExtensionsActivityMonitor()),
      notifications_enabled_(false),
      max_commit_batch_size_(kDefaultMaxCommitBatchSize),
      debug_info_getter_(debug_info_getter) {
  std::vector<SyncEngineEventListener*>::const_iterator it;
  for (it = listeners.begin(); it != listeners.end(); ++it)
    listeners_.AddObserver(*it);
}

SyncSessionContext::SyncSessionContext()
    : connection_manager_(NULL),
      directory_manager_(NULL),
      registrar_(NULL),
      extensions_activity_monitor_(NULL),
      debug_info_getter_(NULL) {
}

SyncSessionContext::~SyncSessionContext() {
  // In unittests, there may be no UI thread, so the above will fail.
  if (!BrowserThread::DeleteSoon(BrowserThread::UI, FROM_HERE,
                                extensions_activity_monitor_)) {
    delete extensions_activity_monitor_;
  }
}

void SyncSessionContext::SetUnthrottleTime(syncable::ModelTypeSet types,
                                           const base::TimeTicks& time) {
  for (syncable::ModelTypeSet::Iterator it = types.First();
       it.Good(); it.Inc()) {
    unthrottle_times_[it.Get()] = time;
  }
}

void SyncSessionContext::PruneUnthrottledTypes(const base::TimeTicks& time) {
  UnthrottleTimes::iterator it = unthrottle_times_.begin();
  while (it != unthrottle_times_.end()) {
    if (it->second <= time) {
      // Delete and increment the iterator.
      UnthrottleTimes::iterator iterator_to_delete = it;
      ++it;
      unthrottle_times_.erase(iterator_to_delete);
    } else {
      // Just increment the iterator.
      ++it;
    }
  }
}

// TODO(lipalani): Call this function and fill the return values in snapshot
// so it could be shown in the about:sync page.
syncable::ModelTypeSet SyncSessionContext::GetThrottledTypes() const {
  syncable::ModelTypeSet types;
  for (UnthrottleTimes::const_iterator it = unthrottle_times_.begin();
       it != unthrottle_times_.end();
       ++it) {
    types.Put(it->first);
  }
  return types;
}

}  // namespace sessions
}  // namespace browser_sync
