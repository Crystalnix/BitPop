// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_GLUE_SYNC_BACKEND_HOST_MOCK_H__
#define CHROME_BROWSER_SYNC_GLUE_SYNC_BACKEND_HOST_MOCK_H__
#pragma once

#include <set>

#include "base/callback_forward.h"
#include "chrome/browser/sync/glue/sync_backend_host.h"
#include "chrome/browser/sync/profile_sync_test_util.h"
#include "content/public/browser/notification_types.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace browser_sync {

class SyncBackendHostMock : public SyncBackendHost {
 public:
  SyncBackendHostMock();
  virtual ~SyncBackendHostMock();

  MOCK_METHOD6(ConfigureDataTypes,
               void(syncable::ModelTypeSet,
                    syncable::ModelTypeSet,
                    sync_api::ConfigureReason,
                    base::Callback<void(syncable::ModelTypeSet)>,
                    base::Callback<void()>,
                    bool));
  MOCK_METHOD0(StartSyncingWithServer, void());
};

}  // namespace browser_sync

#endif  // CHROME_BROWSER_SYNC_GLUE_SYNC_BACKEND_HOST_MOCK_H__
