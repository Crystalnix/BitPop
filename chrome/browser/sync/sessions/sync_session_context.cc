// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/sessions/sync_session_context.h"

#include "chrome/browser/sync/util/extensions_activity_monitor.h"
#include "chrome/browser/sync/sessions/session_state.h"
#include "content/browser/browser_thread.h"

namespace browser_sync {
namespace sessions {

SyncSessionContext::SyncSessionContext(
    ServerConnectionManager* connection_manager,
    syncable::DirectoryManager* directory_manager,
    ModelSafeWorkerRegistrar* model_safe_worker_registrar,
    const std::vector<SyncEngineEventListener*>& listeners)
    : resolver_(NULL),
      connection_manager_(connection_manager),
      directory_manager_(directory_manager),
      registrar_(model_safe_worker_registrar),
      extensions_activity_monitor_(new ExtensionsActivityMonitor()),
      notifications_enabled_(false),
      max_commit_batch_size_(kDefaultMaxCommitBatchSize) {
  std::vector<SyncEngineEventListener*>::const_iterator it;
  for (it = listeners.begin(); it != listeners.end(); ++it)
    listeners_.AddObserver(*it);
}

SyncSessionContext::~SyncSessionContext() {
  // In unittests, there may be no UI thread, so the above will fail.
  if (!BrowserThread::DeleteSoon(BrowserThread::UI, FROM_HERE,
                                extensions_activity_monitor_)) {
    delete extensions_activity_monitor_;
  }
}

}  // namespace sessions
}  // namespace browser_sync
