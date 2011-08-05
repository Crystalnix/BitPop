// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/glue/sync_backend_host_mock.h"

namespace browser_sync {

ACTION(InvokeTask) {
  arg3->Run();
  delete arg3;
}

SyncBackendHostMock::SyncBackendHostMock() {
  // By default, invoke the ready callback.
  ON_CALL(*this, ConfigureDataTypes(testing::_, testing::_, testing::_,
    testing::_, testing::_)).
      WillByDefault(InvokeTask());
}

SyncBackendHostMock::~SyncBackendHostMock() {}

}  // namespace browser_sync
