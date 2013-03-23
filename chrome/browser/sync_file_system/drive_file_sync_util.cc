// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync_file_system/drive_file_sync_util.h"

#include "base/logging.h"

namespace sync_file_system {

fileapi::SyncStatusCode GDataErrorCodeToSyncStatusCode(
    google_apis::GDataErrorCode error) {
  // NOTE: Please update DriveFileSyncService::UpdateServiceState when you add
  // more error code mapping.
  switch (error) {
    case google_apis::HTTP_SUCCESS:
    case google_apis::HTTP_CREATED:
    case google_apis::HTTP_FOUND:
      return fileapi::SYNC_STATUS_OK;

    case google_apis::HTTP_NOT_MODIFIED:
      return fileapi::SYNC_STATUS_NOT_MODIFIED;

    case google_apis::HTTP_CONFLICT:
      return fileapi::SYNC_STATUS_HAS_CONFLICT;

    case google_apis::HTTP_UNAUTHORIZED:
      return fileapi::SYNC_STATUS_AUTHENTICATION_FAILED;

    case google_apis::GDATA_NO_CONNECTION:
      return fileapi::SYNC_STATUS_NETWORK_ERROR;

    case google_apis::HTTP_INTERNAL_SERVER_ERROR:
    case google_apis::HTTP_SERVICE_UNAVAILABLE:
    case google_apis::GDATA_CANCELLED:
    case google_apis::GDATA_NOT_READY:
      return fileapi::SYNC_STATUS_RETRY;

    case google_apis::HTTP_NOT_FOUND:
      return fileapi::SYNC_FILE_ERROR_NOT_FOUND;

    case google_apis::GDATA_FILE_ERROR:
      return fileapi::SYNC_FILE_ERROR_FAILED;

    case google_apis::HTTP_RESUME_INCOMPLETE:
    case google_apis::HTTP_BAD_REQUEST:
    case google_apis::HTTP_FORBIDDEN:
    case google_apis::HTTP_LENGTH_REQUIRED:
    case google_apis::HTTP_PRECONDITION:
    case google_apis::GDATA_PARSE_ERROR:
    case google_apis::GDATA_OTHER_ERROR:
      return fileapi::SYNC_STATUS_FAILED;

    case google_apis::GDATA_NO_SPACE:
      return fileapi::SYNC_FILE_ERROR_NO_SPACE;
  }
  NOTREACHED();
  return fileapi::SYNC_STATUS_FAILED;
}

}  // namespace sync_file_system
