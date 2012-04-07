// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_TEST_NULL_DIRECTORY_CHANGE_DELEGATE_H_
#define CHROME_BROWSER_SYNC_TEST_NULL_DIRECTORY_CHANGE_DELEGATE_H_
#pragma once

#include "base/compiler_specific.h"
#include "chrome/browser/sync/syncable/directory_change_delegate.h"

namespace syncable {

// DirectoryChangeDelegate that does nothing in all delegate methods.
class NullDirectoryChangeDelegate : public DirectoryChangeDelegate {
 public:
  virtual ~NullDirectoryChangeDelegate();

  virtual void HandleCalculateChangesChangeEventFromSyncApi(
      const ImmutableWriteTransactionInfo& write_transaction_info,
      BaseTransaction* trans) OVERRIDE;
  virtual void HandleCalculateChangesChangeEventFromSyncer(
      const ImmutableWriteTransactionInfo& write_transaction_info,
      BaseTransaction* trans) OVERRIDE;
  virtual ModelTypeSet HandleTransactionEndingChangeEvent(
      const ImmutableWriteTransactionInfo& write_transaction_info,
      BaseTransaction* trans) OVERRIDE;
  virtual void HandleTransactionCompleteChangeEvent(
      ModelTypeSet models_with_changes) OVERRIDE;
};

}  // namespace syncable

#endif  // CHROME_BROWSER_SYNC_TEST_NULL_DIRECTORY_CHANGE_DELEGATE_H_
