// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/internal_api/includes/syncer_error.h"

#include "base/logging.h"

namespace browser_sync {

#define ENUM_CASE(x) case x: return #x; break;
const char* GetSyncerErrorString(SyncerError value) {
  switch (value) {
    ENUM_CASE(UNSET);
    ENUM_CASE(DIRECTORY_LOOKUP_FAILED);
    ENUM_CASE(NETWORK_CONNECTION_UNAVAILABLE);
    ENUM_CASE(NETWORK_IO_ERROR);
    ENUM_CASE(SYNC_SERVER_ERROR);
    ENUM_CASE(SYNC_AUTH_ERROR);
    ENUM_CASE(SERVER_RETURN_INVALID_CREDENTIAL);
    ENUM_CASE(SERVER_RETURN_UNKNOWN_ERROR);
    ENUM_CASE(SERVER_RETURN_THROTTLED);
    ENUM_CASE(SERVER_RETURN_TRANSIENT_ERROR);
    ENUM_CASE(SERVER_RETURN_MIGRATION_DONE);
    ENUM_CASE(SERVER_RETURN_CLEAR_PENDING);
    ENUM_CASE(SERVER_RETURN_NOT_MY_BIRTHDAY);
    ENUM_CASE(SERVER_RESPONSE_VALIDATION_FAILED);
    ENUM_CASE(SYNCER_OK);
  }
  NOTREACHED();
  return "INVALID";
}
#undef ENUM_CASE

} // namespace browser_sync

