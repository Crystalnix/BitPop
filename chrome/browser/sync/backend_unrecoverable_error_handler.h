// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_BACKEND_UNRECOVERABLE_ERROR_HANDLER_H_
#define CHROME_BROWSER_SYNC_BACKEND_UNRECOVERABLE_ERROR_HANDLER_H_
#pragma once

#include <string>

#include "base/location.h"
#include "base/memory/weak_ptr.h"

#include "chrome/browser/sync/internal_api/includes/unrecoverable_error_handler.h"
#include "chrome/browser/sync/util/weak_handle.h"

class ProfileSyncService;
namespace browser_sync {

class BackendUnrecoverableErrorHandler : public UnrecoverableErrorHandler {
 public:
  BackendUnrecoverableErrorHandler(
      const WeakHandle<ProfileSyncService>& service);
  virtual ~BackendUnrecoverableErrorHandler();
  virtual void OnUnrecoverableError(const tracked_objects::Location& from_here,
                                    const std::string& message) OVERRIDE;

 private:
  WeakHandle<ProfileSyncService> service_;
};
}  // namespace browser_sync
#endif  // CHROME_BROWSER_SYNC_BACKEND_UNRECOVERABLE_ERROR_HANDLER_H_

