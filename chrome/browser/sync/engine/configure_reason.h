// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_ENGINE_CONFIGURE_REASON_H_
#define CHROME_BROWSER_SYNC_ENGINE_CONFIGURE_REASON_H_
#pragma once

namespace sync_api {

// Note: This should confirm with the enums in sync.proto for
// GetUpdatesCallerInfo. They will have 1:1 mapping but this will only map
// to a subset of the GetUpdatesCallerInfo enum values.
enum ConfigureReason {
  // We should never be here during actual configure. This is for setting
  // default values.
  CONFIGURE_REASON_UNKNOWN,

  // The client is configuring because the user opted to sync a different set
  // of datatypes.
  CONFIGURE_REASON_RECONFIGURATION,

  // The client is configuring because the client is being asked to migrate.
  CONFIGURE_REASON_MIGRATION
};

} // namespace sync_api

#endif  // CHROME_BROWSER_SYNC_ENGINE_CONFIGURE_REASON_H_
